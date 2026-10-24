#pragma once
#include "CadDocument.h"
#include <Geom2d_Curve.hxx>
#include <Geom_Curve.hxx>
#include <map>
#include <memory>

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
    // Prepared display/snap data of one solid reference. Reused while the solid's shape identity, placement and the
    // sketch plane are unchanged, so sketch edits and visibility toggles do not repeat the OCCT section/projection.
    struct SolidData{std::vector<Curve> curves;std::vector<Point> points;Ghost ghost;TopoDS_Shape source;};   // source keeps the keyed shape alive
    std::vector<Curve> curves;     // sketches (active and referenced)
    std::vector<Point> points;
    std::vector<std::shared_ptr<const SolidData>> solids;   // visible solid references, in source order
    std::map<juce::String,std::shared_ptr<const SolidData>> solidCache;
    template<typename F> void eachCurve(F&& f) const{for(const auto& c:curves)f(c);for(const auto& s:solids)for(const auto& c:s->curves)f(c);}
    template<typename F> void eachPoint(F&& f) const{for(const auto& p:points)f(p);for(const auto& s:solids)for(const auto& p:s->points)f(p);}
    juce::String active,symmetryMode;juce::Point<double> symmetryCentre;
    int rebuilds=0,solidPreparations=0;juce::var timing;   // diagnostics: rebuilds, solid preparations and the last rebuild's cost
    void addCurve(const Handle(Geom_Curve)&,double,double,const CadSketchFrame&,const juce::String&,const juce::String&,bool,bool,bool);
    void addPoint(const gp_Pnt&,const CadSketchFrame&,const juce::String&,const juce::String&,const juce::String&,bool);
    static juce::Point<double> closest(const Curve&,juce::Point<double>);
    static void buildPath(const Curve&,double scale);
};
}
