#pragma once
#include "CadDocument.h"
#include <Geom2d_Curve.hxx>
#include <Geom_Curve.hxx>
#include <map>

namespace djehuti::cad::ui
{
struct SnapPreferences
{
    bool enabled=true;
    // Grid, Vertex, Edge, Midpoint, Center, Intersection, Face.
    std::array<bool,7> modes{false,true,false,false,false,false,true};
    static juce::StringArray names(){return {"grid","vertex","edge","midpoint","center","intersection","face"};}
    juce::var state() const;
    bool configure(const juce::var&,juce::String& error);
};
struct SnapHit
{
    bool found=false,reference=false;
    juce::Point<double> point;
    juce::String type,source,entity;
    juce::var state() const;
};
struct ReferenceInput
{
    juce::String id,feature,label;
    bool solid=false,visible=true;
    gp_Trsf transform;
};
// Disposable display/query data. Native sketch/OCAF geometry remains authoritative.
class SketchReferenceScene
{
public:
    void rebuild(const CadDocument&,const juce::String& active,const std::vector<ReferenceInput>&);
    SnapHit snap(juce::Point<double> raw,double pixelsPerMm,double gridStep,const SnapPreferences&,bool referenceSnapping,const juce::String& excludedEntity={}) const;
    juce::String referenceAt(juce::Point<double>,double tolerance) const;
    void paint(juce::Graphics&,const juce::AffineTransform& planeToScreen,double scale,float opacity,const juce::String& selected,const SnapHit& highlight) const;
    juce::Rectangle<double> referenceBounds() const;
    juce::var state() const;
private:
    struct Curve
    {
        Handle(Geom2d_Curve) geometry;
        double first=0,last=1;
        juce::String source,entity;
        bool reference=false,solid=false,section=false;
        mutable juce::Path path;
        mutable double pathScale=0;
        juce::Rectangle<double> bounds;
    };
    struct Point{juce::Point<double> position;juce::String source,entity,type;bool reference=false;};
    struct Ghost{juce::String source;juce::Path fill;};
    std::vector<Curve> curves;
    std::vector<Point> points;
    std::vector<Ghost> ghosts;
    juce::String active,symmetryMode;juce::Point<double> symmetryCentre;
    void addCurve(const Handle(Geom_Curve)&,double,double,const CadSketchFrame&,const juce::String&,const juce::String&,bool,bool,bool);
    void addPoint(const gp_Pnt&,const CadSketchFrame&,const juce::String&,const juce::String&,const juce::String&,bool);
    static juce::Point<double> closest(const Curve&,juce::Point<double>);
    static void buildPath(const Curve&,double scale);
};
}
