#include "CadHoleGeometry.h"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <atomic>
#include <cmath>

namespace djehuti::cad
{
namespace
{
juce::var clone(const juce::var& v){return juce::JSON::fromString(juce::JSON::toString(v,false,17));}
juce::var object(){return juce::var(new juce::DynamicObject());}
bool number(const juce::var& v){return (v.isDouble()||v.isInt()||v.isInt64())&&std::isfinite((double)v);}
bool vector(const juce::var& v,bool nonzero=false)
{
    if(!v.isArray()||v.size()!=3)return false;double length=0;
    for(int i=0;i<3;++i){if(!number(v[i]))return false;length+=(double)v[i]*(double)v[i];}
    return std::isfinite(length)&&(!nonzero||length>1e-18);
}
gp_Pnt point(const juce::var& v){return {(double)v[0],(double)v[1],(double)v[2]};}
gp_Dir direction(const juce::var& v){return {(double)v[0],(double)v[1],(double)v[2]};}
double volume(const TopoDS_Shape& s){GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass();}
bool validSolid(const TopoDS_Shape& s,juce::String& error)
{
    if(s.IsNull()||!TopExp_Explorer(s,TopAbs_SOLID).More()||!BRepCheck_Analyzer(s,true).IsValid()){error="Operation did not produce valid solids.";return false;}
    for(TopExp_Explorer ex(s,TopAbs_SOLID);ex.More();ex.Next())if(!std::isfinite(volume(ex.Current()))||volume(ex.Current())<=1e-12){error="Operation produced an inverted or empty solid.";return false;}
    return true;
}
template<typename Operation>
CadFaceOrigins mapped(Operation& op,const CadFaceOrigins& names,const TopoDS_Shape& result)
{
    CadFaceOrigins out;
    for(TopExp_Explorer ex(result,TopAbs_FACE);ex.More();ex.Next())
    {
        juce::StringArray tokens;
        for(const auto& entry:names)
        {
            bool match=ex.Current().IsSame(entry.first);
            if(!match)for(TopTools_ListIteratorOfListOfShape it(op.Modified(entry.first));it.More();it.Next())if(ex.Current().IsSame(it.Value())){match=true;break;}
            if(!match)for(TopTools_ListIteratorOfListOfShape it(op.Generated(entry.first));it.More();it.Next())if(ex.Current().IsSame(it.Value())){match=true;break;}
            if(match)for(const auto& t:entry.second)tokens.addIfNotAlreadyThere(t);
        }
        if(!tokens.isEmpty())out.push_back({ex.Current(),tokens});
    }
    return out;
}
juce::var reference(const char* type){auto r=object();r.getDynamicObject()->setProperty("type",type);return r;}
juce::StringArray referenceKeys(const juce::String& type)
{
    if(type=="mirror")return {"plane"};
    if(type=="linearPattern")return {"direction1","direction2"};
    return {"axis"};
}
}

juce::var replicationDefaults(const juce::String& type)
{
    auto p=object();auto* o=p.getDynamicObject();o->setProperty("sources",juce::Array<juce::var>());o->setProperty("operation","bodies");o->setProperty("target","");o->setProperty("includeSource",true);
    if(type=="mirror")o->setProperty("plane",reference("YZ"));
    if(type=="linearPattern")
    {o->setProperty("direction1",reference("X"));o->setProperty("direction2",reference("Y"));o->setProperty("count1",2);o->setProperty("count2",1);o->setProperty("spacing1",20.0);o->setProperty("spacing2",20.0);o->setProperty("reverse1",false);o->setProperty("reverse2",false);}
    if(type=="circularPattern"){o->setProperty("axis",reference("Z"));o->setProperty("count",6);o->setProperty("angleDegrees",360.0);o->setProperty("reverse",false);}
    return p;
}

juce::StringArray replicationDependencies(const juce::var& p)
{
    juce::StringArray ids;if(!p.isObject())return ids;
    if(const auto* a=p["sources"].getArray())for(const auto& v:*a)ids.addIfNotAlreadyThere(v.toString());
    if(p["target"].toString().isNotEmpty())ids.addIfNotAlreadyThere(p["target"].toString());
    for(const auto* key:{"plane","direction1","direction2","axis"})if(p.getProperty(key,juce::var())["feature"].toString().isNotEmpty())ids.addIfNotAlreadyThere(p.getProperty(key,juce::var())["feature"].toString());
    return ids;
}

bool validateReplication(const juce::String& type,const juce::var& p,juce::String& error)
{
    const auto fail=[&](const juce::String& e){error=e;return false;};
    if(!isReplicationType(type)||!p.isObject())return fail("Invalid Mirror/Pattern definition.");
    auto allowed=replicationDefaults(type).getDynamicObject()->getProperties();
    for(const auto& prop:p.getDynamicObject()->getProperties())if(!allowed.contains(prop.name))return fail("Unknown parameter: "+prop.name.toString());
    if(!p["sources"].isArray()||p["sources"].size()<1||p["sources"].size()>32)return fail("Select between 1 and 32 source bodies, or one modeling feature.");
    juce::StringArray seen;for(const auto& v:*p["sources"].getArray()){if(!v.isString()||v.toString().isEmpty()||seen.contains(v.toString()))return fail("Source IDs must be nonempty and distinct.");seen.add(v.toString());}
    if(!p["operation"].isString()||!juce::StringArray{"bodies","feature","add","remove"}.contains(p["operation"].toString()))return fail("Operation must be bodies, feature, add or remove.");
    if(p["operation"].toString()=="feature"&&p["sources"].size()!=1)return fail("Feature repetition supports one source operation at a time.");
    if(!p["target"].isString()||!p["includeSource"].isBool())return fail("Target must be an ID and includeSource a boolean.");
    if(juce::StringArray{"add","remove"}.contains(p["operation"].toString())&&p["target"].toString().isEmpty())return fail("Add/remove mode requires a target solid.");
    for(const auto& key:referenceKeys(type))
    {
        const auto r=p.getProperty(key,juce::var());if(!r.isObject()||!r["type"].isString())return fail("Choose a valid "+key+" reference.");
        for(const auto& prop:r.getDynamicObject()->getProperties())if(!juce::StringArray{"type","origin","vector","feature","face","reference","edge"}.contains(prop.name.toString()))return fail("Unknown reference field: "+prop.name.toString());
        const auto kind=r["type"].toString();const bool plane=key=="plane";
        if(!(plane?juce::StringArray{"XY","XZ","YZ","custom","face"}:juce::StringArray{"X","Y","Z","custom","edge","face"}).contains(kind))return fail("Unsupported "+key+" type.");
        if(kind=="custom"&&(!vector(r["vector"],true)||!vector(r.getProperty("origin",juce::Array<juce::var>{0,0,0}))))return fail("Custom reference needs a finite nonzero vector and finite origin.");
        if(kind=="face"&&(!r["feature"].isString()||r["feature"].toString().isEmpty()||!r["face"].isString()||r["face"].toString().isEmpty()))return fail("Face reference requires feature and persistent face IDs.");
        if(kind=="edge"&&(!r["feature"].isString()||r["feature"].toString().isEmpty()||!r["reference"].isString()||r["reference"].toString().isEmpty()))return fail("Edge reference requires feature and persistent reference (or an edge index on create/update).");
    }
    if(type=="linearPattern")
    {
        for(const auto* key:{"count1","count2"})if(!number(p.getProperty(key,juce::var()))||(double)p.getProperty(key,juce::var())!=std::floor((double)p.getProperty(key,juce::var()))||(double)p.getProperty(key,juce::var())<1||(double)p.getProperty(key,juce::var())>256)return fail("Pattern counts must be integers from 1 to 256.");
        if((int)p["count1"]*(int)p["count2"]>256||(int)p["count1"]*(int)p["count2"]<2)return fail("Linear pattern needs 2 to 256 grid positions.");
        for(const auto* key:{"spacing1","spacing2"})if(!number(p.getProperty(key,juce::var()))||(double)p.getProperty(key,juce::var())<=1e-7)return fail("Spacing must be a finite positive distance in mm.");
        if(!p["reverse1"].isBool()||!p["reverse2"].isBool())return fail("Reverse flags must be boolean.");
    }
    if(type=="circularPattern")
    {
        if(!number(p["count"])||(double)p["count"]!=std::floor((double)p["count"])||(double)p["count"]<2||(double)p["count"]>256)return fail("Circular count must be an integer from 2 to 256.");
        if(!number(p["angleDegrees"])||(double)p["angleDegrees"]<=1e-7||(double)p["angleDegrees"]>360)return fail("Pattern angle must be greater than zero and no greater than 360 degrees.");
        if(!p["reverse"].isBool())return fail("Reverse must be boolean.");
    }
    const int count=type=="mirror"?2:type=="linearPattern"?(int)p["count1"]*(int)p["count2"]:(int)p["count"];
    if(count*p["sources"].size()>512)return fail("Limit each pattern to 512 total body copies.");
    return true;
}

TopoDS_Shape CadDocument::replicateShape(const CadFeature& feature,CadFaceOrigins& origins,juce::String& error,juce::StringArray& active)
{
    const auto p=feature.replication;if(!validateReplication(feature.type,p,error))return {};
    const auto resolve=[&](const juce::var& r,bool axis,gp_Pnt& o,gp_Dir& d)->bool
    {
        const auto type=r["type"].toString();o=gp_Pnt(0,0,0);
        if(type=="custom"){o=point(r.getProperty("origin",juce::Array<juce::var>{0,0,0}));d=direction(r["vector"]);return true;}
        if(type=="X"||type=="YZ"){d=gp::DX();return true;}if(type=="Y"||type=="XZ"){d=gp::DY();return true;}if(type=="Z"||type=="XY"){d=gp::DZ();return true;}
        const auto* f=find(r["feature"].toString());if(f==nullptr){error="Reference feature no longer exists.";return false;}
        CadFeature owner=*f;owner.shape=evaluate(owner.id,error,active,&owner.faceOrigins);if(owner.shape.IsNull())return false;
        if(type=="edge")return replicationEdge(owner,r["reference"].toString(),o,d,axis,error);
        TopoDS_Face face;CadSketchFrame frame;if(!holeSupport(owner.id,r["face"].toString(),frame,face,error,active))return false;o=frame.origin;d=frame.normal;return true;
    };
    std::vector<gp_Trsf> transforms(1);
    if(feature.type=="mirror")
    {gp_Pnt o;gp_Dir n;if(!resolve(p["plane"],false,o,n))return {};gp_Trsf t;t.SetMirror(gp_Ax2(o,n));transforms.push_back(t);}
    if(feature.type=="linearPattern")
    {
        gp_Pnt unused;gp_Dir a,b;if(!resolve(p["direction1"],false,unused,a)||!resolve(p["direction2"],false,unused,b))return {};
        if((int)p["count1"]>1&&(int)p["count2"]>1&&gp_Vec(a).Crossed(gp_Vec(b)).Magnitude()<1e-7){error="Two-direction patterns require nonparallel directions.";return {};}
        transforms.clear();for(int j=0;j<(int)p["count2"];++j)for(int i=0;i<(int)p["count1"];++i)
        {gp_Trsf t;t.SetTranslation(gp_Vec(a)*(i*(double)p["spacing1"]*((bool)p["reverse1"]?-1:1))+gp_Vec(b)*(j*(double)p["spacing2"]*((bool)p["reverse2"]?-1:1)));transforms.push_back(t);}
    }
    if(feature.type=="circularPattern")
    {
        gp_Pnt o;gp_Dir d;if(!resolve(p["axis"],true,o,d))return {};transforms.clear();const int count=(int)p["count"];const auto span=(double)p["angleDegrees"];
        const auto step=span/(span==360.0?count:count-1)*juce::MathConstants<double>::pi/180.0*((bool)p["reverse"]?-1:1);
        for(int i=0;i<count;++i){gp_Trsf t;t.SetRotation(gp_Ax1(o,d),i*step);transforms.push_back(t);}
    }
    const auto operation=p["operation"].toString();TopoDS_Shape result;CadFaceOrigins names;
    auto targetId=p["target"].toString();
    if(operation=="feature"&&targetId.isEmpty())if(const auto* source=find(p["sources"][0].toString()))targetId=source->firstOperand;
    if(operation!="bodies"){result=evaluate(targetId,error,active,&names);if(result.IsNull()||!validSolid(result,error))return {};BRepBuilderAPI_Copy copy(result,true,false);names=mapped(copy,names,copy.Shape());result=copy.Shape();}
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
    for(int s=0;s<p["sources"].size();++s)
    {
        const auto sourceId=p["sources"][s].toString();const auto* f=find(sourceId);if(f==nullptr){error="Missing pattern source: "+sourceId;return {};}
        CadFaceOrigins seedNames;auto seed=evaluate(sourceId,error,active,&seedNames);if(seed.IsNull()||!validSolid(seed,error))return {};
        bool remove=operation=="remove";
        if(operation=="feature")
        {
            remove=f->type=="hole"||f->type=="cut"||f->type=="subtract";
            if(!remove&&f->type!="union"){error="Feature mode supports Hole, Cut, Subtract and Union. Use bodies, or add/remove with an explicit target for other solids.";return {};}
            auto base=evaluate(f->firstOperand,error,active);if(base.IsNull())return {};
            if(f->type=="hole"||f->type=="cut")
            {
                // Reuse the actual source cutter, not the material clipped at the seed position.
                // Through-all extent is resolved separately at each transformed instance below.
                seed={};seedNames.clear();
            }
            else
            {
            BRepBuilderAPI_Copy c1(seed,true,false),c2(base,true,false);
            BRepAlgoAPI_Cut delta(remove?c2.Shape():c1.Shape(),remove?c1.Shape():c2.Shape());delta.Build();
            if(!delta.IsDone()||delta.HasErrors()||delta.Shape().IsNull()||!validSolid(delta.Shape(),error)){error="Source feature has no usable added/removed material.";return {};}
            auto all=seedNames;CadFaceOrigins baseNames;evaluate(f->firstOperand,error,active,&baseNames);all.insert(all.end(),baseNames.begin(),baseNames.end());
            // Match each delta face by the operation history on private copies.
            auto n1=mapped(c1,seedNames,c1.Shape()),n2=mapped(c2,baseNames,c2.Shape());n1.insert(n1.end(),n2.begin(),n2.end());seedNames=mapped(delta,n1,delta.Shape());seed=delta.Shape();
            }
        }
        for(size_t i=(bool)p["includeSource"]?0:1;i<transforms.size();++i)
        {
            auto instanceSeed=seed;auto instanceNames=seedNames;
            if(operation=="feature"&&(f->type=="hole"||f->type=="cut"))
            {
                BRepBuilderAPI_Transform inverseTarget(result,transforms[i].Inverted(),true);const auto extent=inverseTarget.Shape();
                if(f->type=="hole")
                {
                    CadSketchFrame frame;TopoDS_Face face;if(!holeSupport(f->targetFeatureId,f->hole.face,frame,face,error,active))return {};
                    instanceSeed=makeHoleCutter(f->hole,frame,extent,f->id,instanceNames,error);
                }
                else
                {
                    const auto* sketch=findSketch(f->sketchId.isEmpty()?f->secondOperand:f->sketchId);CadSketchFrame frame;
                    if(sketch==nullptr||!resolveSketchFrame(*sketch,frame,error,active))return {};
                    double depth=f->depth;
                    if(f->throughAll||depth<=0){Bnd_Box bounds;BRepBndLib::AddOptimal(extent,bounds,false,false);double a,b,c,d,e,g;bounds.Get(a,b,c,d,e,g);depth=std::hypot(d-a,e-b,g-c)+2;}
                    instanceSeed=extrudeSketch(*sketch,frame,depth,f->reverse!=sketch->onFace(),error,&instanceNames,f->id);
                }
                if(instanceSeed.IsNull())return {};instanceSeed=place(instanceSeed,*f,&instanceNames);
            }
            BRepBuilderAPI_Transform transform(instanceSeed,transforms[i],true);auto copy=transform.Shape();
            if(volume(copy)<0)copy.Reverse();if(!validSolid(copy,error))return {};
            CadFaceOrigins copyNames;
            for(const auto& entry:instanceNames)
            {const auto image=transform.ModifiedShape(entry.first);juce::StringArray tokens;for(const auto& token:entry.second)tokens.add(feature.id+"/instance:"+juce::String((int)i)+"/source:"+juce::String(s)+"/"+token);if(!image.IsNull())copyNames.push_back({image,tokens});}
            if(operation=="bodies")
            {
                CadTopologyEvolution history;history.featureId=feature.id;
                for(const auto kind:{TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer ex(instanceSeed,kind);ex.More();ex.Next())
                {const auto image=transform.ModifiedShape(ex.Current());if(!image.IsNull())history.generated.push_back({ex.Current(),image});}
                operationHistory.push_back(std::move(history));builder.Add(compound,copy);origins.insert(origins.end(),copyNames.begin(),copyNames.end());
            }
            else
            {
                const auto before=volume(result);auto all=names;all.insert(all.end(),copyNames.begin(),copyNames.end());
                const auto run=[&](auto& op)->bool
                {
                    op.Build();if(!op.IsDone()||op.HasErrors()||!validSolid(op.Shape(),error))return false;
                    CadTopologyEvolution history;history.featureId=feature.id;
                    for(TopExp_Explorer ex(result,TopAbs_FACE);ex.More();ex.Next())
                    {for(TopTools_ListIteratorOfListOfShape it(op.Modified(ex.Current()));it.More();it.Next())history.modified.push_back({ex.Current(),it.Value()});if(op.IsDeleted(ex.Current()))history.deleted.push_back(ex.Current());}
                    operationHistory.push_back(std::move(history));names=mapped(op,all,op.Shape());result=op.Shape();return true;
                };
                if(remove){BRepAlgoAPI_Cut cut(result,copy);if(!run(cut))return {};}
                else {BRepAlgoAPI_Fuse fuse(result,copy);if(!run(fuse))return {};}
                if(std::abs(volume(result)-before)<=std::max(1e-8,std::abs(before)*1e-12)){error="Instance "+juce::String((int)i)+" does not modify the target (outside, duplicate or fully overlapping).";return {};}
            }
        }
    }
    if(operation=="bodies")result=compound;else origins=names;
    if(!validSolid(result,error))return {};return result;
}

std::shared_ptr<const CadRenderSnapshot> CadDocument::replicationPreview(const juce::String& type,const juce::var& args,juce::var& result,juce::String& error) const
{
    CadDocument draft;draft.geometryCache=geometryCache;if(!draft.deserialize(serialize(),error))return {};
    auto request=clone(args);request.getDynamicObject()->setProperty("action",args["id"].toString().isEmpty()?"create":"update");result=draft.replicationCommand(type,request);
    if(!(bool)result["ok"]){error=result["error"].toString();return {};}
    result.getDynamicObject()->setProperty("volume_mm3",volume(draft.find(result["id"].toString())->shape));result.getDynamicObject()->setProperty("preview",true);result.getDynamicObject()->setProperty("committed",false);if(args["id"].toString().isEmpty())result.getDynamicObject()->removeProperty("id");
    static std::atomic<std::uint64_t> serial{0};auto snapshot=std::make_shared<CadRenderSnapshot>(*draft.snapshot());snapshot->revision=(std::uint64_t(1)<<63)|++serial;snapshot->documentInstance=instanceId;return snapshot;
}

juce::var CadDocument::replicationCommand(const juce::String& type,const juce::var& args)
{
    auto reply=object();auto* out=reply.getDynamicObject();juce::String error;
    const auto fail=[&](const char* code,const juce::String& e){out->setProperty("ok",false);out->setProperty("code",code);out->setProperty("error",e);return reply;};
    if(!isReplicationType(type)||!args.isObject())return fail("invalid_arguments","Invalid Mirror/Pattern request.");
    const auto action=args.getProperty("action","get").toString();auto id=args["id"].toString();
    if(action=="references"){auto a=clone(args);a.getDynamicObject()->setProperty("action","edges");return chamferCommand(a);}
    if(action=="preview"){juce::var details;if(replicationPreview(type,args,details,error)==nullptr)return fail("modeling_failed",error);return details;}
    if(!juce::StringArray{"create","get","update","delete"}.contains(action))return fail("invalid_arguments","Action must be create/get/update/delete/preview/references.");
    const auto* f=find(id);if(action!="create"&&(f==nullptr||f->type!=type))return fail("not_found","Unknown "+type+" feature: "+id);
    if(action=="delete"){if(!remove(id,error))return fail("dependency_error",error);out->setProperty("ok",true);out->setProperty("id",id);out->setProperty("deleted",true);return reply;}
    if(action=="create"||action=="update")
    {
        if(!args["parameters"].isObject())return fail("invalid_arguments","Parameters must be an object.");
        auto p=action=="create"?replicationDefaults(type):clone(f->replication);
        for(const auto& prop:args["parameters"].getDynamicObject()->getProperties())p.getDynamicObject()->setProperty(prop.name,clone(prop.value));
        for(const auto& key:referenceKeys(type))
        {
            auto r=p.getProperty(key,juce::var());if(r["type"].toString()=="edge"&&r.hasProperty("edge"))
            {
                if(!number(r["edge"])||(double)r["edge"]!=std::floor((double)r["edge"]))return fail("invalid_reference","Edge index must be an integer.");
                auto a=object();a.getDynamicObject()->setProperty("action","edges");a.getDynamicObject()->setProperty("target",r["feature"]);const auto edges=chamferCommand(a);const int index=(int)r["edge"];
                if(!(bool)edges["ok"]||index<0||index>=edges["edges"].size())return fail("invalid_reference","Edge index is outside the reference body.");
                r.getDynamicObject()->setProperty("reference",edges["edges"][index]["reference"]);r.getDynamicObject()->removeProperty("edge");
            }
        }
        if(!validateReplication(type,p,error))return fail("invalid_arguments",error);
        for(const auto& dependency:replicationDependencies(p))if(dependency==id||find(dependency)==nullptr)return fail("invalid_reference","Missing or self-referencing dependency: "+dependency);
        auto created=id;const bool ok=mutate([&](juce::String&)
        {
            if(action=="create"){CadFeature value;value.id=newId(type.toRawUTF8(),nextNumber++);value.type=type;value.name=(type=="mirror"?juce::String("Mirror"):type=="linearPattern"?juce::String("Linear Pattern"):juce::String("Circular Pattern"))+" "+juce::String(nextNumber-1);items.push_back(value);created=value.id;}
            for(auto& item:items)if(item.id==created){item.replication=p;item.firstOperand=p["sources"][0].toString();item.targetFeatureId=p["target"].toString();}
            for(auto& item:items){if(item.id==p["target"].toString()&&p["operation"].toString()!="bodies")item.visible=false;if(const auto* sources=p["sources"].getArray())for(const auto& source:*sources)if(item.id==source.toString())item.visible=false;}
            return true;
        },error);
        if(!ok)return fail("modeling_failed",error.isEmpty()?"OCCT could not construct this Mirror/Pattern. Check geometry and references.":error);id=created;f=find(id);
    }
    out->setProperty("ok",true);out->setProperty("id",id);out->setProperty("type",type);out->setProperty("parameters",clone(f->replication));
    const auto p=f->replication;auto resolvedTarget=p["target"].toString();if(p["operation"].toString()=="feature"&&resolvedTarget.isEmpty())if(const auto* seed=find(p["sources"][0].toString()))resolvedTarget=seed->firstOperand;out->setProperty("resolvedTarget",resolvedTarget);
    const int count=type=="mirror"?2:type=="linearPattern"?(int)p["count1"]*(int)p["count2"]:(int)p["count"];
    juce::Array<juce::var> indices;for(int i=(bool)p["includeSource"]?0:1;i<count;++i)indices.add(i);out->setProperty("instanceIndices",indices);out->setProperty("instanceCount",indices.size());return reply;
}
}
