#include "CadDocument.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <atomic>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

namespace djehuti::cad
{
namespace
{
using Stations = std::vector<std::array<double,2>>; // axial position, diameter (mm)
bool readStations(const juce::var& value, Stations& stations, bool outer, juce::String& error)
{
    const auto* rows=value.getArray();
    if(rows==nullptr||rows->size()<2||rows->size()>256){error="A lathe profile needs 2 to 256 axial/diameter stations.";return false;}
    for(const auto& row:*rows)
    {
        const auto* pair=row.getArray();
        if(pair==nullptr||pair->size()!=2){error="Each lathe station is [axial_mm, diameter_mm].";return false;}
        for(const auto& v:*pair)if(!(v.isDouble()||v.isInt()||v.isInt64())||!std::isfinite((double)v)){error="Lathe stations must contain finite numbers.";return false;}
        const double z=(double)(*pair)[0],d=(double)(*pair)[1];
        if(d<0||(outer&&d<=0)){error="Outside diameters must be positive; bore diameters may be zero.";return false;}
        if(!stations.empty()&&(z<stations.back()[0]||(z==stations.back()[0]&&d==stations.back()[1])))
        {error="List stations in increasing axial order. Repeat an axial position only to create a shoulder or groove.";return false;}
        stations.push_back({z,d});
    }
    if(stations.back()[0]-stations.front()[0]<=1e-7){error="The turned profile must have positive axial length.";return false;}
    return true;
}
juce::var stationJson(const Stations& stations)
{
    juce::Array<juce::var> rows;for(const auto& p:stations)rows.add(juce::Array<juce::var>{p[0],p[1]});return rows;
}
bool profileFromSketch(const CadSketch& sketch, Stations& outside, Stations& bore)
{
    if(sketch.entities.size()<4)return false;
    for(const auto& c:sketch.constraints)if(c.type!="coincident")return false;
    std::vector<const CadSketchEntity*> inner;
    for(const auto& e:sketch.entities)
    {
        if(e.type!="line")return false;
        if(e.groupId=="latheOuter")
        {if(outside.empty())outside.push_back({e.y1,2*e.x1});outside.push_back({e.y2,2*e.x2});}
        else if(e.groupId=="latheBore")inner.push_back(&e);
        else if(e.groupId!="latheStart"&&e.groupId!="latheEnd")return false;
    }
    for(auto it=inner.rbegin();it!=inner.rend();++it)
    {const auto& e=**it;if(bore.empty())bore.push_back({e.y2,2*e.x2});bore.push_back({e.y1,2*e.x1});}
    juce::String error;Stations a,b;
    return readStations(stationJson(outside),a,true,error)&&readStations(stationJson(bore),b,false,error);
}
}

juce::var latheProfileFromText(const juce::String& outer, const juce::String& inner, juce::String& error)
{
    auto* result=new juce::DynamicObject();juce::var value(result);
    for(const auto& input:std::vector<std::pair<const char*,juce::String>>{{"outer",outer},{"inner",inner}})
    {
        juce::Array<juce::var> rows;
        for(const auto& line:juce::StringArray::fromLines(input.second))
        {
            if(line.trim().isEmpty())continue;
            const auto fields=juce::StringArray::fromTokens(line,",; \t",{});
            juce::StringArray tokens;for(const auto& f:fields)if(f.isNotEmpty())tokens.add(f);
            if(tokens.size()!=2){error="Enter one axial position and diameter per line, separated by a comma.";return {};}
            juce::Array<juce::var> pair;
            for(const auto& t:tokens)
            {
                char* end=nullptr;const auto text=t.toStdString();const auto number=std::strtod(text.c_str(),&end);
                if(end==text.c_str()||*end!='\0'||!std::isfinite(number)){error="Profile values must be finite numbers in millimetres.";return {};}
                pair.add(number);
            }
            rows.add(pair);
        }
        if(rows.isEmpty()&&juce::String(input.first)=="inner")continue;
        result->setProperty(input.first,rows);
    }
    Stations a,b;
    if(!readStations(value["outer"],a,true,error))return {};
    if(value.hasProperty("inner")&&!readStations(value["inner"],b,false,error))return {};
    return value;
}

juce::String latheStationsText(const juce::var& stations)
{
    juce::String result;if(const auto* rows=stations.getArray())for(const auto& row:*rows)
    {if(result.isNotEmpty())result<<"\n";result<<juce::String((double)row[0],8).trimCharactersAtEnd("0").trimCharactersAtEnd(".")<<", "<<juce::String((double)row[1],8).trimCharactersAtEnd("0").trimCharactersAtEnd(".");}
    return result;
}

juce::var CadDocument::latheContours(const juce::String& sketchId) const
{
    juce::Array<juce::var> contours;const auto* sketch=findSketch(sketchId);if(sketch==nullptr)return contours;
    struct Edge{int a,b;juce::String id;};std::vector<Edge> edges;std::vector<std::pair<double,double>> vertices;
    const auto vertex=[&](double x,double y){for(size_t i=0;i<vertices.size();++i)if(std::hypot(vertices[i].first-x,vertices[i].second-y)<1e-7)return (int)i;vertices.push_back({x,y});return (int)vertices.size()-1;};
    juce::StringArray keys;
    const auto append=[&](juce::StringArray ids)
    {ids.sort(false);const auto key=ids.joinIntoString("|");if(keys.contains(key))return;keys.add(key);auto* c=new juce::DynamicObject();c->setProperty("label","Contour "+juce::String(contours.size()+1)+" ("+juce::String(ids.size())+" entities)");juce::Array<juce::var> list;for(const auto& id:ids)list.add(id);c->setProperty("entities",list);contours.add(c);};
    for(const auto& e:sketch->entities)
    {if(e.type=="circle")append({e.id});else if(e.type=="line"||e.type=="bezier")edges.push_back({vertex(e.x1,e.y1),vertex(e.x2,e.y2),e.id});}
    // Each edge plus a shortest alternate path gives a bounded set of closed-contour choices,
    // even when open construction geometry is attached to a contour vertex.
    std::vector<std::vector<int>> adjacent(vertices.size());for(size_t i=0;i<edges.size();++i){adjacent[edges[i].a].push_back((int)i);adjacent[edges[i].b].push_back((int)i);}
    for(size_t excluded=0;excluded<edges.size();++excluded)
    {
        const auto& edge=edges[excluded];if(edge.a==edge.b)continue;std::vector<int> previous(vertices.size(),-1);std::queue<int> pending;pending.push(edge.a);previous[edge.a]=-2;
        while(!pending.empty()&&previous[edge.b]==-1)
        {const auto a=pending.front();pending.pop();for(const auto index:adjacent[a])if(index!=(int)excluded){const auto& next=edges[(size_t)index];const auto b=next.a==a?next.b:next.a;if(previous[b]==-1){previous[b]=index;pending.push(b);}}}
        if(previous[edge.b]==-1)continue;juce::StringArray ids{edge.id};auto at=edge.b;
        while(at!=edge.a){const auto& step=edges[(size_t)previous[at]];ids.add(step.id);at=step.a==at?step.b:step.a;}
        if(ids.size()>=2)append(ids);
    }
    return contours;
}

std::shared_ptr<const CadRenderSnapshot> CadDocument::lathePreview(const juce::var& args,juce::var& result,juce::String& error) const
{
    CadDocument draft;draft.geometryCache=geometryCache;if(!draft.deserialize(serialize(),error))return {};
    auto request=juce::JSON::parse(juce::JSON::toString(args,false,17));request.getDynamicObject()->setProperty("action",args["id"].toString().isEmpty()?"create":"update");
    result=draft.latheCommand(request);if(!(bool)result["ok"]){error=result["error"].toString();return {};}
    const auto* feature=draft.find(result["id"].toString());GProp_GProps props;BRepGProp::VolumeProperties(feature->shape,props);result.getDynamicObject()->setProperty("volumeMm3",props.Mass());result.getDynamicObject()->setProperty("preview",true);
    result.getDynamicObject()->setProperty("committed",false);if(args["id"].toString().isEmpty())result.getDynamicObject()->removeProperty("id");
    static std::atomic<std::uint64_t> serial{0};auto view=std::make_shared<CadRenderSnapshot>(*draft.snapshot());view->documentInstance=instanceId;view->revision=(std::uint64_t(1)<<62)|++serial;return view;
}

juce::var CadDocument::latheCommand(const juce::var& args)
{
    auto* reply=new juce::DynamicObject();juce::var result(reply);juce::String error;
    const auto fail=[&](const char* code,const juce::String& text){reply->setProperty("ok",false);reply->setProperty("code",code);reply->setProperty("error",text);return result;};
    if(!args.isObject())return fail("invalid_arguments","Lathe arguments must be an object.");
    const auto action=args.getProperty("action","get").toString();auto id=args.getProperty("id","").toString();
    if(action=="profiles"){const auto sketch=args["sketch"].toString();if(findSketch(sketch)==nullptr)return fail("not_found","Unknown sketch: "+sketch);reply->setProperty("ok",true);reply->setProperty("contours",latheContours(sketch));return result;}
    if(action=="preview"){juce::var details;if(lathePreview(args,details,error)==nullptr)return fail("modeling_failed",error);return details;}
    if(!juce::StringArray{"create","get","update","delete"}.contains(action))return fail("invalid_arguments","Lathe action must be create, get, update, delete, profiles or preview.");
    const auto* feature=find(id);
    if(action!="create"&&(feature==nullptr||feature->type!="revolve"||!feature->lathe))return fail("not_found","Unknown lathe feature: "+id);
    if(action=="delete")
    {if(!remove(id,error))return fail("dependency_error",error);reply->setProperty("ok",true);reply->setProperty("id",id);reply->setProperty("deleted",true);return result;}
    if(action=="create"||action=="update")
    {
        const auto params=args.getProperty("parameters",juce::var());
        if(!params.isObject())return fail("invalid_arguments","Lathe parameters must be an object.");
        if(!params.hasProperty("outer")&&!params.hasProperty("inner"))
        {
            const auto sketchId=params.getProperty("sketch",feature!=nullptr?feature->sketchId:juce::String()).toString();
            if(findSketch(sketchId)==nullptr)return fail("invalid_arguments","Choose an existing closed sketch profile; no station table is required.");
            auto p=action=="update"?feature->revolve:CadRevolveParameters();
            if(!readRevolveParameters(params,p,error))return fail("invalid_arguments",error);
            if(p.axisType=="sketchLine"&&p.axisSketch.isEmpty())p.axisSketch=sketchId;
            if(p.entities.isEmpty())
            {const auto contours=latheContours(sketchId);if(contours.size()!=1)return fail("ambiguous_profile","Select one closed contour explicitly with parameters.entities.");for(const auto& v:*contours[0]["entities"].getArray())p.entities.add(v.toString());}
            const bool ok=mutate([&](juce::String& e)
            {
                CadFeature* turn=nullptr;
                if(action=="create"){CadFeature f;f.id=newId("revolve",nextNumber++);f.name="Virtual Lathe "+juce::String(nextNumber-1);f.type="revolve";f.lathe=true;items.push_back(f);turn=&items.back();id=turn->id;}
                else for(auto& f:items)if(f.id==id)turn=&f;
                turn->sketchId=sketchId;turn->firstOperand=sketchId;turn->secondOperand=p.axisType=="sketchLine"?p.axisSketch:juce::String();turn->revolve=p;
                for(auto& f:items)if(f.id==sketchId)f.visible=false;
                return true;
            },error);
            if(!ok)return fail("modeling_failed",error);feature=find(id);
        }
        else
        {
        Stations outer,inner;bool replace=params.hasProperty("outer")||params.hasProperty("inner");
        if(action=="create"&&!params.hasProperty("outer"))return fail("invalid_arguments","Lathe creation requires an outer profile.");
        if(replace)
        {
            Stations currentOuter,currentInner;
            if(action=="update"&&!profileFromSketch(*findSketch(feature->sketchId),currentOuter,currentInner))
                return fail("custom_profile","This profile has custom geometry or constraints. Edit its sketch instead of replacing the station table.");
            if(!readStations(params.getProperty("outer",stationJson(currentOuter)),outer,true,error))return fail("invalid_arguments",error);
            if(params.hasProperty("inner")||action=="update")
            {if(!readStations(params.getProperty("inner",stationJson(currentInner)),inner,false,error))return fail("invalid_arguments",error);}
            else inner={{outer.front()[0],0},{outer.back()[0],0}};
            if(std::abs(outer.front()[0]-inner.front()[0])>1e-7||std::abs(outer.back()[0]-inner.back()[0])>1e-7)
                return fail("invalid_arguments","Outside and bore profiles must start and end at the same axial positions.");
            std::vector<double> positions;for(const auto& s:outer)positions.push_back(s[0]);for(const auto& s:inner)positions.push_back(s[0]);
            std::sort(positions.begin(),positions.end());positions.erase(std::unique(positions.begin(),positions.end()),positions.end());
            const auto endpoints=positions;for(size_t i=1;i<endpoints.size();++i)positions.push_back((endpoints[i-1]+endpoints[i])*0.5);
            for(const auto z:positions)
            {
                const auto range=[&](const Stations& rows)
                {
                    double low=1e300,high=-1e300;
                    for(const auto& r:rows)if(std::abs(r[0]-z)<1e-9){low=std::min(low,r[1]);high=std::max(high,r[1]);}
                    for(size_t i=1;i<rows.size();++i)if(z>=rows[i-1][0]&&z<=rows[i][0]&&rows[i][0]>rows[i-1][0])
                    {const auto d=rows[i-1][1]+(rows[i][1]-rows[i-1][1])*(z-rows[i-1][0])/(rows[i][0]-rows[i-1][0]);low=std::min(low,d);high=std::max(high,d);}
                    return std::array<double,2>{low,high};
                };
                if(range(outer)[0]<=range(inner)[1]+1e-7)return fail("invalid_arguments","Bore diameter must stay below the outside diameter at every station and shoulder.");
            }
        }
        auto p=action=="create"?CadRevolveParameters():feature->revolve;
        auto revolved=juce::JSON::parse(juce::JSON::toString(params,false,17));
        revolved.getDynamicObject()->removeProperty("outer");revolved.getDynamicObject()->removeProperty("inner");
        if(!readRevolveParameters(revolved,p,error))return fail("invalid_arguments",error);
        if(replace)p.entities.clear(); // Legacy station replacement authors a complete replacement contour.
        if(params.hasProperty("sketch"))return fail("invalid_arguments","Use the source sketch editor or cad_revolve to select a different profile.");
        juce::String created=id;
        const bool ok=mutate([&](juce::String& e)
        {
            CadSketch* sketch=nullptr;CadFeature* turn=nullptr;
            if(action=="create")
            {
                CadSketch s;s.id=newId("sketch",nextNumber++);s.plane="XZ";sketches.push_back(s);sketch=&sketches.back();
                CadFeature source;source.id=s.id;source.type="sketch";source.sketchId=s.id;source.name="Lathe profile "+juce::String(nextNumber-1);source.visible=false;items.push_back(source);
                CadFeature f;f.id=newId("revolve",nextNumber++);f.type="revolve";f.name="Turned part "+juce::String(nextNumber-1);f.sketchId=s.id;f.firstOperand=s.id;f.lathe=true;items.push_back(f);turn=&items.back();created=f.id;
            }
            else
            {for(auto& f:items)if(f.id==id){turn=&f;break;}sketch=mutableSketch(turn->sketchId);}
            turn->revolve=p;
            if(replace)
            {
                const auto previous=sketch->entities;sketch->entities.clear();sketch->constraints.clear();
                struct Segment{std::array<double,2>a,b;juce::String group;};std::vector<Segment> segments;
                const auto add=[&](std::array<double,2>a,std::array<double,2>b,const char* group)
                {segments.push_back({{a[1]*0.5,a[0]},{b[1]*0.5,b[0]},group});};
                for(size_t i=1;i<outer.size();++i)add(outer[i-1],outer[i],"latheOuter");
                add(outer.back(),inner.back(),"latheEnd");
                for(size_t i=inner.size()-1;i>0;--i)add(inner[i],inner[i-1],"latheBore");
                add(inner.front(),outer.front(),"latheStart");
                for(size_t i=0;i<segments.size();++i)
                {
                    const auto& segment=segments[i];
                    if(std::hypot(segment.b[0]-segment.a[0],segment.b[1]-segment.a[1])<1e-8){e="Outside and bore profiles meet with zero wall thickness.";return false;}
                    CadSketchEntity entity;entity.id=previous.size()==segments.size()?previous[i].id:newId("line",nextNumber++);entity.type="line";entity.groupId=segment.group;
                    entity.x1=segment.a[0];entity.y1=segment.a[1];entity.x2=segment.b[0];entity.y2=segment.b[1];sketch->entities.push_back(entity);
                }
                for(size_t i=0;i<sketch->entities.size();++i)
                {CadSketchConstraint c;c.id=newId("constraint",nextNumber++);c.type="coincident";c.firstEntity=sketch->entities[i].id+":p2";c.secondEntity=sketch->entities[(i+1)%sketch->entities.size()].id+":p1";sketch->constraints.push_back(c);}
                if(!validateSketch(*sketch,e))return false;
            }
            return true;
        },error);
        if(!ok)return fail("modeling_failed",error);id=created;feature=find(id);
        }
    }
    Stations outer,inner;const bool editable=profileFromSketch(*findSketch(feature->sketchId),outer,inner);
    auto params=revolveParametersToJson(feature->revolve);
    params.getDynamicObject()->setProperty("sketch",feature->sketchId);
    if(editable){params.getDynamicObject()->setProperty("outer",stationJson(outer));params.getDynamicObject()->setProperty("inner",stationJson(inner));}
    reply->setProperty("ok",true);reply->setProperty("id",id);reply->setProperty("type","revolve");reply->setProperty("workflow","lathe");reply->setProperty("sketch",feature->sketchId);reply->setProperty("profileEditable",editable);reply->setProperty("parameters",params);
    return result;
}
}
