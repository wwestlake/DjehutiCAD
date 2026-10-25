#include "CadDocument.h"
#include "CadGear.h"
#include "CadDinRail.h"
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom2d_Line.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <gp_Ax3.hxx>
#include <atomic>
#include <Standard_Failure.hxx>
#include <cmath>

namespace djehuti::cad
{
namespace
{
juce::var object(){return juce::var(new juce::DynamicObject());}
juce::var copyJson(const juce::var& p){return juce::JSON::fromString(juce::JSON::toString(p,false,17));}
bool numeric(const juce::var& v){return (v.isInt()||v.isInt64()||v.isDouble())&&std::isfinite((double)v);}
bool triple(const juce::var& v,bool nonzero=false)
{if(!v.isArray()||v.size()!=3)return false;double sum=0;for(int i=0;i<3;++i){if(!numeric(v[i]))return false;sum+=(double)v[i]*(double)v[i];}return std::isfinite(sum)&&(!nonzero||sum>1e-18);}
gp_Pnt point(const juce::var& v){return {(double)v[0],(double)v[1],(double)v[2]};}
gp_Dir direction(const juce::var& v){return {(double)v[0],(double)v[1],(double)v[2]};}
double volume(const TopoDS_Shape& s){GProp_GProps props;BRepGProp::VolumeProperties(s,props);return props.Mass();}
double length(const TopoDS_Shape& s){GProp_GProps props;BRepGProp::LinearProperties(s,props);return props.Mass();}
bool selfIntersects(const TopoDS_Shape& shape)
{BOPAlgo_ArgumentAnalyzer analyzer;analyzer.SetShape1(shape);analyzer.SelfInterMode()=true;analyzer.Perform();return analyzer.HasFaulty();}
bool solid(const TopoDS_Shape& shape,juce::String& error)
{
    if(shape.IsNull()||!TopExp_Explorer(shape,TopAbs_SOLID).More()||!BRepCheck_Analyzer(shape,true).IsValid()){error="OCCT produced invalid or degenerate solid geometry.";return false;}
    for(TopExp_Explorer ex(shape,TopAbs_SOLID);ex.More();ex.Next())if(!std::isfinite(volume(ex.Current()))||volume(ex.Current())<=1e-10){error="The result contains an empty or inverted solid.";return false;}
    if(selfIntersects(shape)){error="The resulting solid self-intersects. Adjust the profile, path, sections or cutter.";return false;}return true;
}
bool closed(const TopoDS_Wire& wire)
{TopoDS_Vertex first,last;TopExp::Vertices(wire,first,last);return !first.IsNull()&&!last.IsNull()&&BRep_Tool::Pnt(first).Distance(BRep_Tool::Pnt(last))<1e-7;}
bool faceFromWire(const TopoDS_Wire& wire,TopoDS_Face& face,juce::String& error)
{
    if(!closed(wire)){error="A closed planar contour is required.";return false;}
    BRepBuilderAPI_MakeFace maker(wire,true);if(!maker.IsDone()||!BRepCheck_Analyzer(maker.Face(),true).IsValid()||selfIntersects(wire)){error="The contour is degenerate, nonplanar or self-intersecting.";return false;}face=maker.Face();return true;
}
template<typename Operation>
CadFaceOrigins mapNames(Operation& op,const CadFaceOrigins& names,const TopoDS_Shape& result)
{
    CadFaceOrigins out;for(TopExp_Explorer ex(result,TopAbs_FACE);ex.More();ex.Next())
    {
        juce::StringArray tokens;for(const auto& entry:names)
        {
            bool match=ex.Current().IsSame(entry.first);
            if(!match)for(TopTools_ListIteratorOfListOfShape it(op.Modified(entry.first));it.More();it.Next())if(ex.Current().IsSame(it.Value())){match=true;break;}
            if(!match)for(TopTools_ListIteratorOfListOfShape it(op.Generated(entry.first));it.More();it.Next())if(ex.Current().IsSame(it.Value())){match=true;break;}
            if(match)for(const auto& t:entry.second)tokens.addIfNotAlreadyThere(t);
        }
        if(!tokens.isEmpty())out.push_back({ex.Current(),tokens});
    }return out;
}
void nameRemaining(const TopoDS_Shape& shape,const juce::String& owner,CadFaceOrigins& names)
{int i=0;for(TopExp_Explorer ex(shape,TopAbs_FACE);ex.More();ex.Next(),++i){bool found=false;for(const auto& n:names)if(n.first.IsSame(ex.Current())){found=true;break;}if(!found)names.push_back({ex.Current(),{owner+"/surface:"+juce::String(i)}});}}
juce::var pathDefault(){auto p=object();p.getDynamicObject()->setProperty("sketch","");p.getDynamicObject()->setProperty("reverse",false);return p;}
bool validPath(const juce::var& p,juce::String& error)
{
    if(!p.isObject()){error="Path must identify a sketch or persistent model edges.";return false;}
    for(const auto& v:p.getDynamicObject()->getProperties())if(!juce::StringArray{"sketch","feature","edges","entities","reverse"}.contains(v.name.toString())){error="Unknown path field: "+v.name.toString();return false;}
    const bool sketch=p["sketch"].isString()&&p["sketch"].toString().isNotEmpty();const bool model=p["feature"].isString()&&p["feature"].toString().isNotEmpty();
    if(sketch==model){error="Select either a path sketch or edges on one solid.";return false;}
    if(p.hasProperty("reverse")&&!p["reverse"].isBool()){error="Path reverse must be boolean.";return false;}
    if(model&&(!p["edges"].isArray()||p["edges"].size()<1||p["edges"].size()>256)){error="Select 1 to 256 connected path edges.";return false;}
    for(const auto* key:{"edges","entities"})if(p.hasProperty(key))
    {if(!p[key].isArray()){error="Path edge/entity references must be an array.";return false;}juce::StringArray seen;for(const auto& v:*p[key].getArray()){if(!v.isString()||v.toString().isEmpty()||seen.contains(v.toString())){error="Path references must be distinct persistent IDs.";return false;}seen.add(v.toString());}}
    return true;
}
TopoDS_Face offsetFace(const TopoDS_Face& source,double distance,juce::String& error)
{
    if(std::abs(distance)<1e-10)return source;
    BRepOffsetAPI_MakeOffset op(source,GeomAbs_Arc,false);op.Perform(distance);
    if(!op.IsDone()||op.Shape().IsNull()){error="The cutter does not fit this region, or its offset cannot be constructed.";return {};}
    TopoDS_Wire wire;int count=0;for(TopExp_Explorer ex(op.Shape(),TopAbs_WIRE);ex.More();ex.Next()){wire=TopoDS::Wire(ex.Current());++count;}
    TopoDS_Face face;if(count!=1||!faceFromWire(wire,face,error)){error="The cutter cannot access one connected region at this diameter. Reduce its size or simplify the region.";return {};}
    if(distance<0)
    {
        BRepExtrema_DistShapeShape clearance(wire,BRepTools::OuterWire(source));clearance.Perform();
        if(!clearance.IsDone()||clearance.Value()<std::abs(distance)-1e-5){error="The tool diameter exceeds the accessible region width.";return {};}
        for(BRepTools_WireExplorer e(wire);e.More();e.Next()){BRepAdaptor_Curve curve(e.Current());for(int i=0;i<=8;++i){BRepClass_FaceClassifier inside(source,curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/8.0),1e-7);if(inside.State()!=TopAbs_IN){error="The tool cannot fit inside this region.";return {};}}}
    }
    return face;
}
TopoDS_Shape prism(const TopoDS_Face& face,const gp_Vec& extent,const juce::String& owner,CadFaceOrigins& names)
{
    BRepPrimAPI_MakePrism op(face,extent);const auto shape=op.Shape();names.push_back({op.FirstShape(),{owner+"/entry"}});names.push_back({op.LastShape(),{owner+"/floor"}});int i=0;
    for(TopExp_Explorer ex(face,TopAbs_EDGE);ex.More();ex.Next(),++i)for(TopTools_ListIteratorOfListOfShape it(op.Generated(ex.Current()));it.More();it.Next())if(it.Value().ShapeType()==TopAbs_FACE)names.push_back({it.Value(),{owner+"/wall:"+juce::String(i)}});return shape;
}
}

juce::var modelingDefaults(const juce::String& type)
{
    if(isSurfaceType(type))return surfaceDefaults(type);
    if(gear::isGearType(type))return gear::defaults();
    if(din::isDinType(type))return din::defaults(type);
    auto p=object();auto* o=p.getDynamicObject();o->setProperty("target","");
    if(type=="sweep")
    {o->setProperty("operation","new");o->setProperty("profile","");o->setProperty("path",pathDefault());o->setProperty("orientation","transport");o->setProperty("binormal",juce::Array<juce::var>{0,0,1});o->setProperty("alignProfile",true);o->setProperty("rollDegrees",0.0);o->setProperty("twistDegrees",0.0);o->setProperty("transition","round");}
    if(type=="loft")
    {o->setProperty("operation","new");o->setProperty("sections",juce::Array<juce::var>());o->setProperty("ruled",false);o->setProperty("alignment","auto");o->setProperty("offsets",juce::Array<juce::var>());o->setProperty("reverseSections",juce::Array<juce::var>());o->setProperty("seamAngles",juce::Array<juce::var>());}
    if(type=="mill")
    {auto r=object();r.getDynamicObject()->setProperty("type","XY");r.getDynamicObject()->setProperty("origin",juce::Array<juce::var>{0,0,0});r.getDynamicObject()->setProperty("vector",juce::Array<juce::var>{0,0,1});o->setProperty("reference",r);o->setProperty("strategy","pocket");o->setProperty("region","");o->setProperty("path",pathDefault());o->setProperty("toolDiameter",6.0);o->setProperty("depth",3.0);o->setProperty("throughAll",false);o->setProperty("alignToReference",true);o->setProperty("compensation","on");}
    return p;
}

juce::StringArray modelingDependencies(const juce::var& p)
{
    juce::StringArray ids;if(!p.isObject())return ids;
    if(p.hasProperty("method"))
    {
        const auto method=p["method"].toString();
        if(method=="loft"){if(const auto* values=p["sections"].getArray())for(const auto& v:*values)ids.addIfNotAlreadyThere(v.toString());}
        else {if(p["profile"].toString().isNotEmpty())ids.add(p["profile"].toString());if(method=="sweep")for(const auto* key:{"sketch","feature"})if(p["path"][key].toString().isNotEmpty())ids.addIfNotAlreadyThere(p["path"][key].toString());}
        return ids;
    }
    for(const auto* key:{"profile","region","target"})if(p[key].toString().isNotEmpty())ids.addIfNotAlreadyThere(p[key].toString());
    for(const auto* key:{"sections","sources"})if(const auto* values=p[key].getArray())for(const auto& v:*values)ids.addIfNotAlreadyThere(v.isObject()?v["feature"].toString():v.toString());
    for(const auto* key:{"sketch","feature"})if(p["tool"][key].toString().isNotEmpty())ids.addIfNotAlreadyThere(p["tool"][key].toString());
    for(const auto* key:{"sketch","feature"})if(p["path"][key].toString().isNotEmpty())ids.addIfNotAlreadyThere(p["path"][key].toString());
    if(p["reference"]["feature"].toString().isNotEmpty())ids.addIfNotAlreadyThere(p["reference"]["feature"].toString());return ids;
}

bool validateModeling(const juce::String& type,const juce::var& p,juce::String& error)
{
    if(isSurfaceType(type))return validateSurface(type,p,error);
    if(gear::isGearType(type))return gear::validate(p,error);
    if(din::isDinType(type))return din::validate(type,p,error);
    const auto fail=[&](const juce::String& e){error=e;return false;};if(!isModelingType(type)||!p.isObject())return fail("Invalid modeling definition.");
    const auto defaults=modelingDefaults(type);for(const auto& v:p.getDynamicObject()->getProperties())if(!defaults.hasProperty(v.name))return fail("Unknown parameter: "+v.name.toString());
    if(!p["target"].isString())return fail("Target must be a stable body ID.");
    if(type!="mill")
    {if(!p["operation"].isString()||!juce::StringArray{"new","add","subtract"}.contains(p["operation"].toString()))return fail("Operation must be new, add or subtract.");if(p["operation"].toString()!="new"&&p["target"].toString().isEmpty())return fail("Add/subtract requires a target solid.");}
    if(type=="sweep")
    {
        if(!p["profile"].isString()||p["profile"].toString().isEmpty())return fail("Choose a closed profile sketch.");if(!validPath(p["path"],error))return false;
        if(!p["alignProfile"].isBool()||!triple(p["binormal"],true))return fail("Profile alignment must be boolean and the fixed binormal a nonzero finite vector.");
        if(!juce::StringArray{"transport","frenet","fixed"}.contains(p["orientation"].toString()))return fail("Orientation must be transport, frenet or fixed.");
        if(!juce::StringArray{"round","right","transformed"}.contains(p["transition"].toString()))return fail("Unsupported path-corner transition.");
        for(const auto* key:{"rollDegrees","twistDegrees"})if(!numeric(p[key])||std::abs((double)p[key])>3600)return fail("Roll/twist must be finite and within +/-3600 degrees.");
        if(std::abs((double)p["twistDegrees"])>1e-9&&p["orientation"].toString()!="transport")return fail("Controlled twist requires transport orientation and a straight path.");
    }
    if(type=="loft")
    {
        if(!p["sections"].isArray()||p["sections"].size()<2||p["sections"].size()>32)return fail("Choose 2 to 32 ordered, distinct section sketches.");
        juce::StringArray seen;for(const auto& id:*p["sections"].getArray()){if(!id.isString()||id.toString().isEmpty()||seen.contains(id.toString()))return fail("Loft section IDs must be distinct.");seen.add(id.toString());}
        if(!p["ruled"].isBool()||!juce::StringArray{"auto","manual"}.contains(p["alignment"].toString()))return fail("Choose smooth/ruled and auto/manual correspondence.");
        for(const auto* key:{"offsets","reverseSections","seamAngles"})
        {
            if(!p[key].isArray()||(p[key].size()!=0&&p[key].size()!=p["sections"].size()))return fail("Section correspondence arrays must be empty or have one entry per section.");
            for(const auto& v:*p[key].getArray())if(juce::String(key)=="reverseSections"?!v.isBool():!numeric(v)||(juce::String(key)=="offsets"?((double)v<0||(double)v>65535||(double)v!=std::floor((double)v)):std::abs((double)v)>3600))return fail("Invalid section offset, reverse flag or seam angle.");
        }
    }
    if(type=="mill")
    {
        if(p["target"].toString().isEmpty())return fail("Choose a machining target solid.");
        if(!juce::StringArray{"pocket","slot","profileCut"}.contains(p["strategy"].toString()))return fail("Machining strategy must be pocket, slot or profileCut.");
        if(p["strategy"].toString()=="pocket"){if(!p["region"].isString()||p["region"].toString().isEmpty())return fail("Choose a closed pocket-region sketch.");}else if(!validPath(p["path"],error))return false;
        if(!numeric(p["toolDiameter"])||(double)p["toolDiameter"]<=1e-6||!numeric(p["depth"])||!p["throughAll"].isBool()||!p["alignToReference"].isBool()||(!(bool)p["throughAll"]&&(double)p["depth"]<=1e-6))return fail("Tool diameter and blind depth must be positive finite millimetres; extent flags must be boolean.");
        if(!juce::StringArray{"on","inside","outside"}.contains(p["compensation"].toString()))return fail("Profile compensation must be on, inside or outside.");
        const auto r=p["reference"];if(!r.isObject()||!juce::StringArray{"XY","XZ","YZ","custom","face"}.contains(r["type"].toString()))return fail("Choose a principal/custom plane or named planar reference face.");
        for(const auto& v:r.getDynamicObject()->getProperties())if(!juce::StringArray{"type","origin","vector","feature","face","reverse"}.contains(v.name.toString()))return fail("Unknown machining-reference field.");
        if(r.hasProperty("reverse")&&!r["reverse"].isBool())return fail("Reference reverse must be boolean.");
        if(r["type"].toString()=="custom"&&(!triple(r["origin"])||!triple(r["vector"],true)))return fail("Custom machining plane needs a finite origin and nonzero normal.");
        if(r["type"].toString()=="face"&&(!r["feature"].isString()||r["feature"].toString().isEmpty()||!r["face"].isString()||r["face"].toString().isEmpty()))return fail("Reference face requires feature and persistent face IDs.");
    }
    return true;
}

bool CadDocument::modelingSection(const juce::String& id,TopoDS_Face& face,CadSketchFrame& frame,juce::String& error,juce::StringArray& active)
{
    const auto* sketch=findSketch(id);if(sketch==nullptr){error="Unknown section sketch: "+id;return false;}if(!resolveSketchFrame(*sketch,frame,error,active))return false;
    std::vector<TopoDS_Face> faces;if(!sketchProfiles(*sketch,frame,faces,error)||faces.size()!=1){if(error.isEmpty())error="Each section must contain exactly one closed contour. Separate nested/disconnected contours.";return false;}
    face=faces.front();if(!BRepCheck_Analyzer(face,true).IsValid()||selfIntersects(face)){error="Section "+id+" is degenerate or self-intersecting.";return false;}return true;
}

TopoDS_Wire CadDocument::modelingPath(const juce::var& p,const CadSketchFrame* overrideFrame,juce::String& error,juce::StringArray& active)
{
    if(!validPath(p,error))return {};Handle(TopTools_HSequenceOfShape) edges=new TopTools_HSequenceOfShape();
    if(p["sketch"].toString().isNotEmpty())
    {
        const auto* sketch=findSketch(p["sketch"].toString());if(sketch==nullptr){error="Unknown path sketch.";return {};}CadSketchFrame frame;if(!resolveSketchFrame(*sketch,frame,error,active))return {};if(overrideFrame!=nullptr)frame=*overrideFrame;
        const auto at=[&](double x,double y){return frame.origin.Translated(gp_Vec(frame.u)*x+gp_Vec(frame.v)*y);};juce::StringArray found;
        for(const auto& e:sketch->entities)
        {
            if(p["entities"].isArray()&&p["entities"].size()>0){bool selected=false;for(const auto& v:*p["entities"].getArray())if(v.toString()==e.id)selected=true;if(!selected)continue;found.add(e.id);}
            if(e.type=="line")edges->Append(BRepBuilderAPI_MakeEdge(at(e.x1,e.y1),at(e.x2,e.y2)).Edge());
            else if(e.type=="circle")edges->Append(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(at(e.x1,e.y1),frame.normal,frame.u),e.radius)).Edge());
            else if(e.type=="bezier"){TColgp_Array1OfPnt poles(1,4);poles(1)=at(e.x1,e.y1);poles(2)=at(e.cx1,e.cy1);poles(3)=at(e.cx2,e.cy2);poles(4)=at(e.x2,e.y2);edges->Append(BRepBuilderAPI_MakeEdge(Handle(Geom_Curve)(new Geom_BezierCurve(poles))).Edge());}
            else {error="Unsupported path entity: "+e.type;return {};}
        }
        if(p["entities"].size()>0&&found.size()!=p["entities"].size()){error="A selected path entity no longer exists.";return {};}
    }
    else
    {
        const auto* f=find(p["feature"].toString());if(f==nullptr){error="Path body no longer exists.";return {};}CadFeature owner=*f;owner.shape=evaluate(owner.id,error,active,&owner.faceOrigins);if(owner.shape.IsNull())return {};
        for(const auto& v:*p["edges"].getArray()){TopoDS_Edge edge;if(!modelingEdge(owner,v.toString(),edge,error))return {};BRepBuilderAPI_Copy copied(edge,true,false);edges->Append(copied.Shape());}
    }
    if(edges->Length()<1||edges->Length()>256){error="The path must have 1 to 256 supported segments.";return {};}
    Handle(TopTools_HSequenceOfShape) wires=new TopTools_HSequenceOfShape();ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges,1e-7,false,wires);
    if(wires->Length()!=1){error="Path segments must form one connected, unbranched chain.";return {};}
    auto wire=TopoDS::Wire(wires->Value(1));int count=0;for(BRepTools_WireExplorer ex(wire);ex.More();ex.Next())++count;
    if(count!=edges->Length()||!BRepCheck_Analyzer(wire,true).IsValid()||selfIntersects(wire)){error="The path is branched, degenerate or self-intersecting.";return {};}
    if((bool)p.getProperty("reverse",false))wire.Reverse();return wire;
}

TopoDS_Shape CadDocument::evaluateModeling(const CadFeature& feature,CadFaceOrigins& origins,juce::String& error,juce::StringArray& active,bool surfaceOutput)
{
    if(isSurfaceType(feature.type))return evaluateSurface(feature,origins,error,active);
    if(din::isDinType(feature.type)){din::FaceNames names;auto shape=din::build(feature.type,feature.modeling,feature.id,names,error);if(shape.IsNull())return {};origins=names;return shape;}
    if(gear::isGearType(feature.type)){gear::FaceNames names;auto shape=gear::build(feature.modeling,feature.id,names,error);if(shape.IsNull())return {};origins=names;return shape;}
    const auto p=feature.modeling;if(!validateModeling(feature.type,p,error))return {};TopoDS_Shape tool;CadFaceOrigins toolNames;
    if(feature.type=="sweep")
    {
        TopoDS_Wire sourceProfile;CadSketchFrame frame;
        if(surfaceOutput){if(!modelingProfile(p["profile"].toString(),sourceProfile,frame,error,active))return {};}
        else {TopoDS_Face section;if(!modelingSection(p["profile"].toString(),section,frame,error,active))return {};sourceProfile=BRepTools::OuterWire(section);}
        const auto spine=modelingPath(p["path"],nullptr,error,active);if(spine.IsNull())return {};
        BRepTools_WireExplorer ex(spine);if(!ex.More()){error="The sweep path is empty.";return {};}const auto first=ex.Current();BRepAdaptor_Curve curve(first);gp_Pnt start;gp_Vec tangent;curve.D1(first.Orientation()==TopAbs_REVERSED?curve.LastParameter():curve.FirstParameter(),start,tangent);if(first.Orientation()==TopAbs_REVERSED)tangent.Reverse();
        if(tangent.Magnitude()<1e-9){error="The path has a degenerate starting tangent.";return {};}const gp_Dir normal(tangent);gp_Vec u(frame.u);u-=gp_Vec(normal)*u.Dot(gp_Vec(normal));if(u.Magnitude()<1e-7){u=gp_Vec(frame.v);u-=gp_Vec(normal)*u.Dot(gp_Vec(normal));}const gp_Dir targetU(u);
        gp_Trsf align;if((bool)p["alignProfile"])align.SetDisplacement(gp_Ax3(frame.origin,frame.normal,frame.u),gp_Ax3(start,normal,targetU));
        else if(std::abs(gp_Vec(frame.origin,start).Dot(gp_Vec(frame.normal)))>1e-6||std::abs(frame.normal.Dot(normal))<1-1e-7){error="The unaligned profile must pass through the path start and be normal to its tangent.";return {};}
        gp_Trsf roll;roll.SetRotation(gp_Ax1(start,normal),(double)p["rollDegrees"]*juce::MathConstants<double>::pi/180.0);align=roll.Multiplied(align);BRepBuilderAPI_Transform moved(sourceProfile,align,true);auto profile=TopoDS::Wire(moved.Shape());
        BRepOffsetAPI_MakePipeShell pipe(spine);pipe.SetTolerance(1e-6,1e-6,1e-5);const auto orientation=p["orientation"].toString();
        if(orientation=="fixed")pipe.SetMode(direction(p["binormal"]));else pipe.SetMode(orientation=="frenet");
        const auto twist=(double)p["twistDegrees"]*juce::MathConstants<double>::pi/180.0;
        if(std::abs(twist)>1e-10)
        {
            const auto extent=length(spine);TopoDS_Vertex a,b;TopExp::Vertices(spine,a,b);bool straight=!a.IsNull()&&!b.IsNull()&&std::abs(BRep_Tool::Pnt(a).Distance(BRep_Tool::Pnt(b))-extent)<1e-6;
            for(BRepTools_WireExplorer e(spine);e.More();e.Next())if(BRepAdaptor_Curve(e.Current()).GetType()!=GeomAbs_Line)straight=false;
            if(!straight){error="Controlled twist is supported on straight paths only. Use zero twist on curved paths.";return {};}
            const auto radial=targetU.Transformed(roll);Handle(Geom_CylindricalSurface) cylinder=new Geom_CylindricalSurface(gp_Ax3(start,normal,radial),1.0);Handle(Geom2d_Line) law=new Geom2d_Line(gp_Pnt2d(0,0),gp_Dir2d(twist,extent));
            auto edge=BRepBuilderAPI_MakeEdge(law,cylinder,0,std::hypot(twist,extent)).Edge();if(!BRepLib::BuildCurve3d(edge,1e-6)){error="Unable to construct the twist guide.";return {};}pipe.SetMode(BRepBuilderAPI_MakeWire(edge).Wire(),true,BRepFill_NoContact);
        }
        const auto transition=p["transition"].toString();pipe.SetTransitionMode(transition=="round"?BRepBuilderAPI_RoundCorner:transition=="right"?BRepBuilderAPI_RightCorner:BRepBuilderAPI_Transformed);pipe.Add(profile,false,!surfaceOutput);pipe.Build();
        if(!pipe.IsDone()||(!surfaceOutput&&!pipe.MakeSolid())){error="Sweep failed: invalid profile/path placement, tight bends, corner transition or self-intersection (OCCT status "+juce::String((int)pipe.GetStatus())+").";return {};}
        tool=pipe.Shape();if(!pipe.FirstShape().IsNull()&&pipe.FirstShape().ShapeType()==TopAbs_FACE)toolNames.push_back({pipe.FirstShape(),{feature.id+"/start"}});if(!pipe.LastShape().IsNull()&&pipe.LastShape().ShapeType()==TopAbs_FACE)toolNames.push_back({pipe.LastShape(),{feature.id+"/end"}});int index=0;
        CadTopologyEvolution history;history.featureId=feature.id;for(BRepTools_WireExplorer e(profile);e.More();e.Next(),++index)for(TopTools_ListIteratorOfListOfShape it(pipe.Generated(e.Current()));it.More();it.Next())if(it.Value().ShapeType()==TopAbs_FACE){toolNames.push_back({it.Value(),{feature.id+"/side:"+juce::String(index)}});history.generated.push_back({e.Current(),it.Value()});}operationHistory.push_back(std::move(history));
    }
    if(feature.type=="loft")
    {
        BRepOffsetAPI_ThruSections loft(!surfaceOutput,(bool)p["ruled"],1e-6);loft.SetMutableInput(false);loft.CheckCompatibility(p["alignment"].toString()=="auto");std::vector<TopoDS_Wire> sections;gp_Pnt previous;int expected=-1,expectedClosure=-1;
        for(int i=0;i<p["sections"].size();++i)
        {
            TopoDS_Wire wire;CadSketchFrame frame;GProp_GProps props;
            if(surfaceOutput){if(!modelingProfile(p["sections"][i].toString(),wire,frame,error,active))return {};BRepGProp::LinearProperties(wire,props);}
            else {TopoDS_Face face;if(!modelingSection(p["sections"][i].toString(),face,frame,error,active))return {};wire=BRepTools::OuterWire(face);BRepGProp::SurfaceProperties(face,props);}if(i>0&&props.CentreOfMass().Distance(previous)<1e-7){error="Consecutive loft sections must have distinct centres/planes.";return {};}previous=props.CentreOfMass();
            if(surfaceOutput){const int closure=closed(wire)?1:0;if(expectedClosure>=0&&expectedClosure!=closure){error="Surface Loft sections must consistently be open or closed.";return {};}expectedClosure=closure;}
            const bool reversed=p["reverseSections"].size()>0&&(bool)p["reverseSections"][i];if(reversed)wire.Reverse();std::vector<TopoDS_Edge> edges;for(BRepTools_WireExplorer e(wire);e.More();e.Next())edges.push_back(e.Current());const int count=(int)edges.size();
            const int offset=p["offsets"].size()>0?(int)p["offsets"][i]:0;const double seam=p["seamAngles"].size()>0?(double)p["seamAngles"][i]:0;
            if(offset>=count){error="Section seam offset is outside its edge count.";return {};}
            if(p["alignment"].toString()=="manual"&&expected>=0&&expected!=count){error="Manual correspondence requires equal edge counts. Choose auto alignment for compatible mixed sections.";return {};}expected=count;
            if(std::abs(seam)>1e-9)
            {if(count!=1||BRepAdaptor_Curve(edges.front()).GetType()!=GeomAbs_Circle){error="Numeric seam angles apply to circular sections; use edge offsets for polygons.";return {};}const auto circle=BRepAdaptor_Curve(edges.front()).Circle();const auto angle=seam*juce::MathConstants<double>::pi/180;auto edge=BRepBuilderAPI_MakeEdge(circle,angle,angle+2*juce::MathConstants<double>::pi).Edge();if(reversed)edge.Reverse();wire=BRepBuilderAPI_MakeWire(edge).Wire();}
            else if(offset!=0){if(!closed(wire)){error="Starting-edge offsets require closed sections.";return {};}BRepBuilderAPI_MakeWire reordered;for(int j=0;j<count;++j)reordered.Add(edges[(j+offset)%count]);wire=reordered.Wire();}
            sections.push_back(wire);loft.AddWire(wire);
        }
        loft.Build();if(!loft.IsDone()||loft.Shape().IsNull()){error="Loft failed: incompatible sections/correspondence or degenerate geometry.";return {};}tool=loft.Shape();if(!loft.FirstShape().IsNull()&&loft.FirstShape().ShapeType()==TopAbs_FACE)toolNames.push_back({loft.FirstShape(),{feature.id+"/start"}});if(!loft.LastShape().IsNull()&&loft.LastShape().ShapeType()==TopAbs_FACE)toolNames.push_back({loft.LastShape(),{feature.id+"/end"}});
        CadTopologyEvolution history;history.featureId=feature.id;for(size_t s=0;s<sections.size();++s){int i=0;for(BRepTools_WireExplorer e(sections[s]);e.More();e.Next(),++i)for(TopTools_ListIteratorOfListOfShape it(loft.Generated(e.Current()));it.More();it.Next())if(it.Value().ShapeType()==TopAbs_FACE){toolNames.push_back({it.Value(),{feature.id+"/section:"+juce::String((int)s)+"/edge:"+juce::String(i)}});history.generated.push_back({e.Current(),it.Value()});}}operationHistory.push_back(std::move(history));
    }
    if(surfaceOutput){if(tool.IsNull()||!BRepCheck_Analyzer(tool,true).IsValid()||selfIntersects(tool)){error="The generated surface is invalid or self-intersecting.";return {};}nameRemaining(tool,feature.id,toolNames);origins=toolNames;return tool;}
    CadFaceOrigins targetNames;TopoDS_Shape target;const auto mode=feature.type=="mill"?juce::String("subtract"):p["operation"].toString();
    if(mode!="new"){target=evaluate(p["target"].toString(),error,active,&targetNames);if(target.IsNull()||!solid(target,error))return {};}
    if(feature.type=="mill")
    {
        CadSketchFrame frame;const auto r=p["reference"];const auto kind=r["type"].toString();
        if(kind=="face"){TopoDS_Face face;if(!holeSupport(r["feature"].toString(),r["face"].toString(),frame,face,error,active))return {};}
        else
        {
            if(kind=="custom"){frame.origin=point(r["origin"]);frame.normal=direction(r["vector"]);}else {frame.origin=gp_Pnt(0,0,0);frame.normal=kind=="XY"?gp::DZ():kind=="XZ"?gp::DY():gp::DX();}
            gp_Vec u(gp::DX());u-=gp_Vec(frame.normal)*u.Dot(gp_Vec(frame.normal));if(u.Magnitude()<1e-7){u=gp_Vec(gp::DY());u-=gp_Vec(frame.normal)*u.Dot(gp_Vec(frame.normal));}frame.u=gp_Dir(u);frame.v=frame.normal.Crossed(frame.u);
        }
        if((bool)r.getProperty("reverse",false)){frame.normal.Reverse();frame.v.Reverse();}
        Bnd_Box bounds;BRepBndLib::AddOptimal(target,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);const auto scale=std::hypot(x1-x0,y1-y0,z1-z0);double depth=(double)p["depth"];
        if((bool)p["throughAll"]){depth=0;for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1})depth=std::max(depth,gp_Vec(frame.origin,gp_Pnt(x,y,z)).Dot(gp_Vec(frame.normal.Reversed())));depth+=std::max(1e-4,scale*1e-6);}
        // A straight axial cutter must approach from outside the entire solid.
        const auto span=std::max(100.0,scale+frame.origin.Distance(gp_Pnt((x0+x1)/2,(y0+y1)/2,(z0+z1)/2))+10);const auto plane=BRepBuilderAPI_MakeFace(gp_Pln(frame.origin,frame.normal),-span,span,-span,span).Face();const auto above=BRepPrimAPI_MakeHalfSpace(plane,frame.origin.Translated(gp_Vec(frame.normal)*span)).Solid();BRepBuilderAPI_Copy accessCopy(target,true,false);BRepAlgoAPI_Common access(accessCopy.Shape(),above);access.Build();
        if(!access.IsDone()||access.HasErrors()||(!access.Shape().IsNull()&&TopExp_Explorer(access.Shape(),TopAbs_SOLID).More()&&std::abs(volume(access.Shape()))>std::max(1e-7,volume(target)*1e-10))){error="Cutter accessibility failed: choose an entry plane outside/on the outer side of all target material. Internal/obstructed entry planes are unsupported.";return {};}
        const auto strategy=p["strategy"].toString();TopoDS_Face region;TopoDS_Wire wire;
        if(strategy=="pocket")
        {
            CadSketchFrame original;if(!modelingSection(p["region"].toString(),region,original,error,active))return {};
            if((bool)p["alignToReference"]){gp_Trsf trsf;trsf.SetDisplacement(gp_Ax3(original.origin,original.normal,original.u),gp_Ax3(frame.origin,frame.normal,frame.u));region=TopoDS::Face(BRepBuilderAPI_Transform(region,trsf,true).Shape());}
            wire=BRepTools::OuterWire(region);
        }
        else
        {wire=modelingPath(p["path"],(bool)p["alignToReference"]&&p["path"]["sketch"].toString().isNotEmpty()?&frame:nullptr,error,active);if(wire.IsNull())return {};}
        // Check every curve in the chosen plane, including sampled curved interiors.
        for(BRepTools_WireExplorer e(wire);e.More();e.Next()){BRepAdaptor_Curve c(e.Current());for(int j=0;j<=16;++j)if(std::abs(gp_Vec(frame.origin,c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*j/16.0)).Dot(gp_Vec(frame.normal)))>1e-6){error="Machining contours must be planar in the reference plane. Enable align-to-reference for a sketch or choose coplanar geometry.";return {};}}
        const double radius=(double)p["toolDiameter"]*.5;const auto extent=gp_Vec(frame.normal)*-depth;
        if(strategy=="pocket")
        {const auto centres=offsetFace(region,-radius,error);if(centres.IsNull())return {};const auto reachable=offsetFace(centres,radius,error);if(reachable.IsNull())return {};tool=prism(reachable,extent,feature.id,toolNames);}
        else if(strategy=="slot")
        {if(closed(wire)){error="Slot needs an open path; use Profile cut for a closed path.";return {};}BRepBuilderAPI_MakeFace carrier(gp_Pln(frame.origin,frame.normal),wire,false);if(!carrier.IsDone()){error="Unable to place the slot path in its reference plane.";return {};}BRepOffsetAPI_MakeOffset offset(carrier.Face(),GeomAbs_Arc,false);offset.Perform(radius);if(!offset.IsDone()){error="Unable to construct the cutter-width slot.";return {};}TopoDS_Wire outline;int count=0;for(TopExp_Explorer e(offset.Shape(),TopAbs_WIRE);e.More();e.Next()){outline=TopoDS::Wire(e.Current());++count;}TopoDS_Face face;if(count!=1||!faceFromWire(outline,face,error))return {};tool=prism(face,extent,feature.id,toolNames);}
        else
        {
            if(!faceFromWire(wire,region,error))return {};const auto compensation=p["compensation"].toString();const auto outer=offsetFace(region,compensation=="inside"?0:compensation=="outside"?2*radius:radius,error);const auto inner=offsetFace(region,compensation=="outside"?0:compensation=="inside"?-2*radius:-radius,error);if(outer.IsNull()||inner.IsNull())return {};
            CadFaceOrigins innerNames;auto a=prism(outer,extent,feature.id,toolNames),b=prism(inner,extent,feature.id+"/inner",innerNames);toolNames.insert(toolNames.end(),innerNames.begin(),innerNames.end());BRepAlgoAPI_Cut band(a,b);band.Build();if(!band.IsDone()||band.HasErrors()){error="Unable to form the profile-cut cutter band.";return {};}tool=band.Shape();toolNames=mapNames(band,toolNames,tool);
        }
    }
    if(!tool.IsNull()&&volume(tool)<0)tool.Reverse();
    if(tool.IsNull()||!solid(tool,error))return {};nameRemaining(tool,feature.id,toolNames);if(mode=="new"){origins=toolNames;return tool;}
    BRepBuilderAPI_Copy copied(target,true,false);auto all=mapNames(copied,targetNames,copied.Shape());all.insert(all.end(),toolNames.begin(),toolNames.end());TopoDS_Shape result;
    const auto run=[&](auto& op)->bool
    {
        op.Build();if(!op.IsDone()||op.HasErrors()||!solid(op.Shape(),error))return false;result=op.Shape();origins=mapNames(op,all,result);CadTopologyEvolution history;history.featureId=feature.id;
        for(const auto kind:{TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer ex(target,kind);ex.More();ex.Next()){const auto image=copied.ModifiedShape(ex.Current());for(TopTools_ListIteratorOfListOfShape it(op.Modified(image));it.More();it.Next())history.modified.push_back({ex.Current(),it.Value()});for(TopTools_ListIteratorOfListOfShape it(op.Generated(image));it.More();it.Next())history.generated.push_back({ex.Current(),it.Value()});if(op.IsDeleted(image))history.deleted.push_back(ex.Current());}operationHistory.push_back(std::move(history));return true;
    };
    if(mode=="add"){BRepAlgoAPI_Fuse op(copied.Shape(),tool);if(!run(op))return {};}else {BRepAlgoAPI_Cut op(copied.Shape(),tool);if(!run(op))return {};}
    const auto delta=volume(result)-volume(target);if(mode=="add"?delta<=std::max(1e-8,volume(target)*1e-12):-delta<=std::max(1e-8,volume(target)*1e-12)){error="The operation does not change target material. Check placement, depth and mode.";return {};}nameRemaining(result,feature.id,origins);return result;
}

std::shared_ptr<const CadRenderSnapshot> CadDocument::modelingPreview(const juce::String& type,const juce::var& args,juce::var& result,juce::String& error) const
{
    CadDocument draft;draft.geometryCache=geometryCache;if(!draft.deserialize(serialize(),error))return {};auto request=copyJson(args);request.getDynamicObject()->setProperty("action",args["id"].toString().isEmpty()?"create":"update");result=draft.modelingCommand(type,request);if(!(bool)result["ok"]){error=result["error"].toString();return {};}
    result.getDynamicObject()->setProperty("volume_mm3",isSurfaceType(type)?(double)result["topology"]["volume_mm3"]:volume(draft.find(result["id"].toString())->shape));result.getDynamicObject()->setProperty("preview",true);result.getDynamicObject()->setProperty("committed",false);if(args["id"].toString().isEmpty())result.getDynamicObject()->removeProperty("id");static std::atomic<std::uint64_t> serial{0};auto view=std::make_shared<CadRenderSnapshot>(*draft.snapshot());view->documentInstance=instanceId;view->revision=(std::uint64_t(1)<<63)|(std::uint64_t(1)<<60)|++serial;return view;
}

juce::var CadDocument::modelingCommand(const juce::String& type,const juce::var& args)
{
    auto reply=object();auto* out=reply.getDynamicObject();juce::String error;const auto fail=[&](const char* code,const juce::String& e){out->setProperty("ok",false);out->setProperty("code",code);out->setProperty("error",e);return reply;};
    if(!isModelingType(type)||!args.isObject())return fail("invalid_arguments","Invalid modeling request.");const auto action=args.getProperty("action","get").toString();auto id=args["id"].toString();
    if(action=="references"){auto a=copyJson(args);a.getDynamicObject()->setProperty("action","edges");return chamferCommand(a);}
    if(action=="preview"){juce::var details;if(modelingPreview(type,args,details,error)==nullptr)return fail("modeling_failed",error);return details;}
    if(!juce::StringArray{"create","get","update","delete"}.contains(action))return fail("invalid_arguments","Action must be create/get/update/delete/preview/references.");const auto* f=find(id);
    if(action!="create"&&(f==nullptr||f->type!=type))return fail("not_found","Unknown "+type+" feature: "+id);
    if(action=="delete"){if(!remove(id,error))return fail("dependency_error",error);out->setProperty("ok",true);out->setProperty("id",id);out->setProperty("deleted",true);return reply;}
    if(action=="create"||action=="update")
    {
        if(!args["parameters"].isObject())return fail("invalid_arguments","Parameters must be an object.");auto p=action=="create"?modelingDefaults(type):copyJson(f->modeling);for(const auto& v:args["parameters"].getDynamicObject()->getProperties())p.getDynamicObject()->setProperty(v.name,copyJson(v.value));
        auto path=p["path"];if(path.isObject()&&path["feature"].toString().isNotEmpty()&&path["edges"].isArray())
        {
            auto a=object();a.getDynamicObject()->setProperty("action","edges");a.getDynamicObject()->setProperty("target",path["feature"]);const auto list=chamferCommand(a);juce::Array<juce::var> tokens;
            for(const auto& v:*path["edges"].getArray()){if(v.isString()){tokens.add(v);continue;}if(!numeric(v)||(double)v!=std::floor((double)v)||(double)v<0||(double)v>=list["edges"].size())return fail("invalid_reference","Path edge index is out of range.");tokens.add(list["edges"][(int)v]["reference"]);}path.getDynamicObject()->setProperty("edges",tokens);
        }
        if(!validateModeling(type,p,error))return fail("invalid_arguments",error);
        if(type=="surface_trim"&&(action=="create"||p["regionReference"]["face"].toString().isEmpty()||p["target"]!=f->modeling["target"]||p["face"]!=f->modeling["face"]||juce::JSON::toString(p["seed"],true)!=juce::JSON::toString(f->modeling["seed"],true)))
        {
            try
            {
            const auto* source=find(p["target"].toString());if(source==nullptr)return fail("invalid_reference","Missing trim source.");const auto selectedFace=p["face"].toString();
            const auto vertex=BRepBuilderAPI_MakeVertex(point(p["seed"])).Vertex();int count=0;auto anchor=object();
            for(TopExp_Explorer e(source->shape,TopAbs_FACE);e.More();e.Next())
            {
                juce::String token;for(const auto& name:source->faceOrigins)if(name.first.IsSame(e.Current()))for(const auto& t:name.second)if(selectedFace.isEmpty()||t==selectedFace){token=t;break;}
                if(token.isEmpty())continue;BRepExtrema_DistShapeShape distance(vertex,e.Current());distance.Perform();if(!distance.IsDone()||distance.Value()>1e-6||distance.SupportTypeShape2(1)!=BRepExtrema_IsInFace)continue;
                double u=0,v=0;distance.ParOnFaceS2(1,u,v);anchor.getDynamicObject()->setProperty("face",token);anchor.getDynamicObject()->setProperty("u",u);anchor.getDynamicObject()->setProperty("v",v);++count;
            }
            if(count!=1)return fail("invalid_reference","Trim region point must lie inside one uniquely named source face. Pick the region again.");p.getDynamicObject()->setProperty("regionReference",anchor);
            }
            catch(const Standard_Failure& failure){return fail("invalid_reference",juce::String("Cannot locate the trim region: ")+failure.GetMessageString());}
        }
        for(const auto& dependency:modelingDependencies(p))if(dependency==id||find(dependency)==nullptr)return fail("invalid_reference","Missing or self-referencing source: "+dependency);
        auto created=id;const auto ok=mutate([&](juce::String&)
        {
            if(action=="create"){CadFeature value;value.id=newId(type.toRawUTF8(),nextNumber++);value.type=type;value.name=modelingLabel(type)+" "+juce::String(nextNumber-1);items.push_back(value);created=value.id;}
            for(auto& value:items)if(value.id==created){value.modeling=p;value.firstOperand=isSurfaceType(type)?(modelingDependencies(p).isEmpty()?juce::String():modelingDependencies(p)[0]):type=="loft"?p["sections"][0].toString():type=="sweep"?p["profile"].toString():p["target"].toString();value.targetFeatureId=p["target"].toString();}
            for(auto& value:items)
            {
                const bool target=value.id==p["target"].toString();
                if((target&&(isSurfaceType(type)||type=="mill"||p["operation"].toString()!="new"))||(!target&&modelingDependencies(p).contains(value.id)&&value.id!=p["reference"]["feature"].toString()))value.visible=false;
            }
            return true;
        },error);
        if(!ok)return fail("modeling_failed",error.isEmpty()?"OCCT could not construct this feature. Check source geometry and parameters.":error);id=created;f=find(id);
    }
    out->setProperty("ok",true);out->setProperty("id",id);out->setProperty("type",type);out->setProperty("parameters",copyJson(f->modeling));if(isSurfaceType(type))out->setProperty("topology",surfaceTopology(f->shape));return reply;
}
}
