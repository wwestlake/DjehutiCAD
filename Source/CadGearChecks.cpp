#include "CadDocument.h"
#include "CadGear.h"
#include "CadInterference.h"
#include "CadKinematics.h"
#include "CadSelection.h"
#include "CadWorkspacePanel.h"
#include "DjehutiAgent.h"
#include "FrustEngine.h"
#include "NodeCompiler.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <set>

extern "C" const char* djehuti_cad_gear(const char* argsJson);
extern "C" const char* djehuti_cad_gear_pair(const char* argsJson);

// Focused checks for parametric spur gears and matched pairs (--cad-gear-check).
namespace djehuti::cad
{
namespace
{
constexpr double pi = juce::MathConstants<double>::pi;
juce::var obj(std::initializer_list<std::pair<juce::Identifier, juce::var>> fields)
{
    auto* o = new juce::DynamicObject(); for (const auto& f : fields) o->setProperty(f.first, f.second); return o;
}
juce::var json(const juce::String& text) { return juce::JSON::parse(text); }
struct Checks
{
    juce::Array<juce::var> rows; int failed = 0;
    const juce::uint32 started = juce::Time::getMillisecondCounter();
    const juce::File progress = juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-gear-check.progress.log");
    Checks() { progress.replaceWithText({}); }
    void check(bool ok, const juce::String& name, const juce::String& detail = {})
    {
        rows.add(obj({ { "name", name }, { "ok", ok }, { "detail", detail } })); if (!ok) ++failed;
        progress.appendText(juce::String((juce::Time::getMillisecondCounter() - started) / 1000.0, 1) + " s " + (ok ? "PASS " : "FAIL ") + name + (ok ? juce::String() : " :: " + detail) + "\n");
    }
    bool finish(juce::var& report) { report = obj({ { "ok", failed == 0 }, { "passed", rows.size() - failed }, { "failed", failed }, { "checks", rows } }); return failed == 0; }
};
bool closeTo(double a, double b, double tolerance) { return std::isfinite(a) && std::abs(a - b) <= tolerance; }
double volume(const TopoDS_Shape& shape) { GProp_GProps p; BRepGProp::VolumeProperties(shape, p); return p.Mass(); }
double volume(const CadCommandService& s, const juce::String& id) { const auto* f = s.document().find(id); return f == nullptr || f->shape.IsNull() ? -1.0 : volume(f->shape); }
int tokenCount(const CadCommandService& s, const juce::String& id, const juce::String& role)
{
    std::set<juce::String> found;
    if (const auto* f = s.document().find(id))
        for (const auto& entry : f->faceOrigins) for (const auto& token : entry.second) if (token.startsWith(id + "/" + role)) found.insert(token);
    return (int) found.size();
}
juce::var gearRequest(const char* action, const juce::String& id, const juce::var& parameters)
{
    auto r = obj({ { "action", action } });
    if (id.isNotEmpty()) r.getDynamicObject()->setProperty("id", id);
    if (parameters.isObject()) r.getDynamicObject()->setProperty("parameters", parameters);
    return r;
}
juce::String problemsOf(const juce::var& status)
{
    juce::StringArray out; if (const auto* a = status["problems"].getArray()) for (const auto& p : *a) out.add(p.toString()); return out.joinIntoString(" | ");
}
// Largest deviation (mm, measured along the pitch direction) of sampled B-spline flank points from the analytic involute.
double involuteDeviation(const TopoDS_Shape& shape, const gear::Spur& s, int& flankEdges)
{
    double worst = 0.0; flankEdges = 0;
    const double pitch = 2.0 * pi / s.teeth;
    TopTools_IndexedMapOfShape edges; TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i)
    {
        BRepAdaptor_Curve curve(TopoDS::Edge(edges(i)));
        if (curve.GetType() != GeomAbs_BSplineCurve) continue;
        ++flankEdges;
        for (int k = 1; k < 40; ++k)
        {
            const gp_Pnt p = curve.Value(curve.FirstParameter() + (curve.LastParameter() - curve.FirstParameter()) * k / 40.0);
            const double r = std::hypot(p.X(), p.Y());
            double angle = std::atan2(p.Y(), p.X()); if (angle < 0) angle += 2.0 * pi;
            const double tooth = std::round(angle / pitch) * pitch;
            const double actual = std::abs(angle - tooth), expected = s.halfAngle(r);
            worst = std::max(worst, r * std::abs(actual - expected));
        }
    }
    return worst;
}
double maxVertexRadius(const TopoDS_Shape& shape)
{
    double r = 0.0; TopTools_IndexedMapOfShape v; TopExp::MapShapes(shape, TopAbs_VERTEX, v);
    for (int i = 1; i <= v.Extent(); ++i) { const auto p = BRep_Tool::Pnt(TopoDS::Vertex(v(i))); r = std::max(r, std::hypot(p.X(), p.Y())); }
    return r;
}
int solids(const TopoDS_Shape& shape) { int n = 0; for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) ++n; return n; }
}

bool runCadGearCheck(juce::var& report)
{
    Checks checks; juce::String error;
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/gear-check"); folder.createDirectory();

    // 1. Derived spur dimensions (m 2, z 20, 20 degrees).
    {
        auto service = std::make_shared<CadCommandService>();
        const auto d = service->gearCommand(gearRequest("describe", {}, json(R"({"teeth":20,"module":2})")));
        checks.check((bool) d["ok"] && closeTo((double) d["derived"]["pitchDiameter"], 40, 1e-12) && closeTo((double) d["derived"]["tipDiameter"], 44, 1e-12)
                     && closeTo((double) d["derived"]["rootDiameter"], 35, 1e-12) && closeTo((double) d["derived"]["baseDiameter"], 40 * std::cos(20 * pi / 180), 1e-12)
                     && closeTo((double) d["derived"]["circularPitch"], 2 * pi, 1e-12) && closeTo((double) d["derived"]["toothThickness"], pi, 1e-12),
                     "Describe reports standard pitch, tip, root and base diameters, circular pitch and tooth thickness", juce::JSON::toString(d["derived"], true));
        checks.check(service->document().revision() == 0 || service->document().features().empty(), "Describe does not change the document");
    }

    // 2. A single spur gear: real valid solid, tooth count, involute flanks, radii, persistence, editing, undo/redo.
    auto service = std::make_shared<CadCommandService>();
    auto created = service->gearCommand(gearRequest("create", {}, json(R"({"teeth":20,"module":2,"faceWidth":8,"boreDiameter":8,"pressureAngleDegrees":20})")));
    const auto gearId = created["id"].toString();
    checks.check((bool) created["ok"] && gearId.startsWith("gear-") && created["derived"].isObject(), "cad_gear create returns the stable gear id, parameters and derived dimensions", created["error"].toString());
    const auto* g = service->document().find(gearId);
    gear::Spur spur; gear::spur(gear::defaults(), spur, error);
    if (g != nullptr) gear::spur(g->modeling, spur, error);
    checks.check(g != nullptr && !g->shape.IsNull() && BRepCheck_Analyzer(g->shape, true).IsValid() && solids(g->shape) == 1, "Gear is one valid OCCT solid");
    checks.check(tokenCount(*service, gearId, "tip:") == 20 && tokenCount(*service, gearId, "root:") == 20, "20-tooth gear has 20 named tip faces and 20 root faces",
                 juce::String(tokenCount(*service, gearId, "tip:")) + " tips, " + juce::String(tokenCount(*service, gearId, "root:")) + " roots");
    checks.check(tokenCount(*service, gearId, "flank:") == 40 && tokenCount(*service, gearId, "bore") == 1 && tokenCount(*service, gearId, "start") == 1 && tokenCount(*service, gearId, "end") == 1,
                 "Both flanks of every tooth, the bore and both end faces carry persistent names");
    if (g != nullptr)
    {
        int flanks = 0; const double deviation = involuteDeviation(g->shape, spur, flanks);
        checks.check(flanks == 80 && deviation < 1.0e-4, "Flank curves follow the analytic involute within 0.0001 mm (2 faces x 40 flanks)", "max deviation " + juce::String(deviation, 9) + " mm over " + juce::String(flanks) + " flank edges");
        checks.check(closeTo(maxVertexRadius(g->shape), 22.0, 1e-7), "Tip corners lie on the 44 mm tip circle", juce::String(maxVertexRadius(g->shape), 9));
        const double v = volume(g->shape);
        checks.check(v > pi * (17.5 * 17.5 - 16) * 8 && v < pi * (22.0 * 22.0 - 16) * 8, "Gear volume lies between the root-circle and tip-circle discs less the bore", juce::String(v, 3) + " mm3");
    }
    std::vector<CadFaceInfo> faces; service->listFaces(gearId, faces, error);
    bool namedEnd = false; for (const auto& f : faces) if (f.token == gearId + "/end" && f.planar && closeTo(f.normal.Z(), 1, 1e-9) && closeTo(f.centre.Z(), 8, 1e-7)) namedEnd = true;
    checks.check(namedEnd, "The end face is a named planar face at z = face width, usable for face sketches");

    const auto before = volume(*service, gearId);
    auto edited = service->gearCommand(gearRequest("update", gearId, json(R"({"teeth":24})")));
    checks.check((bool) edited["ok"] && tokenCount(*service, gearId, "tip:") == 24 && closeTo((double) edited["derived"]["pitchDiameter"], 48, 1e-12), "Editing the tooth count regenerates the gear (24 teeth, 48 mm pitch diameter)", edited["error"].toString());
    edited = service->gearCommand(gearRequest("update", gearId, json(R"({"module":1.5,"faceWidth":12,"profileShift":0.3,"backlash":0.05})")));
    checks.check((bool) edited["ok"] && closeTo((double) edited["parameters"]["teeth"], 24, 0) && closeTo((double) edited["derived"]["tipDiameter"], 1.5 * 24 + 2 * 1.5 + 2 * 0.3 * 1.5, 1e-9), "Update merges module, face width, profile shift and backlash with the existing parameters", edited["error"].toString());
    service->undo(error); service->undo(error);
    checks.check(tokenCount(*service, gearId, "tip:") == 20 && closeTo(volume(*service, gearId), before, 1e-6), "Undo restores the original 20-tooth gear exactly");
    service->redo(error);
    checks.check(tokenCount(*service, gearId, "tip:") == 24, "Redo reapplies the tooth-count edit");
    service->redo(error);
    const auto finalParameters = juce::JSON::toString(service->document().find(gearId)->modeling, true);
    const auto finalVolume = volume(*service, gearId);
    for (const auto* ext : { "djo", "djc" })
    {
        CadCommandService back; const auto file = folder.getChildFile(juce::String("gear.") + ext);
        const bool ok = service->save(file, error) && back.load(file, error);
        const auto* r = back.document().find(gearId);
        checks.check(ok && r != nullptr && juce::JSON::toString(r->modeling, true) == finalParameters && closeTo(volume(back, gearId), finalVolume, std::abs(finalVolume) * 1e-9),
                     juce::String("Save and reopen (.") + ext + ") preserves gear parameters and regenerates identical geometry", error);
    }
    const auto placed = service->setPlacement(gearId, 10, -5, 3, 0, 0, 30, error);
    const auto* moved = service->document().find(gearId);
    checks.check(placed && moved != nullptr && closeTo(moved->x, 10, 1e-12) && closeTo(moved->rz, 30, 1e-12) && (bool) service->gearCommand(gearRequest("get", gearId, {}))["ok"], "A gear is placed like any other body", error);
    juce::String instance; checks.check(service->createInstance(gearId, "Gear instance", instance, error), "A gear can be used as an assembly instance", error);

    // 3. Clear errors for impossible or unsupported definitions.
    const auto refused = [&](const juce::String& parameters, const juce::String& needle, const juce::String& name)
    {
        const auto r = service->gearCommand(gearRequest("create", {}, json(parameters)));
        checks.check(!(bool) r["ok"] && r["error"].toString().containsIgnoreCase(needle), name, r["error"].toString());
    };
    const auto countBefore = service->document().features().size();
    refused(R"({"teeth":4})", "tooth count", "Fewer than 5 teeth is refused");
    refused(R"({"teeth":20.5})", "whole number", "Fractional tooth counts are refused");
    refused(R"({"family":"bevel"})", "not implemented", "Bevel family is refused until its own geometry exists");
    refused(R"({"family":"worm"})", "not implemented", "Worm family is refused until its own geometry exists");
    refused(R"({"teeth":20,"module":2,"boreDiameter":40})", "bore", "A bore that leaves no rim is refused");
    refused(R"({"teeth":10,"module":1,"profileShift":1.5})", "pointed", "Pointed teeth are refused");
    refused(R"({"rootFilletCoefficient":0.3})", "fillet", "A root fillet larger than the tip clearance is refused");
    refused(R"({"backlash":3})", "backlash", "Backlash larger than the tooth is refused");
    refused(R"({"helixAngle":15})", "unknown", "Unknown parameters are refused");
    checks.check(service->document().features().size() == countBefore, "Refused definitions leave the document unchanged");
    const auto smallPinion = service->gearCommand(gearRequest("describe", {}, json(R"({"teeth":12,"module":1})")));
    checks.check((bool) smallPinion["ok"] && (bool) smallPinion["derived"]["undercutRisk"] && smallPinion["derived"]["warnings"].size() > 0, "A 12-tooth standard pinion builds but warns about undercut");
    for (const int teeth : { 5, 8, 13, 41, 42, 43, 60, 150 })
    {
        auto p = json(R"({"module":1,"boreDiameter":0})"); p.getDynamicObject()->setProperty("teeth", teeth);
        if (teeth <= 8) p.getDynamicObject()->setProperty("profileShift", teeth == 5 ? 0.2 : 0.4);
        const auto r = service->gearCommand(gearRequest("create", {}, p));
        const auto id = r["id"].toString();
        checks.check((bool) r["ok"] && tokenCount(*service, id, "tip:") == teeth && BRepCheck_Analyzer(service->document().find(id)->shape, true).IsValid(),
                     juce::String(teeth) + "-tooth gear builds as a valid solid (" + (teeth < 42 ? "radial flank below base circle" : "involute meets root circle") + ")", r["error"].toString());
    }

    // 4. Pair planning: centre distance, ratio, contact ratio, profile shift and refusal of non-meshing pairs.
    auto pairs = std::make_shared<CadCommandService>();
    auto plan = pairs->gearPairCommand(obj({ { "action", "plan" }, { "parameters", json(R"({"module":2,"gearA":{"teeth":20},"gearB":{"teeth":40}})") } }));
    checks.check((bool) plan["ok"] && closeTo((double) plan["plan"]["centreDistance"], 60, 1e-12) && closeTo((double) plan["plan"]["ratio"], -0.5, 1e-15) && closeTo((double) plan["plan"]["contactRatio"], 1.635, 0.01),
                 "20:40 module 2 plan: 60 mm centre distance, ratio -0.5, contact ratio about 1.64", juce::JSON::toString(plan, true).substring(0, 300));
    checks.check(closeTo((double) plan["plan"]["rotationBDegrees"], 180 - 180.0 / 40, 1e-12), "Gear B is rotated half a tooth pitch from facing gear A so a tooth space meets gear A's tooth");
    plan = pairs->gearPairCommand(obj({ { "action", "plan" }, { "parameters", json(R"({"module":2,"gearA":{"teeth":12},"gearB":{"teeth":30}})") } }));
    checks.check(!(bool) plan["ok"] && plan["error"].toString().contains("Involute interference"), "An unshifted 12:30 pair is refused for involute interference", plan["error"].toString());
    plan = pairs->gearPairCommand(obj({ { "action", "plan" }, { "parameters", json(R"({"module":2,"gearA":{"teeth":12,"profileShift":0.5},"gearB":{"teeth":30}})") } }));
    {
        const double alpha = 20 * pi / 180, target = gear::involute(alpha) + 2 * std::tan(alpha) * 0.5 / 42;
        double lo = alpha, hi = 0.8; for (int i = 0; i < 200; ++i) { const double mid = (lo + hi) / 2; (gear::involute(mid) < target ? lo : hi) = mid; }
        const double expected = 42 * std::cos(alpha) / std::cos((lo + hi) / 2);
        checks.check((bool) plan["ok"] && closeTo((double) plan["plan"]["centreDistance"], expected, 1e-9) && (double) plan["plan"]["centreDistance"] > 42.0,
                     "Profile shift (+0.5 on a 12-tooth pinion) gives the working-pressure-angle centre distance", juce::String((double) plan["plan"]["centreDistance"], 9) + " vs " + juce::String(expected, 9) + " " + plan["error"].toString());
    }
    plan = pairs->gearPairCommand(obj({ { "action", "plan" }, { "parameters", json(R"({"module":2,"addendumCoefficient":0.3,"dedendumCoefficient":0.6,"rootFilletCoefficient":0.1,"gearA":{"teeth":20},"gearB":{"teeth":21}})") } }));
    checks.check(!(bool) plan["ok"] && plan["error"].toString().contains("Contact ratio"), "A pair with contact ratio below 1 is refused", plan["error"].toString());

    // 5. Pair creation with mechanism: geometry, placement, joints, coupling, kinematics and interference.
    auto created2 = pairs->gearPairCommand(obj({ { "action", "create" }, { "parameters", json(R"({"module":2,"faceWidth":10,"backlash":0.1,"gearA":{"teeth":20,"boreDiameter":8},"gearB":{"teeth":40,"boreDiameter":10},"mechanism":"create"})") } }));
    const auto gearA = created2["gearA"].toString(), gearB = created2["gearB"].toString();
    checks.check((bool) created2["ok"] && pairs->document().find(gearA) != nullptr && pairs->document().find(gearB) != nullptr, "cad_gear_pair create builds two gear features", created2["error"].toString());
    const auto& mechanics = pairs->document().mechanicsData();
    checks.check(mechanics.instances.size() == 2 && mechanics.joints.size() == 2 && mechanics.couplings.size() == 1 && mechanics.couplings[0].type == "gear" && closeTo(mechanics.couplings[0].ratio, -0.5, 1e-15),
                 "mechanism create adds two instances, two revolute shaft joints and one existing gear coupling with ratio -0.5");
    checks.check(tokenCount(*pairs, gearA, "tip:") == 20 && tokenCount(*pairs, gearB, "tip:") == 40, "Paired gears have their own tooth counts (20 and 40)");
    const auto* fb = pairs->document().find(gearB);
    checks.check(fb != nullptr && closeTo(fb->x, 60, 1e-12) && closeTo(fb->y, 0, 1e-12) && closeTo(fb->rz, 175.5, 1e-12), "Gear B is placed at the 60 mm centre distance with its engaged phase");
    auto status = pairs->gearPairCommand(obj({ { "action", "check" }, { "id", gearA } }));
    checks.check((bool) status["compatible"] && status["mechanism"]["couplings"].size() == 1, "Pair check confirms geometry, placement, phase, joints and coupling", problemsOf(status));
    checks.check(pairs->document().find(gearA)->modeling["pair"]["mate"].toString() == gearB && pairs->document().find(gearB)->modeling["pair"]["role"].toString() == "B", "Each gear records its mate and role");

    const auto solveAt = [&](const CadCommandService& s, double degrees, kin::SolveResult& result)
    {
        juce::String driver, driven;
        for (const auto& j : s.document().mechanicsData().joints) (j.role == "driver" ? driver : driven) = j.id;
        kin::SolveRequest request; request.prescribed[driver] = degrees * pi / 180.0;
        result = kin::solve(s.document().mechanicsData(), request);
        return std::pair<juce::String, juce::String> { driver, driven };
    };
    const auto meshFree = [&](const CadCommandService& s, const juce::String& label, double ratio)
    {
        int clean = 0, total = 0; juce::String detail;
        for (const double degrees : { 0.0, 3.7, 9.0, 181.0 })
        {
            kin::SolveResult result; const auto joints = solveAt(s, degrees, result); ++total;
            const bool coupled = result.valid && closeTo(result.coordinates[joints.second], ratio * degrees * pi / 180.0, 1e-9);
            const auto inspected = interference::check(s.document(), &result.world, (std::uint64_t) total);
            if (coupled && inspected.completed && inspected.interferences.empty()) ++clean;
            else detail << degrees << " deg: valid " << (int) result.valid << " coupled " << (int) coupled << " interferences " << (int) inspected.interferences.size() << (inspected.interferences.empty() ? juce::String() : " volume " + juce::String(inspected.interferences.front().volumeMm3, 6)) << "; ";
        }
        checks.check(clean == total, label, detail);
    };
    meshFree(*pairs, "Driving gear A turns gear B at -0.5 through the existing solver, with no interference at 4 engagement positions", -0.5);
    {
        // The check is meaningful: rotating B by half a tooth pitch makes the teeth collide.
        auto clash = std::make_shared<CadCommandService>();
        const auto r = clash->gearPairCommand(obj({ { "action", "create" }, { "parameters", json(R"({"module":2,"gearA":{"teeth":20},"gearB":{"teeth":40}})") } }));
        const auto b = r["gearB"].toString();
        checks.check(interference::check(clash->document(), nullptr, 1).interferenceFree(), "Pair without mechanism is interference free at its reference placement");
        const auto* f = clash->document().find(b);
        clash->setPlacement(b, f->x, f->y, f->z, 0, 0, f->rz + 180.0 / 40, error);
        const auto inspected = interference::check(clash->document(), nullptr, 2);
        const auto wrong = clash->gearPairCommand(obj({ { "action", "check" }, { "id", b } }));
        checks.check(inspected.completed && !inspected.interferences.empty() && !(bool) wrong["compatible"] && problemsOf(wrong).contains("tooth phase"),
                     "Mis-phasing gear B by half a pitch produces real interference and the pair check reports the phase", problemsOf(wrong));
    }
    {
        auto shifted = std::make_shared<CadCommandService>();
        const auto r = shifted->gearPairCommand(obj({ { "action", "create" }, { "parameters", json(R"({"module":2,"backlash":0.08,"gearA":{"teeth":12,"profileShift":0.5,"boreDiameter":6},"gearB":{"teeth":30},"lineOfCentersDegrees":35,"origin":[5,-3,2],"mechanism":"create"})") } }));
        checks.check((bool) r["ok"] && (bool) shifted->gearPairCommand(obj({ { "action", "check" }, { "id", r["gearA"] } }))["compatible"], "A profile-shifted 12:30 pair on a 35 degree line of centres is created compatible", r["error"].toString());
        meshFree(*shifted, "The profile-shifted pair meshes without interference at 4 engagement positions", -12.0 / 30.0);
    }

    // 6. Editing one paired gear reports the broken relationship; pair update restores it in one transaction.
    auto broken = pairs->gearCommand(gearRequest("update", gearA, json(R"({"teeth":25})")));
    checks.check((bool) broken["ok"] && !(bool) broken["pairing"]["compatible"] && broken["warning"].toString().contains("no longer meshes")
                 && problemsOf(broken["pairing"]).contains("centre distance") && problemsOf(broken["pairing"]).contains("ratio"),
                 "Changing one paired gear's teeth reports the centre distance and coupling ratio mismatch", problemsOf(broken["pairing"]));
    checks.check(closeTo(pairs->document().mechanicsData().couplings[0].ratio, -0.5, 1e-15), "The coupling is not silently changed by a single-gear edit");
    const auto revisionBeforeUpdate = pairs->document().revision();
    auto preview = pairs->gearPairCommand(obj({ { "action", "preview" }, { "id", gearB }, { "parameters", json(R"({"gearA":{"teeth":25}})") } }));
    checks.check((bool) preview["ok"] && (bool) preview["preview"] && pairs->document().revision() == revisionBeforeUpdate, "Pair preview evaluates on a draft without changing the document", preview["error"].toString());
    auto repaired = pairs->gearPairCommand(obj({ { "action", "update" }, { "id", gearB }, { "parameters", json(R"({"gearA":{"teeth":25}})") } }));
    status = pairs->gearPairCommand(obj({ { "action", "check" }, { "id", gearA } }));
    checks.check((bool) repaired["ok"] && (bool) status["compatible"] && closeTo(pairs->document().find(gearB)->x, 65, 1e-12) && closeTo(pairs->document().mechanicsData().couplings[0].ratio, -25.0 / 40.0, 1e-15),
                 "Pair update moves gear B to 65 mm, realigns its shaft joint and sets the coupling to -25/40", problemsOf(status) + repaired["error"].toString());
    meshFree(*pairs, "The updated 25:40 pair meshes without interference at 4 engagement positions", -25.0 / 40.0);
    const auto parameters = pairs->gearPairCommand(obj({ { "action", "get" }, { "id", gearA } }));
    checks.check((bool) parameters["ok"] && (int) (double) parameters["parameters"]["gearA"]["teeth"] == 25 && closeTo((double) parameters["parameters"]["backlash"], 0.1, 1e-12), "cad_gear_pair get returns the current pair definition", juce::JSON::toString(parameters["parameters"], true));
    pairs->undo(error);
    checks.check(!(bool) pairs->gearPairCommand(obj({ { "action", "check" }, { "id", gearA } }))["compatible"] && closeTo(pairs->document().find(gearB)->x, 60, 1e-12) && closeTo(pairs->document().mechanicsData().couplings[0].ratio, -0.5, 1e-15),
                 "One undo reverts the whole pair update (gear B, joint and coupling)");
    pairs->redo(error);
    checks.check((bool) pairs->gearPairCommand(obj({ { "action", "check" }, { "id", gearA } }))["compatible"], "Redo reapplies the pair update");
    for (const auto* ext : { "djo", "djc" })
    {
        CadCommandService back; const auto file = folder.getChildFile(juce::String("gear-pair.") + ext);
        const bool ok = pairs->save(file, error) && back.load(file, error);
        const auto reopened = back.gearPairCommand(obj({ { "action", "check" }, { "id", gearA } }));
        checks.check(ok && (bool) reopened["compatible"] && back.document().mechanicsData().couplings.size() == 1, juce::String("Save and reopen (.") + ext + ") preserves the pair, its joints and coupling", problemsOf(reopened) + error);
    }
    {
        auto undoAll = std::make_shared<CadCommandService>();
        undoAll->gearPairCommand(obj({ { "action", "create" }, { "parameters", json(R"({"mechanism":"create"})") } }));
        undoAll->undo(error);
        checks.check(undoAll->document().features().empty() && undoAll->document().mechanicsData().joints.empty() && undoAll->document().mechanicsData().couplings.empty(), "One undo removes a created pair with its instances, joints and coupling");
    }

    // 7. Associating existing joints, refusing misaligned joints, unpairing.
    {
        auto manual = std::make_shared<CadCommandService>();
        const auto r = manual->gearPairCommand(obj({ { "action", "create" }, { "parameters", json(R"({"module":1.5,"gearA":{"teeth":18},"gearB":{"teeth":27},"lineOfCentersDegrees":90})") } }));
        const auto a = r["gearA"].toString(), b = r["gearB"].toString();
        juce::String ia, ib, ja, jb, jbad;
        manual->createInstance(a, {}, ia, error); manual->createInstance(b, {}, ib, error);
        const auto shaft = [&](const juce::String& feature, const juce::String& inst, double offset, juce::String& id)
        { const auto* f = manual->document().find(feature); CadJointRequest q; q.type = "revolute"; q.childInstance = inst; q.originX = f->x + offset; q.originY = f->y; q.originZ = f->z; return manual->createJoint(q, id, error); };
        shaft(a, ia, 0, ja); shaft(b, ib, 0, jb);
        manual->setJointRole(jb, "dependent", 0, false, error);
        status = manual->gearPairCommand(obj({ { "action", "check" }, { "id", a } }));
        checks.check((bool) status["compatible"] && status["warnings"].size() > 0, "Joints without a coupling are reported as a warning, not as a meshing problem");
        const auto coupled = manual->gearPairCommand(obj({ { "action", "couple" }, { "id", a }, { "jointA", ja }, { "jointB", jb } }));
        checks.check((bool) coupled["ok"] && manual->document().mechanicsData().couplings.size() == 1 && closeTo(manual->document().mechanicsData().couplings[0].ratio, -18.0 / 27.0, 1e-15),
                     "couple associates existing revolute joints with the tooth-count ratio", coupled["error"].toString());
        juce::String ibad; manual->createInstance(b, "offset", ibad, error); shaft(b, ibad, 1.0, jbad);
        const auto refusedCouple = manual->gearPairCommand(obj({ { "action", "couple" }, { "id", a }, { "jointA", ja }, { "jointB", jbad } }));
        checks.check(!(bool) refusedCouple["ok"] && refusedCouple["error"].toString().contains("does not turn about"), "couple refuses a joint whose axis misses the gear axis", refusedCouple["error"].toString());
        const auto unpaired = manual->gearPairCommand(obj({ { "action", "unpair" }, { "id", b } }));
        checks.check((bool) unpaired["ok"] && !(bool) manual->gearPairCommand(obj({ { "action", "check" }, { "id", a } }))["paired"] && !(bool) manual->gearPairCommand(obj({ { "action", "check" }, { "id", b } }))["paired"], "unpair clears both pair links");
        auto modules = std::make_shared<CadCommandService>();
        const auto m = modules->gearPairCommand(obj({ { "action", "create" }, { "parameters", json("{}") } }));
        modules->gearCommand(gearRequest("update", m["gearB"].toString(), json(R"({"module":2.5})")));
        checks.check(problemsOf(modules->gearPairCommand(obj({ { "action", "check" }, { "id", m["gearA"] } }))).contains("Modules differ"), "Pair check reports different modules");
        modules->remove(m["gearB"].toString(), error);
        checks.check(problemsOf(modules->gearPairCommand(obj({ { "action", "check" }, { "id", m["gearA"] } }))).contains("no longer exists"), "Pair check reports a deleted partner");
    }

    // 8. Agent, FRust and node access use the same commands.
    const auto catalogue = juce::JSON::toString(DjehutiAgentPanel::toolCatalogue());
    checks.check(catalogue.contains("\"cad_gear\"") && catalogue.contains("\"cad_gear_pair\""), "The agent and local API catalogue lists cad_gear and cad_gear_pair");
    {
        auto host = std::make_shared<CadCommandService>();
        frust_engine::setCadCommandService(host);
        const auto args = juce::JSON::toString(obj({ { "action", "create" }, { "parameters", json(R"({"mechanism":"create","gearA":{"teeth":16},"gearB":{"teeth":32}})") } }));
        const auto reply = juce::JSON::parse(djehuti_cad_gear_pair(args.toRawUTF8()));
        const auto single = juce::JSON::parse(djehuti_cad_gear(juce::JSON::toString(gearRequest("get", reply["gearA"].toString(), {})).toRawUTF8()));
        checks.check((bool) reply["ok"] && (bool) single["ok"] && (int) (double) single["parameters"]["teeth"] == 16 && host->document().mechanicsData().couplings.size() == 1, "FRust host functions run the shared gear and gear pair commands");
        frust_engine::setCadCommandService({});
        for (const auto* type : { "cad_gear", "cad_gear_pair" })
        {
            const auto graph = std::string("{\"functionName\":\"gear_command\",\"params\":[{\"name\":\"args\",\"type\":\"String\"}],\"output\":\"r\",\"nodes\":[{\"id\":\"r\",\"type\":\"") + type + "\",\"inputs\":[{\"param\":\"args\"}]}]}";
            const auto node = node_compiler::CompileGraphToSource(graph);
            checks.check(node.ok && node.source.find(std::string("extern fn djehuti_") + type) != std::string::npos, juce::String(type) + " node compiles to its FRust host command", node.errorMessage);
        }
    }

    // 9. GUI: Create Gear and Create Gear Pair through the real task controls.
    {
        auto ui = std::make_shared<CadCommandService>();
        auto panel = std::make_unique<CadWorkspacePanel>(ui); panel->setSize(1400, 900);
        auto selection = std::make_shared<CadSelection>(); panel->setSelection(selection);
        const auto act = [&](const juce::var& a) { return panel->automation(a); };
        act(obj({ { "action", "command" }, { "command", "gear" } }));
        act(obj({ { "action", "set" }, { "field", "adv_teeth" }, { "value", 18.0 } }));
        act(obj({ { "action", "set" }, { "field", "adv_module" }, { "value", 1.25 } }));
        auto state = act(obj({ { "action", "click" }, { "button", "modelingPreview" } }));
        checks.check(ui->document().features().empty(), "Gear preview does not create a feature");
        state = act(obj({ { "action", "click" }, { "button", "apply" } }));
        juce::String single; for (const auto& f : ui->document().features()) if (f.type == "gear") single = f.id;
        checks.check(single.isNotEmpty() && (int) (double) ui->document().find(single)->modeling["teeth"] == 18 && closeTo((double) ui->document().find(single)->modeling["module"], 1.25, 1e-12), "Create Gear task builds the gear from the form", juce::JSON::toString(state).substring(0, 300));
        act(obj({ { "action", "select" }, { "id", single } }));
        act(obj({ { "action", "click" }, { "button", "modelingEdit" } }));
        act(obj({ { "action", "set" }, { "field", "adv_teeth" }, { "value", 22.0 } }));
        state = act(obj({ { "action", "click" }, { "button", "apply" } }));
        checks.check(tokenCount(*ui, single, "tip:") == 22, "Edit gear parameters regenerates the selected gear", juce::JSON::toString(state).substring(0, 300));
        act(obj({ { "action", "command" }, { "command", "gear" } }));
        act(obj({ { "action", "set" }, { "field", "adv_teeth" }, { "value", 4.0 } }));
        state = act(obj({ { "action", "state" } }));
        checks.check(!(bool) state["state"]["canApply"], "An invalid tooth count disables Apply in the gear task");
        act(obj({ { "action", "click" }, { "button", "cancel" } }));

        act(obj({ { "action", "command" }, { "command", "gearPair" } }));
        act(obj({ { "action", "set" }, { "field", "gpTeethA" }, { "value", 15.0 } }));
        act(obj({ { "action", "set" }, { "field", "gpTeethB" }, { "value", 45.0 } }));
        act(obj({ { "action", "set" }, { "field", "gpMechanism" }, { "value", true } }));
        const auto revision = ui->document().revision();
        act(obj({ { "action", "click" }, { "button", "gearPairPreview" } }));
        checks.check(ui->document().revision() == revision, "Gear Pair preview leaves the document unchanged");
        state = act(obj({ { "action", "click" }, { "button", "apply" } }));
        juce::String pairA; for (const auto& f : ui->document().features()) if (f.type == "gear" && f.modeling["pair"]["role"].toString() == "A") pairA = f.id;
        const auto pairStatus = ui->gearPairCommand(obj({ { "action", "check" }, { "id", pairA } }));
        checks.check(pairA.isNotEmpty() && (bool) pairStatus["compatible"] && ui->document().mechanicsData().couplings.size() == 1 && closeTo(ui->document().mechanicsData().couplings[0].ratio, -1.0 / 3.0, 1e-15),
                     "Create Gear Pair task builds a compatible 15:45 pair with joints and coupling", problemsOf(pairStatus) + juce::JSON::toString(state).substring(0, 300));
        act(obj({ { "action", "select" }, { "id", pairA } }));
        state = act(obj({ { "action", "state" } }));
        checks.check(juce::JSON::toString(state).contains("gearPairEdit"), "The gear inspector offers Edit gear pair for a paired gear");
        act(obj({ { "action", "click" }, { "button", "gearPairEdit" } }));
        act(obj({ { "action", "set" }, { "field", "gpTeethB" }, { "value", 30.0 } }));
        state = act(obj({ { "action", "click" }, { "button", "apply" } }));
        const auto edited2 = ui->gearPairCommand(obj({ { "action", "check" }, { "id", pairA } }));
        checks.check((bool) edited2["compatible"] && closeTo((double) edited2["expected"]["centreDistance"], 2.0 * 45 / 2, 1e-12) && closeTo(ui->document().mechanicsData().couplings[0].ratio, -0.5, 1e-15),
                     "Edit Gear Pair updates both gears, the placement and the coupling", problemsOf(edited2) + juce::JSON::toString(state).substring(0, 300));
        ui->save(folder.getChildFile("gui-gear-pair.djo"), error);
    }
    return checks.finish(report);
}
}
