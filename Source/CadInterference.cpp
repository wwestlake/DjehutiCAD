#include "CadInterference.h"
#include "CadDocument.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <set>

namespace djehuti::cad
{
namespace
{
constexpr double kFuzzyMm = 1.0e-5;          // coincident faces within this distance are treated as touching
constexpr double kMinimumVolumeMm3 = 1.0e-3; // common volumes below max(this, 1e-6 x smaller part) are contact
constexpr int kMaxRegionSegments = 4000;

double volumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

std::vector<std::array<float, 6>> outline(const TopoDS_Shape& region)
{
    std::vector<std::array<float, 6>> segments;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(region, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent() && (int) segments.size() < kMaxRegionSegments; ++i)
    {
        const auto& edge = TopoDS::Edge(edges(i));
        if (BRep_Tool::Degenerated(edge)) continue;
        BRepAdaptor_Curve curve(edge);
        GCPnts_QuasiUniformDeflection points(curve, 0.1);
        if (!points.IsDone() || points.NbPoints() < 2) continue;
        for (int k = 1; k < points.NbPoints(); ++k)
        {
            const auto a = points.Value(k), b = points.Value(k + 1);
            segments.push_back({ (float) a.X(), (float) a.Y(), (float) a.Z(), (float) b.X(), (float) b.Y(), (float) b.Z() });
        }
    }
    return segments;
}

juce::var pairVar(const CadInterferencePair& p)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty("first", p.firstId); o->setProperty("firstName", p.firstName);
    o->setProperty("second", p.secondId); o->setProperty("secondName", p.secondName);
    o->setProperty("status", p.status);
    if (p.status == "interference") o->setProperty("volumeMm3", p.volumeMm3);
    if (p.detail.isNotEmpty()) o->setProperty("detail", p.detail);
    return juce::var(o.release());
}
}

juce::var CadInterferenceReport::toVar() const
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty("ok", error.isEmpty());
    if (error.isNotEmpty()) o->setProperty("error", error);
    o->setProperty("completed", completed);
    o->setProperty("result", !error.isEmpty() ? "error" : !completed ? "incomplete" : interferences.empty() ? "noInterference" : "interference");
    o->setProperty("pose", pose);
    o->setProperty("poseSerial", (juce::int64) poseSerial);
    o->setProperty("documentRevision", (juce::int64) documentRevision);
    o->setProperty("components", components);
    o->setProperty("pairs", pairs);
    o->setProperty("candidatePairs", candidatePairs);
    o->setProperty("thresholdMm3", thresholdMm3);
    juce::Array<juce::var> found, failed, skip;
    for (const auto& p : interferences) found.add(pairVar(p));
    for (const auto& p : failures) failed.add(pairVar(p));
    for (const auto& s : skipped) skip.add(s);
    o->setProperty("interferences", found);
    o->setProperty("failures", failed);
    if (!skipped.isEmpty()) o->setProperty("skipped", skip);
    return juce::var(o.release());
}

namespace interference
{
CadInterferenceReport check(const CadDocument& document, const std::map<juce::String, gp_Trsf>* instanceWorld, std::uint64_t poseSerial)
{
    CadInterferenceReport report;
    report.pose = instanceWorld != nullptr ? "shown" : "reference";
    report.documentRevision = document.revision();
    report.documentInstance = document.documentInstance();
    report.poseSerial = poseSerial;

    // Components: instances of visible solid bodies, then visible solid bodies without instances (display rules).
    const auto& mechanics = document.mechanicsData();
    std::set<juce::String> instanced;
    for (const auto& instance : mechanics.instances) instanced.insert(instance.featureId);
    const auto solid = [](const CadFeature* f) { return f != nullptr && f->visible && f->type != "sketch" && !f->shape.IsNull() && TopExp_Explorer(f->shape, TopAbs_SOLID).More(); };
    std::vector<CadInterferenceComponent> components;
    for (const auto& instance : mechanics.instances)
    {
        const auto* f = document.find(instance.featureId);
        if (!solid(f)) { if (f != nullptr && f->visible) report.skipped.add(instance.name + " (no solid geometry)"); continue; }
        gp_Trsf world = instance.placement.toTrsf();
        if (instanceWorld != nullptr)
        {
            const auto found = instanceWorld->find(instance.id);
            if (found == instanceWorld->end()) { report.error = "The current pose has no frame for instance " + instance.name + "; the mechanism may not be solved."; return report; }
            world = found->second;
        }
        components.push_back({ instance.id, instance.name, f->id, true, document.instanceShapeTransform(f->id, world) });
    }
    for (const auto& f : document.features())
        if (instanced.count(f.id) == 0 && solid(&f)) components.push_back({ f.id, f.name, f.id, false, gp_Trsf() });
    report.components = (int) components.size();

    // Private copies (one per body) so OCCT Booleans never touch the live document shapes.
    std::map<juce::String, TopoDS_Shape> copies;
    std::map<juce::String, double> volumes;
    std::vector<TopoDS_Shape> placed(components.size());
    std::vector<Bnd_Box> boxes(components.size());
    try
    {
        for (size_t i = 0; i < components.size(); ++i)
        {
            const auto& c = components[i];
            if (copies.count(c.featureId) == 0)
            {
                const auto& source = document.find(c.featureId)->shape;
                copies[c.featureId] = BRepBuilderAPI_Copy(source, Standard_True, Standard_False).Shape();
                volumes[c.featureId] = volumeOf(source);
            }
            placed[i] = copies[c.featureId].Moved(TopLoc_Location(c.placement));
            BRepBndLib::Add(placed[i], boxes[i], Standard_False);
            boxes[i].Enlarge(kFuzzyMm);
        }
    }
    catch (const Standard_Failure& e) { report.error = juce::String("OCCT could not prepare the components: ") + e.GetMessageString(); return report; }

    report.completed = true;
    for (size_t i = 0; i < components.size(); ++i)
        for (size_t j = i + 1; j < components.size(); ++j)
        {
            ++report.pairs;
            if (boxes[i].IsOut(boxes[j])) continue;   // broad phase: bounding boxes apart
            ++report.candidatePairs;
            const auto& a = components[i];
            const auto& b = components[j];
            CadInterferencePair pair { a.id, a.name, b.id, b.name, {}, 0.0, {}, {} };
            const double smaller = std::min(volumes[a.featureId], volumes[b.featureId]);
            const double threshold = std::max(kMinimumVolumeMm3, 1.0e-6 * smaller);
            report.thresholdMm3 = std::max(report.thresholdMm3, threshold);
            try
            {
                BRepAlgoAPI_Common common(placed[i], placed[j]);
                common.SetFuzzyValue(kFuzzyMm);
                common.Build();
                if (!common.IsDone() || common.HasErrors())
                {
                    pair.status = "failed"; pair.detail = "OCCT could not intersect these solids.";
                    report.failures.push_back(pair); report.completed = false; continue;
                }
                const auto region = common.Shape();
                const double volume = region.IsNull() ? 0.0 : volumeOf(region);
                if (volume <= threshold) continue;   // separated by less than the fuzzy distance, or touching
                pair.status = "interference";
                pair.volumeMm3 = volume;
                if (std::abs(volume - volumes[a.featureId]) <= 1.0e-6 * volumes[a.featureId]) pair.detail = a.name + " lies entirely inside " + b.name + ".";
                else if (std::abs(volume - volumes[b.featureId]) <= 1.0e-6 * volumes[b.featureId]) pair.detail = b.name + " lies entirely inside " + a.name + ".";
                pair.regionEdges = outline(region);
                report.interferences.push_back(std::move(pair));
            }
            catch (const Standard_Failure& e)
            {
                pair.status = "failed"; pair.detail = juce::String("OCCT failed: ") + e.GetMessageString();
                report.failures.push_back(pair); report.completed = false;
            }
        }
    return report;
}
}
}
