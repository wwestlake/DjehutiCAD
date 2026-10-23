#include "CadDocument.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <BRep_Builder.hxx>
#include <cmath>

namespace djehuti::cad
{
namespace
{
juce::var obj() { return juce::var(new juce::DynamicObject()); }
juce::var clone(const juce::var& v) { return juce::JSON::fromString(juce::JSON::toString(v,false,17)); }
bool number(const juce::var& v) { return (v.isInt()||v.isInt64()||v.isDouble())&&std::isfinite((double)v); }
bool vector3(const juce::var& v,bool direction=false)
{
    if(!v.isArray()||v.size()!=3)return false;
    double sum=0;for(const auto& x:*v.getArray()){if(!number(x))return false;sum+=(double)x*(double)x;}
    return std::isfinite(sum)&&(!direction||sum>1e-18);
}
double area(const TopoDS_Shape& s) { GProp_GProps p;BRepGProp::SurfaceProperties(s,p);return p.Mass(); }
double volume(const TopoDS_Shape& s) { GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass(); }
juce::var xyz(const gp_Pnt& p) { return juce::Array<juce::var>{p.X(),p.Y(),p.Z()}; }
bool selfIntersection(const TopoDS_Shape& s)
{
    BOPAlgo_ArgumentAnalyzer check;check.SetShape1(s);check.SelfInterMode()=true;check.Perform();return check.HasFaulty();
}
bool validShape(const TopoDS_Shape& s,bool solid,juce::String& error)
{
    if(s.IsNull()||!TopExp_Explorer(s,TopAbs_FACE).More()||!BRepCheck_Analyzer(s,true).IsValid()||area(s)<1e-10)
    {error="The operation produced invalid, empty or degenerate surface topology.";return false;}
    if(selfIntersection(s)){error="The result self-intersects. Reduce extension/thickness or revise the source geometry.";return false;}
    if(solid&&(!TopExp_Explorer(s,TopAbs_SOLID).More()||!std::isfinite(volume(s))||volume(s)<=1e-10))
    {error="A valid, consistently oriented, positive-volume solid could not be constructed.";return false;}
    return true;
}
bool wireClosed(const TopoDS_Wire& w)
{
    TopoDS_Vertex a,b;TopExp::Vertices(w,a,b);return !a.IsNull()&&!b.IsNull()&&BRep_Tool::Pnt(a).Distance(BRep_Tool::Pnt(b))<1e-7;
}
TopoDS_Shape facesTogether(const std::vector<TopoDS_Face>& faces)
{
    if(faces.size()==1)return faces.front();
    BRepBuilderAPI_Sewing sew(1e-7);for(const auto& f:faces)sew.Add(f);sew.Perform();return sew.SewedShape();
}
juce::var pipeParameters(const juce::var& p,const juce::String& method)
{
    auto result=modelingDefaults(method);
    for(const auto& v:result.getDynamicObject()->getProperties())
        if(p.hasProperty(v.name))result.getDynamicObject()->setProperty(v.name,clone(p[v.name]));
    return result;
}
void nameFaces(const TopoDS_Shape& s,const juce::String& id,CadFaceOrigins& names)
{
    int i=0;for(TopExp_Explorer e(s,TopAbs_FACE);e.More();e.Next(),++i)
    {bool present=false;for(const auto& n:names)if(n.first.IsSame(e.Current()))present=true;if(!present)names.push_back({e.Current(),{id+"/surface:"+juce::String(i)}});}
}
template<class Operation,class History>
void track(Operation& operation,const TopoDS_Shape& source,const CadFaceOrigins& sourceNames,
           const TopoDS_Shape& result,const juce::String& id,CadFaceOrigins& names,History& history)
{
    history.featureId=id;
    for(const auto kind:{TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer ex(source,kind);ex.More();ex.Next())
    {
        for(TopTools_ListIteratorOfListOfShape it(operation.Modified(ex.Current()));it.More();it.Next())history.modified.push_back({ex.Current(),it.Value()});
        for(TopTools_ListIteratorOfListOfShape it(operation.Generated(ex.Current()));it.More();it.Next())history.generated.push_back({ex.Current(),it.Value()});
        if(operation.IsDeleted(ex.Current()))history.deleted.push_back(ex.Current());
    }
    for(TopExp_Explorer e(result,TopAbs_FACE);e.More();e.Next())
    {
        juce::StringArray tokens;
        for(const auto& n:sourceNames)
        {
            bool match=e.Current().IsSame(n.first);
            for(TopTools_ListIteratorOfListOfShape it(operation.Modified(n.first));it.More();it.Next())if(e.Current().IsSame(it.Value()))match=true;
            for(TopTools_ListIteratorOfListOfShape it(operation.Generated(n.first));it.More();it.Next())if(e.Current().IsSame(it.Value()))match=true;
            if(match)for(const auto& token:n.second)tokens.addIfNotAlreadyThere(token);
        }
        if(!tokens.isEmpty())names.push_back({e.Current(),tokens});
    }
}
}

juce::String modelingLabel(const juce::String& type)
{
    if(type=="sweep")return "Sweep";if(type=="loft")return "Loft";if(type=="mill")return "Virtual Router / Mill";
    if(type=="surface")return "Surface";if(type=="surface_trim")return "Trim Surface";if(type=="surface_extend")return "Extend Surface";
    if(type=="surface_sew")return "Sew Surfaces";if(type=="surface_solidify")return "Solidify Shell";return "Thicken Surface";
}

juce::var surfaceDefaults(const juce::String& type)
{
    auto p=obj();auto* o=p.getDynamicObject();
    if(type=="surface")
    {
        o->setProperty("flipNormals",false);o->setProperty("method","planar");o->setProperty("profile","");o->setProperty("distance",10.0);o->setProperty("direction",juce::Array<juce::var>{0,0,1});
        const auto revolve=revolveParametersToJson(CadRevolveParameters());for(const auto& v:revolve.getDynamicObject()->getProperties())o->setProperty(v.name,v.value);
        for(const auto* method:{"sweep","loft"}){const auto defaults=modelingDefaults(method);for(const auto& v:defaults.getDynamicObject()->getProperties())
            if(v.name.toString()!="target"&&v.name.toString()!="operation")o->setProperty(v.name,clone(v.value));}
        return p;
    }
    if(type=="surface_sew") { o->setProperty("sources",juce::Array<juce::var>());o->setProperty("tolerance",1e-5);return p; }
    o->setProperty("target","");
    if(type!="surface_solidify")o->setProperty("face","");
    if(type=="surface_trim")
    {auto tool=obj();tool.getDynamicObject()->setProperty("sketch","");o->setProperty("tool",tool);o->setProperty("regionReference",obj());o->setProperty("seed",juce::Array<juce::var>{0,0,0});o->setProperty("keep",true);}
    if(type=="surface_extend") {o->setProperty("distance",5.0);o->setProperty("sides",juce::Array<juce::var>{true,true,true,true});}
    if(type=="surface_thicken") {o->setProperty("thickness",2.0);o->setProperty("direction","positive");}
    return p;
}

bool validateSurface(const juce::String& type,const juce::var& p,juce::String& error)
{
    const auto fail=[&](const juce::String& text){error=text;return false;};
    if(!p.isObject()||!isSurfaceType(type))return fail("Invalid surface definition.");
    const auto defaults=surfaceDefaults(type);for(const auto& v:p.getDynamicObject()->getProperties())if(!defaults.hasProperty(v.name))return fail("Unknown surface parameter: "+v.name.toString());
    if(type=="surface")
    {
        if(!p["flipNormals"].isBool())return fail("Flip normals must be boolean.");
        const auto method=p["method"].toString();if(!juce::StringArray{"planar","extrude","revolve","sweep","loft"}.contains(method))return fail("Surface method must be planar, extrude, revolve, sweep or loft.");
        if(method=="sweep"||method=="loft")return validateModeling(method,pipeParameters(p,method),error);
        if(!p["profile"].isString()||p["profile"].toString().isEmpty())return fail("Select a source profile sketch.");
        if(method=="extrude"&&(!number(p["distance"])||std::abs((double)p["distance"])<1e-6||std::abs((double)p["distance"])>1e6||!vector3(p["direction"],true)))return fail("Extrusion needs a nonzero direction and signed distance between 0.000001 and 1000000 mm.");
        if(method=="revolve") {auto r=obj();for(const auto* key:{"axis","angleDegrees","reverse"})r.getDynamicObject()->setProperty(key,p[key]);CadRevolveParameters parameters;if(!readRevolveParameters(r,parameters,error))return false;}
        return true;
    }
    if(type=="surface_sew")
    {
        if(!p["sources"].isArray()||p["sources"].size()<2||p["sources"].size()>128)return fail("Choose 2 to 128 surfaces or shells to sew.");
        juce::StringArray seen;for(const auto& s:*p["sources"].getArray())
        {
            if(!(s.isString()&&s.toString().isNotEmpty())&&!(s.isObject()&&s["feature"].isString()&&s["feature"].toString().isNotEmpty()&&s["face"].isString()&&s["face"].toString().isNotEmpty()))return fail("Sew sources must be feature IDs or {feature,face} persistent face references.");
            if(s.isObject())for(const auto& v:s.getDynamicObject()->getProperties())if(v.name.toString()!="feature"&&v.name.toString()!="face")return fail("Unknown source-reference field.");
            const auto key=juce::JSON::toString(s,true);if(seen.contains(key))return fail("Sew sources must be distinct.");seen.add(key);
        }
        return (number(p["tolerance"])&&(double)p["tolerance"]>=1e-7&&(double)p["tolerance"]<=0.1)||fail("Sew tolerance must be between 0.0000001 and 0.1 mm.");
    }
    if(!p["target"].isString()||p["target"].toString().isEmpty())return fail("Choose a source surface or shell.");
    if(p.hasProperty("face")&&!p["face"].isString())return fail("Face must be a persistent face token, or empty for the whole source.");
    if(type=="surface_trim")
    {
        const auto t=p["tool"];if(!t.isObject())return fail("Trim tool must reference a boundary sketch or intersecting feature.");
        for(const auto& v:t.getDynamicObject()->getProperties())if(!juce::StringArray{"sketch","feature","face"}.contains(v.name.toString())||!v.value.isString())return fail("Invalid trim-tool field.");
        if(t["sketch"].toString().isNotEmpty()==t["feature"].toString().isNotEmpty())return fail("Choose either a trim sketch or an intersecting feature.");
        if(!vector3(p["seed"])||!p["keep"].isBool())return fail("Trim requires a finite world-space seed point and keep boolean.");
        const auto ref=p["regionReference"];if(!ref.isObject())return fail("Trim region reference must be an object.");
        if(ref.getDynamicObject()->getProperties().size()>0)
        {if(!ref["face"].isString()||ref["face"].toString().isEmpty()||!number(ref["u"])||!number(ref["v"]))return fail("Invalid trim region face/UV reference.");for(const auto& v:ref.getDynamicObject()->getProperties())if(!juce::StringArray{"face","u","v"}.contains(v.name.toString()))return fail("Unknown trim region reference field.");}
    }
    if(type=="surface_extend")
    {
        if(!number(p["distance"])||(double)p["distance"]<1e-6||(double)p["distance"]>1e5)return fail("Extension distance must be between 0.000001 and 100000 mm.");
        if(!p["sides"].isArray()||p["sides"].size()!=4)return fail("Extension sides are four booleans: U min, U max, V min, V max.");
        bool any=false;for(const auto& v:*p["sides"].getArray()){if(!v.isBool())return fail("Extension sides must be booleans.");any=any||(bool)v;}if(!any)return fail("Choose at least one boundary to extend.");
    }
    if(type=="surface_thicken"&&(!number(p["thickness"])||(double)p["thickness"]<1e-6||(double)p["thickness"]>1e5||!juce::StringArray{"positive","negative","symmetric"}.contains(p["direction"].toString())))return fail("Thickness must be positive (0.000001Ã¢â‚¬â€œ100000 mm); direction is positive, negative or symmetric.");
    return true;
}

juce::var surfaceTopology(const TopoDS_Shape& s)
{
    auto result=obj();auto* o=result.getDynamicObject();
    for(const auto kind:{TopAbs_FACE,TopAbs_SHELL,TopAbs_SOLID})
    {TopTools_IndexedMapOfShape map;TopExp::MapShapes(s,kind,map);o->setProperty(kind==TopAbs_FACE?"faces":kind==TopAbs_SHELL?"shells":"solids",map.Extent());}
    TopTools_IndexedDataMapOfShapeListOfShape edges;TopExp::MapShapesAndAncestors(s,TopAbs_EDGE,TopAbs_FACE,edges);int multiple=0;juce::Array<juce::var> free;
    for(int i=1;i<=edges.Extent();++i)
    {
        const auto edge=TopoDS::Edge(edges.FindKey(i));if(BRep_Tool::Degenerated(edge))continue;
        const auto& faces=edges.FindFromIndex(i);int uses=faces.Extent();if(uses==1&&BRep_Tool::IsClosed(edge,TopoDS::Face(faces.First())))uses=2;
        if(uses>2)++multiple;
        if(uses==1)
        {
            auto item=obj();GProp_GProps length;BRepGProp::LinearProperties(edge,length);item.getDynamicObject()->setProperty("length_mm",length.Mass());
            TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b);if(!a.IsNull())item.getDynamicObject()->setProperty("start",xyz(BRep_Tool::Pnt(a)));if(!b.IsNull())item.getDynamicObject()->setProperty("end",xyz(BRep_Tool::Pnt(b)));free.add(item);
        }
    }
    o->setProperty("area_mm2",area(s));o->setProperty("volume_mm3",(int)result["solids"]>0?volume(s):0.0);o->setProperty("unmatchedEdges",free.size());o->setProperty("freeBoundaries",free);o->setProperty("nonmanifoldEdges",multiple);o->setProperty("closed",free.isEmpty()&&multiple==0);return result;
}

bool CadDocument::modelingProfile(const juce::String& id,TopoDS_Wire& wire,CadSketchFrame& frame,juce::String& error,juce::StringArray& active)
{
    const auto* sketch=findSketch(id);if(sketch==nullptr){error="Unknown profile sketch: "+id;return false;}
    if(!resolveSketchFrame(*sketch,frame,error,active))return false;
    auto p=obj();p.getDynamicObject()->setProperty("sketch",id);wire=modelingPath(p,nullptr,error,active);return !wire.IsNull();
}

bool CadDocument::surfaceInput(const juce::String& id,const juce::String& faceToken,TopoDS_Shape& shape,CadFaceOrigins& names,juce::String& error,juce::StringArray& active)
{
    shape=evaluate(id,error,active,&names);if(shape.IsNull())return false;
    if(faceToken.isNotEmpty())
    {
        TopoDS_Shape found;int count=0;
        for(const auto& n:names)if(n.second.contains(faceToken))for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next())if(e.Current().IsSame(n.first)){found=e.Current();++count;}
        if(count!=1){error="The selected face is missing or split. Reselect a unique persistent face.";return false;}
        shape=found;std::erase_if(names,[&](const auto& n){return !n.first.IsSame(shape);});
    }
    if(!TopExp_Explorer(shape,TopAbs_FACE).More()){error="Choose a surface, shell or a named face.";return false;}
    BRepBuilderAPI_Copy copied(shape,true,false);CadFaceOrigins newNames;
    for(const auto& n:names){const auto image=copied.ModifiedShape(n.first);if(!image.IsNull())newNames.push_back({image,n.second});}
    CadTopologyEvolution copying;copying.featureId=active[active.size()-1];
    for(const auto kind:{TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer e(shape,kind);e.More();e.Next()){const auto image=copied.ModifiedShape(e.Current());if(!image.IsNull())copying.modified.push_back({e.Current(),image});}
    operationHistory.push_back(std::move(copying));shape=copied.Shape();names=std::move(newNames);return true;
}

TopoDS_Shape CadDocument::evaluateSurface(const CadFeature& feature,CadFaceOrigins& origins,juce::String& error,juce::StringArray& active)
{
    const auto p=feature.modeling;const auto type=feature.type;if(!validateSurface(type,p,error))return {};
    TopoDS_Shape result;CadTopologyEvolution history;history.featureId=feature.id;
    if(type=="surface")
    {
        const auto method=p["method"].toString();
        if(method=="sweep"||method=="loft")
        {auto reused=feature;reused.type=method;reused.modeling=pipeParameters(p,method);result=evaluateModeling(reused,origins,error,active,true);}
        else if(method=="planar")
        {TopoDS_Face face;CadSketchFrame frame;if(!modelingSection(p["profile"].toString(),face,frame,error,active))return {};result=face;history.generated.push_back({find(p["profile"].toString())->shape,face});}
        else
        {
            TopoDS_Wire profile;CadSketchFrame frame;if(!modelingProfile(p["profile"].toString(),profile,frame,error,active))return {};
            if(method=="extrude")
            {
                gp_Vec v((double)p["direction"][0],(double)p["direction"][1],(double)p["direction"][2]);v.Normalize();v*=double(p["distance"]);
                BRepPrimAPI_MakePrism prism(profile,v,true);result=prism.Shape();track(prism,profile,{},result,feature.id,origins,history);
            }
            else
            {
                CadRevolveParameters r;auto parameters=obj();for(const auto* key:{"axis","angleDegrees","reverse"})parameters.getDynamicObject()->setProperty(key,p[key]);if(!readRevolveParameters(parameters,r,error))return {};
                gp_Pnt origin;gp_Dir axis;
                if(r.axisType=="sketchU"||r.axisType=="sketchV"){origin=frame.origin;axis=r.axisType=="sketchU"?frame.u:frame.v;}
                else if(r.axisType=="custom"){origin=gp_Pnt(r.origin[0],r.origin[1],r.origin[2]);axis=gp_Dir(r.direction[0],r.direction[1],r.direction[2]);}
                else axis=r.axisType=="worldX"?gp::DX():r.axisType=="worldY"?gp::DY():gp::DZ();
                if(std::abs(axis.Dot(frame.normal))>1e-9||std::abs(gp_Vec(frame.origin,origin).Dot(gp_Vec(frame.normal)))>1e-7){error="The surface revolution axis must lie in the profile plane.";return {};}
                BRepPrimAPI_MakeRevol revolve(profile,gp_Ax1(origin,r.reverse?axis.Reversed():axis),r.angleDegrees*juce::MathConstants<double>::pi/180,true);if(!revolve.IsDone()){error="Surface revolution failed. Check the axis and profile crossing.";return {};}result=revolve.Shape();track(revolve,profile,{},result,feature.id,origins,history);
            }
        }
    }
    else if(type=="surface_sew")
    {
        BRepBuilderAPI_Sewing sew((double)p["tolerance"],true,true,true,false);std::vector<TopoDS_Shape> inputs;CadFaceOrigins inputNames;
        for(const auto& ref:*p["sources"].getArray())
        {
            TopoDS_Shape source;CadFaceOrigins names;
            if(!surfaceInput(ref.isString()?ref.toString():ref["feature"].toString(),ref.isString()?juce::String():ref["face"].toString(),source,names,error,active))return {};
            if(TopExp_Explorer(source,TopAbs_SOLID).More()){error="Sew accepts surfaces/shells or selected solid faces, not entire solids.";return {};}
            inputs.push_back(source);inputNames.insert(inputNames.end(),names.begin(),names.end());sew.Add(source);
        }
        sew.Perform();result=sew.SewedShape();
        if(result.IsNull()||sew.NbMultipleEdges()>0||sew.NbDeletedFaces()>0){error="Sewing failed: nonmanifold edges or faces smaller than the tolerance. Reduce tolerance or repair inputs.";return {};}
        if(result.ShapeType()!=TopAbs_SHELL&&result.ShapeType()!=TopAbs_FACE){error="The surfaces do not sew into one connected shell at this tolerance. Check the unmatched boundaries/gaps.";return {};}
        for(const auto& source:inputs)for(const auto kind:{TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer e(source,kind);e.More();e.Next())
        {const auto mapped=sew.IsModifiedSubShape(e.Current())?sew.ModifiedSubShape(e.Current()):e.Current();history.modified.push_back({e.Current(),mapped});}
        for(const auto& n:inputNames){const auto image=sew.IsModifiedSubShape(n.first)?sew.ModifiedSubShape(n.first):n.first;for(TopExp_Explorer e(result,TopAbs_FACE);e.More();e.Next())if(e.Current().IsSame(image))origins.push_back({e.Current(),n.second});}
    }
    else
    {
        TopoDS_Shape source;CadFaceOrigins names;if(!surfaceInput(p["target"].toString(),p["face"].toString(),source,names,error,active))return {};
        if(TopExp_Explorer(source,TopAbs_SOLID).More()){error="Select an open surface/shell or a named face; whole solids are not surface inputs.";return {};}
        if(type=="surface_trim")
        {
            TopoDS_Shape tool;CadFaceOrigins toolNames;const auto t=p["tool"];
            if(t["sketch"].toString().isNotEmpty()){TopoDS_Wire w;CadSketchFrame frame;if(!modelingProfile(t["sketch"].toString(),w,frame,error,active))return {};tool=w;}
            else if(!surfaceInput(t["feature"].toString(),t["face"].toString(),tool,toolNames,error,active))return {};
            TopTools_ListOfShape arguments,tools;arguments.Append(source);tools.Append(tool);BRepAlgoAPI_Splitter split;split.SetArguments(arguments);split.SetTools(tools);split.SetNonDestructive(true);split.Build();
            if(!split.IsDone()||split.HasErrors()||split.Shape().IsNull()){error="Trim intersection failed. The curves must lie on the surface or intersecting geometry must cross it.";return {};}
            TopTools_IndexedMapOfShape before,after;TopExp::MapShapes(source,TopAbs_FACE,before);TopExp::MapShapes(split.Shape(),TopAbs_FACE,after);
            if(after.Extent()<=before.Extent()){error="The trim tool does not divide the surface into selectable regions.";return {};}
            gp_Pnt seed((double)p["seed"][0],(double)p["seed"][1],(double)p["seed"][2]);
            if(p["regionReference"]["face"].toString().isNotEmpty())
            {int count=0;for(const auto& n:names)if(n.second.contains(p["regionReference"]["face"].toString())){seed=BRepAdaptor_Surface(TopoDS::Face(n.first)).Value((double)p["regionReference"]["u"],(double)p["regionReference"]["v"]);++count;}if(count!=1){error="The trim region's anchored source face is missing or split. Pick its region again.";return {};}}
            const auto vertex=BRepBuilderAPI_MakeVertex(seed).Vertex();int selected=-1;
            for(int i=1;i<=after.Extent();++i)
            {BRepExtrema_DistShapeShape distance(vertex,after(i));distance.Perform();if(distance.IsDone()&&distance.Value()<1e-6){if(selected!=-1){error="Trim seed lies on a shared boundary. Pick inside the desired region.";return {};}selected=i;}}
            if(selected<0){error="Trim seed must lie on the interior of the desired resulting surface region (world coordinates).";return {};}
            for(TopExp_Explorer e(after(selected),TopAbs_EDGE);e.More();e.Next()){BRepExtrema_DistShapeShape distance(vertex,e.Current());if(distance.IsDone()&&distance.Value()<1e-6){error="Pick a trim region away from its boundary.";return {};}}
            std::vector<TopoDS_Face> retained;for(int i=1;i<=after.Extent();++i)if((bool)p["keep"]?(i==selected):(i!=selected))retained.push_back(TopoDS::Face(after(i)));
            if(retained.empty()){error="Trimming would remove the entire surface.";return {};}result=facesTogether(retained);track(split,source,names,result,feature.id,origins,history);
            const double delta=area(source)-area(result);if(delta<1e-8){error="Trim did not remove surface area.";return {};}
        }
        if(type=="surface_extend")
        {
            TopTools_IndexedMapOfShape faces;TopExp::MapShapes(source,TopAbs_FACE,faces);if(faces.Extent()!=1){error="Extend requires a single face. Select one named face of the shell.";return {};}
            const auto face=TopoDS::Face(faces(1));double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            BRepBuilderAPI_MakeFace rectangle(BRep_Tool::Surface(face),u0,u1,v0,v1,1e-7);
            if(!rectangle.IsDone()||std::abs(area(rectangle.Face())-area(face))>std::max(1e-7,area(face)*1e-7)){error="Extend supports untrimmed rectangular U/V boundaries. Trimmed contours, holes and arbitrary boundary extensions are unsupported.";return {};}
            TopoDS_Face extended;BRepLib::ExtendFace(face,(double)p["distance"],(bool)p["sides"][0],(bool)p["sides"][1],(bool)p["sides"][2],(bool)p["sides"][3],extended);
            if(extended.IsNull()||area(extended)<=area(face)+1e-8){error="This surface cannot continue in the requested U/V directions (closed/periodic limit or unsupported basis surface).";return {};}
            result=extended;history.modified.push_back({face,extended});for(const auto& n:names)if(n.first.IsSame(face))origins.push_back({extended,n.second});
        }
        if(type=="surface_solidify")
        {
            const auto topology=surfaceTopology(source);
            if((int)topology["unmatchedEdges"]>0){error="Solidify requires closure: "+topology["unmatchedEdges"].toString()+" unmatched boundary edges remain. Sew/repair the gaps first.";return {};}
            if((int)topology["nonmanifoldEdges"]>0){error="Solidify rejects nonmanifold boundaries.";return {};}
            TopTools_IndexedMapOfShape shells;TopExp::MapShapes(source,TopAbs_SHELL,shells);if(shells.Extent()!=1){error="Solidify requires one closed connected shell.";return {};}
            BRepBuilderAPI_MakeSolid make(TopoDS::Shell(shells(1)));if(!make.IsDone()){error="OCCT could not construct a solid from this shell.";return {};}
            auto solid=make.Solid();if(!BRepLib::OrientClosedSolid(solid)){error="The shell is not closed and consistently orientable.";return {};}result=solid;origins=names;history.generated.push_back({source,solid});
        }
        if(type=="surface_thicken")
        {
            if(source.ShapeType()!=TopAbs_FACE&&source.ShapeType()!=TopAbs_SHELL){error="Thicken requires one connected face or open shell.";return {};}
            if((bool)surfaceTopology(source)["closed"]){error="Thicken expects a face or open shell. Select an open portion; use Solidify for a closed shell.";return {};}
            const auto mode=p["direction"].toString();const double thickness=(double)p["thickness"];
            if(mode=="symmetric")
            {
                BRepOffsetAPI_MakeOffsetShape offset;offset.PerformBySimple(source,-0.5*thickness);
                if(!offset.IsDone()||!validShape(offset.Shape(),false,error)){if(error.isEmpty())error="The symmetric half-offset failed. Reduce thickness or repair the surface.";return {};}
                CadFaceOrigins movedNames;track(offset,source,names,offset.Shape(),feature.id,movedNames,history);source=offset.Shape();names=movedNames;
            }
            BRepOffsetAPI_MakeThickSolid thick;thick.MakeThickSolidBySimple(source,mode=="negative"?-thickness:thickness);
            if(!thick.IsDone()||thick.Shape().IsNull()){error="Surface thickening failed: incompatible topology, excessive thickness or an invalid offset.";return {};}
            result=thick.Shape();track(thick,source,names,result,feature.id,origins,history);
            if(result.ShapeType()==TopAbs_SOLID){auto solid=TopoDS::Solid(result);if(!BRepLib::OrientClosedSolid(solid)){error="Thickening could not produce an orientable closed solid.";return {};}result=solid;}
        }
    }
    if(type=="surface"&&(bool)p["flipNormals"]&&!result.IsNull())result.Reverse();
    if(!validShape(result,type=="surface_solidify"||type=="surface_thicken",error))return {};
    // TNaming builders replace an attribute's evolution list. Aggregate this feature's
    // copy, offset and operation steps before handing them to the existing writer.
    for(const auto& step:operationHistory)if(step.featureId==feature.id)
    {history.generated.insert(history.generated.end(),step.generated.begin(),step.generated.end());history.modified.insert(history.modified.end(),step.modified.begin(),step.modified.end());history.deleted.insert(history.deleted.end(),step.deleted.begin(),step.deleted.end());}
    std::erase_if(operationHistory,[&](const auto& step){return step.featureId==feature.id;});
    nameFaces(result,feature.id,origins);if(!history.generated.empty()||!history.modified.empty()||!history.deleted.empty())operationHistory.push_back(std::move(history));return result;
}
}
