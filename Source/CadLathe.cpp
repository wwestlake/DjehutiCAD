#include "CadDocument.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

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

juce::var CadDocument::latheCommand(const juce::var& args)
{
    auto* reply=new juce::DynamicObject();juce::var result(reply);juce::String error;
    const auto fail=[&](const char* code,const juce::String& text){reply->setProperty("ok",false);reply->setProperty("code",code);reply->setProperty("error",text);return result;};
    if(!args.isObject())return fail("invalid_arguments","Lathe arguments must be an object.");
    const auto action=args.getProperty("action","get").toString();auto id=args.getProperty("id","").toString();
    if(!juce::StringArray{"create","get","update","delete"}.contains(action))return fail("invalid_arguments","Lathe action must be create, get, update or delete.");
    const auto* feature=find(id);
    if(action!="create"&&(feature==nullptr||feature->type!="revolve"||!feature->lathe))return fail("not_found","Unknown lathe feature: "+id);
    if(action=="delete")
    {if(!remove(id,error))return fail("dependency_error",error);reply->setProperty("ok",true);reply->setProperty("id",id);reply->setProperty("deleted",true);return result;}
    if(action=="create"||action=="update")
    {
        const auto params=args.getProperty("parameters",juce::var());
        if(!params.isObject())return fail("invalid_arguments","Lathe parameters must be an object.");
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
    Stations outer,inner;const bool editable=profileFromSketch(*findSketch(feature->sketchId),outer,inner);
    auto params=revolveParametersToJson(feature->revolve);
    if(editable){params.getDynamicObject()->setProperty("outer",stationJson(outer));params.getDynamicObject()->setProperty("inner",stationJson(inner));}
    reply->setProperty("ok",true);reply->setProperty("id",id);reply->setProperty("type","revolve");reply->setProperty("workflow","lathe");reply->setProperty("sketch",feature->sketchId);reply->setProperty("profileEditable",editable);reply->setProperty("parameters",params);
    return result;
}
}
