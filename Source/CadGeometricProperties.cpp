#include "CadGeometricProperties.h"
#include "CadDocument.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GProp_PrincipalProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Mat.hxx>

#include <algorithm>
#include <cmath>

namespace djehuti::cad
{
namespace
{
CadGeometricProperties failure(const juce::String& status, const juce::String& message, const juce::String& frame)
{
    CadGeometricProperties p; p.status = status; p.message = message; p.frame = frame; return p;
}

int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum what, TopAbs_ShapeEnum avoid = TopAbs_SHAPE)
{
    int n = 0;
    for (TopExp_Explorer ex(shape, what, avoid); ex.More(); ex.Next()) ++n;
    return n;
}

CadGeometricProperties compute(const TopoDS_Shape& shape, const juce::String& frame)
{
    if (shape.IsNull()) return failure("nullShape", "There is no shape to measure.", frame);
    const int solids = count(shape, TopAbs_SOLID);
    if (solids == 0) return failure("notSolid", "The shape contains no closed solid; volume properties need solid geometry.", frame);
    // Loose topology outside the solids would be silently ignored by a volume integral; refuse it instead.
    const int freeShells = count(shape, TopAbs_SHELL, TopAbs_SOLID), freeFaces = count(shape, TopAbs_FACE, TopAbs_SHELL),
              freeEdges = count(shape, TopAbs_EDGE, TopAbs_FACE), freeVertices = count(shape, TopAbs_VERTEX, TopAbs_EDGE);
    if (freeShells + freeFaces + freeEdges + freeVertices > 0)
        return failure("notSolid", "The shape mixes solids with loose shells, faces, edges or vertices; only solid geometry is supported.", frame);
    try
    {
        if (!BRepCheck_Analyzer(shape).IsValid()) return failure("invalidShape", "The shape fails the OCCT validity check (BRepCheck).", frame);
        GProp_GProps props;
        BRepGProp::VolumeProperties(shape, props);
        const double volume = props.Mass();
        if (!std::isfinite(volume)) return failure("computationFailed", "OCCT returned a non-finite volume.", frame);
        if (volume < 0.0) return failure("invalidShape", "The solid encloses a negative volume (inverted orientation).", frame);
        if (volume <= 1.0e-9) return failure("zeroVolume", "The solid encloses no measurable volume.", frame);

        CadGeometricProperties p;
        p.frame = frame; p.solids = solids; p.volumeMm3 = volume; p.centroidMm = props.CentreOfMass();
        const gp_Mat inertia = props.MatrixOfInertia();   // about the centre of mass, axes parallel to the shape's frame
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) p.inertiaMm5[(size_t) i][(size_t) j] = inertia.Value(i + 1, j + 1);

        const GProp_PrincipalProps principal = props.PrincipalProperties();
        double moments[3] {}; principal.Moments(moments[0], moments[1], moments[2]);
        const gp_Vec axes[3] { principal.FirstAxisOfInertia(), principal.SecondAxisOfInertia(), principal.ThirdAxisOfInertia() };
        int order[3] { 0, 1, 2 };
        std::sort(order, order + 3, [&](int a, int b) { return moments[a] < moments[b]; });
        for (int k = 0; k < 3; ++k)
        {
            p.principalMomentsMm5[(size_t) k] = moments[order[k]];
            const auto axis = axes[order[k]].Magnitude() > 0.0 ? axes[order[k]].Normalized() : gp_Vec(0, 0, 0);
            p.principalAxes[(size_t) k] = { axis.X(), axis.Y(), axis.Z() };
        }
        for (const auto& row : p.inertiaMm5) for (const auto v : row) if (!std::isfinite(v)) return failure("computationFailed", "OCCT returned a non-finite inertia integral.", frame);
        p.status = "ok";
        return p;
    }
    catch (const Standard_Failure& e) { return failure("computationFailed", "OCCT failed computing volume properties: " + juce::String(e.GetMessageString()), frame); }
}

bool rigid(const gp_Trsf& transform)
{
    return !transform.IsNegative() && std::abs(transform.ScaleFactor() - 1.0) <= 1.0e-12;
}
}

juce::var CadGeometricProperties::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("status", status);
    if (message.isNotEmpty()) o->setProperty("message", message);
    o->setProperty("frame", frame);
    o->setProperty("kind", "geometric (unit density; not physical mass properties)");
    if (!ok()) return juce::var(o);
    const auto triple = [](double a, double b, double c) { return juce::var(juce::Array<juce::var> { a, b, c }); };
    o->setProperty("solids", solids);
    o->setProperty("volumeMm3", volumeMm3);
    o->setProperty("centroidMm", triple(centroidMm.X(), centroidMm.Y(), centroidMm.Z()));
    juce::Array<juce::var> rows, axes;
    for (const auto& r : inertiaMm5) rows.add(triple(r[0], r[1], r[2]));
    for (const auto& a : principalAxes) axes.add(triple(a[0], a[1], a[2]));
    o->setProperty("inertiaAboutCentroidMm5", rows);
    o->setProperty("principalMomentsMm5", triple(principalMomentsMm5[0], principalMomentsMm5[1], principalMomentsMm5[2]));
    o->setProperty("principalAxes", axes);
    return juce::var(o);
}

namespace geomprops
{
CadGeometricProperties ofShape(const TopoDS_Shape& shape) { return compute(shape, "shape"); }

CadGeometricProperties ofShape(const TopoDS_Shape& shape, const gp_Trsf& transform)
{
    if (!rigid(transform)) return failure("nonRigidTransform", "Only rotations and translations are supported; the transform scales or mirrors.", "shape");
    if (shape.IsNull()) return compute(shape, "shape");
    return compute(shape.Moved(TopLoc_Location(transform)), "shape");
}

CadGeometricProperties ofFeature(const CadDocument& document, const juce::String& featureId)
{
    const auto* feature = document.find(featureId);
    if (feature == nullptr) return failure("unknownFeature", "Unknown feature: " + featureId, "world");
    auto p = compute(feature->shape, "world");   // evaluated shapes are already in world coordinates
    if (!p.ok()) p.message = feature->name + ": " + p.message;
    return p;
}

CadGeometricProperties ofInstance(const CadDocument& document, const juce::String& instanceId, const std::map<juce::String, gp_Trsf>* instanceWorld)
{
    const auto* instance = document.mechanicsData().findInstance(instanceId);
    if (instance == nullptr) return failure("unknownInstance", "Unknown assembly instance: " + instanceId, "world");
    const auto* feature = document.find(instance->featureId);
    if (feature == nullptr) return failure("unknownFeature", instance->name + " refers to a missing body: " + instance->featureId, "world");
    gp_Trsf world = instance->placement.toTrsf();
    if (instanceWorld != nullptr) if (const auto it = instanceWorld->find(instanceId); it != instanceWorld->end()) world = it->second;
    auto p = ofShape(feature->shape, document.instanceShapeTransform(feature->id, world));
    p.frame = "world";
    if (!p.ok()) p.message = instance->name + ": " + p.message;
    return p;
}
}
}
