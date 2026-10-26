#include "CadDocument.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepFill.hxx>
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
#include <BRepClass_FaceClassifier.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <gp_Pnt2d.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

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
// Phase-1 creation references use the existing path/wire schema. Empty source preserves legacy profile IDs.
juce::var creationSource(const juce::var& p,bool second=false)
{
    const auto source=p[second?"secondSource":"source"];
    if(source.isObject()&&source.getDynamicObject()->getProperties().size()>0)return clone(source);
    auto ref=obj();if(!second){ref.getDynamicObject()->setProperty("sketch",p["profile"]);if(p["profileEntities"].isArray()&&p["profileEntities"].size()>0)ref.getDynamicObject()->setProperty("entities",p["profileEntities"]);}return ref;
}
bool validCreationSource(const juce::var& ref,juce::String& error)
{
    const auto fail=[&](const juce::String& text){error=text;return false;};
    if(!ref.isObject())return fail("Choose a sketch or a connected edge/wire source.");
    for(const auto& item:ref.getDynamicObject()->getProperties())if(!juce::StringArray{"sketch","entities","feature","edges","reverse"}.contains(item.name.toString()))return fail("Unknown surface source parameter: "+item.name.toString());
    const bool sketch=ref["sketch"].isString()&&ref["sketch"].toString().isNotEmpty(),feature=ref["feature"].isString()&&ref["feature"].toString().isNotEmpty();
    if(sketch==feature)return fail("A surface source must identify either one sketch or one feature's edges.");
    if((sketch&&ref.hasProperty("edges"))||(feature&&ref.hasProperty("entities")))return fail("Sketch sources use entity IDs; model sources use persistent edge references.");
    if(ref.hasProperty("reverse")&&!ref["reverse"].isBool())return fail("Source reversal must be boolean.");
    const auto key=sketch?"entities":"edges";if((feature||ref.hasProperty(key))&&!ref[key].isArray())return fail("Source entities/edges must be an array of stable identifiers.");
    if(feature&&ref[key].size()==0)return fail("Select the connected edges defining the surface boundary or curve.");
    juce::StringArray seen;if(const auto* values=ref[key].getArray())for(const auto& value:*values){if(!value.isString()||value.toString().isEmpty()||seen.contains(value.toString()))return fail("Source identifiers must be nonempty, unique strings (use persistent edge references).");seen.add(value.toString());}
    return true;
}
bool validCutTool(const juce::var& ref,juce::String& error,bool allowFace=true)
{
    if(!ref.isObject()){error="Choose a sketch curve, connected model edges or an intersecting surface.";return false;}
    for(const auto& field:ref.getDynamicObject()->getProperties())
        if(!juce::StringArray{"sketch","entities","feature","edges","face"}.contains(field.name.toString())||
           (field.name.toString()=="face"&&!allowFace)){error="Unknown or unsupported cutting-geometry reference field.";return false;}
    const bool sketch=ref["sketch"].isString()&&ref["sketch"].toString().isNotEmpty();
    const bool feature=ref["feature"].isString()&&ref["feature"].toString().isNotEmpty();
    if(sketch==feature){error="Choose exactly one sketch or model feature for cutting geometry.";return false;}
    if((sketch&&(ref.hasProperty("face")||ref.hasProperty("edges")))||
       (feature&&ref.hasProperty("entities"))||(ref.hasProperty("face")&&ref.hasProperty("edges")))
    {error="Sketches use entity IDs; model geometry uses either persistent edges or one face.";return false;}
    const auto key=sketch?"entities":"edges";
    if(ref.hasProperty(key)&&!ref[key].isArray()){error="Selected curve/edge identifiers must be an array.";return false;}
    if(ref.hasProperty("edges")&&ref["edges"].size()==0){error="Select at least one persistent cutting edge.";return false;}
    if(ref.hasProperty("face")&&(!ref["face"].isString()||ref["face"].toString().isEmpty())){error="The selected cutting face needs a persistent face token.";return false;}
    juce::StringArray seen;if(const auto* ids=ref[key].getArray())for(const auto& id:*ids)
        if(!id.isString()||id.toString().isEmpty()||seen.contains(id.toString())){error="Cutting curve/edge IDs must be unique nonempty strings.";return false;}else seen.add(id.toString());
    return true;
}
bool validRegionReference(const juce::var& ref,juce::String& error,bool required)
{
    if(!ref.isObject()){error="Region reference must be an object.";return false;}
    if(ref.getDynamicObject()->getProperties().size()==0){if(required){error="A split region needs a source-face anchor.";return false;}return true;}
    if(!ref["face"].isString()||ref["face"].toString().isEmpty()||!number(ref["u"])||!number(ref["v"]))
    {error="Region reference needs a persistent face and finite U/V coordinates.";return false;}
    for(const auto& field:ref.getDynamicObject()->getProperties())if(!juce::StringArray{"face","u","v"}.contains(field.name.toString())){error="Unknown region-reference field.";return false;}
    return true;
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
    if(selfIntersection(s)){error="The result self-intersects. Revise the source curves, axis or surface parameters.";return false;}
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
    // Preserve unique construction origins. Split/offset images can inherit the
    // same token; give each such face its own result role instead of exposing an
    // ambiguous token to face selection and downstream tools.
    CadFaceOrigins canonical;std::map<juce::String,int> uses;
    for(TopExp_Explorer e(s,TopAbs_FACE);e.More();e.Next())
    {juce::StringArray tokens;for(const auto& n:names)if(n.first.IsSame(e.Current()))for(const auto& t:n.second)tokens.addIfNotAlreadyThere(t);for(const auto& t:tokens)++uses[t];canonical.push_back({e.Current(),tokens});}
    int index=0;for(auto& n:canonical)
    {juce::StringArray tokens;for(const auto& t:n.second)if(uses[t]==1)tokens.addIfNotAlreadyThere(t);tokens.addIfNotAlreadyThere(id+"/surface:"+juce::String(index++));for(const auto& t:n.second)tokens.addIfNotAlreadyThere(t);n.second=tokens;}names=std::move(canonical);
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
    if(type=="tslot_rail")return "T-slot rail";if(type=="tslot_half")return "T-slot rail half";
    if(type=="din_rail")return "DIN rail";if(type=="din_bottom")return "DIN module bottom";if(type=="din_clip")return "DIN clip";if(type=="din_screw")return "DIN friction screw";
    if(type=="gear")return "Gear";    if(type=="sweep")return "Sweep";if(type=="loft")return "Loft";if(type=="mill")return "Virtual Router / Mill";
    if(type=="surface")return "Surface";if(type=="surface_trim")return "Trim Surface";if(type=="surface_extend")return "Extend Surface";if(type=="surface_split")return "Split Surface";if(type=="surface_split_region")return "Split Region";
    if(type=="surface_sew")return "Sew Surfaces";if(type=="surface_solidify")return "Create Solid from Shell";if(type=="surface_offset")return "Offset Surface";if(type=="surface_hollow")return "Hollow Solid";return "Thicken Surface";
}

juce::var surfaceDefaults(const juce::String& type)
{
    auto p=obj();auto* o=p.getDynamicObject();
    if(type=="surface")
    {
        o->setProperty("source",obj());o->setProperty("secondSource",obj());o->setProperty("profileEntities",juce::Array<juce::var>());o->setProperty("flipNormals",false);o->setProperty("method","planar");o->setProperty("profile","");o->setProperty("distance",10.0);o->setProperty("direction",juce::Array<juce::var>{0,0,1});
        const auto revolve=revolveParametersToJson(CadRevolveParameters());for(const auto& v:revolve.getDynamicObject()->getProperties())o->setProperty(v.name,v.value);
        for(const auto* method:{"sweep","loft"}){const auto defaults=modelingDefaults(method);for(const auto& v:defaults.getDynamicObject()->getProperties())
            if(v.name.toString()!="target"&&v.name.toString()!="operation")o->setProperty(v.name,clone(v.value));}
        return p;
    }
    if(type=="surface_sew") { o->setProperty("sources",juce::Array<juce::var>());o->setProperty("tolerance",1e-5);return p; }
    o->setProperty("target","");
    if(type!="surface_solidify"&&type!="surface_hollow")o->setProperty("face","");
    if(type=="surface_trim")
    {auto tool=obj();tool.getDynamicObject()->setProperty("sketch","");o->setProperty("tool",tool);o->setProperty("regionReference",obj());o->setProperty("seed",juce::Array<juce::var>{0,0,0});o->setProperty("keep",true);}
    if(type=="surface_extend") {o->setProperty("distance",5.0);o->setProperty("sides",juce::Array<juce::var>{true,true,true,true});o->setProperty("boundary","");o->setProperty("toward",obj());}
    if(type=="surface_split") {o->setProperty("tool",obj());}
    if(type=="surface_split_region") {o->setProperty("parent","");o->setProperty("regionReference",obj());}
    if(type=="surface_thicken") {o->setProperty("thickness",2.0);o->setProperty("direction","positive");}
    if(type=="surface_offset")o->setProperty("distance",2.0);
    if(type=="surface_hollow"){o->setProperty("thickness",2.0);o->setProperty("removeFaces",juce::Array<juce::var>());}
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
        const auto method=p["method"].toString();if(!juce::StringArray{"planar","extrude","revolve","ruled","sweep","loft"}.contains(method))return fail("Surface method must be planar, extrude, revolve, ruled, sweep or loft.");
        if(method=="sweep"||method=="loft")return validateModeling(method,pipeParameters(p,method),error);
        for(const auto* key:{"source","secondSource"})if(p.hasProperty(key)&&!p[key].isObject())return fail("Surface source references must be objects.");
        if(p.hasProperty("profileEntities")){if(!p["profileEntities"].isArray())return fail("Profile entities must be an array of stable identifiers.");for(const auto& id:*p["profileEntities"].getArray())if(!id.isString()||id.toString().isEmpty())return fail("Profile entity identifiers must be nonempty strings.");}
        if(!validCreationSource(creationSource(p),error))return false;
        if(method=="ruled"&&!validCreationSource(creationSource(p,true),error))return false;
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
    if(!p["target"].isString()||p["target"].toString().isEmpty())return fail(type=="surface_hollow"?"Choose a source solid.":"Choose a source surface or shell.");
    if(p.hasProperty("face")&&!p["face"].isString())return fail("Face must be a persistent face token, or empty for the whole source.");
    if(type=="surface_trim"||type=="surface_split")
    {
        if(!validCutTool(p["tool"],error))return false;
        if(type=="surface_trim")
        {if(!vector3(p["seed"])||!p["keep"].isBool())return fail("Trim requires a finite world-space seed point and keep boolean.");if(!validRegionReference(p["regionReference"],error,false))return false;}
    }
    if(type=="surface_extend")
    {
        if(!number(p["distance"])||(double)p["distance"]<1e-6||(double)p["distance"]>1e5)return fail("Extension distance must be between 0.000001 and 100000 mm.");
        if(!p["boundary"].isString())return fail("Extension boundary must be a persistent edge token or empty for legacy U/V selection.");
        if(!p["toward"].isObject())return fail("Extension target geometry must be an object.");
        if(p["toward"].getDynamicObject()->getProperties().size()>0&&!validCutTool(p["toward"],error,false))return false;
        if(!p["sides"].isArray()||p["sides"].size()!=4)return fail("Extension sides are four booleans: U min, U max, V min, V max.");
        bool any=p["boundary"].toString().isNotEmpty();for(const auto& v:*p["sides"].getArray()){if(!v.isBool())return fail("Extension sides must be booleans.");any=any||(bool)v;}if(!any)return fail("Choose at least one boundary to extend.");
    }
    if(type=="surface_split_region")
    {if(!p["parent"].isString()||p["parent"].toString().isEmpty())return fail("Split region needs its parent operation.");if(!validRegionReference(p["regionReference"],error,true))return false;}
    if(type=="surface_thicken"&&(!number(p["thickness"])||(double)p["thickness"]<1e-6||(double)p["thickness"]>1e5||!juce::StringArray{"positive","negative","symmetric"}.contains(p["direction"].toString())))return fail("Thickness must be positive (0.000001ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ100000 mm); direction is positive, negative or symmetric.");
    if(type=="surface_offset"&&(!number(p["distance"])||std::abs((double)p["distance"])<1e-6||std::abs((double)p["distance"])>1e5))return fail("Offset distance must be finite and between -100000 and 100000 mm, excluding zero.");
    if(type=="surface_hollow")
    {
        if(!number(p["thickness"])||(double)p["thickness"]<1e-6||(double)p["thickness"]>1e5)return fail("Hollow wall thickness must be between 0.000001 and 100000 mm.");
        if(!p["removeFaces"].isArray())return fail("Removed faces must be an array of persistent face tokens.");
        juce::StringArray seen;for(const auto& v:*p["removeFaces"].getArray())if(!v.isString()||v.toString().isEmpty()||seen.contains(v.toString()))return fail("Removed faces must have distinct nonempty persistent tokens.");else seen.add(v.toString());
    }
    return true;
}

juce::var surfaceTopology(const TopoDS_Shape& s)
{
    auto result=obj();auto* o=result.getDynamicObject();
    TopTools_IndexedMapOfShape allFaces;
    TopExp::MapShapes(s,TopAbs_FACE,allFaces);
    for(const auto kind:{TopAbs_FACE,TopAbs_SHELL,TopAbs_SOLID})
    {TopTools_IndexedMapOfShape map;TopExp::MapShapes(s,kind,map);o->setProperty(kind==TopAbs_FACE?"faces":kind==TopAbs_SHELL?"shells":"solids",map.Extent());}
    TopTools_IndexedDataMapOfShapeListOfShape edges;TopExp::MapShapesAndAncestors(s,TopAbs_EDGE,TopAbs_FACE,edges);int multiple=0;juce::Array<juce::var> free;
    std::vector<int> parent((size_t)allFaces.Extent()+1);for(int i=1;i<=allFaces.Extent();++i)parent[(size_t)i]=i;
    const auto root=[&](int value){while(parent[(size_t)value]!=value){parent[(size_t)value]=parent[(size_t)parent[(size_t)value]];value=parent[(size_t)value];}return value;};
    double openLength=0;
    for(int i=1;i<=edges.Extent();++i)
    {
        const auto edge=TopoDS::Edge(edges.FindKey(i));if(BRep_Tool::Degenerated(edge))continue;
        const auto& faces=edges.FindFromIndex(i);int uses=faces.Extent();if(uses==1&&BRep_Tool::IsClosed(edge,TopoDS::Face(faces.First())))uses=2;
        int first=0;for(TopTools_ListIteratorOfListOfShape face(faces);face.More();face.Next())
        {const int index=allFaces.FindIndex(face.Value());if(index<1)continue;if(first==0)first=index;else parent[(size_t)root(index)]=root(first);}
        if(uses>2)++multiple;
        if(uses==1)
        {
            auto item=obj();GProp_GProps length;BRepGProp::LinearProperties(edge,length);item.getDynamicObject()->setProperty("length_mm",length.Mass());
            openLength+=length.Mass();
            TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b);if(!a.IsNull())item.getDynamicObject()->setProperty("start",xyz(BRep_Tool::Pnt(a)));if(!b.IsNull())item.getDynamicObject()->setProperty("end",xyz(BRep_Tool::Pnt(b)));free.add(item);
        }
    }
    int regions=0;for(int i=1;i<=allFaces.Extent();++i)if(root(i)==i)++regions;
    // An enclosed cavity has two disconnected boundary shells but still forms one closed solid.
    const bool closed=(regions==1||(int)result["solids"]==1)&&free.isEmpty()&&multiple==0;
    const bool convertible=(int)result["shells"]==1&&(int)result["solids"]==0&&closed&&BRepCheck_Analyzer(s,true).IsValid();
    o->setProperty("area_mm2",area(s));o->setProperty("volume_mm3",(int)result["solids"]>0?volume(s):0.0);
    o->setProperty("unmatchedEdges",free.size());o->setProperty("freeBoundaries",free);o->setProperty("openBoundaryLength_mm",openLength);
    o->setProperty("nonmanifoldEdges",multiple);o->setProperty("connectedRegions",regions);o->setProperty("closed",closed);
    o->setProperty("solidConvertible",convertible);
    o->setProperty("shellState",(int)result["solids"]>0||(int)result["shells"]==0?"not_shell":regions!=1||multiple>0?"invalid":closed?"closed":"open");return result;
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

bool CadDocument::surfaceCutTool(const juce::var& reference,TopoDS_Shape& shape,CadFaceOrigins& names,juce::String& error,juce::StringArray& active)
{
    if(!validCutTool(reference,error))return false;
    if(reference["sketch"].toString().isNotEmpty()||reference["edges"].isArray())
    {auto wire=modelingPath(reference,nullptr,error,active);if(wire.IsNull())return false;shape=wire;return true;}
    return surfaceInput(reference["feature"].toString(),reference["face"].toString(),shape,names,error,active);
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
        else
        {
            const auto source=creationSource(p);
            const auto wireFrom=[&](juce::var ref)->TopoDS_Wire
            {
                if(const auto* sketch=findSketch(ref["sketch"].toString()))
                {
                    juce::Array<juce::var> selected;juce::StringArray found;
                    for(const auto& entity:sketch->entities)
                    {
                        bool chosen=ref["entities"].size()==0;if(const auto* ids=ref["entities"].getArray())for(const auto& id:*ids)if(id.toString()==entity.id)chosen=true;
                        if(!chosen)continue;if(entity.construction){if(ref["entities"].size()>0){error="Construction entities cannot form a surface profile. Use them as a rotation axis instead.";return {};}continue;}
                        selected.add(entity.id);found.add(entity.id);
                    }
                    if(selected.isEmpty()){error="Select non-construction sketch curves for the surface.";return {};}
                    if(ref["entities"].size()>0&&found.size()!=ref["entities"].size()){error="A selected sketch curve is missing. Reselect the surface source.";return {};}
                    ref.getDynamicObject()->setProperty("entities",selected);
                }
                return modelingPath(ref,nullptr,error,active);
            };
            const auto profile=wireFrom(source);if(profile.IsNull())return {};
            if(method=="planar")
            {
                if(!wireClosed(profile)){error="Planar Surface needs one closed boundary. Join the endpoints or select a closed wire.";return {};}
                BRepBuilderAPI_MakeFace face(profile,true);if(!face.IsDone()){error="The selected boundary is not a valid planar closed wire.";return {};}
                result=face.Face();history.generated.push_back({profile,result});
            }
            else if(method=="extrude")
            {
                gp_Vec v((double)p["direction"][0],(double)p["direction"][1],(double)p["direction"][2]);v.Normalize();v*=double(p["distance"]);
                BRepPrimAPI_MakePrism prism(profile,v,true);if(!prism.IsDone()){error="Surface extrusion failed. Choose a direction transverse to the profile.";return {};}
                result=prism.Shape();track(prism,profile,{},result,feature.id,origins,history);
            }
            else if(method=="ruled")
            {
                const auto second=wireFrom(creationSource(p,true));if(second.IsNull())return {};
                int firstCount=0,secondCount=0;for(BRepTools_WireExplorer e(profile);e.More();e.Next())++firstCount;for(BRepTools_WireExplorer e(second);e.More();e.Next())++secondCount;
                if(wireClosed(profile)!=wireClosed(second)){error="Ruled Surface requires two open curves or two closed wires; open and closed inputs are incompatible.";return {};}
                if(firstCount!=secondCount){error="Ruled Surface requires matching segment counts. Select compatible curves or split their chains at corresponding vertices.";return {};}
                result=BRepFill::Shell(profile,second);if(result.IsNull()){error="Ruled Surface failed. Check curve compatibility and direction.";return {};}
                history.generated.push_back({profile,result});history.generated.push_back({second,result});
            }
            else
            {
                CadRevolveParameters r;auto parameters=obj();for(const auto* key:{"axis","angleDegrees","reverse"})parameters.getDynamicObject()->setProperty(key,p[key]);if(!readRevolveParameters(parameters,r,error))return {};
                gp_Pnt origin;gp_Dir axis;const auto sketchId=source["sketch"].toString();
                if(r.axisType=="sketchLine"&&r.axisSketch.isNotEmpty())
                {const auto* sketch=findSketch(r.axisSketch);CadSketchFrame frame;if(!sketch||!resolveSketchFrame(*sketch,frame,error,active)){if(error.isEmpty())error="The rotation-axis sketch is missing.";return {};}sketchFrames[r.axisSketch]=frame;}
                if(const auto* sketch=findSketch(sketchId))
                {CadSketchFrame frame;if(!resolveSketchFrame(*sketch,frame,error,active))return {};sketchFrames[sketchId]=frame;if(!revolveAxis(sketchId,r,origin,axis,error))return {};}
                else if(r.axisType=="sketchLine")
                {if(!revolveAxis(r.axisSketch,r,origin,axis,error))return {};}
                else if(r.axisType=="custom"){origin=gp_Pnt(r.origin[0],r.origin[1],r.origin[2]);axis=gp_Dir(r.direction[0],r.direction[1],r.direction[2]);}
                else if(r.axisType=="sketchU"||r.axisType=="sketchV"){error="Sketch U/V axes require a sketch source. Choose a world axis, custom axis or sketch line for a model wire.";return {};}
                else axis=r.axisType=="worldX"?gp::DX():r.axisType=="worldY"?gp::DY():gp::DZ();
                BRepPrimAPI_MakeRevol revolve(profile,gp_Ax1(origin,r.reverse?axis.Reversed():axis),r.angleDegrees*juce::MathConstants<double>::pi/180,true);if(!revolve.IsDone()){error="Surface revolution failed. Check the axis and profile crossing.";return {};}result=revolve.Shape();track(revolve,profile,{},result,feature.id,origins,history);
            }
            if(!result.IsNull()&&TopExp_Explorer(result,TopAbs_SOLID).More()){error="Surface creation must produce faces or shells, never a capped solid.";return {};}
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
        if(result.IsNull()){error="OCCT could not sew the selected surfaces. Check their boundaries and tolerance.";return {};}
        if(sew.NbMultipleEdges()>0){error=juce::String(sew.NbMultipleEdges())+" nonmanifold edge(s) join more than two faces. Remove overlapping faces or revise the inputs.";return {};}
        if(sew.NbDeletedFaces()>0){error=juce::String(sew.NbDeletedFaces())+" face(s) collapsed at the sewing tolerance. Reduce the tolerance or repair small faces.";return {};}
        const auto topology=surfaceTopology(result);
        if(result.ShapeType()!=TopAbs_SHELL||(int)topology["connectedRegions"]!=1)
        {error="Sewing produced "+topology["connectedRegions"].toString()+" separate region(s), not one shell. Join matching boundaries or select connected faces.";return {};}
        for(const auto& source:inputs)for(const auto kind:{TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer e(source,kind);e.More();e.Next())
        {const auto mapped=sew.IsModifiedSubShape(e.Current())?sew.ModifiedSubShape(e.Current()):e.Current();history.modified.push_back({e.Current(),mapped});}
        for(const auto& n:inputNames){const auto image=sew.IsModifiedSubShape(n.first)?sew.ModifiedSubShape(n.first):n.first;for(TopExp_Explorer e(result,TopAbs_FACE);e.More();e.Next())if(e.Current().IsSame(image))origins.push_back({e.Current(),n.second});}
    }
    else if(type=="surface_split_region")
    {
        TopoDS_Shape parent,source;CadFaceOrigins parentNames,sourceNames;
        if(!surfaceInput(p["parent"].toString(),{},parent,parentNames,error,active)||
           !surfaceInput(p["target"].toString(),p["face"].toString(),source,sourceNames,error,active))return {};
        gp_Pnt seed;int supports=0;TopoDS_Shape supportingFace;
        const auto anchor=p["regionReference"];
        for(const auto& named:sourceNames)if(named.second.contains(anchor["face"].toString()))
        {if(!supportingFace.IsNull()&&supportingFace.IsSame(named.first))continue;supportingFace=named.first;seed=BRepAdaptor_Surface(TopoDS::Face(named.first)).Value((double)anchor["u"],(double)anchor["v"]);++supports;}
        if(supports!=1){error="Split region source face is missing or ambiguous after regeneration.";return {};}
        const auto vertex=BRepBuilderAPI_MakeVertex(seed).Vertex();int matches=0;
        for(TopExp_Explorer e(parent,TopAbs_FACE);e.More();e.Next())
        {BRepExtrema_DistShapeShape distance(vertex,e.Current());distance.Perform();if(distance.IsDone()&&distance.NbSolution()==1&&distance.Value()<1e-6&&distance.SupportTypeShape2(1)==BRepExtrema_IsInFace)
         {result=e.Current();++matches;}}
        if(matches!=1){error="Split region changed or vanished. Edit the split or its source geometry.";return {};}
        for(const auto& named:parentNames)if(named.first.IsSame(result))origins.push_back({result,named.second});
        history.generated.push_back({parent,result});
    }
    else
    {
        TopoDS_Shape source;CadFaceOrigins names;if(!surfaceInput(p["target"].toString(),p["face"].toString(),source,names,error,active))return {};
        if(type!="surface_hollow"&&TopExp_Explorer(source,TopAbs_SOLID).More()){error="Select an open surface/shell or a named face; whole solids are not surface inputs.";return {};}
        if(type=="surface_trim"||type=="surface_split")
        {
            TopoDS_Shape tool;CadFaceOrigins toolNames;if(!surfaceCutTool(p["tool"],tool,toolNames,error,active))return {};
            TopTools_ListOfShape arguments,tools;arguments.Append(source);tools.Append(tool);BRepAlgoAPI_Splitter split;split.SetArguments(arguments);split.SetTools(tools);split.SetNonDestructive(true);split.Build();
            if(!split.IsDone()||split.HasErrors()||split.Shape().IsNull()){error="Surface cutting intersection failed. The curves must lie on the surface or intersecting geometry must cross it.";return {};}
            TopTools_IndexedMapOfShape before,after;TopExp::MapShapes(source,TopAbs_FACE,before);TopExp::MapShapes(split.Shape(),TopAbs_FACE,after);
            if(after.Extent()<=before.Extent()){error="The trim tool does not divide the surface into selectable regions.";return {};}
            if(type=="surface_split")
            {if(after.Extent()>64){error="Split would create more than 64 regions; simplify the cutting geometry.";return {};}result=split.Shape();track(split,source,names,result,feature.id,origins,history);}
            else
            {
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
        }
        if(type=="surface_extend")
        {
            TopTools_IndexedMapOfShape faces;TopExp::MapShapes(source,TopAbs_FACE,faces);if(faces.Extent()!=1){error="Extend requires a single face. Select one named face of the shell.";return {};}
            const auto face=TopoDS::Face(faces(1));double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            BRepBuilderAPI_MakeFace rectangle(BRep_Tool::Surface(face),u0,u1,v0,v1,1e-7);
            if(!rectangle.IsDone()||std::abs(area(rectangle.Face())-area(face))>std::max(1e-7,area(face)*1e-7)){error="Extend supports untrimmed rectangular U/V boundaries. Trimmed contours, holes and arbitrary boundary extensions are unsupported.";return {};}
            bool sides[4]={(bool)p["sides"][0],(bool)p["sides"][1],(bool)p["sides"][2],(bool)p["sides"][3]};
            double extension=(double)p["distance"];
            const auto surface=BRep_Tool::Surface(face);
            const auto project=[&](const gp_Pnt& world,double& u,double& v,double& gap)->bool
            {GeomAPI_ProjectPointOnSurf projection(world,surface);if(!projection.IsDone()||projection.NbPoints()<1)return false;projection.LowerDistanceParameters(u,v);gap=projection.LowerDistance();return std::isfinite(u)&&std::isfinite(v)&&std::isfinite(gap);};
            int boundarySide=-1;
            if(p["boundary"].toString().isNotEmpty())
            {
                const auto* target=find(p["target"].toString());TopoDS_Edge boundary;
                if(target==nullptr||!modelingEdge(*target,p["boundary"].toString(),boundary,error))return {};
                BRepAdaptor_Curve curve(boundary);const auto a=curve.FirstParameter(),b=curve.LastParameter();
                double uv[2][2],gap=0;
                for(int i=0;i<2;++i)if(!project(curve.Value(a+(b-a)*(i==0?0.25:0.75)),uv[i][0],uv[i][1],gap)||gap>1e-5)
                {error="The selected edge does not lie on the target surface.";return {};}
                const double tolerance=1e-5;
                const bool candidates[4]={std::abs(uv[0][0]-u0)<tolerance&&std::abs(uv[1][0]-u0)<tolerance,
                                          std::abs(uv[0][0]-u1)<tolerance&&std::abs(uv[1][0]-u1)<tolerance,
                                          std::abs(uv[0][1]-v0)<tolerance&&std::abs(uv[1][1]-v0)<tolerance,
                                          std::abs(uv[0][1]-v1)<tolerance&&std::abs(uv[1][1]-v1)<tolerance};
                for(int i=0;i<4;++i)if(candidates[i]){if(boundarySide>=0){error="The picked edge is ambiguous at a surface seam. Choose another boundary.";return {};}boundarySide=i;}
                if(boundarySide<0){error="Select one untrimmed rectangular U/V boundary edge on the target face.";return {};}
                for(auto& side:sides)side=false;sides[boundarySide]=true;
            }
            if(p["toward"].isObject()&&p["toward"].getDynamicObject()->getProperties().size()>0)
            {
                if(boundarySide<0){error="Select the surface boundary edge before extending toward geometry.";return {};}
                if((p["toward"]["sketch"].toString().isEmpty()&&!p["toward"]["edges"].isArray())||p["toward"]["face"].toString().isNotEmpty())
                {error="Extend toward requires a sketch curve or selected persistent model edges.";return {};}
                TopoDS_Shape guide;CadFaceOrigins guideNames;if(!surfaceCutTool(p["toward"],guide,guideNames,error,active))return {};
                const bool alongU=boundarySide<2;const auto boundaryParameter=boundarySide==0?u0:boundarySide==1?u1:boundarySide==2?v0:v1;
                double guideParameter=0;int samples=0;
                for(TopExp_Explorer e(guide,TopAbs_EDGE);e.More();e.Next())
                {
                    BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));const auto a=curve.FirstParameter(),b=curve.LastParameter();
                    for(int i=0;i<3;++i)
                    {
                        double u=0,v=0,gap=0;if(!project(curve.Value(a+(b-a)*(i+1)/4.0),u,v,gap)||gap>1e-5)
                        {error="The extension target must lie on the natural continuation of the source surface.";return {};}
                        const auto parameter=alongU?u:v;
                        if(samples>0&&std::abs(parameter-guideParameter)>1e-5){error="The extension target must follow one parallel surface-parameter boundary.";return {};}
                        guideParameter=parameter;++samples;
                    }
                }
                if(samples==0){error="The extension target contains no usable curve edges.";return {};}
                const auto delta=(boundarySide==0||boundarySide==2)?boundaryParameter-guideParameter:guideParameter-boundaryParameter;
                if(delta<=1e-7){error="The extension target must lie beyond the chosen boundary.";return {};}
                BRepAdaptor_Surface basis(face,true);const auto resolution=alongU?basis.UResolution(1.0):basis.VResolution(1.0);
                if(!std::isfinite(resolution)||resolution<=0){error="This surface has no usable extension direction.";return {};}
                extension=delta/resolution;if(!std::isfinite(extension)||extension<1e-6||extension>1e5){error="Target extension distance is outside the supported range.";return {};}
            }
            TopoDS_Face extended;BRepLib::ExtendFace(face,extension,sides[0],sides[1],sides[2],sides[3],extended);
            if(extended.IsNull()||area(extended)<=area(face)+1e-8){error="This surface cannot continue in the requested U/V directions (closed/periodic limit or unsupported basis surface).";return {};}
            BRepAdaptor_Surface basis(face,true);const auto kind=basis.GetType();
            if(kind==GeomAbs_Plane||kind==GeomAbs_Cylinder||kind==GeomAbs_Cone||kind==GeomAbs_Sphere||kind==GeomAbs_Torus)
            {
                double eu0,eu1,ev0,ev1;BRepTools::UVBounds(extended,eu0,eu1,ev0,ev1);
                const auto requestedU=u1-u0+basis.UResolution(extension)*(int(sides[0])+int(sides[1]));
                const auto requestedV=v1-v0+basis.VResolution(extension)*(int(sides[2])+int(sides[3]));
                if(eu1-eu0<requestedU-1e-7||ev1-ev0<requestedV-1e-7){error="The requested extension reaches a periodic or natural surface limit. Reduce the distance or choose another boundary.";return {};}
            }
            result=extended;history.modified.push_back({face,extended});for(const auto& n:names)if(n.first.IsSame(face))origins.push_back({extended,n.second});
        }
        if(type=="surface_solidify")
        {
            if(source.ShapeType()!=TopAbs_SHELL){error="Create Solid from Shell requires one shell feature, not a face, compound or existing solid.";return {};}
            const auto topology=surfaceTopology(source);
            if((int)topology["unmatchedEdges"]>0){error="Solidify requires closure: "+topology["unmatchedEdges"].toString()+" unmatched boundary edges remain. Sew/repair the gaps first.";return {};}
            if((int)topology["nonmanifoldEdges"]>0){error="Solidify rejects nonmanifold boundaries.";return {};}
            if((int)topology["connectedRegions"]!=1||!(bool)topology["solidConvertible"]){error="The shell is disconnected or geometrically invalid and cannot form one solid.";return {};}
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
        if(type=="surface_offset")
        {
            if(source.ShapeType()!=TopAbs_FACE)
            {
                TopTools_IndexedMapOfShape faces;TopExp::MapShapes(source,TopAbs_FACE,faces);
                if(faces.Extent()!=1){error="Offset Surface requires one face. Pick a persistent face from a multi-face shell.";return {};}
                source=faces(1);std::erase_if(names,[&](const auto& n){return !n.first.IsSame(source);});
            }
            BRepOffsetAPI_MakeOffsetShape offset;offset.PerformBySimple(source,(double)p["distance"]);
            if(!offset.IsDone()||offset.Shape().IsNull()){error="Surface offset failed. Reduce the distance or choose a face with compatible curvature and topology.";return {};}
            result=offset.Shape();track(offset,source,names,result,feature.id,origins,history);
            if(TopExp_Explorer(result,TopAbs_SOLID).More()){error="Offset Surface must remain a surface, not a solid.";return {};}
        }
        if(type=="surface_hollow")
        {
            const auto sourceTopology=surfaceTopology(source);
            if(source.ShapeType()!=TopAbs_SOLID||!BRepCheck_Analyzer(source,true).IsValid()||(int)sourceTopology["unmatchedEdges"]!=0||(int)sourceTopology["nonmanifoldEdges"]!=0)
            {error="Hollow requires one valid closed solid body.";return {};}
            TopTools_ListOfShape removed;TopTools_IndexedMapOfShape chosen;
            for(const auto& token:*p["removeFaces"].getArray())
            {
                TopoDS_Shape match;int count=0;
                for(const auto& named:names)if(named.second.contains(token.toString())){if(!match.IsNull()&&match.IsSame(named.first))continue;match=named.first;++count;}
                if(count!=1||chosen.Contains(match)){error="A removed face is missing, split or duplicated after regeneration: "+token.toString();return {};}
                chosen.Add(match);removed.Append(match);
            }
            if(removed.IsEmpty())
            {
                // With no closing faces OCCT returns only the inward offset body;
                // subtract it from the original to retain both cavity walls.
                BRepOffsetAPI_MakeOffsetShape inner;inner.PerformByJoin(source,-(double)p["thickness"],1e-6);
                if(!inner.IsDone()||inner.Shape().IsNull()||!validShape(inner.Shape(),true,error))
                {if(error.isEmpty())error="The inward cavity offset collapsed or self-intersected. Reduce wall thickness.";return {};}
                BRepAlgoAPI_Cut cut(source,inner.Shape());cut.SetNonDestructive(true);cut.Build();
                if(!cut.IsDone()||cut.HasErrors()||cut.Shape().IsNull()){error="OCCT could not subtract the enclosed cavity from the solid.";return {};}
                result=cut.Shape();track(cut,source,names,result,feature.id,origins,history);
            }
            else
            {
                BRepOffsetAPI_MakeThickSolid hollow;hollow.MakeThickSolidByJoin(source,removed,-(double)p["thickness"],1e-6);
                if(!hollow.IsDone()||hollow.Shape().IsNull()){error="Hollow failed: wall thickness may collapse the interior or intersect small features. Reduce thickness or change removed faces.";return {};}
                result=hollow.Shape();track(hollow,source,names,result,feature.id,origins,history);
            }
            if(volume(result)>=volume(source)-1e-8){error="Hollow removed no interior material. Reduce thickness or revise openings.";return {};}
        }
    }
    if(type=="surface"&&(bool)p["flipNormals"]&&!result.IsNull())result.Reverse();
    const bool solidResult=type=="surface_solidify"||type=="surface_thicken"||type=="surface_hollow";
    if(!validShape(result,solidResult,error))return {};
    if(solidResult)
    {
        const auto topology=surfaceTopology(result);
        if((int)topology["solids"]!=1||(int)topology["unmatchedEdges"]!=0||(int)topology["nonmanifoldEdges"]!=0)
        {error="The operation did not produce one closed manifold solid. Reduce thickness or repair the source topology.";return {};}
    }
    // TNaming builders replace an attribute's evolution list. Aggregate this feature's
    // copy, offset and operation steps before handing them to the existing writer.
    for(const auto& step:operationHistory)if(step.featureId==feature.id)
    {history.generated.insert(history.generated.end(),step.generated.begin(),step.generated.end());history.modified.insert(history.modified.end(),step.modified.begin(),step.modified.end());history.deleted.insert(history.deleted.end(),step.deleted.begin(),step.deleted.end());}
    std::erase_if(operationHistory,[&](const auto& step){return step.featureId==feature.id;});
    nameFaces(result,feature.id,origins);if(!history.generated.empty()||!history.modified.empty()||!history.deleted.empty())operationHistory.push_back(std::move(history));return result;
}

bool CadDocument::splitRegionAnchors(const juce::var& parameters,juce::Array<juce::var>& anchors,juce::String& error) const
{
    try
    {
    anchors.clear();CadDocument draft;
    if(!draft.deserialize(serialize(),error))return false;
    CadFeature operation;operation.id="surface-split-preview";operation.type="surface_split";operation.modeling=parameters;
    juce::StringArray active;active.add(operation.id);CadFaceOrigins regions;
    const auto divided=draft.evaluateSurface(operation,regions,error,active);if(divided.IsNull())return false;
    TopoDS_Shape source;CadFaceOrigins names;
    if(!draft.surfaceInput(parameters["target"].toString(),parameters["face"].toString(),source,names,error,active))return false;
    for(TopExp_Explorer region(divided,TopAbs_FACE);region.More();region.Next())
    {
        const auto face=TopoDS::Face(region.Current());double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1))
        {error="A split region has unbounded parameters and cannot be identified persistently.";return false;}
        double bestClearance=-1;
        juce::var bestAnchor;
        for(const double fu:{0.5,0.25,0.75,0.125,0.875})
        {
            for(const double fv:{0.5,0.25,0.75,0.125,0.875})
            {
                const auto u=u0+(u1-u0)*fu,v=v0+(v1-v0)*fv;
                BRepClass_FaceClassifier inside(face,gp_Pnt2d(u,v),1e-8);
                if(inside.State()!=TopAbs_IN)continue;
                const auto point=BRepAdaptor_Surface(face).Value(u,v);
                const auto vertex=BRepBuilderAPI_MakeVertex(point).Vertex();juce::var anchor;int count=0;std::vector<TopoDS_Shape> visited;
                for(const auto& named:names)
                {
                    bool duplicate=false;for(const auto& prior:visited)if(prior.IsSame(named.first)){duplicate=true;break;}if(duplicate)continue;visited.push_back(named.first);
                    BRepExtrema_DistShapeShape distance(vertex,named.first);distance.Perform();
                    if(!distance.IsDone()||distance.NbSolution()!=1||distance.Value()>1e-6||distance.SupportTypeShape2(1)!=BRepExtrema_IsInFace)continue;
                    auto token=juce::String();for(const auto& name:named.second)if(name.startsWith(parameters["target"].toString()+"/surface:")){token=name;break;}
                    if(token.isEmpty()&&!named.second.isEmpty())token=named.second[0];
                    if(token.isEmpty())continue;
                    double su=0,sv=0;distance.ParOnFaceS2(1,su,sv);
                    anchor=obj();anchor.getDynamicObject()->setProperty("face",token);anchor.getDynamicObject()->setProperty("u",su);anchor.getDynamicObject()->setProperty("v",sv);++count;
                }
                if(count==1)
                {
                    double clearance=std::numeric_limits<double>::infinity();
                    for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next())
                    {
                        BRepExtrema_DistShapeShape boundaryDistance(vertex,edge.Current());boundaryDistance.Perform();
                        if(!boundaryDistance.IsDone()){clearance=0;break;}
                        clearance=std::min(clearance,boundaryDistance.Value());
                    }
                    if(clearance>bestClearance){bestClearance=clearance;bestAnchor=anchor;}
                }
            }
        }
        if(bestClearance<=1e-7){error="Could not locate a stable interior anchor for a split region. Simplify the cutter or source.";return false;}
        anchors.add(bestAnchor);
    }
    if(anchors.size()<2||anchors.size()>64){error="Split needs 2 to 64 stable regions.";return false;}
    return true;
    }
    catch(const Standard_Failure& failure){error=juce::String("OCCT could not identify the split regions: ")+failure.GetMessageString();return false;}
    catch(const std::exception& failure){error=juce::String("Could not identify the split regions: ")+failure.what();return false;}
}
}
