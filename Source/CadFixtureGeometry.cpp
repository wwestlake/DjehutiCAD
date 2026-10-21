#include "CadFixtureGeometry.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

namespace djehuti::cad::fixturegeom
{
bool face(const CadDocument& doc,const juce::String& feature,const juce::String& token,
          TopoDS_Face& out,gp_Pnt& centre,bool& planar,juce::String& error)
{
    try
    {
        const auto* f=doc.find(feature);
        if(!f||f->shape.IsNull()){error="Attachment body is missing.";return false;}
        std::vector<TopoDS_Shape> matches;
        for(const auto& [candidate,tokens]:f->faceOrigins)if(tokens.contains(token))
        {bool seen=false;for(const auto& m:matches)seen|=m.IsSame(candidate);if(!seen)matches.push_back(candidate);}
        if(matches.size()!=1){error="Persistent face is missing or split/ambiguous.";return false;}
        out=TopoDS::Face(matches.front());GProp_GProps props;BRepGProp::SurfaceProperties(out,props);
        if(props.Mass()<=0){error="Attachment face has no area.";return false;}
        centre=props.CentreOfMass();planar=BRepAdaptor_Surface(out,true).GetType()==GeomAbs_Plane;return true;
    }
    catch(const Standard_Failure&){error="Attachment geometry could not be evaluated.";return false;}
}
bool onFace(const TopoDS_Face& face,const gp_Pnt& point)
{
    try
    {
        BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(),face);
        return distance.IsDone()&&distance.NbSolution()>0&&distance.Value()<=attachmentToleranceMm;
    }
    catch(const Standard_Failure&){return false;}
}
gp_Trsf featurePlacement(const CadFeature& f)
{return mech::frameFromEulerDegrees(f.x,f.y,f.z,f.rx,f.ry,f.rz).toTrsf();}
}
