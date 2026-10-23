#include "CadSketchReferences.h"
#include <BRep_Tool.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Geom_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_Plane.hxx>
#include <GeomProjLib.hxx>
#include <GeomAPI.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ2d.hxx>
#include <gp_Elips2d.hxx>
#include <gp_Pln.hxx>
#include <gp.hxx>
#include <algorithm>
#include <limits>

namespace djehuti::cad::ui
{
juce::var SnapPreferences::state() const
{auto* v=new juce::DynamicObject();v->setProperty("enabled",enabled);const auto keys=names();for(int i=0;i<7;++i)v->setProperty(keys[i],modes[(size_t)i]);return v;}
bool SnapPreferences::configure(const juce::var& value,juce::String& error)
{
    if(!value.isObject()){error="Snap preferences must be an object.";return false;}const auto keys=names();
    for(const auto& p:value.getDynamicObject()->getProperties())if((p.name.toString()!="enabled"&&!keys.contains(p.name.toString()))||!p.value.isBool()){error="Snap modes must be known boolean preferences.";return false;}
    enabled=value.getProperty("enabled",enabled);for(int i=0;i<7;++i)modes[(size_t)i]=value.getProperty(keys[i],modes[(size_t)i]);return true;
}
juce::var SnapHit::state() const
{auto* v=new juce::DynamicObject();v->setProperty("found",found);v->setProperty("type",type);v->setProperty("x",point.x);v->setProperty("y",point.y);v->setProperty("source",source);v->setProperty("entity",entity);v->setProperty("reference",reference);return v;}
namespace
{
juce::Point<double> projected(const gp_Pnt& point,const CadSketchFrame& frame)
{const gp_Vec offset(frame.origin,point);return {offset.Dot(gp_Vec(frame.u)),offset.Dot(gp_Vec(frame.v))};}
juce::Point<double> point2(const gp_Pnt2d& p){return {p.X(),p.Y()};}
}
void SketchReferenceScene::addPoint(const gp_Pnt& point,const CadSketchFrame& frame,const juce::String& source,const juce::String& entity,const juce::String& type,bool reference)
{points.push_back({projected(point,frame),source,entity,type,reference});}
void SketchReferenceScene::addCurve(const Handle(Geom_Curve)& world,double first,double last,const CadSketchFrame& frame,const juce::String& source,const juce::String& entity,bool reference,bool solid,bool section)
{
    if(world.IsNull()||!std::isfinite(first)||!std::isfinite(last)||last-first<1e-12)return;
    try
    {
        gp_Trsf local;local.SetTransformation(gp_Ax3(frame.origin,frame.normal,frame.u));
        // XZ's historical normal is not U cross V. Use the actual U/V basis for coordinates.
        const auto normal=frame.u.Crossed(frame.v);local.SetTransformation(gp_Ax3(frame.origin,normal,frame.u));
        const auto inPlane=GeomProjLib::ProjectOnPlane(Handle(Geom_Curve)::DownCast(world->Transformed(local)),new Geom_Plane(gp_Pln(gp::XOY())),gp_Dir(0,0,1),true);
        if(inPlane.IsNull())return;const auto curve=GeomAPI::To2d(inPlane,gp_Pln(gp::XOY()));if(curve.IsNull())return;
        Curve item;item.geometry=curve;item.first=first;item.last=last;item.source=source;item.entity=entity;item.reference=reference;item.solid=solid;item.section=section;curves.push_back(std::move(item));
        Geom2dAdaptor_Curve adaptor(curve,first,last);
        const bool fullCircle=(adaptor.GetType()==GeomAbs_Circle||adaptor.GetType()==GeomAbs_Ellipse)&&last-first>=juce::MathConstants<double>::twoPi-1e-9;
        if(!fullCircle)
        {
            const auto length=GCPnts_AbscissaPoint::Length(adaptor,first,last,1e-9);
            GCPnts_AbscissaPoint mid(adaptor,length*.5,first);if(mid.IsDone())points.push_back({point2(curve->Value(mid.Parameter())),source,entity,"midpoint",reference});
        }
        if(adaptor.GetType()==GeomAbs_Circle)points.push_back({point2(adaptor.Circle().Location()),source,entity,"center",reference});
        else if(adaptor.GetType()==GeomAbs_Ellipse)points.push_back({point2(adaptor.Ellipse().Location()),source,entity,"center",reference});
    }
    catch(const Standard_Failure&){} // A normal-to-plane edge can collapse under orthographic projection.
}
void SketchReferenceScene::rebuild(const CadDocument& document,const juce::String& sketchId,const std::vector<ReferenceInput>& sources)
{
    active=sketchId;symmetryMode="off";if(const auto* sketch=document.findSketch(active)){symmetryMode=sketch->symmetryMode;symmetryCentre={sketch->symmetryX,sketch->symmetryY};}curves.clear();points.clear();ghosts.clear();CadSketchFrame plane;if(!document.sketchFrame(active,plane))return;
    const auto addSketch=[&](const juce::String& id,const juce::String& source,bool reference)
    {
        const auto* sketch=document.findSketch(id);CadSketchFrame frame;if(sketch==nullptr||!document.sketchFrame(id,frame))return;
        const auto world=[&](double u,double v){return frame.origin.Translated(gp_Vec(frame.u)*u+gp_Vec(frame.v)*v);};
        for(const auto& e:sketch->entities)
        {
            try
            {
                if(e.type=="circle")
                {if(e.radius<=0)continue;addCurve(new Geom_Circle(gp_Ax2(world(e.x1,e.y1),frame.u.Crossed(frame.v),frame.u),e.radius),0,juce::MathConstants<double>::twoPi,plane,source,e.id,reference,false,false);}
                else if(e.type=="line"||e.type=="bezier")
                {
                    addPoint(world(e.x1,e.y1),plane,source,e.id,"vertex",reference);addPoint(world(e.x2,e.y2),plane,source,e.id,"vertex",reference);
                    if(e.type=="line"){const auto a=world(e.x1,e.y1),b=world(e.x2,e.y2);if(a.Distance(b)>1e-8)addCurve(new Geom_Line(a,gp_Dir(gp_Vec(a,b))),0,a.Distance(b),plane,source,e.id,reference,false,false);}
                    else{TColgp_Array1OfPnt poles(1,4);poles(1)=world(e.x1,e.y1);poles(2)=world(e.cx1,e.cy1);poles(3)=world(e.cx2,e.cy2);poles(4)=world(e.x2,e.y2);addCurve(new Geom_BezierCurve(poles),0,1,plane,source,e.id,reference,false,false);}
                }
            }catch(const Standard_Failure&){}
        }
    };
    addSketch(active,active,false);
    const auto meshes=document.snapshot(true);
    for(const auto& source:sources)if(source.visible&&source.id!=active)
    {
        if(!source.solid){addSketch(source.feature,source.id,true);continue;}
        const auto* feature=document.find(source.feature);if(feature==nullptr||feature->shape.IsNull())continue;
        try
        {
            const auto shape=BRepBuilderAPI_Transform(feature->shape,source.transform,false).Shape();
            int vertex=0;for(TopExp_Explorer ex(shape,TopAbs_VERTEX);ex.More();ex.Next())addPoint(BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())),plane,source.id,"vertex-"+juce::String(vertex++),"vertex",true);
            const auto edges=[&](const TopoDS_Shape& geometry,bool section)
            {
                int index=0;for(TopExp_Explorer ex(geometry,TopAbs_EDGE);ex.More();ex.Next())
                {
                    TopLoc_Location location;double first,last;const auto curve=BRep_Tool::Curve(TopoDS::Edge(ex.Current()),location,first,last);
                    if(!curve.IsNull())addCurve(Handle(Geom_Curve)::DownCast(curve->Transformed(location.Transformation())),first,last,plane,source.id,(section?"section-":"edge-")+juce::String(index++),true,true,section);
                }
            };
            BRepAlgoAPI_Section section(shape,gp_Pln(plane.origin,plane.normal),false);section.Approximation(false);section.Build();if(section.IsDone())
            {edges(section.Shape(),true);int i=0;for(TopExp_Explorer ex(section.Shape(),TopAbs_VERTEX);ex.More();ex.Next())addPoint(BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())),plane,source.id,"section-vertex-"+juce::String(i++),"vertex",true);}
            edges(shape,false);
            Ghost ghost;ghost.source=source.id;ghost.fill.setUsingNonZeroWinding(true);
            for(const auto& mesh:meshes->meshes)if(mesh.featureId==source.feature)for(size_t i=0;i+2<mesh.indices.size();i+=3)
            {
                juce::Point<double> p[3];for(int j=0;j<3;++j){const auto n=(size_t)mesh.indices[i+(size_t)j]*6;p[j]=projected(gp_Pnt(mesh.vertices[n],mesh.vertices[n+1],mesh.vertices[n+2]).Transformed(source.transform),plane);}
                const auto area=(p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);if(std::abs(area)<1e-12)continue;if(area<0)std::swap(p[1],p[2]);
                ghost.fill.startNewSubPath((float)p[0].x,(float)p[0].y);ghost.fill.lineTo((float)p[1].x,(float)p[1].y);ghost.fill.lineTo((float)p[2].x,(float)p[2].y);ghost.fill.closeSubPath();
            }
            ghosts.push_back(std::move(ghost));
        }catch(const Standard_Failure&){}
    }
}
juce::Point<double> SketchReferenceScene::closest(const Curve& curve,juce::Point<double> p)
{
    auto result=point2(curve.geometry->Value(curve.first));double best=result.getDistanceFrom(p);const auto end=point2(curve.geometry->Value(curve.last));if(end.getDistanceFrom(p)<best){result=end;best=end.getDistanceFrom(p);}
    try{Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(p.x,p.y),curve.geometry,curve.first,curve.last);if(projection.NbPoints()>0&&projection.LowerDistance()<best)result=point2(projection.NearestPoint());}catch(const Standard_Failure&){}return result;
}
SnapHit SketchReferenceScene::snap(juce::Point<double> raw,double scale,double grid,const SnapPreferences& preferences,bool references,const juce::String& excluded) const
{
    SnapHit hit;hit.point=raw;if(!preferences.enabled||scale<=0)return hit;const auto tolerance=8.0/scale;int priority=100;double best=tolerance;
    const auto offer=[&](juce::Point<double> p,const juce::String& type,const juce::String& source,const juce::String& entity,bool reference,int rank)
    {const auto distance=raw.getDistanceFrom(p);if(distance>tolerance+1e-12||rank>priority||(rank==priority&&distance>=best-1e-12))return;priority=rank;best=distance;hit={true,reference,p,type,source,entity};};
    for(const auto& p:points)if((!p.reference||references)&&!(p.source==active&&excluded.isNotEmpty()&&(p.entity==excluded||p.entity.contains("_"+excluded+"_"))))
    {const int mode=p.type=="vertex"?1:p.type=="center"?4:3;if(preferences.modes[(size_t)mode])offer(p.position,p.type,p.source,p.entity,p.reference,p.type=="vertex"?0:p.type=="center"?2:3);}
    std::vector<const Curve*> nearby;
    for(const auto& c:curves)if((!c.reference||references)&&!(c.source==active&&excluded.isNotEmpty()&&(c.entity==excluded||c.entity.contains("_"+excluded+"_"))))
    {const auto p=closest(c,raw);if(raw.getDistanceFrom(p)<=tolerance){nearby.push_back(&c);if(preferences.modes[2])offer(p,"edge",c.source,c.entity,c.reference,4);}}
    if(preferences.modes[5]&&priority>0)for(size_t i=0;i<nearby.size();++i)for(size_t j=i+1;j<nearby.size();++j)
    {
        try
        {
            const auto& a=*nearby[i];const auto& b=*nearby[j];Geom2dAPI_InterCurveCurve intersection(new Geom2d_TrimmedCurve(a.geometry,a.first,a.last),new Geom2d_TrimmedCurve(b.geometry,b.first,b.last),1e-9);
            for(int k=1;k<=intersection.NbPoints();++k)offer(point2(intersection.Point(k)),"intersection",a.source,a.entity+" & "+b.source+"/"+b.entity,a.reference||b.reference,1);
        }catch(const Standard_Failure&){}
    }
    if(symmetryMode.isNotEmpty()&&symmetryMode!="off")
    {
        if(preferences.modes[2]){if(symmetryMode!="vertical")offer({raw.x,symmetryCentre.y},"edge",active,"symmetry-horizontal",false,4);if(symmetryMode!="horizontal")offer({symmetryCentre.x,raw.y},"edge",active,"symmetry-vertical",false,4);}
        if(preferences.modes[5]&&symmetryMode=="both")offer(symmetryCentre,"intersection",active,"symmetry-centre",false,1);
    }
    if(preferences.modes[0]&&std::isfinite(grid)&&grid>0)offer({std::round(raw.x/grid)*grid,std::round(raw.y/grid)*grid},"grid",active,{},false,5);
    return hit;
}
juce::String SketchReferenceScene::referenceAt(juce::Point<double> point,double tolerance) const
{juce::String id;double best=tolerance;for(const auto& c:curves)if(c.reference){const auto distance=closest(c,point).getDistanceFrom(point);if(distance<best){best=distance;id=c.source;}}return id;}
void SketchReferenceScene::buildPath(const Curve& c,double scale)
{
    if(c.pathScale>0&&scale/c.pathScale>.8&&scale/c.pathScale<1.25)return;c.path.clear();c.pathScale=scale;
    const auto start=point2(c.geometry->Value(c.first));c.path.startNewSubPath((float)start.x,(float)start.y);int segments=0;
    std::function<void(double,juce::Point<double>,double,juce::Point<double>,int)> add;
    const double tolerance=.25/juce::jmax(.001,scale);
    add=[&](double a,juce::Point<double> p,double b,juce::Point<double> q,int depth)
    {
        const auto mid=(a+b)*.5;const auto r=point2(c.geometry->Value(mid));const auto quarter=point2(c.geometry->Value((a+mid)*.5)),three=point2(c.geometry->Value((mid+b)*.5));
        const auto deviation=[&](juce::Point<double> v){juce::Point<double> nearest;return juce::Line<double>(p,q).getDistanceFromPoint(v,nearest);};
        if(depth<20&&segments<8192&&std::max({deviation(r),deviation(quarter),deviation(three)})>tolerance){add(a,p,mid,r,depth+1);add(mid,r,b,q,depth+1);}else{c.path.lineTo((float)q.x,(float)q.y);++segments;}
    };
    add(c.first,start,c.last,point2(c.geometry->Value(c.last)),0);
}
void SketchReferenceScene::paint(juce::Graphics& g,const juce::AffineTransform& transform,double scale,float opacity,const juce::String& selected,const SnapHit& highlight) const
{
    for(const auto& ghost:ghosts){g.setColour(juce::Colour(0xff8194b0).withAlpha(opacity*(selected==ghost.source?.35f:.16f)));g.fillPath(ghost.fill,transform);}
    for(const auto& c:curves)
    {
        const bool hovered=highlight.found&&highlight.source==c.source&&highlight.entity==c.entity;
        if(!c.reference&&!hovered)continue;buildPath(c,scale);auto path=c.path;path.applyTransform(transform);
        const bool chosen=selected==c.source;g.setColour((hovered?juce::Colour(0xffffda72):chosen?juce::Colour(0xff75d9ff):c.solid?juce::Colour(0xffa6adc8):juce::Colour(0xff72b8cf)).withAlpha(hovered||chosen?.95f:opacity*(c.section?1.0f:.65f)));
        if(c.reference&&!c.solid&&!chosen&&!hovered){const float dash[]{5,4};juce::Path dashed;juce::PathStrokeType(1).createDashedStroke(dashed,path,dash,2);g.fillPath(dashed);}else g.strokePath(path,juce::PathStrokeType(hovered||chosen?2.f:c.section?1.4f:1.f));
    }
}
juce::Rectangle<double> SketchReferenceScene::referenceBounds() const
{
    juce::Rectangle<double> bounds;bool found=false;for(const auto& c:curves)if(c.reference){buildPath(c,1);const auto next=c.path.getBounds().toDouble();bounds=found?bounds.getUnion(next):next;found=true;}return bounds;
}
juce::var SketchReferenceScene::state() const
{auto* v=new juce::DynamicObject();v->setProperty("curves",(int)curves.size());v->setProperty("points",(int)points.size());v->setProperty("solids",(int)ghosts.size());return v;}
}
