#include <JuceHeader.h>

#include "DjehutiCAD.h"
#include "FrustExecution.h"
#include "CadDocument.h"
#include "CadIkCommands.h"
#include "Viewport3D.h"

#include <BRepBndLib.hxx>
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


