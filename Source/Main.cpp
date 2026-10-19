#include <JuceHeader.h>

#include "DjehutiCAD.h"
#include "FrustExecution.h"
#include "CadDocument.h"
#include "CadInterference.h"
#include "CadMotionRuntime.h"
#include "CadIkCommands.h"
#include "Viewport3D.h"

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
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <cstdlib>

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
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
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


