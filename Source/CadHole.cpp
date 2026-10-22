#include "CadDocument.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <algorithm>
#include <cmath>

namespace djehuti::cad
{
namespace
{
bool numeric(const juce::var& v){return v.isDouble()||v.isInt()||v.isInt64();}
double solidVolume(const TopoDS_Shape& shape){GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
template<typename Operation>
CadFaceOrigins carryFaces(Operation& operation,const CadFaceOrigins& names,const TopoDS_Shape& result)
{
    CadFaceOrigins mapped;
    for(TopExp_Explorer ex(result,TopAbs_FACE);ex.More();ex.Next())
    {
        juce::StringArray tokens;
        for(const auto& entry:names)
        {
            bool matches=ex.Current().IsSame(entry.first);
            if(!matches)for(TopTools_ListIteratorOfListOfShape it(operation.Modified(entry.first));it.More();it.Next())if(ex.Current().IsSame(it.Value())){matches=true;break;}
            if(!matches)for(TopTools_ListIteratorOfListOfShape it(operation.Generated(entry.first));it.More();it.Next())if(ex.Current().IsSame(it.Value())){matches=true;break;}
            if(matches)for(const auto& token:entry.second)tokens.addIfNotAlreadyThere(token);
        }
        if(!tokens.isEmpty())mapped.push_back({ex.Current(),tokens});
    }
    return mapped;
}
CadFaceOrigins toolNames(const TopoDS_Shape& shape,const juce::String& owner,const char* wall,const char* bottom)
{
    CadFaceOrigins names;
    for(TopExp_Explorer ex(shape,TopAbs_FACE);ex.More();ex.Next())
    {
        const auto face=TopoDS::Face(ex.Current());const auto type=BRepAdaptor_Surface(face).GetType();
        const auto role=type==GeomAbs_Plane?juce::String(bottom):juce::String(wall);
        names.push_back({face,{owner+"/"+role}});
    }
    return names;
}
}

juce::var holeParametersToJson(const CadHoleParameters& p)
{
    auto* o=new juce::DynamicObject();o->setProperty("face",p.face);o->setProperty("style",p.style);o->setProperty("u",p.u);o->setProperty("v",p.v);o->setProperty("diameter",p.diameter);o->setProperty("depth",p.depth);o->setProperty("throughAll",p.throughAll);o->setProperty("headDiameter",p.headDiameter);o->setProperty("headDepth",p.headDepth);o->setProperty("countersinkAngleDegrees",p.countersinkAngleDegrees);return o;
}

bool readHoleParameters(const juce::var& args,CadHoleParameters& p,juce::String& error)
{
    if(!args.isObject()){error="Hole parameters must be an object.";return false;}auto copy=p;
    for(const auto& prop:args.getDynamicObject()->getProperties())if(!juce::StringArray{"face","style","u","v","diameter","depth","throughAll","headDiameter","headDepth","countersinkAngleDegrees"}.contains(prop.name.toString())){error="Unknown Hole parameter: "+prop.name.toString();return false;}
    for(const auto& item:std::vector<std::pair<const char*,double*>>{{"u",&copy.u},{"v",&copy.v},{"diameter",&copy.diameter},{"depth",&copy.depth},{"headDiameter",&copy.headDiameter},{"headDepth",&copy.headDepth},{"countersinkAngleDegrees",&copy.countersinkAngleDegrees}})
        if(args.hasProperty(item.first)){const auto v=args[item.first];if(!numeric(v)||!std::isfinite((double)v)){error=juce::String(item.first)+" must be a finite number.";return false;}*item.second=(double)v;}
    for(const auto* key:{"face","style"})if(args.hasProperty(key))
    {if(!args[key].isString()){error=juce::String(key)+" must be a string.";return false;}if(juce::String(key)=="face")copy.face=args[key].toString();else copy.style=args[key].toString();}
    if(args.hasProperty("throughAll")){if(!args["throughAll"].isBool()){error="throughAll must be a boolean.";return false;}copy.throughAll=(bool)args["throughAll"];}
    if(copy.face.isEmpty()){error="Choose a persistently named planar face.";return false;}
    if(!juce::StringArray{"plain","counterbore","countersink"}.contains(copy.style)){error="Hole style must be plain, counterbore or countersink.";return false;}
    if(copy.diameter<=0||(!copy.throughAll&&copy.depth<=0)){error="Hole diameter and blind depth must be greater than zero in millimetres.";return false;}
    if(copy.style!="plain"&&copy.headDiameter<=copy.diameter){error="Head diameter must be greater than the hole diameter.";return false;}
    if(copy.style=="counterbore"&&copy.headDepth<=0){error="Counterbore depth must be greater than zero.";return false;}
    if(copy.style=="countersink"&&(copy.countersinkAngleDegrees<=0||copy.countersinkAngleDegrees>=180)){error="Countersink included angle must be greater than 0 and less than 180 degrees.";return false;}
    p=copy;return true;
}

bool CadDocument::holeSupport(const juce::String& targetId,const juce::String& faceToken,CadSketchFrame& frame,TopoDS_Face& face,juce::String& error,juce::StringArray& active)
{
    const auto* source=find(targetId);
    if(source==nullptr||source->type=="sketch"){error="Choose an existing solid target for the hole.";return false;}
    CadFeature target=*source;target.shape=evaluate(targetId,error,active,&target.faceOrigins);
    if(target.shape.IsNull()||!TopExp_Explorer(target.shape,TopAbs_SOLID).More()){if(error.isEmpty())error="Hole target has no solids.";return false;}
    if(!supportFace(target,faceToken,face,error))return false;
    CadSketch reference;reference.id="Hole support";reference.plane="FACE";reference.supportFeature=targetId;reference.supportFace=faceToken;
    return resolveSketchFrame(reference,frame,error,active);
}

TopoDS_Shape CadDocument::drillShape(const CadFeature& feature,const TopoDS_Shape& source,const CadFaceOrigins& sourceNames,CadFaceOrigins& origins,juce::String& error,juce::StringArray& active)
{
    CadHoleParameters p=feature.hole;if(!readHoleParameters(holeParametersToJson(p),p,error))return {};
    CadSketchFrame frame;TopoDS_Face face;if(!holeSupport(feature.targetFeatureId,p.face,frame,face,error,active))return {};
    const auto centre=frame.origin.Translated(gp_Vec(frame.u)*p.u+gp_Vec(frame.v)*p.v);
    BRepClass_FaceClassifier classifier(face,centre,1e-7);
    if(classifier.State()!=TopAbs_IN){error="Hole centre is outside the selected face or lies on an edge/existing opening. Adjust U/V positioning.";return {};}
    const auto inward=frame.normal.Reversed();Bnd_Box box;BRepBndLib::AddOptimal(source,box,false,false);
    double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
    const double scale=std::hypot(x1-x0,y1-y0,z1-z0),epsilon=std::max(1e-6,scale*1e-9);double depth=p.depth;
    if(p.throughAll)
    {
        depth=0;for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1})depth=std::max(depth,gp_Vec(centre,gp_Pnt(x,y,z)).Dot(gp_Vec(inward)));
        depth+=std::max(1e-4,scale*1e-6);
    }
    if(!std::isfinite(depth)||depth<=epsilon){error="Hole depth is too small for the target geometry.";return {};}
    const gp_Ax2 axis(centre.Translated(gp_Vec(frame.normal)*epsilon),inward,frame.u);
    TopoDS_Shape tool=BRepPrimAPI_MakeCylinder(axis,p.diameter*0.5,depth+epsilon).Shape();
    auto names=toolNames(tool,feature.id,"wall","bottom");
    if(p.style!="plain")
    {
        double headDepth=p.headDepth;TopoDS_Shape head;CadFaceOrigins headNames;
        if(p.style=="countersink")
        {
            const double slope=std::tan(p.countersinkAngleDegrees*juce::MathConstants<double>::pi/360.0);
            headDepth=(p.headDiameter-p.diameter)*0.5/slope;
            if(!std::isfinite(headDepth)||headDepth<=epsilon||headDepth>=depth-epsilon){error="Countersink is deeper than the hole. Increase depth, reduce head diameter or change its included angle.";return {};}
            head=BRepPrimAPI_MakeCone(axis,p.headDiameter*0.5+epsilon*slope,p.diameter*0.5,headDepth+epsilon).Shape();headNames=toolNames(head,feature.id,"countersink","head-end");
        }
        else
        {
            if(headDepth>=depth-epsilon){error="Counterbore depth must be less than the hole depth/through-all extent.";return {};}
            head=BRepPrimAPI_MakeCylinder(axis,p.headDiameter*0.5,headDepth+epsilon).Shape();headNames=toolNames(head,feature.id,"counterbore-wall","counterbore-shoulder");
        }
        names.insert(names.end(),headNames.begin(),headNames.end());BRepAlgoAPI_Fuse join(tool,head);join.Build();
        if(!join.IsDone()||join.HasErrors()){error="Unable to construct the hole head geometry.";return {};}
        tool=join.Shape();names=carryFaces(join,names,tool);
    }
    BRepBuilderAPI_Copy copy(source,true,false);const auto target=copy.Shape();CadFaceOrigins targetNames;
    for(const auto& entry:sourceNames)targetNames.push_back({copy.ModifiedShape(entry.first),entry.second});
    BRepAlgoAPI_Cut operation(target,tool);operation.Build();
    if(!operation.IsDone()||operation.HasErrors()||operation.Shape().IsNull()){error="OCCT could not drill the target at this position and size.";return {};}
    const auto result=operation.Shape();
    if(!BRepCheck_Analyzer(result,true).IsValid()||!TopExp_Explorer(result,TopAbs_SOLID).More()){error="Hole would remove the target or produce an invalid solid.";return {};}
    const auto before=solidVolume(source),after=solidVolume(result);
    if(!std::isfinite(after)||after<=1e-12||before-after<=std::max(1e-9,std::abs(before)*1e-12)){error="Hole does not remove usable material or would eliminate the solid. Check position, size and depth.";return {};}
    for(TopExp_Explorer ex(result,TopAbs_SOLID);ex.More();ex.Next())if(solidVolume(ex.Current())<=1e-12){error="Hole produced a zero-volume or inverted solid.";return {};}
    targetNames.insert(targetNames.end(),names.begin(),names.end());origins=carryFaces(operation,targetNames,result);
    CadTopologyEvolution history;history.featureId=feature.id;
    for(const auto kind:{TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer ex(source,kind);ex.More();ex.Next())
    {
        const auto copied=copy.ModifiedShape(ex.Current());
        for(TopTools_ListIteratorOfListOfShape it(operation.Modified(copied));it.More();it.Next())history.modified.push_back({ex.Current(),it.Value()});
        for(TopTools_ListIteratorOfListOfShape it(operation.Generated(copied));it.More();it.Next())history.generated.push_back({ex.Current(),it.Value()});
        if(operation.IsDeleted(copied))history.deleted.push_back(ex.Current());
    }
    operationHistory.push_back(std::move(history));return result;
}

juce::var CadDocument::holeCommand(const juce::var& args)
{
    auto* reply=new juce::DynamicObject();juce::var result(reply);juce::String error;
    const auto fail=[&](const char* code,const juce::String& message){reply->setProperty("ok",false);reply->setProperty("code",code);reply->setProperty("error",message);return result;};
    if(!args.isObject())return fail("invalid_arguments","Hole arguments must be an object.");
    const auto action=args.getProperty("action","get").toString();auto id=args.getProperty("id","").toString();
    if(!juce::StringArray{"create","get","update","delete","face"}.contains(action))return fail("invalid_arguments","Hole action must be create, get, update, delete or face.");
    if(action=="face")
    {
        CadSketchFrame frame;TopoDS_Face face;juce::StringArray active;
        if(!holeSupport(args.getProperty("target","").toString(),args.getProperty("face","").toString(),frame,face,error,active))return fail("invalid_face",error);
        GProp_GProps props;BRepGProp::SurfaceProperties(face,props);const gp_Vec offset(frame.origin,props.CentreOfMass());
        auto* f=new juce::DynamicObject();f->setProperty("origin",juce::Array<juce::var>{frame.origin.X(),frame.origin.Y(),frame.origin.Z()});
        f->setProperty("u",juce::Array<juce::var>{frame.u.X(),frame.u.Y(),frame.u.Z()});f->setProperty("v",juce::Array<juce::var>{frame.v.X(),frame.v.Y(),frame.v.Z()});f->setProperty("normal",juce::Array<juce::var>{frame.normal.X(),frame.normal.Y(),frame.normal.Z()});
        reply->setProperty("ok",true);reply->setProperty("frame",f);reply->setProperty("centreUV",juce::Array<juce::var>{offset.Dot(gp_Vec(frame.u)),offset.Dot(gp_Vec(frame.v))});return result;
    }
    const auto* feature=find(id);
    if(action!="create"&&(feature==nullptr||feature->type!="hole"))return fail("not_found","Unknown Hole feature: "+id);
    if(action=="delete"){if(!remove(id,error))return fail("dependency_error",error);reply->setProperty("ok",true);reply->setProperty("id",id);reply->setProperty("deleted",true);return result;}
    if(action=="create"||action=="update")
    {
        const auto params=args.getProperty("parameters",juce::var());if(!params.isObject())return fail("invalid_arguments","Hole parameters must be an object.");
        const auto target=params.getProperty("target",action=="update"?feature->targetFeatureId:juce::String()).toString();
        if(target==id&&id.isNotEmpty())return fail("invalid_arguments","A hole cannot target itself.");
        auto p=action=="update"?feature->hole:CadHoleParameters();
        if(params.hasProperty("face"))p.face=params["face"].toString();
        if(action=="create"||target!=feature->targetFeatureId||p.face!=feature->hole.face)
        {
            CadSketchFrame frame;TopoDS_Face face;juce::StringArray active;if(!holeSupport(target,p.face,frame,face,error,active))return fail("invalid_face",error);
            GProp_GProps props;BRepGProp::SurfaceProperties(face,props);const gp_Vec delta(frame.origin,props.CentreOfMass());p.u=delta.Dot(gp_Vec(frame.u));p.v=delta.Dot(gp_Vec(frame.v));
        }
        auto definition=juce::JSON::parse(juce::JSON::toString(params,false,17));definition.getDynamicObject()->removeProperty("target");
        if(!readHoleParameters(definition,p,error))return fail("invalid_arguments",error);
        auto created=id;
        const bool ok=mutate([&](juce::String&)
        {
            if(action=="create"){CadFeature f;f.id=newId("hole",nextNumber++);f.name="Hole "+juce::String(nextNumber-1);f.type="hole";items.push_back(f);created=f.id;}
            for(auto& f:items)if(f.id==created){f.targetFeatureId=target;f.firstOperand=target;f.hole=p;}
            for(auto& f:items)if(f.id==target)f.visible=false;return true;
        },error);
        if(!ok)return fail("modeling_failed",error);id=created;feature=find(id);
    }
    auto p=holeParametersToJson(feature->hole);p.getDynamicObject()->setProperty("target",feature->targetFeatureId);
    reply->setProperty("ok",true);reply->setProperty("id",id);reply->setProperty("type","hole");reply->setProperty("parameters",p);return result;
}
}
