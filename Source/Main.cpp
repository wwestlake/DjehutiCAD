// Advanced Sketch editing and focused validation.
#include <JuceHeader.h>

#include "DjehutiCAD.h"
#include "FrustExecution.h"
#include "CadDocument.h"
#include "CadEngineeringUnits.h"
#include "CadGeometricProperties.h"
#include "CadComponentBrowser.h"
#include "CadCraftStock.h"
#include "CadCraftBrowser.h"
#include "CadInterference.h"
#include "CadMotionRuntime.h"
#include "CadIkCommands.h"
#include "Viewport3D.h"
#include "CadMaterialsManager.h"
#include "CadFixturesPanel.h"
bool runAdvancedSketchCheck(juce::var&);
bool runSketchToolboxCheck(juce::var&);
bool runSketchUsabilityCheck(juce::var&);
bool runCadWorkflowUiCheck(juce::var&);
bool runSketchLineRepairCheck(juce::var&);
namespace djehuti::cad { bool runCadStockCheck(juce::var&); bool runCadRevolveCheck(juce::var&); bool runCadLatheCheck(juce::var&); bool runCadChamferCheck(juce::var&); bool runCadModelingCheck(juce::var&); bool runCadSurfaceCheck(juce::var&); bool runCadReplicationCheck(juce::var&); bool runCadHoleCheck(juce::var&); bool runCadRevolveUiCheck(juce::var&); }

#include <BRepBndLib.hxx>
#include <Poly_Triangulation.hxx>
#include <RWStl.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include "lib3mf_implicit.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Mat.hxx>
#include <gp_Pln.hxx>
#include <cstdlib>
#include <tuple>

namespace
{
// Opt-in checks of this feature through the real document/runtime/command path.
// Uses the existing app target; it does not build a separate JUCE test runner.
// Focused service check for face-attached sketches: reference resolution, dependent rebuild after a parameter
// edit, undo/redo, native save/reopen, a non-horizontal face, and refusal when the support face disappears.
// Focused manufacturing-export check. A 60 x 40 x 10 mm plate with a 12 mm through-hole and an 8 mm cylindrical
// boss is exported as 3MF, binary STL and STEP and read back with lib3mf, OCCT RWStl and OCCT STEPCAFControl;
// then a two-instance assembly with a second body, a cancellation and an output failure. Files stay in
// build/export-check for inspection.
// Focused rebuild check: a chain of dependent unions plus an independent body, compared with a direct OCCT fuse
// computed outside the document; upstream edit, undo/redo, save/reopen, and mutation timings on ~50 features.
// Focused interference check: separated, overlapping, face-contact and contained boxes; a single body built from
// overlapping features; and a jointed assembly whose interference state follows the shown (preview) pose.
bool runCadInterferenceCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto item = new juce::DynamicObject(); item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        return ok;
    };
    const auto summary = [](const CadInterferenceReport& r)
    {
        juce::String s = juce::String(r.components) + " components, " + juce::String((int) r.interferences.size()) + " interference(s)";
        for (const auto& p : r.interferences) s << "; " << p.firstName << "/" << p.secondName << " " << juce::String(p.volumeMm3, 3) << " mm3 " << p.detail;
        if (r.error.isNotEmpty()) s << "; error " << r.error;
        return s;
    };
    // Two 10 mm boxes, the second at x offset dx (plus an optional third box).
    const auto twoBoxes = [&](double dx, CadCommandService& svc)
    {
        juce::String e, a, b;
        return svc.createBox(10, 10, 10, a, e) && svc.createBox(10, 10, 10, b, e) && svc.setPlacement(b, dx, 0, 0, 0, 0, 0, e);
    };
    {
        CadCommandService svc; twoBoxes(20, svc);
        const auto r = interference::check(svc.document(), nullptr, 0);
        record(r.completed && r.components == 2 && r.interferences.empty() && r.candidatePairs == 0, "separated boxes: no interference (rejected by bounding boxes)", summary(r));
    }
    {
        CadCommandService svc; twoBoxes(5, svc);
        const auto r = interference::check(svc.document(), nullptr, 0);
        record(r.completed && r.interferences.size() == 1 && std::abs(r.interferences[0].volumeMm3 - 500.0) < 1.0e-6 && !r.interferences[0].regionEdges.empty(),
               "overlapping boxes: one pair, common volume 500 mm3, region outlined", summary(r));
    }
    {
        CadCommandService svc; twoBoxes(10, svc);
        const auto r = interference::check(svc.document(), nullptr, 0);
        record(r.completed && r.candidatePairs == 1 && r.interferences.empty(), "boxes sharing a face: contact, not interference", summary(r));
    }
    {
        CadCommandService svc; juce::String e, big, small;
        svc.createBox(20, 20, 20, big, e); svc.createBox(4, 4, 4, small, e); svc.setPlacement(small, 8, 8, 8, 0, 0, 0, e);
        const auto r = interference::check(svc.document(), nullptr, 0);
        record(r.completed && r.interferences.size() == 1 && std::abs(r.interferences[0].volumeMm3 - 64.0) < 1.0e-6 && r.interferences[0].detail.contains("entirely inside"),
               "contained box: interference of its whole volume, reported as contained", summary(r));
    }
    {
        CadCommandService svc; juce::String e, a, b, u;
        twoBoxes(5, svc);
        svc.booleanOp("union", "box-1", "box-2", u, e);
        const auto r = interference::check(svc.document(), nullptr, 0);
        record(r.completed && r.components == 1 && r.pairs == 0 && r.interferences.empty(), "overlapping features inside one finished body are not compared", summary(r));
    }
    {
        // Assembly: grounded block and a slider 5 mm away on a prismatic X joint. Moving it 10 mm makes them overlap.
        auto svc = std::make_shared<CadCommandService>();
        juce::String e, block, slider, groundInst, sliderInst, joint;
        bool built = svc->createBox(20, 20, 20, block, e) && svc->createBox(10, 10, 10, slider, e) && svc->setPlacement(slider, 25, 5, 5, 0, 0, 0, e)
                     && svc->createInstance(block, "Block", groundInst, e) && svc->createInstance(slider, "Slider", sliderInst, e) && svc->setInstanceGrounded(groundInst, true, e);
        CadJointRequest request; request.name = "Slide"; request.type = "prismatic"; request.parentInstance = groundInst; request.childInstance = sliderInst;
        request.originX = 25; request.originY = 10; request.originZ = 10; request.axisX = -1; request.axisY = 0; request.axisZ = 0;
        built = built && svc->createJoint(request, joint, e);
        if (!record(built, "assembly: grounded block, slider 5 mm away on a prismatic joint", e)) return false;
        CadMotionRuntime runtime(svc);
        runtime.synchronize();
        const auto revision = svc->document().revision();
        const bool modified = svc->document().modified();
        const auto atRest = interference::check(svc->document(), &runtime.shownInstanceWorld(), runtime.pose()->serial);
        record(atRest.completed && atRest.components == 2 && atRest.interferences.empty(), "reference pose: no interference", summary(atRest));
        const bool moved = runtime.setPreviewCoordinate(joint, 0.010, e);
        const auto shown = interference::check(svc->document(), &runtime.shownInstanceWorld(), runtime.pose()->serial);
        record(moved && shown.completed && shown.interferences.size() == 1 && std::abs(shown.interferences[0].volumeMm3 - 500.0) < 1.0e-6 && shown.poseSerial != atRest.poseSerial,
               "preview 10 mm into the block: 500 mm3 interference at the shown pose", summary(shown));
        const auto saved = interference::check(svc->document(), nullptr, 0);
        record(saved.completed && saved.interferences.empty(), "the saved reference placements are still interference-free (preview not committed)", summary(saved));
        const bool contact = runtime.setPreviewCoordinate(joint, 0.005, e);
        const auto touching = interference::check(svc->document(), &runtime.shownInstanceWorld(), runtime.pose()->serial);
        record(contact && touching.completed && touching.interferences.empty(), "preview 5 mm: faces touch after a joint transform, still contact only", summary(touching));
        record(svc->document().revision() == revision && svc->document().modified() == modified && svc->document().mechanicsData().joints.size() == 1,
               "inspection did not change the document, joints or revision");
    }
    return passed;
}

// Focused geometric-property check: an analytic box, a translated and rotated assembly instance (centroid
// c' = R c + t, tensor I' = R I R^T), a rotated body placement, and explicit refusal of non-solid geometry.
bool runCadGeometryCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto item = new juce::DynamicObject(); item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        return ok;
    };
    using Tensor = std::array<std::array<double, 3>, 3>;
    const auto close = [](double a, double b, double scale) { return std::abs(a - b) <= 1.0e-9 * juce::jmax(1.0, scale); };
    const auto tensorError = [](const Tensor& a, const Tensor& b) { double e = 0.0; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) e = juce::jmax(e, std::abs(a[(size_t) i][(size_t) j] - b[(size_t) i][(size_t) j])); return e; };
    const auto describe = [](const CadGeometricProperties& p)
    {
        if (!p.ok()) return p.status + ": " + p.message;
        juce::String s = "V " + juce::String(p.volumeMm3, 6) + " mm3, c (" + juce::String(p.centroidMm.X(), 6) + ", " + juce::String(p.centroidMm.Y(), 6) + ", " + juce::String(p.centroidMm.Z(), 6) + ") mm, I [";
        for (const auto& r : p.inertiaMm5) s << " (" << juce::String(r[0], 3) << " " << juce::String(r[1], 3) << " " << juce::String(r[2], 3) << ")";
        return s + " ] mm5";
    };
    // Box a x b x c (mm): V = abc, central tensor diag(V(b2+c2)/12, V(a2+c2)/12, V(a2+b2)/12).
    const double a = 40.0, b = 20.0, c = 10.0, V = a * b * c;
    const Tensor boxTensor { { { V * (b * b + c * c) / 12.0, 0.0, 0.0 }, { 0.0, V * (a * a + c * c) / 12.0, 0.0 }, { 0.0, 0.0, V * (a * a + b * b) / 12.0 } } };
    const double scale = boxTensor[2][2];

    // 1. Rectangular solid placed with its corner at (5, -3, 2).
    {
        CadCommandService svc; juce::String e, box;
        const bool built = svc.createBox(a, b, c, box, e) && svc.setPlacement(box, 5, -3, 2, 0, 0, 0, e);
        const auto p = geomprops::ofFeature(svc.document(), box);
        record(built && p.ok() && p.frame == "world" && p.solids == 1 && close(p.volumeMm3, V, V)
                   && close(p.centroidMm.X(), 25.0, 1.0) && close(p.centroidMm.Y(), 7.0, 1.0) && close(p.centroidMm.Z(), 7.0, 1.0)
                   && tensorError(p.inertiaMm5, boxTensor) <= 1.0e-9 * scale,
               "box 40 x 20 x 10 at (5,-3,2): V 8000 mm3, centroid (25,7,7) mm, central tensor diag(333333.33, 1133333.33, 1333333.33) mm5", describe(p) + (built ? "" : " build: " + e));
        record(p.ok() && close(p.principalMomentsMm5[0], boxTensor[0][0], scale) && close(p.principalMomentsMm5[1], boxTensor[1][1], scale) && close(p.principalMomentsMm5[2], boxTensor[2][2], scale)
                   && std::abs(std::abs(p.principalAxes[0][0]) - 1.0) < 1.0e-9 && std::abs(std::abs(p.principalAxes[2][2]) - 1.0) < 1.0e-9,
               "box principal moments ascending along X, Y, Z", juce::String(p.principalMomentsMm5[0], 3) + ", " + juce::String(p.principalMomentsMm5[1], 3) + ", " + juce::String(p.principalMomentsMm5[2], 3));
    }
    // 2. Assembly instance of a box modelled at the origin, placed at (100, 50, 20) rotated 20, -35, 30 deg (extrinsic XYZ).
    {
        CadCommandService svc; juce::String e, box, inst;
        bool built = svc.createBox(a, b, c, box, e) && svc.createInstance(box, "Moved box", inst, e);
        const auto placement = mech::frameFromEulerDegrees(100, 50, 20, 20, -35, 30);
        built = built && svc.setInstancePlacement(inst, placement, e);
        const auto revision = svc.document().revision(); const bool modified = svc.document().modified();
        const auto p = geomprops::ofInstance(svc.document(), inst);
        const auto trsf = placement.toTrsf();
        const auto expectedCentroid = gp_Pnt(a / 2, b / 2, c / 2).Transformed(trsf);
        const gp_Mat R = trsf.VectorialPart();
        Tensor expected {};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { double s = 0.0; for (int k = 0; k < 3; ++k) s += R.Value(i + 1, k + 1) * boxTensor[(size_t) k][(size_t) k] * R.Value(j + 1, k + 1); expected[(size_t) i][(size_t) j] = s; }
        double offDiagonal = 0.0; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) if (i != j) offDiagonal = juce::jmax(offDiagonal, std::abs(p.inertiaMm5[(size_t) i][(size_t) j]));
        record(built && p.ok() && close(p.volumeMm3, V, V) && expectedCentroid.Distance(p.centroidMm) < 1.0e-9 * 100.0,
               "rotated, translated instance: volume unchanged, centroid = R c + t", describe(p) + " expected c (" + juce::String(expectedCentroid.X(), 6) + ", " + juce::String(expectedCentroid.Y(), 6) + ", " + juce::String(expectedCentroid.Z(), 6) + ")" + (built ? "" : " build: " + e));
        record(p.ok() && tensorError(p.inertiaMm5, expected) <= 1.0e-9 * scale && offDiagonal > 1.0e3,
               "rotated instance tensor = R I R^T (with non-zero products of inertia)", "max error " + juce::String(tensorError(p.inertiaMm5, expected), 9) + " mm5, largest product " + juce::String(offDiagonal, 3) + " mm5");
        record(p.ok() && close(p.principalMomentsMm5[0], boxTensor[0][0], scale) && close(p.principalMomentsMm5[1], boxTensor[1][1], scale) && close(p.principalMomentsMm5[2], boxTensor[2][2], scale),
               "principal moments are invariant under the rotation", juce::String(p.principalMomentsMm5[0], 3) + ", " + juce::String(p.principalMomentsMm5[1], 3) + ", " + juce::String(p.principalMomentsMm5[2], 3));
        const auto direct = geomprops::ofShape(svc.document().find(box)->shape, trsf);
        record(direct.ok() && direct.centroidMm.Distance(p.centroidMm) < 1.0e-9 && tensorError(direct.inertiaMm5, p.inertiaMm5) <= 1.0e-9 * scale,
               "ofShape with the same rigid transform matches ofInstance", describe(direct));
        std::map<juce::String, gp_Trsf> pose; gp_Trsf shifted = trsf; shifted.SetTranslationPart(gp_Vec(0, 0, 500)); pose[inst] = shifted;
        const auto atPose = geomprops::ofInstance(svc.document(), inst, &pose);
        record(atPose.ok() && atPose.centroidMm.Distance(gp_Pnt(a / 2, b / 2, c / 2).Transformed(shifted)) < 1.0e-9 * 500.0 && tensorError(atPose.inertiaMm5, p.inertiaMm5) <= 1.0e-9 * scale,
               "a supplied pose moves the centroid only (same rotation, same tensor)", describe(atPose));
        record(svc.document().revision() == revision && svc.document().modified() == modified, "property queries did not change the document");
    }
    // A body placement rotated 90 deg about Z swaps the X and Y moments.
    {
        CadCommandService svc; juce::String e, box;
        const bool built = svc.createBox(a, b, c, box, e) && svc.setPlacement(box, 10, 0, 0, 0, 0, 90, e);
        const auto p = geomprops::ofFeature(svc.document(), box);
        Bnd_Box bounds; BRepBndLib::Add(svc.document().find(box)->shape, bounds); double x0, y0, z0, x1, y1, z1; bounds.Get(x0, y0, z0, x1, y1, z1);
        const gp_Pnt middle((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
        const Tensor swapped { { { boxTensor[1][1], 0.0, 0.0 }, { 0.0, boxTensor[0][0], 0.0 }, { 0.0, 0.0, boxTensor[2][2] } } };
        record(built && p.ok() && close(p.volumeMm3, V, V) && p.centroidMm.Distance(middle) < 1.0e-6 && tensorError(p.inertiaMm5, swapped) <= 1.0e-9 * scale,
               "body placed with Rotate Z 90: I_xx and I_yy swap, centroid at the bounding-box centre", describe(p) + (built ? "" : " build: " + e));
    }
    // 3. Unsupported or invalid geometry fails explicitly.
    {
        CadCommandService svc; juce::String e, sketch, box, inst;
        svc.createSketch("XY", 0, sketch, e); svc.createBox(10, 10, 10, box, e); svc.createInstance(box, "Box", inst, e);
        const auto ofSketch = geomprops::ofFeature(svc.document(), sketch);
        record(!ofSketch.ok() && ofSketch.status == "nullShape" && ofSketch.volumeMm3 == 0.0, "sketch feature (no solid): refused", describe(ofSketch));
        const auto unknown = geomprops::ofFeature(svc.document(), "box-999");
        const auto unknownInstance = geomprops::ofInstance(svc.document(), "inst-999");
        record(unknown.status == "unknownFeature" && unknownInstance.status == "unknownInstance", "unknown feature and instance ids: refused", describe(unknown) + " | " + describe(unknownInstance));
        record(geomprops::ofShape(TopoDS_Shape()).status == "nullShape", "null shape: refused");
        const auto face = geomprops::ofShape(BRepBuilderAPI_MakeFace(gp_Pln(), 0, 10, 0, 10).Face());
        record(face.status == "notSolid", "planar face: refused as not solid", describe(face));
        const auto shell = geomprops::ofShape(BRepPrimAPI_MakeBox(10, 10, 10).Shell());
        record(shell.status == "notSolid", "closed shell without a solid: refused as not solid", describe(shell));
        TopoDS_Compound mixed; BRep_Builder builder; builder.MakeCompound(mixed);
        builder.Add(mixed, BRepPrimAPI_MakeBox(10, 10, 10).Solid()); builder.Add(mixed, BRepBuilderAPI_MakeFace(gp_Pln(), 20, 30, 0, 10).Face());
        const auto mix = geomprops::ofShape(mixed);
        record(mix.status == "notSolid", "solid plus a loose face: refused instead of integrating part of it", describe(mix));
        TopoDS_Compound twoSolids; builder.MakeCompound(twoSolids);
        builder.Add(twoSolids, BRepPrimAPI_MakeBox(10, 10, 10).Solid()); builder.Add(twoSolids, BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 10, 10, 10).Solid());
        const auto both = geomprops::ofShape(twoSolids);
        record(both.ok() && both.solids == 2 && close(both.volumeMm3, 2000.0, 2000.0) && close(both.centroidMm.X(), 15.0, 1.0), "compound of two solids: supported, combined", describe(both));
        gp_Trsf scaled; scaled.SetScale(gp_Pnt(), 2.0);
        gp_Trsf mirrored; mirrored.SetMirror(gp_Ax2(gp_Pnt(), gp_Dir(1, 0, 0)));
        const auto solid = svc.document().find(box)->shape;
        record(geomprops::ofShape(solid, scaled).status == "nonRigidTransform" && geomprops::ofShape(solid, mirrored).status == "nonRigidTransform",
               "scaling and mirroring transforms: refused");
    }
    return passed;
}

// Focused physical-material check through the shared command service (the same entry point as the agent and
// local API tools): library create/read/edit/delete, part assignment, undo/redo, native save/reopen, documents
// without material data, and refusal of invalid values and broken references. The two materials are
// illustrative test values, not a materials database. Files stay in build/material-check.
bool runCadMaterialCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto item = new juce::DynamicObject(); item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-material-check.result.json").replaceWithText(juce::JSON::toString(report, false));   // progress survives a crash
        return ok;
    };
    const auto args = [](const juce::String& json) { return juce::JSON::parse(json); };
    const auto text = [](const juce::var& v) { return juce::JSON::toString(v, true); };
    const auto prop = [](const juce::var& material, const char* key) { return material.getProperty("properties", {}).getProperty(key, {}); };
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/material-check");
    folder.createDirectory();

    CadCommandService svc; juce::String e, box, sketch;
    svc.createBox(40, 20, 10, box, e); svc.createSketch("XY", 0, sketch, e);
    const auto mechanicsBefore = text(mech::toJson(svc.document().mechanicsData()));

    // 1. Create and retrieve.
    auto steel = svc.materialCommand("cad_material_create", args(R"({"name":"Illustrative steel - test only","densityKgPerM3":7850,"youngsModulusMPa":200000,"poissonsRatio":0.3,"yieldStrengthMPa":250,"note":"validation value"})"));
    auto pla = svc.materialCommand("cad_material_create", args(R"({"name":"Illustrative PLA - test only","densityKgPerM3":1240,"youngsModulusMPa":3500,"poissonsRatio":0.36})"));
    const auto steelId = steel.getProperty("id", "").toString(), plaId = pla.getProperty("id", "").toString();
    const auto gotSteel = svc.materialCommand("cad_material_list", args(R"({"material":")" + steelId + R"("})")).getProperty("material", {});
    record((bool) steel.getProperty("ok", false) && steelId == "mat-1" && plaId == "mat-2" && (double) prop(gotSteel, "density").getProperty("value", 0) == 7850.0
               && prop(gotSteel, "density").getProperty("unit", "").toString() == "kg/m^3" && prop(gotSteel, "youngsModulus").getProperty("unit", "").toString() == "MPa"
               && (double) prop(gotSteel, "poissonsRatio").getProperty("value", 0) == 0.3 && (int) gotSteel.getProperty("revision", 0) == 1,
           "create and retrieve: stable ids mat-1, mat-2; values with explicit units", text(gotSteel));
    const auto gotPla = svc.materialCommand("cad_material_list", args(R"({"material":"mat-2"})")).getProperty("material", {});
    record(gotPla.getProperty("properties", {}).hasProperty("density") && !gotPla.getProperty("properties", {}).hasProperty("yieldStrength"),
           "unspecified yield strength is absent, not zero", text(gotPla));

    // 2. Edit without changing identity.
    auto edited = svc.materialCommand("cad_material_update", args(R"({"material":"mat-1","name":"Illustrative steel S - test only","youngsModulusMPa":210000,"clear":["yieldStrength"]})"));
    const auto afterEdit = edited.getProperty("material", {});
    record((bool) edited.getProperty("ok", false) && afterEdit.getProperty("id", "").toString() == "mat-1" && (int) afterEdit.getProperty("revision", 0) == 2
               && (double) prop(afterEdit, "youngsModulus").getProperty("value", 0) == 210000.0 && (double) prop(afterEdit, "density").getProperty("value", 0) == 7850.0
               && !afterEdit.getProperty("properties", {}).hasProperty("yieldStrength") && svc.document().materialsData().materials.size() == 2,
           "edit keeps id mat-1, revision 2, untouched fields kept, cleared field unspecified", text(afterEdit));

    // 3. Assign and clear; geometry untouched.
    const auto shapeBefore = svc.document().find(box)->shape;
    const auto volumeBefore = geomprops::ofFeature(svc.document(), box).volumeMm3;
    auto assigned = svc.materialCommand("cad_material_assign", args(R"({"feature":")" + box + R"(","material":"mat-1"})"));
    const auto lookup = svc.materialCommand("cad_material_list", args(R"({"feature":")" + box + R"("})")).getProperty("assignment", {});
    record((bool) assigned.getProperty("ok", false) && lookup.getProperty("material", "").toString() == "mat-1" && lookup.getProperty("featureName", "").toString() == "Box 1",
           "assign mat-1 to Box 1 and retrieve it", text(lookup));
    record(svc.document().find(box)->shape.IsSame(shapeBefore) && std::abs(geomprops::ofFeature(svc.document(), box).volumeMm3 - volumeBefore) < 1.0e-9,
           "assignment did not change the part's geometry (same shape, same volume)");
    svc.materialCommand("cad_material_assign", args(R"({"feature":")" + box + R"(","material":"mat-2"})"));
    record(svc.document().materialsData().assignments.size() == 1 && svc.document().materialsData().assignmentFor(box)->materialId == "mat-2", "re-assigning replaces the material (one per part)");
    auto cleared = svc.materialCommand("cad_material_clear", args(R"({"feature":")" + box + R"("})"));
    const auto none = svc.materialCommand("cad_material_list", args(R"({"feature":")" + box + R"("})"));
    record((bool) cleared.getProperty("ok", false) && none.hasProperty("assignment") && none.getProperty("assignment", {}).isVoid(), "clear: the part has no material (null)", text(none));

    // 4. Undo / redo.
    juce::String ue;
    svc.undo(ue);
    const bool undoClear = svc.document().materialsData().assignmentFor(box) != nullptr && svc.document().materialsData().assignmentFor(box)->materialId == "mat-2";
    svc.undo(ue); svc.undo(ue);
    const bool undoAssign = svc.document().materialsData().assignments.empty();
    svc.undo(ue);
    const auto* reverted = svc.document().materialsData().find("mat-1");
    const bool undoEdit = reverted != nullptr && reverted->revision == 1 && reverted->youngsModulusMPa == 200000.0 && reverted->yieldStrengthMPa == 250.0;
    svc.redo(ue); svc.redo(ue); svc.redo(ue);
    const bool redone = svc.document().materialsData().find("mat-1")->revision == 2 && svc.document().materialsData().assignmentFor(box) != nullptr && svc.document().materialsData().assignmentFor(box)->materialId == "mat-2";
    record(undoClear && undoAssign && undoEdit && redone, "undo restores clear, both assignments and the edit; redo re-applies them",
           juce::String("undoClear ") + (undoClear ? "y" : "n") + " undoAssign " + (undoAssign ? "y" : "n") + " undoEdit " + (undoEdit ? "y" : "n") + " redo " + (redone ? "y" : "n"));

    // 5. Native save and reopen.
    const auto file = folder.getChildFile("materials.djo");
    const auto libraryBefore = text(matl::toJson(svc.document().materialsData()));
    const bool saved = svc.save(file, e);
    CadCommandService reopened; const bool loaded = reopened.load(file, e);
    const auto libraryAfter = text(matl::toJson(reopened.document().materialsData()));
    record(saved && loaded && libraryAfter == libraryBefore && reopened.document().materialsData().assignmentFor(box) != nullptr,
           "save and reopen keep definitions, revisions, assignments and numbering", libraryAfter + (loaded ? "" : " load: " + e));
    juce::String third; reopened.createMaterial({ {}, "Third (test only)", 1, "isotropic", 1000.0, 1000.0, 0.25, {}, {} }, third, e);
    record(third == "mat-3", "after reopen, a new material gets the next unused id", third);
    record(text(mech::toJson(svc.document().mechanicsData())) == mechanicsBefore && text(mech::toJson(reopened.document().mechanicsData())) == mechanicsBefore,
           "mechanics serialization unchanged by material edits, save and reopen");

    // 6. Documents without material data.
    {
        CadCommandService plain; juce::String pe, pbox;
        plain.createBox(10, 10, 10, pbox, pe);
        const auto plainFile = folder.getChildFile("no-materials.djo"), plainJson = folder.getChildFile("no-materials.json");
        const bool ok = plain.save(plainFile, pe) && plain.save(plainJson, pe);
        CadCommandService back; const bool open = back.load(plainFile, pe);
        record(ok && open && back.document().materialsData().empty() && !plainJson.loadFileAsString().contains("\"materials\""),
               "a document without materials saves no material data and reopens with an empty library", pe);
        const auto demo = juce::File("D:/000 Electronics Lab Development/CAD Demonstrations/Articulated Robot Arm - Assemblies and Joints.djo");
        if (demo.existsAsFile())
        {
            CadCommandService old; juce::String oe;
            const bool opened = old.load(demo, oe);
            record(opened && old.document().materialsData().empty() && old.document().mechanicsData().joints.size() == 4 && old.document().mechanicsData().instances.size() == 5,
                   "an existing assembly document (saved before materials existed) opens unchanged: 5 instances, 4 joints, no materials", oe);
        }
        else record(true, "existing assembly document not present on this machine; skipped");
    }

    // 7. Rejections.
    const auto refused = [&](const juce::String& tool, const juce::String& json, const juce::String& expect)
    {
        const auto r = svc.materialCommand(tool, args(json));
        return !(bool) r.getProperty("ok", true) && r.getProperty("error", "").toString().containsIgnoreCase(expect) ? juce::String() : text(r);
    };
    const auto count = svc.document().materialsData().materials.size();
    juce::StringArray failures;
    for (const auto& [tool, json, expect] : std::vector<std::tuple<juce::String, juce::String, juce::String>> {
             { "cad_material_create", R"({"name":"Bad","densityKgPerM3":-1,"youngsModulusMPa":1,"poissonsRatio":0.3})", "density" },
             { "cad_material_create", R"({"name":"Bad","densityKgPerM3":0,"youngsModulusMPa":1,"poissonsRatio":0.3})", "density" },
             { "cad_material_create", R"({"name":"Bad","densityKgPerM3":1,"youngsModulusMPa":1,"poissonsRatio":0.5})", "Poisson" },
             { "cad_material_create", R"({"name":"Bad","densityKgPerM3":1,"poissonsRatio":0.3})", "Young" },
             { "cad_material_create", R"({"name":"","densityKgPerM3":1,"youngsModulusMPa":1,"poissonsRatio":0.3})", "name" },
             { "cad_material_create", R"({"name":"Bad","densityKgPerM3":"heavy","youngsModulusMPa":1,"poissonsRatio":0.3})", "number" },
             { "cad_material_create", R"({"name":"Bad","model":"orthotropic","densityKgPerM3":1,"youngsModulusMPa":1,"poissonsRatio":0.3})", "model" },
             { "cad_material_create", R"({"name":"Bad","densityKgPerM3":1,"youngsModulusMPa":1,"poissonsRatio":0.3,"yieldStrengthMPa":0})", "yield" },
             { "cad_material_update", R"({"material":"mat-1","clear":["density"]})", "density" },
             { "cad_material_update", R"({"material":"mat-99","name":"x"})", "Unknown material" },
             { "cad_material_assign", R"({"feature":"box-99","material":"mat-1"})", "Unknown part" },
             { "cad_material_assign", R"({"feature":")" + sketch + R"(","material":"mat-1"})", "sketch" },
             { "cad_material_assign", R"({"feature":")" + box + R"(","material":"mat-99"})", "Unknown material" },
             { "cad_material_delete", R"({"material":"mat-2"})", "assigned" } })
        if (const auto f = refused(tool, json, expect); f.isNotEmpty()) failures.add(tool + " " + json + " -> " + f);
    record(failures.isEmpty() && svc.document().materialsData().materials.size() == count && svc.document().materialsData().assignmentFor(box)->materialId == "mat-2",
           "invalid values, unknown model, broken references and deleting a used material are refused without changes", failures.joinIntoString(" | "));
    {
        CadMaterials bad; juce::String be;
        const bool wrongUnit = !matl::fromJson(args(R"({"schemaVersion":1,"nextNumber":2,"materials":[{"id":"mat-1","name":"X","revision":1,"model":"isotropic","properties":{"density":{"value":7.85,"unit":"g/cm^3"},"youngsModulus":{"value":1,"unit":"MPa"},"poissonsRatio":{"value":0.3,"unit":"1"}}}],"assignments":[]})"), bad, be) && be.contains("kg/m^3");
        const bool newer = !matl::fromJson(args(R"({"schemaVersion":3,"materials":[],"assignments":[]})"), bad, be) && be.contains("schema");
        const bool dangling = !matl::fromJson(args(R"({"schemaVersion":1,"nextNumber":1,"materials":[],"assignments":[{"feature":"box-1","material":"mat-7"}]})"), bad, be) && be.contains("missing material");
        record(wrongUnit && newer && dangling, "stored data with a wrong unit, a newer schema or a dangling assignment is refused");
    }
    auto removed = svc.materialCommand("cad_material_delete", args(R"({"material":"mat-2","clearAssignments":true})"));
    record((bool) removed.getProperty("ok", false) && svc.document().materialsData().find("mat-2") == nullptr && svc.document().materialsData().assignments.empty(),
           "delete with clearAssignments removes the material and its assignments", text(removed));
    svc.materialCommand("cad_material_assign", args(R"({"feature":")" + box + R"(","material":"mat-1"})"));
    const bool partDeleted = svc.remove(box, e);
    record(partDeleted && svc.document().materialsData().assignments.empty() && svc.document().materialsData().find("mat-1") != nullptr,
           "deleting a part removes its assignment and keeps the material", e);
    svc.undo(ue);
    record(svc.document().find(box) != nullptr && svc.document().materialsData().assignmentFor(box) != nullptr, "undoing the part deletion restores the part and its assignment");
    return passed;
}

// Focused analysis-case check: unit conversions; case create/edit through the shared command service with mixed
// input units; stored SI values; reference validation (refused on create, reported after a geometry or assembly
// change, never re-attached); undo/redo; native save/reopen; documents without analysis data. No solving.
bool runCadAnalysisCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto item = new juce::DynamicObject(); item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-analysis-check.result.json").replaceWithText(juce::JSON::toString(report, false));
        return ok;
    };
    const auto args = [](const juce::String& json) { return juce::JSON::parse(json); };
    const auto text = [](const juce::var& v) { return juce::JSON::toString(v, true); };
    const auto approx = [](double a, double b) { return std::abs(a - b) <= 1.0e-12 * juce::jmax(1.0, std::abs(b)); };
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/analysis-check");
    folder.createDirectory();

    // 1. Units.
    {
        juce::String e; double v = 0.0; juce::StringArray bad;
        const auto si = [&](const char* q, double x, const char* u, double expect) { if (!units::toSI(q, x, u, v, e) || !approx(v, expect)) bad.add(juce::String(q) + " " + juce::String(x) + " " + u + " -> " + juce::String(v, 12) + " " + e); };
        si("force", 2.5, "kN", 2500.0); si("force", 1.0, "lbf", 4.4482216152605); si("moment", 1500.0, "N*mm", 1.5); si("moment", 2.0, "kN*m", 2000.0);
        si("length", 250.0, "mm", 0.25); si("length", 1.0, "in", 0.0254); si("mass", 1.5, "t", 1500.0); si("mass", 500.0, "g", 0.5);
        si("stress", 210000.0, "MPa", 2.1e11); si("stress", 1.0, "N/mm^2", 1.0e6); si("density", 7.85, "g/cm^3", 7850.0);
        si("acceleration", -1.0, "g0", -9.80665); si("area", 100.0, "mm^2", 1.0e-4); si("secondMomentOfArea", 1.0e6, "mm^4", 1.0e-6); si("volume", 1.0e9, "mm^3", 1.0);
        double back = 0.0; units::fromSI("moment", 1.5, "N*mm", back, e); if (!approx(back, 1500.0)) bad.add("fromSI N*mm");
        if (!approx(units::massKg(7850.0, 1.0e6), 7.85) || !approx(units::mm5ToM5(1.0e15), 1.0) || !approx(units::mpaToPa(200000.0), 2.0e11) || !approx(units::nmmToNm(1000.0), 1.0)) bad.add("fixed helpers");
        record(bad.isEmpty(), "unit conversions to and from SI (force, moment, length, mass, stress, density, acceleration, area, I, volume)", bad.joinIntoString(" | "));
        juce::String e1, e2; double x = 0.0;
        record(!units::toSI("length", 1.0, "kN", x, e1) && e1.contains("not a length unit") && !units::toSI("speed", 1.0, "m/s", x, e2) && e2.contains("Unknown quantity"),
               "wrong unit for a quantity and unknown quantities are refused", e1 + " | " + e2);
    }

    // Model: a beam (box 200 x 20 x 20) with an instance, and a union of two boxes for face invalidation.
    CadCommandService svc; juce::String e, beam, inst, a, b, u;
    svc.createBox(200, 20, 20, beam, e); svc.createInstance(beam, "Beam", inst, e);
    svc.createBox(20, 20, 20, a, e); svc.createBox(10, 10, 10, b, e); svc.setPlacement(b, 15, 5, 5, 0, 0, 0, e); svc.booleanOp("union", a, b, u, e);
    std::vector<CadFaceInfo> faces; svc.listFaces(beam, faces, e);
    juce::String endFace, startFace;
    for (const auto& f : faces) { if (f.normal.X() > 0.9) endFace = f.token; if (f.normal.X() < -0.9) startFace = f.token; }
    juce::String unionFace;
    for (const auto& [shape, tokens] : svc.document().find(u)->faceOrigins) for (const auto& t : tokens) if (t.startsWith(b + "/")) unionFace = t;
    record(endFace.isNotEmpty() && startFace.isNotEmpty() && unionFace.isNotEmpty(), "test model with persistent face tokens", endFace + ", " + startFace + ", " + unionFace + " " + e);
    const auto mechanicsBefore = text(mech::toJson(svc.document().mechanicsData()));
    const auto materialsBefore = text(matl::toJson(svc.document().materialsData()));

    // 2. Create with mixed input units.
    const auto createJson = R"({"name":"Cantilever tip load","description":"illustrative test case","parts":[{"feature":")" + beam + R"("}],
        "loads":[{"name":"Tip force","type":"force","target":{"feature":")" + beam + R"(","face":")" + endFace + R"("},"frame":"world","direction":[0,0,-2],"magnitude":{"value":2,"unit":"kN"},"point":{"value":[0.2,0.01,0.02],"unit":"m"}},
                 {"name":"Tip torque","type":"moment","target":{"feature":")" + beam + R"(","face":")" + endFace + R"("},"frame":")" + inst + R"(","direction":[1,0,0],"magnitude":{"value":1500,"unit":"N*mm"}}],
        "supports":[{"name":"Root","target":{"feature":")" + beam + R"(","face":")" + startFace + R"("},"kind":"fixed"}],
        "gravity":{"acceleration":{"value":[0,0,-1],"unit":"g0"}},
        "settings":{"analysisType":"linearStatic","options":{"note":"future solver options are kept"}}})";
    auto created = svc.analysisCommand("cad_analysis_create", args(createJson));
    const auto c1 = created.getProperty("case", {});
    const auto load1 = c1.getProperty("loads", {})[0], load2 = c1.getProperty("loads", {})[1], support = c1.getProperty("supports", {})[0];
    record((bool) created.getProperty("ok", false) && created.getProperty("id", "").toString() == "case-1" && c1.getProperty("status", "").toString() == "valid"
               && (double) load1.getProperty("magnitude", {}).getProperty("value", 0) == 2000.0 && load1.getProperty("magnitude", {}).getProperty("unit", "").toString() == "N"
               && (double) load1.getProperty("direction", {})[2] == -1.0 && (double) load1.getProperty("point", {}).getProperty("value", {})[0] == 200.0 && load1.getProperty("point", {}).getProperty("unit", "").toString() == "mm"
               && approx((double) load2.getProperty("magnitude", {}).getProperty("value", 0), 1.5) && load2.getProperty("magnitude", {}).getProperty("unit", "").toString() == "N*m" && load2.getProperty("frame", "").toString() == inst
               && support.getProperty("fixed", {}).size() == 6 && approx((double) c1.getProperty("gravity", {}).getProperty("acceleration", {}).getProperty("value", {})[2], -9.80665)
               && load1.getProperty("id", "").toString() == "load-1" && load2.getProperty("id", "").toString() == "load-2" && support.getProperty("id", "").toString() == "support-3",
           "create: case-1 valid; 2 kN -> 2000 N, 1500 N*mm -> 1.5 N*m, 0.2 m -> 200 mm, unit direction, fixed support, gravity 1 g0 -> 9.80665 m/s^2", text(created));
    record(c1.getProperty("settings", {}).getProperty("options", {}).getProperty("note", "").toString() == "future solver options are kept", "free-form settings options are kept");

    // 3. Modify.
    auto updated = svc.analysisCommand("cad_analysis_update", args(R"({"case":"case-1","name":"Cantilever tip load 3 kN","loads":[{"id":"load-1","type":"force","target":{"feature":")" + beam + R"(","face":")" + endFace + R"("},"direction":[0,0,-1],"magnitude":{"value":3,"unit":"kN"}},{"type":"force","target":{"feature":")" + beam + R"("},"direction":[0,1,0],"magnitude":{"value":100,"unit":"N"}}],"gravity":null})"));
    const auto c2 = updated.getProperty("case", {});
    record((bool) updated.getProperty("ok", false) && c2.getProperty("id", "").toString() == "case-1" && (int) c2.getProperty("revision", 0) == 2 && c2.getProperty("name", "").toString() == "Cantilever tip load 3 kN"
               && (double) c2.getProperty("loads", {})[0].getProperty("magnitude", {}).getProperty("value", 0) == 3000.0 && c2.getProperty("loads", {})[1].getProperty("id", "").toString() == "load-4"
               && !c2.hasProperty("gravity") && c2.getProperty("supports", {}).size() == 1,
           "edit: same id, revision 2, loads replaced (kept id load-1, new id load-4), gravity removed, supports kept", text(c2));

    // 4. Serialization round trip.
    {
        CadAnalysisCases copy; juce::String re;
        const auto stored = analysis::toJson(svc.document().analysisData());
        const bool ok = analysis::fromJson(juce::JSON::parse(text(stored)), copy, re) && text(analysis::toJson(copy)) == text(stored);
        record(ok, "stored loads, supports, frames and units survive a JSON round trip unchanged", re);
    }

    // 5. Invalid input and references are refused without changing the document.
    {
        const auto revision = svc.document().revision();
        juce::StringArray failures;
        const auto base = [&](const juce::String& load, const juce::String& supportJson = {}) { return R"({"name":"Bad","parts":[{"feature":")" + beam + R"("}],"loads":[)" + load + R"(],"supports":[)" + supportJson + "]}"; };
        const auto okLoad = [&](const juce::String& target) { return R"({"type":"force","target":)" + target + R"(,"direction":[0,0,-1],"magnitude":{"value":1,"unit":"N"}})"; };
        for (const auto& [json, expect] : std::vector<std::pair<juce::String, juce::String>> {
                 { R"({"name":"Bad","parts":[{"feature":"box-99"}]})", "does not exist" },
                 { base(okLoad(R"({"feature":")" + beam + R"(","face":")" + beam + R"(/nonexistent"})")), "face" },
                 { base(okLoad(R"({"feature":")" + a + R"("})")), "not one of the case's parts" },
                 { base(R"({"type":"force","target":{"feature":")" + beam + R"("},"frame":"inst-99","direction":[0,0,-1],"magnitude":{"value":1,"unit":"N"}})"), "frame" },
                 { base(R"({"type":"force","target":{"feature":")" + beam + R"("},"direction":[0,0,0],"magnitude":{"value":1,"unit":"N"}})"), "zero" },
                 { base(R"({"type":"force","target":{"feature":")" + beam + R"("},"direction":[0,0,1],"magnitude":{"value":1,"unit":"kg"}})"), "not a force unit" },
                 { base(R"({"type":"force","target":{"feature":")" + beam + R"("},"direction":[0,0,1],"magnitude":{"value":0,"unit":"N"}})"), "greater than zero" },
                 { base(R"({"type":"pressure","target":{"feature":")" + beam + R"("},"direction":[0,0,1],"magnitude":{"value":1,"unit":"N"}})"), "type" },
                 { base(okLoad(R"({"feature":")" + beam + R"(","instance":")" + inst + R"("})")), "exactly one" },
                 { base(okLoad(R"({"feature":")" + beam + R"("})"), R"({"target":{"feature":")" + beam + R"("},"fixed":["ux","uw"]})"), "degree of freedom" },
                 { base(okLoad(R"({"feature":")" + beam + R"("})"), R"({"target":{"feature":")" + beam + R"("},"fixed":[]})"), "fixes no" },
                 { R"({"name":"Bad","settings":{"analysisType":"modal"}})", "analysis type" },
                 { R"({"name":"Bad","parts":[{"feature":")" + beam + R"(","face":")" + endFace + R"("}]})", "faces belong" },
                 { R"({"name":""})", "name" } })
        {
            const auto r = svc.analysisCommand("cad_analysis_create", args(json));
            if ((bool) r.getProperty("ok", true) || !r.getProperty("error", "").toString().containsIgnoreCase(expect)) failures.add(json.substring(0, 80) + " -> " + text(r).substring(0, 200));
        }
        record(failures.isEmpty() && svc.document().revision() == revision && svc.document().analysisData().cases.size() == 1,
               "14 invalid cases (missing part/face/frame, out-of-scope target, bad direction/unit/magnitude/type/dof, unknown analysis type) refused, document unchanged", failures.joinIntoString(" | "));
    }

    // 6. A later geometry or assembly change is reported, not re-attached.
    {
        auto onUnion = svc.analysisCommand("cad_analysis_create", args(R"({"name":"Union face support","parts":[{"feature":")" + u + R"("}],"supports":[{"target":{"feature":")" + u + R"(","face":")" + unionFace + R"("},"kind":"pinned"}]})"));
        auto onInstance = svc.analysisCommand("cad_analysis_create", args(R"({"name":"Instance case","parts":[{"instance":")" + inst + R"("}],"loads":[{"type":"force","target":{"instance":")" + inst + R"("},"frame":")" + inst + R"(","direction":[0,0,-1],"magnitude":{"value":10,"unit":"N"}}]})"));
        const auto unionId = onUnion.getProperty("id", "").toString(), instId = onInstance.getProperty("id", "").toString();
        record((bool) onUnion.getProperty("ok", false) && (bool) onInstance.getProperty("ok", false) && onUnion.getProperty("case", {}).getProperty("status", "").toString() == "valid",
               "cases on a union face and on an assembly instance are valid", text(onUnion).substring(0, 300));
        svc.setPlacement(b, 5, 5, 5, 0, 0, 0, e);   // the second box now lies inside the first: its faces leave the union
        const auto listed = svc.analysisCommand("cad_analysis_list", args(R"({"case":")" + unionId + R"("})")).getProperty("case", {});
        const auto problems = listed.getProperty("problems", {});
        record(listed.getProperty("status", "").toString() == "invalid" && problems.size() == 1 && problems[0].toString().contains(unionFace) && problems[0].toString().contains("not moved")
                   && listed.getProperty("supports", {})[0].getProperty("target", {}).getProperty("face", "").toString() == unionFace,
               "geometry edit that removes a referenced face: case reported invalid, reference kept as authored", text(problems));
        svc.removeInstance(inst, e);
        const auto lost = svc.analysisCommand("cad_analysis_list", args(R"({"case":")" + instId + R"("})")).getProperty("case", {});
        record(lost.getProperty("status", "").toString() == "invalid" && text(lost.getProperty("problems", {})).contains("does not exist"),
               "deleting a referenced assembly instance: case reported invalid", text(lost.getProperty("problems", {})));
        const auto stillValid = svc.analysisCommand("cad_analysis_list", args(R"({"case":"case-1"})")).getProperty("case", {});
        record(stillValid.getProperty("status", "").toString() == "valid", "a case that does not reference the deleted instance stays valid", text(stillValid.getProperty("problems", {})));
        juce::String ue; svc.undo(ue); svc.undo(ue);
        record(svc.analysisCommand("cad_analysis_list", args(R"({"case":")" + unionId + R"("})")).getProperty("case", {}).getProperty("status", "").toString() == "valid"
                   && svc.analysisCommand("cad_analysis_list", args(R"({"case":"case-1"})")).getProperty("case", {}).getProperty("status", "").toString() == "valid",
               "undoing the geometry and instance changes makes the cases valid again");
    }

    // 7. Undo / redo of case edits.
    {
        juce::String ue;
        svc.undo(ue); svc.undo(ue);   // remove the two extra cases
        svc.undo(ue);                 // revert the update
        const auto* c = svc.document().analysisData().find("case-1");
        const bool undone = c != nullptr && c->revision == 1 && c->gravity.has_value() && c->loads.size() == 2 && c->loads[0].magnitude == 2000.0;
        svc.redo(ue);
        const auto* r = svc.document().analysisData().find("case-1");
        const bool redone = r != nullptr && r->revision == 2 && !r->gravity.has_value() && r->loads[0].magnitude == 3000.0;
        auto deleted = svc.analysisCommand("cad_analysis_delete", args(R"({"case":"case-1"})"));
        const bool gone = (bool) deleted.getProperty("ok", false) && svc.document().analysisData().find("case-1") == nullptr;
        svc.undo(ue);
        record(undone && redone && gone && svc.document().analysisData().find("case-1") != nullptr, "undo/redo of create, edit and delete",
               juce::String("undone ") + (undone ? "y" : "n") + " redone " + (redone ? "y" : "n") + " deleted " + (gone ? "y" : "n"));
        record(text(mech::toJson(svc.document().mechanicsData())) == mechanicsBefore && text(matl::toJson(svc.document().materialsData())) == materialsBefore,
               "assembly and material serialization unchanged by analysis-case edits");
    }

    // 8. Native save and reopen.
    {
        const auto file = folder.getChildFile("analysis.djo");
        const auto before = text(analysis::toJson(svc.document().analysisData()));
        juce::String se; const bool saved = svc.save(file, se);
        CadCommandService reopened; const bool loaded = reopened.load(file, se);
        const auto after = text(analysis::toJson(reopened.document().analysisData()));
        const auto listed = reopened.analysisCommand("cad_analysis_list", args("{}"));
        record(saved && loaded && after == before && listed.getProperty("cases", {})[0].getProperty("status", "").toString() == "valid",
               "save and reopen keep cases, loads, supports, units and numbering; references still valid", se + " " + after.substring(0, 300));
        auto next = reopened.analysisCommand("cad_analysis_create", args(R"({"name":"After reopen"})"));
        record(next.getProperty("id", "").toString() == "case-2", "after reopen a new case gets the next unused id", next.getProperty("id", "").toString());
    }

    // 9. Documents without analysis data.
    {
        CadCommandService plain; juce::String pe, pbox;
        plain.createBox(10, 10, 10, pbox, pe);
        const auto plainFile = folder.getChildFile("no-analysis.djo"), plainJson = folder.getChildFile("no-analysis.json");
        const bool ok = plain.save(plainFile, pe) && plain.save(plainJson, pe);
        CadCommandService back; const bool open = back.load(plainFile, pe);
        record(ok && open && back.document().analysisData().empty() && !plainJson.loadFileAsString().contains("analysisCases"),
               "a document without analysis cases stores none and reopens normally", pe);
        const auto demo = juce::File("D:/000 Electronics Lab Development/CAD Demonstrations/Articulated Robot Arm - Assemblies and Joints.djo");
        if (demo.existsAsFile())
        {
            CadCommandService old; juce::String oe;
            const bool opened = old.load(demo, oe);
            record(opened && old.document().analysisData().empty() && old.document().mechanicsData().joints.size() == 4, "an existing assembly document opens normally with no analysis cases", oe);
        }
        else record(true, "existing assembly document not present on this machine; skipped");
        CadAnalysisCases bad; juce::String be;
        const bool newer = !analysis::fromJson(args(R"({"schemaVersion":2,"cases":[]})"), bad, be) && be.contains("schema");
        const bool wrongUnit = !analysis::fromJson(args(R"({"schemaVersion":1,"nextNumber":2,"cases":[{"id":"case-1","name":"X","parts":[{"feature":"box-1"}],"loads":[{"id":"load-1","type":"force","target":{"feature":"box-1"},"direction":[0,0,1],"magnitude":{"value":1,"unit":"kg"}}]}]})"), bad, be);
        record(newer && wrongUnit, "stored data with a newer schema or an invalid unit is refused");
    }
    return passed;
}

// Focused sketch-editing check (document level; the canvas gestures call these same commands): polygon shape kept,
// vertex moves with joined geometry and one undo step each, redo cleared by a new edit, vertex insertion, rectangle
// corner reshaping, Bezier profiles extruded into exact solids, handle edits, tangent smoothing, save/reopen.
bool runCadSketchEditCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto item = new juce::DynamicObject(); item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-sketch-edit-check.result.json").replaceWithText(juce::JSON::toString(report, false));
        return ok;
    };
    const auto volumeOf = [](CadCommandService& s, const juce::String& id) { return geomprops::ofFeature(s.document(), id).volumeMm3; };
    const auto entity = [](CadCommandService& s, const juce::String& sketch, const juce::String& id) -> const CadSketchEntity* { if (const auto* k = s.document().findSketch(sketch)) for (const auto& e : k->entities) if (e.id == id) return &e; return nullptr; };
    const auto shoelace = [](const std::vector<std::pair<double, double>>& p) { double a = 0; for (size_t i = 0; i < p.size(); ++i) { const auto& q = p[i]; const auto& r = p[(i + 1) % p.size()]; a += q.first * r.second - r.first * q.second; } return std::abs(a) * 0.5; };
    const auto close = [](double a, double b, double rel) { return std::abs(a - b) <= rel * juce::jmax(1.0, std::abs(b)); };

    // 1. A trapezoid drawn with the Line tool (ends joined by snapping) keeps its shape and extrudes.
    CadCommandService svc; juce::String e, sketch, l1, l2, l3, l4, solid;
    svc.createSketch("XY", 0, sketch, e);
    svc.addSketchLine(sketch, 0, 0, 40, 0, l1, e);
    svc.addSketchLine(sketch, 40, 0, 30, 20, l2, e, l1 + ":p2");
    svc.addSketchLine(sketch, 30, 20, 10, 20, l3, e, l2 + ":p2");
    svc.addSketchLine(sketch, 10, 20, 0, 0, l4, e, l3 + ":p2", l1 + ":p1");
    svc.createExtrude(sketch, 10, false, solid, e);
    const auto trapezoid = shoelace({ { 0, 0 }, { 40, 0 }, { 30, 20 }, { 10, 20 } }) * 10.0;
    record(solid.isNotEmpty() && close(volumeOf(svc, solid), trapezoid, 1e-9) && std::abs(entity(svc, sketch, l2)->x2 - 30.0) < 1e-12,
           "four joined lines stay a trapezoid (not forced into a rectangle) and extrude: 6000 mm3", juce::String(volumeOf(svc, solid), 6) + " " + e);

    // 2. Moving a vertex carries the joined endpoint and keeps the profile closed; one command, one undo.
    const auto revision = svc.document().revision();
    const bool moved = svc.moveSketchPoint(sketch, l2 + ":p2", 35, 25, e);
    const auto movedArea = shoelace({ { 0, 0 }, { 40, 0 }, { 35, 25 }, { 10, 20 } }) * 10.0;
    record(moved && entity(svc, sketch, l3)->x1 == 35.0 && entity(svc, sketch, l3)->y1 == 25.0 && close(volumeOf(svc, solid), movedArea, 1e-9) && svc.document().revision() == revision + 1,
           "vertex move: the joined line follows, the extrusion rebuilds closed (exact area), one document revision", juce::String(volumeOf(svc, solid), 6) + " " + e);
    juce::String ue;
    svc.undo(ue);
    const bool undone = entity(svc, sketch, l2)->x2 == 30.0 && entity(svc, sketch, l3)->x1 == 30.0 && close(volumeOf(svc, solid), trapezoid, 1e-9);
    svc.redo(ue);
    const bool redone = entity(svc, sketch, l3)->y1 == 25.0 && close(volumeOf(svc, solid), movedArea, 1e-9);
    record(undone && redone, "one Undo restores the whole move (both endpoints and the solid); Redo re-applies it");
    svc.undo(ue);
    svc.moveSketchPoint(sketch, l3 + ":p2", 5, 22, e);   // a new edit after undo
    juce::String re; const bool redoRefused = !svc.redo(re);
    record(redoRefused && entity(svc, sketch, l3)->x1 == 30.0 && entity(svc, sketch, l4)->x1 == 5.0, "a new edit after Undo clears Redo (the undone move cannot come back on top of it)", re);

    // 3. Insert a vertex: the path is unchanged; then moving it reshapes the profile.
    juce::String created; juce::StringArray removed;
    const auto before = volumeOf(svc, solid);
    const bool inserted = svc.insertSketchVertex(sketch, l1, 20, -3, created, removed, e);
    const auto* first = entity(svc, sketch, l1); const auto* second = entity(svc, sketch, created);
    record(inserted && first->x2 == 20.0 && first->y2 == 0.0 && second->x1 == 20.0 && second->x2 == 40.0 && close(volumeOf(svc, solid), before, 1e-9) && removed.isEmpty(),
           "insert vertex at (20, 0) projected from (20, -3): two joined lines, same path and volume", created + " " + e);
    const bool pulled = svc.moveSketchPoint(sketch, l1 + ":p2", 20, -10, e);
    const auto pulledArea = shoelace({ { 0, 0 }, { 20, -10 }, { 40, 0 }, { 30, 20 }, { 10, 20 } });
    const auto withNotch = shoelace({ { 0, 0 }, { 20, -10 }, { 40, 0 }, { 30, 20 }, { 5, 22 } }) * 10.0;
    record(pulled && entity(svc, sketch, created)->x1 == 20.0 && entity(svc, sketch, created)->y1 == -10.0 && close(volumeOf(svc, solid), withNotch, 1e-9),
           "the inserted vertex moves; both halves follow and the solid rebuilds", juce::String(volumeOf(svc, solid), 6) + " expected " + juce::String(withNotch, 6) + " (" + juce::String(pulledArea, 3) + ") " + e);
    svc.undo(ue); svc.undo(ue);
    record(entity(svc, sketch, created) == nullptr && entity(svc, sketch, l1)->x2 == 40.0, "undo removes the move and the insertion");

    // 4. Rectangle: a corner drag reshapes it about the opposite corner and updates its dimensions; splitting an
    //    edge turns it into a polygon and says which constraints were removed.
    {
        CadCommandService r; juce::String re2, rs, group, body;
        r.createSketch("XY", 0, rs, re2); r.addSketchRectangle(rs, 0, 0, 30, 20, group, re2); r.createExtrude(rs, 5, false, body, re2);
        const auto* k = r.document().findSketch(rs);
        const auto bottom = k->entities[0].id, right = k->entities[1].id;
        const bool corner = r.moveSketchPoint(rs, right + ":p2", 50, 35, re2);
        double width = 0, height = 0; for (const auto& c : r.document().findSketch(rs)->constraints) { if (c.type == "distance" && c.firstEntity == bottom) width = c.value; if (c.type == "distance" && c.firstEntity == right) height = c.value; }
        record(corner && width == 50.0 && height == 35.0 && close(volumeOf(r, body), 50.0 * 35.0 * 5.0, 1e-9), "rectangle corner dragged to (50, 35): 50 x 35, dimensions updated, solid rebuilt", re2);
        juce::String newLine; juce::StringArray dropped;
        const bool split = r.insertSketchVertex(rs, bottom, 25, 0, newLine, dropped, re2);
        const bool pulledOut = split && r.moveSketchPoint(rs, bottom + ":p2", 25, -10, re2);
        record(split && dropped.size() >= 2 && pulledOut && close(volumeOf(r, body), (50.0 * 35.0 + 0.5 * 50.0 * 10.0) * 5.0, 1e-9),
               "splitting a rectangle edge reports the removed constraints, then the new vertex can leave the line", dropped.joinIntoString(", ") + " " + re2);
    }

    // 5. Bezier: a closed profile of a line and a curve is a genuine curved solid.
    CadCommandService b; juce::String be, bs, base, curve, arch;
    b.createSketch("XY", 0, bs, be);
    b.addSketchLine(bs, 0, 0, 60, 0, base, be);
    b.addSketchBezier(bs, 60, 0, 60, 40, 0, 40, 0, 0, curve, be, base + ":p2", base + ":p1");
    const bool extruded = b.createExtrude(bs, 8, false, arch, be);
    // Area enclosed by the chord and the cubic: integral of x dy over the curve, sampled finely.
    const auto curveArea = [](double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3)
    {
        const auto at = [&](double t, double a, double p, double q, double d) { const auto s = 1 - t; return s * s * s * a + 3 * s * s * t * p + 3 * s * t * t * q + t * t * t * d; };
        double area = 0, px = x0, py = y0; const int n = 20000;
        for (int i = 1; i <= n; ++i) { const auto t = (double) i / n; const auto x = at(t, x0, x1, x2, x3), y = at(t, y0, y1, y2, y3); area += px * y - x * py; px = x; py = y; }
        area += px * y0 - x0 * py; return std::abs(area) * 0.5;
    };
    const auto archArea = curveArea(60, 0, 60, 40, 0, 40, 0, 0);
    int bezierEdges = 0;
    for (TopExp_Explorer ex(b.document().find(arch)->shape, TopAbs_EDGE); ex.More(); ex.Next()) if (BRepAdaptor_Curve(TopoDS::Edge(ex.Current())).GetType() == GeomAbs_BezierCurve) ++bezierEdges;
    record(extruded && close(volumeOf(b, arch), archArea * 8.0, 1e-6) && bezierEdges >= 2,
           "line + Bezier closed profile extrudes to an exact curved solid (OCCT Bezier edges, volume = area x 8)", juce::String(volumeOf(b, arch), 6) + " expected " + juce::String(archArea * 8.0, 6) + ", Bezier edges " + juce::String(bezierEdges) + " " + be);
    const bool handle = b.moveSketchPoint(bs, curve + ":c1", 70, 60, be);
    const auto reshaped = curveArea(60, 0, 70, 60, 0, 40, 0, 0) * 8.0;
    record(handle && close(volumeOf(b, arch), reshaped, 1e-6), "dragging handle 1 reshapes the curve and the solid", juce::String(volumeOf(b, arch), 6) + " expected " + juce::String(reshaped, 6) + " " + be);
    const bool endMove = b.moveSketchPoint(bs, curve + ":p2", -10, 0, be);
    const auto* c = entity(b, bs, curve);
    record(endMove && entity(b, bs, base)->x1 == -10.0 && c->cx2 == -10.0 && c->cy2 == 40.0, "moving a curve endpoint moves the joined line end and carries that end's handle", be);
    b.undo(ue); b.undo(ue);
    record(close(volumeOf(b, arch), archArea * 8.0, 1e-6) && entity(b, bs, curve)->cx1 == 60.0, "undo restores the endpoint and the handle edit");
    // Tangent join: the start handle is turned onto the line's direction (G1), keeping its length 40.
    b.moveSketchPoint(bs, curve + ":c1", 75, 30, be);
    const bool smoothed = b.smoothSketchJoint(bs, curve, "p1", be);
    const auto* s = entity(b, bs, curve);
    const double hx = s->cx1 - s->x1, hy = s->cy1 - s->y1, lx = s->x1 - entity(b, bs, base)->x1, ly = s->y1 - entity(b, bs, base)->y1;
    record(smoothed && std::abs(hx * ly - hy * lx) < 1e-9 && hx * lx + hy * ly > 0 && close(std::hypot(hx, hy), std::hypot(15.0, 30.0), 1e-12),
           "smooth join: the curve leaves the line along the line's direction (tangent), handle length kept", juce::String(hx, 6) + "," + juce::String(hy, 6) + " " + be);
    record(b.document().find(arch) != nullptr && volumeOf(b, arch) > 0.0, "the smoothed profile still extrudes", juce::String(volumeOf(b, arch), 6));

    // 6. Save, reopen, edit.
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("build/sketch-edit-check/curve.djo");
        file.getParentDirectory().createDirectory();
        const auto volume = volumeOf(b, arch);
        juce::String se; const bool saved = b.save(file, se);
        CadCommandService back; const bool loaded = back.load(file, se);
        const auto* r = loaded ? entity(back, bs, curve) : nullptr;
        record(saved && loaded && r != nullptr && r->type == "bezier" && r->cx1 == s->cx1 && r->cy2 == s->cy2 && close(volumeOf(back, arch), volume, 1e-9),
               "save and reopen keep the curve, its handles and the extruded solid", se);
        const bool edited = back.moveSketchPoint(bs, curve + ":c2", 10, 50, se);
        record(edited && volumeOf(back, arch) > 0.0 && std::abs(volumeOf(back, arch) - volume) > 1.0, "after reopen the curve can be edited and the solid follows", se);
    }

    // 7. Existing behaviour: entity panel edits are one undo step; circles and plain rectangles still work.
    {
        CadCommandService p; juce::String pe, ps, group, circle, body;
        p.createSketch("XY", 0, ps, pe); p.addSketchRectangle(ps, 0, 0, 10, 10, group, pe); p.createExtrude(ps, 2, false, body, pe);
        const auto bottom = p.document().findSketch(ps)->entities[0].id;
        p.editSketchEntity(ps, bottom, true, 5, 5, true, 20, pe);
        const bool both = close(volumeOf(p, body), 20.0 * 10.0 * 2.0, 1e-9) && p.document().findSketch(ps)->entities[0].x1 == 5.0;
        juce::String pu; p.undo(pu);
        record(both && close(volumeOf(p, body), 200.0, 1e-9) && p.document().findSketch(ps)->entities[0].x1 == 0.0, "position + dimension edit is one command: one Undo reverts both", pe);
        juce::String cs, cbody; p.createSketch("XY", 0, cs, pe); p.addSketchCircle(cs, 0, 0, 5, circle, pe); p.createExtrude(cs, 4, false, cbody, pe);
        record(close(volumeOf(p, cbody), juce::MathConstants<double>::pi * 25.0 * 4.0, 1e-6), "circle profiles still extrude", pe);
        record(!p.moveSketchPoint(ps, "constraint-1:p1", 0, 0, pe) && !p.insertSketchVertex(cs, circle, 0, 0, created, removed, pe), "bad point references and inserting into a circle are refused", pe);
    }
    return passed;
}

bool runCadRebuildCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto item = new juce::DynamicObject(); item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        return ok;
    };
    const auto volumeOf = [](const TopoDS_Shape& shape) { GProp_GProps p; BRepGProp::VolumeProperties(shape, p); return p.Mass(); };
    const auto volume = [&](const CadCommandService& svc, const juce::String& id) { const auto* f = svc.document().find(id); return f == nullptr || f->shape.IsNull() ? -1.0 : volumeOf(f->shape); };
    const auto same = [](double a, double b) { return std::abs(a - b) <= 1.0e-6 * std::max(1.0, std::abs(b)); };
    // Direct OCCT reference: base box of length L fused with boxes 1..n, no document involved.
    const auto expected = [&](double length, int n)
    {
        TopoDS_Shape shape = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), length, 10, 10).Shape();
        for (int i = 1; i <= n; ++i) { BRepAlgoAPI_Fuse fuse(shape, BRepPrimAPI_MakeBox(gp_Pnt(i * 9.0, 5, 0), 10, 10, 10).Shape()); fuse.Build(); shape = fuse.Shape(); }
        return volumeOf(shape);
    };

    CadCommandService svc;
    juce::String e, base, independent;
    std::vector<juce::String> unions;
    bool built = svc.createBox(60, 10, 10, base, e);
    juce::String chain = base;
    for (int i = 1; built && i <= 6; ++i)
    {
        juce::String b, u;
        built = svc.createBox(10, 10, 10, b, e) && svc.setPlacement(b, i * 9.0, 5, 0, 0, 0, 0, e) && svc.booleanOp("union", chain, b, u, e);
        chain = u; unions.push_back(u);
    }
    built = built && svc.createBox(7, 7, 7, independent, e) && svc.setPlacement(independent, 200, 0, 0, 0, 0, 0, e);
    if (!record(built, "six dependent unions and an independent box", e)) return false;
    const auto chainMatches = [&](double length)
    {
        for (int i = 0; i < 6; ++i) if (!same(volume(svc, unions[(size_t) i]), expected(length, i + 1))) return false;
        return true;
    };
    record(chainMatches(60) && same(volume(svc, independent), 343.0), "every union equals a direct OCCT fuse; independent box 343 mm3",
           juce::String(volume(svc, unions.back()), 3) + " vs " + juce::String(expected(60, 6), 3));
    const bool edited = svc.setDimensions(base, 80, 10, 10, 0, e) && chainMatches(80) && same(volume(svc, independent), 343.0);
    record(edited, "upstream edit (base 60 -> 80 mm) updates all six downstream unions; independent box unchanged",
           juce::String(volume(svc, unions.back()), 3) + " vs " + juce::String(expected(80, 6), 3));
    record(svc.undo(e) && chainMatches(60) && same(volume(svc, independent), 343.0), "undo restores the chain", e);
    record(svc.redo(e) && chainMatches(80), "redo reapplies the edit", e);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("build").getChildFile("rebuild-check-" + juce::Uuid().toString() + ".djo");
    if (record(svc.save(file, e), "native save", e))
    {
        CadCommandService reopened; juce::String e2;
        bool ok = reopened.load(file, e2);
        for (int i = 0; ok && i < 6; ++i) ok = same(volume(reopened, unions[(size_t) i]), expected(80, i + 1));
        record(ok && same(volume(reopened, independent), 343.0), "reopen evaluates the same chain", e2);
        file.deleteFile();
    }
    // ~50 features: extend the chain, then time one box creation and its undo (the robot-arm scenario).
    for (int i = 7; built && i <= 24; ++i)
    {
        juce::String b, u;
        built = svc.createBox(10, 10, 10, b, e) && svc.setPlacement(b, i * 9.0, 5, 0, 0, 0, 0, e) && svc.booleanOp("union", chain, b, u, e);
        chain = u;
    }
    const auto features = (int) svc.document().features().size();
    juce::String probe;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const bool created = svc.createBox(10, 10, 10, probe, e);
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    const bool undone = svc.undo(e);
    const auto t2 = juce::Time::getMillisecondCounterHiRes();
    record(built && created && undone && svc.document().find(probe) == nullptr && same(volume(svc, chain), expected(80, 24)),
           "timing on " + juce::String(features) + " features (24-union chain)", "create box " + juce::String(t1 - t0, 0) + " ms, undo " + juce::String(t2 - t1, 0) + " ms");
    return passed;
}

bool runCadExportCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto item = new juce::DynamicObject(); item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        return ok;
    };
    const auto approx = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };
    const auto fmt3 = [](const double v[3]) { return juce::String(v[0], 3) + " x " + juce::String(v[1], 3) + " x " + juce::String(v[2], 3); };
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile("build").getChildFile("export-check");
    dir.createDirectory();

    CadCommandService svc;
    juce::String e, plate, hole, cut, boss, part;
    const bool built = svc.createBox(60, 40, 10, plate, e) && svc.createCylinder(6, 30, hole, e) && svc.setPlacement(hole, 30, 20, -10, 0, 0, 0, e)
                       && svc.booleanOp("subtract", plate, hole, cut, e) && svc.createCylinder(8, 5, boss, e) && svc.setPlacement(boss, 12, 12, 10, 0, 0, 0, e)
                       && svc.booleanOp("union", cut, boss, part, e) && svc.rename(part, "Bracket plate", e);
    if (!record(built, "representative part: plate 60x40x10, 12 mm through-hole, 8 mm boss", e)) return false;
    const double holeVolume = juce::MathConstants<double>::pi * 36.0 * 10.0;
    const double partVolume = 60.0 * 40.0 * 10.0 - holeVolume + juce::MathConstants<double>::pi * 64.0 * 5.0;   // 23874.34 mm3; without the hole 25005.31
    const auto revision = svc.document().revision();
    const bool modified = svc.document().modified();

    const auto exportTo = [&](CadExportFormat format, const juce::StringArray& ids, const juce::File& out, CadExportResult& result, CadExportPlan& plan, juce::String& error)
    {
        CadExportSettings settings; settings.format = format; settings.partIds = ids; settings.output = out; settings.overwrite = true;
        if (!svc.planExport(settings, plan, error)) return false;
        result = CadCommandService::runExport(plan);
        if (!result.ok) error = result.error;
        return result.ok;
    };
    // Reads a 3MF back: unit, build items (name, part number, transformed bounds, volume, manifold).
    struct Item3mf { juce::String name, partNumber; double min[3], max[3], volume; bool manifold; };
    const auto read3mf = [](const juce::File& file, bool& millimetres, std::vector<Item3mf>& items, juce::String& error)
    {
        try
        {
            auto wrapper = Lib3MF::CWrapper::loadLibrary();
            auto model = wrapper->CreateModel();
            model->QueryReader("3mf")->ReadFromFile(file.getFullPathName().toStdString());
            millimetres = model->GetUnit() == Lib3MF::eModelUnit::MilliMeter;
            auto it = model->GetBuildItems();
            while (it->MoveNext())
            {
                auto item = it->GetCurrent();
                auto object = item->GetObjectResource();
                auto mesh = model->GetMeshObjectByID(object->GetResourceID());
                const auto t = item->GetObjectTransform();
                std::vector<Lib3MF::sPosition> vertices; std::vector<Lib3MF::sTriangle> triangles;
                mesh->GetVertices(vertices); mesh->GetTriangleIndices(triangles);
                std::vector<gp_Pnt> placed;
                for (const auto& v : vertices)
                {
                    double p[3];
                    for (int c = 0; c < 3; ++c) p[c] = v.m_Coordinates[0] * t.m_Fields[0][c] + v.m_Coordinates[1] * t.m_Fields[1][c] + v.m_Coordinates[2] * t.m_Fields[2][c] + t.m_Fields[3][c];
                    placed.emplace_back(p[0], p[1], p[2]);
                }
                Item3mf out { juce::String(object->GetName()), juce::String(object->GetPartNumber()), { 1e30, 1e30, 1e30 }, { -1e30, -1e30, -1e30 }, 0.0, mesh->IsManifoldAndOriented() };
                for (const auto& p : placed) for (int c = 0; c < 3; ++c) { out.min[c] = std::min(out.min[c], p.Coord(c + 1)); out.max[c] = std::max(out.max[c], p.Coord(c + 1)); }
                for (const auto& tri : triangles)
                    out.volume += gp_Vec(placed[tri.m_Indices[0]].XYZ()).Dot(gp_Vec(placed[tri.m_Indices[1]].XYZ()).Crossed(gp_Vec(placed[tri.m_Indices[2]].XYZ()))) / 6.0;
                items.push_back(out);
            }
            return true;
        }
        catch (const std::exception& ex) { error = ex.what(); return false; }
    };
    // Reads a binary STL back with OCCT: triangle count, bounds, enclosed volume, and the binary size identity.
    const auto readStl = [](const juce::File& file, int& triangles, double min[3], double max[3], double& volume, bool& binary)
    {
        const auto mesh = RWStl::ReadFile(file.getFullPathName().toRawUTF8(), juce::MathConstants<double>::halfPi);
        if (mesh.IsNull()) return false;
        triangles = mesh->NbTriangles(); volume = 0.0;
        for (int c = 0; c < 3; ++c) { min[c] = 1e30; max[c] = -1e30; }
        for (int i = 1; i <= mesh->NbNodes(); ++i) for (int c = 0; c < 3; ++c) { min[c] = std::min(min[c], mesh->Node(i).Coord(c + 1)); max[c] = std::max(max[c], mesh->Node(i).Coord(c + 1)); }
        for (int i = 1; i <= triangles; ++i)
        {
            int a, b, c; mesh->Triangle(i).Get(a, b, c);
            volume += gp_Vec(mesh->Node(a).XYZ()).Dot(gp_Vec(mesh->Node(b).XYZ()).Crossed(gp_Vec(mesh->Node(c).XYZ()))) / 6.0;
        }
        binary = file.getSize() == 84 + 50 * (juce::int64) triangles;
        return true;
    };
    // Reads a STEP file back into an XCAF document: solids, volume, bounds and every product/component name.
    struct StepRead { int solids = 0; double volume = 0.0, min[3] {}, max[3] {}; juce::StringArray names; };
    const auto readStep = [](const juce::File& file, StepRead& out, juce::String& error)
    {
        Handle(TDocStd_Document) doc;
        XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", doc);
        STEPCAFControl_Reader reader; reader.SetNameMode(Standard_True);
        if (reader.ReadFile(file.getFullPathName().toRawUTF8()) != IFSelect_RetDone || !reader.Transfer(doc)) { error = "STEP read failed"; XCAFApp_Application::GetApplication()->Close(doc); return false; }
        const auto tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        TDF_LabelSequence roots; tool->GetFreeShapes(roots);
        const auto nameOf = [](const TDF_Label& label) { Handle(TDataStd_Name) n; return label.FindAttribute(TDataStd_Name::GetID(), n) ? juce::String(TCollection_AsciiString(n->Get()).ToCString()) : juce::String(); };
        Bnd_Box box;
        for (int i = 1; i <= roots.Length(); ++i)
        {
            const auto shape = tool->GetShape(roots.Value(i));
            out.names.add(nameOf(roots.Value(i)));
            TDF_LabelSequence components; XCAFDoc_ShapeTool::GetComponents(roots.Value(i), components);
            for (int k = 1; k <= components.Length(); ++k) { out.names.add(nameOf(components.Value(k))); TDF_Label referred; if (XCAFDoc_ShapeTool::GetReferredShape(components.Value(k), referred)) out.names.add(nameOf(referred)); }
            for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) ++out.solids;
            GProp_GProps props; BRepGProp::VolumeProperties(shape, props); out.volume += props.Mass();
            BRepBndLib::AddOptimal(shape, box, false, false);
        }
        box.Get(out.min[0], out.min[1], out.min[2], out.max[0], out.max[1], out.max[2]);
        out.names.removeEmptyStrings(); out.names.removeDuplicates(false);
        XCAFApp_Application::GetApplication()->Close(doc);
        return true;
    };

    // 1. The representative part in the three formats.
    CadExportResult result; CadExportPlan plan; juce::String error;
    const auto file3mf = dir.getChildFile("bracket-plate.3mf");
    if (record(exportTo(CadExportFormat::threeMF, { part }, file3mf, result, plan, error), "3MF export of the part", error))
    {
        bool mm = false; std::vector<Item3mf> items; juce::String readError;
        const bool read = read3mf(file3mf, mm, items, readError);
        record(read && mm && items.size() == 1 && items[0].name == "Bracket plate" && items[0].partNumber == part && items[0].manifold, "3MF reads back: millimetre unit, one named manifold object", readError);
        if (read && items.size() == 1)
        {
            const double size[3] { items[0].max[0] - items[0].min[0], items[0].max[1] - items[0].min[1], items[0].max[2] - items[0].min[2] };
            record(approx(size[0], 60, 1e-3) && approx(size[1], 40, 1e-3) && approx(size[2], 15, 1e-3), "3MF dimensions 60 x 40 x 15 mm", fmt3(size));
            record(approx(items[0].volume, partVolume, partVolume * 0.01) && items[0].volume < partVolume + 0.5 * holeVolume, "3MF volume matches the solid with its hole (within 1 %)",
                   juce::String(items[0].volume, 2) + " vs " + juce::String(partVolume, 2) + " mm3, " + juce::String(result.parts.front().triangles) + " triangles");
        }
    }
    const auto fileStl = dir.getChildFile("bracket-plate.stl");
    if (record(exportTo(CadExportFormat::stl, { part }, fileStl, result, plan, error), "binary STL export of the part", error))
    {
        int triangles = 0; double min[3], max[3], volume = 0.0; bool binary = false;
        const bool read = readStl(fileStl, triangles, min, max, volume, binary);
        const double size[3] { max[0] - min[0], max[1] - min[1], max[2] - min[2] };
        record(read && binary && triangles > 0, "STL reads back with OCCT RWStl and is binary (84 + 50 x triangles bytes)", juce::String(triangles) + " triangles");
        record(read && approx(size[0], 60, 1e-3) && approx(size[1], 40, 1e-3) && approx(size[2], 15, 1e-3) && approx(min[0], 0, 1e-3) && approx(min[2], 0, 1e-3), "STL dimensions 60 x 40 x 15 mm at the authored position", fmt3(size));
        record(read && approx(volume, partVolume, partVolume * 0.01) && volume < partVolume + 0.5 * holeVolume, "STL encloses the solid volume with its hole (within 1 %)", juce::String(volume, 2) + " mm3");
    }
    const auto fileStep = dir.getChildFile("bracket-plate.step");
    if (record(exportTo(CadExportFormat::step, { part }, fileStep, result, plan, error), "STEP export of the part", error))
    {
        StepRead read; juce::String readError;
        const bool ok = readStep(fileStep, read, readError);
        const double size[3] { read.max[0] - read.min[0], read.max[1] - read.min[1], read.max[2] - read.min[2] };
        const auto text = fileStep.loadFileAsString();
        record(ok && read.solids == 1 && approx(read.volume, partVolume, 1e-3 * partVolume), "STEP reads back as one B-rep solid with the exact volume (hole kept)", juce::String(read.volume, 3) + " mm3");
        record(ok && approx(size[0], 60, 1e-3) && approx(size[1], 40, 1e-3) && approx(size[2], 15, 1e-3), "STEP dimensions 60 x 40 x 15 mm", fmt3(size));
        record(text.contains("AUTOMOTIVE_DESIGN") && text.contains(".MILLI.") && read.names.contains("Bracket plate"), "STEP AP214 schema, millimetre unit and product name", read.names.joinIntoString(", "));
    }

    record(svc.document().revision() == revision && svc.document().modified() == modified, "exports did not change the document (same revision, no undo entry, dirty flag unchanged)");

    // 2. Multi-component: two instances of the part plus a second body; the source body must not be duplicated.
    juce::String clamp, left, right;
    CadFrame turned; turned.y = 80.0; turned.qw = std::cos(juce::MathConstants<double>::pi / 4.0); turned.qz = std::sin(juce::MathConstants<double>::pi / 4.0);
    const bool assembled = svc.createBox(20, 10, 10, clamp, e) && svc.setPlacement(clamp, 100, 0, 0, 0, 0, 0, e) && svc.rename(clamp, "Clamp block", e)
                           && svc.createInstance(part, "Left plate", left, e) && svc.createInstance(part, "Right plate", right, e) && svc.setInstancePlacement(right, turned, e);
    if (record(assembled, "assembly: Left plate and Right plate (instances, right turned 90 deg about Z at y 80) and Clamp block", e))
    {
        const auto revisionAssembly = svc.document().revision();
        const auto asm3mf = dir.getChildFile("assembly.3mf");
        if (record(exportTo(CadExportFormat::threeMF, {}, asm3mf, result, plan, error), "3MF export of the visible model", error))
        {
            bool mm = false; std::vector<Item3mf> items; juce::String readError;
            read3mf(asm3mf, mm, items, readError);
            juce::StringArray names; for (const auto& i : items) names.add(i.name);
            record(items.size() == 3 && names.contains("Left plate") && names.contains("Right plate") && names.contains("Clamp block") && !names.contains("Bracket plate"),
                   "3MF has three named parts and no duplicate source body", names.joinIntoString(", "));
            for (const auto& i : items)
                if (i.name == "Right plate")
                    record(approx(i.min[0], -40, 1e-3) && approx(i.max[0], 0, 1e-3) && approx(i.min[1], 80, 1e-3) && approx(i.max[1], 140, 1e-3) && approx(i.min[2], 0, 1e-3),
                           "3MF Right plate placed once: x -40..0, y 80..140", fmt3(i.min) + " .. " + fmt3(i.max));
        }
        const auto asmStep = dir.getChildFile("assembly.step");
        if (record(exportTo(CadExportFormat::step, {}, asmStep, result, plan, error), "STEP export of the visible model", error))
        {
            StepRead read; juce::String readError;
            readStep(asmStep, read, readError);
            record(read.solids == 3 && approx(read.volume, 2.0 * partVolume + 2000.0, 1e-3 * read.volume) && read.names.contains("Left plate") && read.names.contains("Right plate") && read.names.contains("Clamp block"),
                   "STEP assembly: three solids, names kept, no duplicate source", juce::String(read.solids) + " solids, " + read.names.joinIntoString(", "));
            record(approx(read.min[0], -40, 1e-3) && approx(read.max[0], 120, 1e-3) && approx(read.min[1], 0, 1e-3) && approx(read.max[1], 140, 1e-3), "STEP assembly bounds x -40..120, y 0..140", fmt3(read.min) + " .. " + fmt3(read.max));
        }
        const auto asmStl = dir.getChildFile("assembly.stl");
        if (record(exportTo(CadExportFormat::stl, {}, asmStl, result, plan, error), "STL batch export of the visible model", error))
            record(result.files.size() == 3 && dir.getChildFile("assembly-Left_plate.stl").existsAsFile() && dir.getChildFile("assembly-Right_plate.stl").existsAsFile() && dir.getChildFile("assembly-Clamp_block.stl").existsAsFile(),
                   "one STL per part with clear names", result.files.joinIntoString(", "));
        record(svc.document().revision() == revisionAssembly, "assembly exports did not change the document");
        record(svc.save(dir.getChildFile("export-check.djo"), e), "assembly saved for the interactive check", e);
    }

    // 3. Cancellation keeps an existing destination; a missing folder and an existing file without overwrite are refused.
    const auto sentinel = dir.getChildFile("cancelled.3mf");
    sentinel.replaceWithText("existing file");
    {
        CadExportSettings settings; settings.format = CadExportFormat::threeMF; settings.output = sentinel; settings.overwrite = true;
        CadExportPlan cancelPlan; juce::String planError;
        const bool planned = svc.planExport(settings, cancelPlan, planError);
        const auto cancelled = CadCommandService::runExport(cancelPlan, [](double, const juce::String&) { return false; });
        const auto leftovers = dir.findChildFiles(juce::File::findFiles, false, "*.partial-*");
        record(planned && !cancelled.ok && cancelled.cancelled && sentinel.loadFileAsString() == "existing file" && leftovers.isEmpty(), "cancel keeps the existing file and leaves no partial output", cancelled.error);
    }
    {
        CadExportSettings settings; settings.format = CadExportFormat::stl; settings.output = dir.getChildFile("no-such-folder").getChildFile("x.stl");
        CadExportPlan failPlan; juce::String planError;
        record(!svc.planExport(settings, failPlan, planError) && planError.contains("does not exist"), "missing output folder is refused", planError);
        settings.output = sentinel.withFileExtension(".3mf"); settings.format = CadExportFormat::threeMF;
        record(!svc.planExport(settings, failPlan, planError) && planError.contains("already exists"), "existing file is not replaced without overwrite", planError);
        settings.quality = { "custom", 0.0, 15.0 };
        record(!svc.planExport(settings, failPlan, planError) && planError.contains("chordal"), "invalid custom tolerance is refused", planError);
    }
    return passed;
}

bool runCadFaceCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto item = new juce::DynamicObject(); item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        return ok;
    };
    const auto volume = [](const CadCommandService& svc, const juce::String& id) { const auto* f = svc.document().find(id); if (f == nullptr || f->shape.IsNull()) return -1.0; GProp_GProps p; BRepGProp::VolumeProperties(f->shape, p); return p.Mass(); };
    const auto approx = [](double a, double b) { return std::abs(a - b) < 1.0e-4; };
    const auto faceNamed = [](const CadCommandService& svc, const juce::String& feature, const juce::String& token, CadFaceInfo& out) { std::vector<CadFaceInfo> faces; juce::String e; if (!svc.listFaces(feature, faces, e)) return false; for (const auto& f : faces) if (f.token == token) { out = f; return true; } return false; };
    CadCommandService svc;
    juce::String e, box, sketch, group, cut;
    if (!record(svc.createBox(100, 60, 10, box, e), "plate", e)) return false;
    CadFaceInfo top;
    if (!record(faceNamed(svc, box, box + "/top", top) && top.planar && approx(top.centre.Z(), 10.0), "plate top face is named " + box + "/top")) return false;
    if (!record(svc.createFaceSketch(box, box + "/top", sketch, e), "sketch on top face", e)) return false;
    CadSketchFrame frame;
    record(svc.document().sketchFrame(sketch, frame) && approx(frame.origin.Z(), 10.0) && frame.normal.IsEqual(gp_Dir(0, 0, 1), 1.0e-9) && frame.u.IsEqual(gp_Dir(1, 0, 0), 1.0e-9), "top frame: origin z 10, normal +Z, U +X");
    if (!record(svc.addSketchRectangle(sketch, 20, 15, 30, 20, group, e), "rectangle profile", e)) return false;
    if (!record(svc.createCut(sketch, box, 4, false, false, cut, e), "pocket cut 4 mm into the plate", e)) return false;
    record(approx(volume(svc, cut), 100.0 * 60 * 10 - 30.0 * 20 * 4), "pocket volume", juce::String(volume(svc, cut), 3));
    if (!record(svc.setDimensions(box, 100, 60, 16, 5, e), "plate height 10 -> 16", e)) return false;
    record(svc.document().sketchFrame(sketch, frame) && approx(frame.origin.Z(), 16.0) && frame.u.IsEqual(gp_Dir(1, 0, 0), 1.0e-9), "sketch follows the top face (z 16, axes unchanged)");
    CadFaceInfo floor;
    record(approx(volume(svc, cut), 100.0 * 60 * 16 - 30.0 * 20 * 4) && faceNamed(svc, cut, cut + "/end", floor) && approx(floor.centre.Z(), 12.0) && approx(floor.centre.X(), 35.0), "pocket rebuilt on the moved face (floor z 12, centre x 35)",
           "floor z " + juce::String(floor.centre.Z(), 4));
    record(svc.undo(e) && svc.document().sketchFrame(sketch, frame) && approx(frame.origin.Z(), 10.0) && approx(volume(svc, cut), 100.0 * 60 * 10 - 30.0 * 20 * 4), "undo restores height 10 and the attachment", e);
    record(svc.redo(e) && svc.document().sketchFrame(sketch, frame) && approx(frame.origin.Z(), 16.0), "redo restores height 16", e);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("build").getChildFile("face-check-" + juce::Uuid().toString() + ".djo");
    if (record(svc.save(file, e), "native save", e))
    {
        CadCommandService reopened; juce::String e2;
        const bool loaded = reopened.load(file, e2);
        const auto* s2 = loaded ? reopened.document().findSketch(sketch) : nullptr;
        CadSketchFrame f2;
        record(loaded && s2 != nullptr && s2->onFace() && s2->supportFeature == box && s2->supportFace == box + "/top" && reopened.document().sketchFrame(sketch, f2) && approx(f2.origin.Z(), 16.0)
                   && approx(volume(reopened, cut), 100.0 * 60 * 16 - 30.0 * 20 * 4), "reopen keeps support, frame and pocket", e2);
        file.deleteFile();
    }
    juce::String side;
    if (record(svc.createFaceSketch(box, box + "/right", side, e), "sketch on the right (x = 100) face", e))
        record(svc.document().sketchFrame(side, frame) && approx(frame.origin.X(), 100.0) && frame.normal.IsEqual(gp_Dir(1, 0, 0), 1.0e-9) && frame.u.IsEqual(gp_Dir(0, 1, 0), 1.0e-9) && frame.v.IsEqual(gp_Dir(0, 0, 1), 1.0e-9),
               "side frame: origin x 100, normal +X, U +Y, V +Z");
    juce::String floorSketch;
    if (record(svc.createFaceSketch(cut, cut + "/end", floorSketch, e), "sketch on the pocket floor", e))
    {
        const auto before = volume(svc, cut);
        const bool refused = !svc.setDimensions(cut, 10, 10, 20, 5, e);   // a 20 mm deep pocket goes through: no floor
        record(refused && e.contains("no longer exists") && approx(volume(svc, cut), before) && svc.document().find(cut)->height == 4.0, "through-cut that removes the floor is refused, state kept", e);
    }
    juce::String cantDelete;
    record(!svc.remove(box, cantDelete) && cantDelete.contains("supports"), "deleting the support body is refused", cantDelete);
    return passed;
}

bool runCadIkCheck(const juce::File& examples, juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;
    bool passed = true;
    const auto record = [&](bool ok, const juce::String& name, const juce::var& detail = {})
    {
        auto item = new juce::DynamicObject();
        item->setProperty("check", name); item->setProperty("passed", ok); item->setProperty("detail", detail);
        checks.add(juce::var(item)); passed = passed && ok;
        auto root = new juce::DynamicObject(); root->setProperty("passed", passed); root->setProperty("checks", checks); report = juce::var(root);
        return ok;
    };
    juce::String error;
    auto arm = std::make_shared<CadCommandService>();
    if (!record(arm->load(examples.getChildFile("ik_arm.djc"), error), "load spatial arm", error)) return false;
    const auto tasks = arm->document().mechanicsData().ikTasks;
    if (!record(tasks.size() == 3, "three arm target modes")) return false;
    CadMotionRuntime armRuntime(arm);
    const auto authored = juce::JSON::toString(mech::toJson(arm->document().mechanicsData()));
    const auto revision = arm->document().revision();
    for (const auto& task : tasks)
    {
        armRuntime.resetToReference();
        const auto result = armRuntime.solveIk(task);
        record(result.valid && !result.targets.empty() && result.targets.front().reached, "arm " + task.targets.front().mode, result.toJson());
    }
    auto draft = tasks.front(); draft.id.clear();
    armRuntime.resetToReference();
    const auto draftResult = armRuntime.solveIk(draft);
    record(draftResult.valid, "unsaved task preview", draftResult.toJson());
    const auto command = juce::JSON::parse(ikcmd::execute("solve", juce::JSON::parse("{\"task\":\"" + tasks.back().id + "\"}"), *arm, armRuntime));
    record((bool) command.getProperty("ok", false), "shared IK command", command);
    record(arm->document().revision() == revision && !arm->document().modified()
           && authored == juce::JSON::toString(mech::toJson(arm->document().mechanicsData())), "preview leaves authored model unchanged");
    const auto beforeGuard = armRuntime.pose();
    armRuntime.setDriver(CadMotionRuntime::Driver::simulation);
    record(!armRuntime.solveIk(tasks.front()).valid && armRuntime.pose() == beforeGuard, "DEWB ownership rejects local IK");
    armRuntime.setDriver(CadMotionRuntime::Driver::preview);

    draft.name = "Authored copy";
    for (auto& target : draft.targets) target.id.clear();
    juce::String created;
    if (record(arm->createIkTask(draft, created, error), "author task transaction", error))
    {
        record(arm->undo(error) && arm->document().mechanicsData().findIkTask(created) == nullptr, "undo task creation", error);
        record(arm->redo(error) && arm->document().mechanicsData().findIkTask(created) != nullptr, "redo task creation", error);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("build").getChildFile("ik-check-" + juce::Uuid().toString() + ".djo");
        CadCommandService reopened;
        const bool reopenedOk = arm->save(file, error) && reopened.load(file, error);
        record(reopenedOk && juce::JSON::toString(mech::toJson(arm->document().mechanicsData()))
               == juce::JSON::toString(mech::toJson(reopened.document().mechanicsData())), "native task save/reopen", error);
        if (file.existsAsFile()) file.deleteFile();
    }

    auto slider = std::make_shared<CadCommandService>();
    if (!record(slider->load(examples.getChildFile("ik_slider_crank.djc"), error), "load inverse slider-crank", error)) return false;
    const auto sliderTasks = slider->document().mechanicsData().ikTasks;
    if (!record(sliderTasks.size() == 3, "slider target cases")) return false;
    CadMotionRuntime sliderRuntime(slider);
    const auto reached = sliderRuntime.solveIk(sliderTasks.front());
    record(reached.valid && std::abs(sliderRuntime.coordinate("joint-9") - juce::MathConstants<double>::pi / 3.0) < 1.0e-3,
           "slider target with closed-loop crank at 90 degrees (60 from reference)", reached.toJson());
    for (size_t i = 1; i < sliderTasks.size(); ++i)
    {
        const auto last = sliderRuntime.pose();
        const auto missed = sliderRuntime.solveIk(sliderTasks[i]);
        const auto shown = sliderRuntime.pose();
        bool retained = last->instances.size() == shown->instances.size();
        for (size_t k = 0; retained && k < last->instances.size(); ++k) retained = last->instances[k].matrix == shown->instances[k].matrix;
        record(!missed.valid && retained && sliderRuntime.showingLastValidPose(), sliderTasks[i].name + " rejects candidate and retains pose", missed.toJson());
    }
    return passed;
}

bool runCadFoundationCheck(juce::String& detail)
{
    djehuti::cad::CadCommandService service;
    juce::String error, sketchId, plateId, holeSketchId, cutId, entityId;
    const auto check = [&](bool ok, const char* step) { if (!ok) { detail = juce::String(step) + ": " + error; return false; } return true; };
    const auto measure = [&](const djehuti::cad::CadFeature* feature, double expectedWidth, double expectedVolume, const char* step)
    {
        if (feature == nullptr || feature->shape.IsNull()) { detail = juce::String(step) + ": missing solid"; return false; }
        Bnd_Box bounds; BRepBndLib::Add(feature->shape, bounds);
        Standard_Real xmin, ymin, zmin, xmax, ymax, zmax; bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        GProp_GProps props; BRepGProp::VolumeProperties(feature->shape, props);
        if (std::abs((xmax - xmin) - expectedWidth) > 1.0e-5 || std::abs(props.Mass() - expectedVolume) > 1.0e-3)
        {
            detail = juce::String(step) + ": width=" + juce::String(xmax - xmin, 3) + " volume=" + juce::String(props.Mass(), 3);
            return false;
        }
        return true;
    };
    if (!check(service.createSketch("XY", 0.0, sketchId, error), "create plate sketch")) return false;
    if (!check(service.addSketchRectangle(sketchId, -60.0, -40.0, 120.0, 80.0, entityId, error), "add plate rectangle")) return false;
    if (!check(service.createExtrude(sketchId, 8.0, false, plateId, error), "extrude plate")) return false;
    if (!check(service.createSketch("XY", 0.0, holeSketchId, error), "create hole sketch")) return false;
    if (!check(service.addSketchCircle(holeSketchId, -45.0, -25.0, 5.0, entityId, error), "add hole 1")) return false;
    if (!check(service.addSketchCircle(holeSketchId, 45.0, -25.0, 5.0, entityId, error), "add hole 2")) return false;
    if (!check(service.addSketchCircle(holeSketchId, -45.0, 25.0, 5.0, entityId, error), "add hole 3")) return false;
    if (!check(service.addSketchCircle(holeSketchId, 45.0, 25.0, 5.0, entityId, error), "add hole 4")) return false;
    if (!check(service.createCut(holeSketchId, plateId, 0.0, true, false, cutId, error), "cut through holes")) return false;
    const auto expectedOldVolume = 120.0 * 80.0 * 8.0 - 4.0 * juce::MathConstants<double>::pi * 25.0 * 8.0;
    if (!measure(service.document().find(cutId), 120.0, expectedOldVolume, "initial cut geometry")) return false;
    const auto* plateSketch = service.document().findSketch(sketchId);
    if (plateSketch == nullptr || plateSketch->entities.empty()) { detail = "find plate driving entity: missing"; return false; }
    const auto drivingEntity = plateSketch->entities.front().id;
    if (!check(service.setSketchEntityDimension(sketchId, drivingEntity, 140.0, error), "edit driving dimension")) return false;
    const auto* editedSketch = service.document().findSketch(sketchId);
    if (editedSketch == nullptr || editedSketch->entities.front().x2 - editedSketch->entities.front().x1 < 139.999) { detail = "edited rectangle width was not propagated"; return false; }
    const auto expectedNewVolume = 140.0 * 80.0 * 8.0 - 4.0 * juce::MathConstants<double>::pi * 25.0 * 8.0;
    if (!measure(service.document().find(cutId), 140.0, expectedNewVolume, "edited cut geometry")) return false;
    if (service.setSketchEntityDimension(sketchId, drivingEntity, -1.0, error) || service.document().findSketch(sketchId)->entities.front().x2 - service.document().findSketch(sketchId)->entities.front().x1 < 139.999) { detail = "rejected conflicting edit did not preserve valid model"; return false; }
    if (!check(service.undo(error), "undo driving dimension")) return false;
    if (!measure(service.document().find(cutId), 120.0, expectedOldVolume, "undo geometry")) return false;
    if (!check(service.redo(error), "redo driving dimension")) return false;
    if (!measure(service.document().find(cutId), 140.0, expectedNewVolume, "redo geometry")) return false;

    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto native = temp.getChildFile("djehuti_cad_foundation_check.djo");
    const auto json = temp.getChildFile("djehuti_cad_foundation_check.djc");
    native.deleteFile(); json.deleteFile();
    if (!check(service.save(native, error), "save native document")) return false;
    djehuti::cad::CadCommandService reopened;
    if (!check(reopened.load(native, error), "reopen native document")) return false;
    const auto* reopenedSketch = reopened.document().findSketch(sketchId);
    if (reopenedSketch == nullptr || reopenedSketch->entities.empty() || reopenedSketch->entities.front().x2 - reopenedSketch->entities.front().x1 < 139.999 || reopened.document().find(cutId) == nullptr) { detail = "reopen native document: edited dependencies or geometry missing"; return false; }
    if (!measure(reopened.document().find(cutId), 140.0, expectedNewVolume, "reopened cut geometry")) return false;
    if (!check(reopened.document().exportJson(json, error) && json.existsAsFile() && json.getSize() > 0, "export JSON")) return false;
    const auto exported = juce::JSON::parse(json.loadFileAsString());
    bool exportedDimension = false;
    if (auto* root = exported.getDynamicObject()) if (auto* sketchArray = root->getProperty("sketches").getArray()) for (const auto& sketchValue : *sketchArray) if (auto* sketch = sketchValue.getDynamicObject()) if (auto* constraints = sketch->getProperty("constraints").getArray()) for (const auto& item : *constraints) if (item.getProperty("value", 0.0).toString().getDoubleValue() > 139.999) exportedDimension = true;
    if (!exportedDimension) { detail = "JSON export: edited driving dimension missing"; return false; }
    native.deleteFile(); json.deleteFile();
    return true;
}
}

extern "C" void (*g_frust_dbg_callback)(int, int) = nullptr;

class DjehutiCADApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DjehutiCAD"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().startsWith("--cad-fixture-") || getCommandLineParameters().startsWith("--cad-component-") || getCommandLineParameters().startsWith("--cad-craft-"); }

    void initialise(const juce::String& commandLine) override
    {
        if(commandLine.startsWith("--cad-craft-phase6-check"))
        {
            juce::var report;const bool ok=djehuti::cad::runCadCraftPhase6Check(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase6-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(ok?0:1);
        }
        if(commandLine.startsWith("--cad-craft-phase6-ui-check"))
        {
            const bool interactive=commandLine.contains("--interactive");juce::MessageManager::callAsync([this,interactive]
            {const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-ui-check-p6").getChildFile(juce::Uuid().toDashedString());juce::var report;const bool ok=djehuti::cad::runCadCraftPhase6UiCheck(report,folder,interactive);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase6-ui-check.result.json").replaceWithText(juce::JSON::toString(report));setApplicationReturnValue(ok?0:1);if(!interactive)juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-craft-phase5-check"))
        {
            juce::var report;const bool ok=djehuti::cad::runCadCraftPhase5Check(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase5-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(ok?0:1);
        }
        if(commandLine.startsWith("--cad-craft-phase5-ui-check"))
        {
            const bool interactive=commandLine.contains("--interactive");juce::MessageManager::callAsync([this,interactive]
            {const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-ui-check-p5").getChildFile(juce::Uuid().toDashedString());juce::var report;const bool ok=djehuti::cad::runCadCraftPhase5UiCheck(report,folder,interactive);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase5-ui-check.result.json").replaceWithText(juce::JSON::toString(report));setApplicationReturnValue(ok?0:1);if(!interactive)juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-craft-phase4-check"))
        {
            juce::var report;const bool ok=djehuti::cad::runCadCraftPhase4Check(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase4-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(ok?0:1);
        }
        if(commandLine.startsWith("--cad-craft-phase4-ui-check"))
        {
            const bool interactive=commandLine.contains("--interactive");juce::MessageManager::callAsync([this,interactive]
            {const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-ui-check-p4").getChildFile(juce::Uuid().toDashedString());juce::var report;const bool ok=djehuti::cad::runCadCraftPhase4UiCheck(report,folder,interactive);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase4-ui-check.result.json").replaceWithText(juce::JSON::toString(report));setApplicationReturnValue(ok?0:1);if(!interactive)juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-craft-phase3-check"))
        {
            juce::var report;const bool ok=djehuti::cad::runCadCraftPhase3Check(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase3-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(ok?0:1);
        }
        if(commandLine.startsWith("--cad-craft-phase3-ui-check"))
        {
            const bool interactive=commandLine.contains("--interactive");juce::MessageManager::callAsync([this,interactive]
            {const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-ui-check-p3").getChildFile(juce::Uuid().toDashedString());juce::var report;const bool ok=djehuti::cad::runCadCraftPhase3UiCheck(report,folder,interactive);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase3-ui-check.result.json").replaceWithText(juce::JSON::toString(report));setApplicationReturnValue(ok?0:1);if(!interactive)juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-craft-phase2-check"))
        {
            juce::var report;const bool ok=djehuti::cad::runCadCraftPhase2Check(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase2-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(ok?0:1);
        }
        if(commandLine.startsWith("--cad-craft-phase2-ui-check"))
        {
            const bool interactive=commandLine.contains("--interactive");juce::MessageManager::callAsync([this,interactive]
            {const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-ui-check-p2").getChildFile(juce::Uuid().toDashedString());juce::var report;const bool ok=djehuti::cad::runCadCraftPhase2UiCheck(report,folder,interactive);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-phase2-ui-check.result.json").replaceWithText(juce::JSON::toString(report));setApplicationReturnValue(ok?0:1);if(!interactive)juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-craft-check"))
        {
            juce::var report;const bool ok=djehuti::cad::runCadCraftCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(ok?0:1);
        }
        if(commandLine.startsWith("--cad-craft-ui-check"))
        {
            const bool interactive=commandLine.contains("--interactive");juce::MessageManager::callAsync([this,interactive]
            {const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-ui-check").getChildFile(juce::Uuid().toDashedString());juce::var report;const bool ok=djehuti::cad::runCadCraftUiCheck(report,folder,interactive);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-craft-ui-check.result.json").replaceWithText(juce::JSON::toString(report));setApplicationReturnValue(ok?0:1);if(!interactive)juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-craft-native"))
        {
            juce::MessageManager::callAsync([]{auto* workspace=new DjehutiCAD(true);workspace->setSize(1550,1000);juce::DialogWindow::LaunchOptions options;options.content.setOwned(workspace);options.dialogTitle="Craft Designer Phase 1 smoke test";options.useNativeTitleBar=true;options.launchAsync();workspace->selectWorkspace(7);});return;
        }
        if(commandLine.startsWith("--cad-component-native"))
        {
            juce::MessageManager::callAsync([]{auto* workspace=new DjehutiCAD(true);workspace->setSize(1400,1000);juce::DialogWindow::LaunchOptions options;options.content.setOwned(workspace);options.dialogTitle="Component Library native check";options.useNativeTitleBar=true;options.launchAsync();workspace->selectWorkspace(6);});return;
        }
        if(commandLine.startsWith("--cad-component-ui-check"))
        {
            const bool interactive=commandLine.contains("--interactive");juce::MessageManager::callAsync([this,interactive]
            {const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/component-ui-check").getChildFile(juce::Uuid().toDashedString());juce::var report;const bool ok=runCadComponentUiCheck(report,folder,interactive);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-component-ui-check.result.json").replaceWithText(juce::JSON::toString(report));setApplicationReturnValue(ok?0:1);if(!interactive)juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-component-manufacturer-check"))
        {
            juce::var report;const bool ok=djehuti::cad::runCadManufacturerCheck(report,juce::File::getCurrentWorkingDirectory().getChildFile("Resources"));
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-manufacturer-check.result.json").replaceWithText(juce::JSON::toString(report));
            TerminateProcess(GetCurrentProcess(),ok?0u:1u);
        }
        if(commandLine.startsWith("--cad-component-check"))
        {
            const auto args=juce::JUCEApplication::getCommandLineParameterArray();const int index=args.indexOf("--research-root");const auto root=index>=0&&index+1<args.size()?juce::File(args[index+1]):juce::File();
            juce::var report;const bool ok=djehuti::cad::runCadComponentCheck(report,root);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-component-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(ok?0:1);
        }
        if (commandLine.startsWith("--cad-fixture-cantilever-check"))
        {
            juce::var report;const bool passed=djehuti::cad::runCadCantileverCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-cantilever-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(passed?0:1);
        }
        if (commandLine.startsWith("--cad-fixture-ui-check"))
        {
            const bool keepOpen=commandLine.contains("--interactive");
            juce::MessageManager::callAsync([this,keepOpen]
            {
                const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/fixture-ui-check").getChildFile(juce::Uuid().toDashedString());
                juce::var report;const bool passed=runCadFixtureUiCheck(report,folder,keepOpen);
                juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-fixture-ui-check.result.json").replaceWithText(juce::JSON::toString(report));
                setApplicationReturnValue(passed?0:1);if(!keepOpen)juce::MessageManager::callAsync([this]{quit();});
            });return;
        }
        if (commandLine.startsWith("--cad-fixture-check"))
        {
            juce::var report;const bool passed=djehuti::cad::runCadFixtureCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-fixture-check.result.json").replaceWithText(juce::JSON::toString(report));std::exit(passed?0:1);
        }
        if (commandLine.startsWith("--cad-stock-ui-check"))
        {
            juce::MessageManager::callAsync([this]
            {
                const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/stock-ui-check").getChildFile(juce::Uuid().toDashedString());
                juce::var report; const bool passed = runCadStockUiCheck(report, folder);
                juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-stock-ui-check.result.json").replaceWithText(juce::JSON::toString(report));
                setApplicationReturnValue(passed ? 0 : 1);
                juce::MessageManager::callAsync([this] { quit(); });
            });
            return;
        }
        if(commandLine.startsWith("--cad-workflow-ui-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=runCadWorkflowUiCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-workflow-ui-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-sketch-line-repair-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=runSketchLineRepairCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-sketch-line-repair-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-sketch-toolbox-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=runSketchToolboxCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-sketch-toolbox-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-advanced-sketch-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=runAdvancedSketchCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-advanced-sketch-check.result.json").replaceWithText(juce::JSON::toString(report,false,17));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if(commandLine.startsWith("--cad-sketch-usability-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=runSketchUsabilityCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-sketch-usability-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if (commandLine.startsWith("--cad-workspace-check"))
        {
            // Use JUCE's normal event loop and shutdown path. std::exit inside initialise bypasses
            // JUCE cleanup while the floating-window check has created shared GL resources.
            juce::MessageManager::callAsync([this]
            {
                auto workspace=std::make_unique<DjehutiCAD>(true); auto report=workspace->runWorkspaceCheck();
                const auto result=juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-workspace-check.result.json");
                report.getDynamicObject()->setProperty("stage","checksComplete");
                result.replaceWithText(juce::JSON::toString(report,false));
                const bool ok=report.getProperty("ok",false); workspace.reset();
                report.getDynamicObject()->setProperty("stage","componentShutdownComplete");
                result.replaceWithText(juce::JSON::toString(report,false));
                setApplicationReturnValue(ok?0:1); quit();
            });
            return;
        }
        if (commandLine.startsWith("--cad-ik-check"))
        {
            const auto args = juce::StringArray::fromTokens(commandLine, true);
            const auto examples = args.size() > 1 ? juce::File(args[1].unquoted()) : juce::File::getCurrentWorkingDirectory().getChildFile("examples/mechanisms");
            juce::var report;
            const bool ok = runCadIkCheck(examples, report);
            const auto result = juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-ik-check.result.json");
            result.replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.startsWith("--cad-interference-check"))
        {
            juce::var report;
            const bool ok = runCadInterferenceCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-interference-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.startsWith("--cad-geometry-check"))
        {
            juce::var report;
            const bool ok = runCadGeometryCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-geometry-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.startsWith("--cad-stock-check"))
        {
            juce::var report;
            const bool ok = djehuti::cad::runCadStockCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-stock-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.startsWith("--cad-material-check"))
        {
            juce::var report;
            const bool ok = runCadMaterialCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-material-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.startsWith("--cad-analysis-check"))
        {
            juce::var report;
            const bool ok = runCadAnalysisCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-analysis-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if(commandLine.startsWith("--cad-viewport-range-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=runViewportRangeCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-viewport-range-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if (commandLine.startsWith("--cad-surface-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=djehuti::cad::runCadSurfaceCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-surface-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if (commandLine.startsWith("--cad-modeling-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=djehuti::cad::runCadModelingCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-modeling-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if (commandLine.startsWith("--cad-replication-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=djehuti::cad::runCadReplicationCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-replication-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if (commandLine.startsWith("--cad-hole-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=djehuti::cad::runCadHoleCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-hole-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if (commandLine.startsWith("--cad-chamfer-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=djehuti::cad::runCadChamferCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-chamfer-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if (commandLine.startsWith("--cad-lathe-check"))
        {
            juce::MessageManager::callAsync([this]{juce::var report;const bool ok=djehuti::cad::runCadLatheCheck(report);juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-lathe-check.result.json").replaceWithText(juce::JSON::toString(report,false));setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});});return;
        }
        if (commandLine.startsWith("--cad-revolve-check"))
        {
            juce::var report; const bool ok=djehuti::cad::runCadRevolveCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-revolve-check.result.json").replaceWithText(juce::JSON::toString(report,false));
            std::exit(ok?0:1);
        }
        if (commandLine.startsWith("--cad-revolve-ui-check"))
        {
            juce::MessageManager::callAsync([this]
            {
                juce::var report;const bool ok=djehuti::cad::runCadRevolveUiCheck(report);
                juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-revolve-ui-check.result.json").replaceWithText(juce::JSON::toString(report,false));
                setApplicationReturnValue(ok?0:1);juce::MessageManager::callAsync([this]{quit();});
            });return;
        }
        if (commandLine.startsWith("--cad-sketch-edit-check"))
        {
            juce::var report;
            const bool ok = runCadSketchEditCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-sketch-edit-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.startsWith("--cad-rebuild-check"))
        {
            juce::var report;
            const bool ok = runCadRebuildCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-rebuild-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.startsWith("--cad-export-check"))
        {
            juce::var report;
            const bool ok = runCadExportCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-export-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.startsWith("--cad-face-check"))
        {
            juce::var report;
            const bool ok = runCadFaceCheck(report);
            juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-face-check.result.json").replaceWithText(juce::JSON::toString(report, false));
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.contains("--cad-foundation-check"))
        {
            juce::String detail;
            const bool ok = runCadFoundationCheck(detail);
            const auto result = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("djehuti_cad_foundation_check.result");
            result.replaceWithText(ok ? "CAD_SKETCH_FOUNDATION_CHECK_OK" : "CAD_SKETCH_FOUNDATION_CHECK_FAILED\n" + detail);
            std::exit(ok ? 0 : 1);
        }
        if (commandLine.contains("--cad-camera-check"))
        {
            juce::String detail;
            const bool ok = runViewportCameraCheck(detail);
            const auto result = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("djehuti_cad_camera_check.result");
            result.replaceWithText(ok ? "CAD_CAMERA_CHECK_OK\n" + detail : "CAD_CAMERA_CHECK_FAILED\n" + detail);
            std::exit(ok ? 0 : 1);
        }
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow = nullptr; }

    void systemRequestedQuit() override
    {
        frust_exec::Executor::instance().shutdown([this] { quit(); }, 3000);
    }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setMinimised(false);
            mainWindow->toFront(true);
        }
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff171b20), allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new DjehutiCAD(), true);
            setResizable(true, true);
            setResizeLimits(1100, 720, 3840, 2160);
            centreWithSize(1500, 920);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            if (auto* cad = dynamic_cast<DjehutiCAD*>(getContentComponent()))
                cad->confirmCloseThen([this] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
            else
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(DjehutiCADApplication)


