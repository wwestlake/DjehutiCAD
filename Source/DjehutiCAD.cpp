#include "DjehutiCAD.h"

#include "FrustPanel.h"
#include "NodeDesignerPanel.h"
#include "DjehutiAgent.h"
#include "Viewport3D.h"
#include "CadWorkspacePanel.h"
#include "FrustEngine.h"
#include "CadIkCommands.h"
#include "CadToolbox.h"
#include "LocalAgentApi.h"
#include "CadFixturesPanel.h"
#include "CadComponentBrowser.h"
#include "CadCraftBrowser.h"

namespace
{
constexpr int kCadWorkspace = 100;
constexpr int kFrustEditor = 101;
constexpr int kNodeEditor = 102;
constexpr int kAgent = 103;
constexpr int kAgentSettings = 104;
constexpr int kThreeDViews = 105;
constexpr int kNewCadDocument = 106;
constexpr int kOpenCadDocument = 107;
constexpr int kSaveCadDocument = 108;
constexpr int kSaveCadDocumentAs = 109;
constexpr int kExportCad = 110;

// Agent tool name -> shared IK command.
juce::String ikCommandForTool(const std::string& name)
{
    if (name == "cad_ik_create_task") return "create";
    if (name == "cad_ik_update_task") return "update";
    if (name == "cad_ik_remove_task") return "remove";
    if (name == "cad_ik_inspect") return "inspect";
    if (name == "cad_ik_solve") return "solve";
    if (name == "cad_ik_status") return "status";
    if (name == "cad_ik_reset_preview") return "resetPreview";
    return {};
}
}

// Physical materials and structural analysis cases (shared command service); synchronous structured result.
std::string DjehutiCAD::runEngineeringCommand(const juce::Component::SafePointer<DjehutiCAD>& safe, const std::string& name, const juce::String& arguments)
{
    const auto run = [safe, name, arguments]() -> std::string
    {
        if (safe == nullptr || safe->cadService == nullptr) return R"({"ok": false, "error": "DjehutiCAD is closing."})";
        const auto parsed = juce::JSON::parse(arguments.isEmpty() ? juce::String("{}") : arguments);
        const auto args = parsed.isObject() ? parsed : juce::var(new juce::DynamicObject());
        const auto reply = name == "cad_component" ? safe->cadService->componentCommand(args) : name == "cad_fixture" ? safe->fixtureService->command(args) : name == "cad_catalog" ? safe->cadService->catalogCommand(args) : name == "cad_stock" ? safe->cadService->stockCommand(args) : name.rfind("cad_analysis_", 0) == 0 ? safe->cadService->analysisCommand(juce::String(name), args) : safe->cadService->materialCommand(juce::String(name), args);
        if (name == "cad_fixture" && safe->fixturesPanel != nullptr) safe->fixturesPanel->synchronize(reply["id"].toString());
        if (safe->cadWorkspace != nullptr) safe->cadWorkspace->repaint();
        return juce::JSON::toString(reply, true).toStdString();
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) return run();
    auto done = std::make_shared<juce::WaitableEvent>();
    auto output = std::make_shared<std::string>(R"({"ok": false, "error": "DjehutiCAD did not answer within 20 s."})");
    juce::MessageManager::callAsync([run, done, output] { *output = run(); done->signal(); });
    done->wait(20000);
    return *output;
}

// Runs one shared IK command on the message thread and waits for its structured result, so automation
// receives the actual outcome rather than a "scheduled" acknowledgement.
// Face references for automation: list a body's named faces, or attach a sketch to one (synchronous result).
std::string DjehutiCAD::runFaceCommand(const juce::Component::SafePointer<DjehutiCAD>& safe, const std::string& name, const juce::String& arguments)
{
    const auto run = [safe, name, arguments]() -> std::string
    {
        if (safe == nullptr || safe->cadService == nullptr) return R"({"ok": false, "error": "DjehutiCAD is closing."})";
        const auto parsed = juce::JSON::parse(arguments.isEmpty() ? juce::String("{}") : arguments);
        const auto feature = parsed.getProperty("feature", "").toString();
        auto reply = std::make_unique<juce::DynamicObject>();
        juce::String error, value;
        bool ok = false;
        if (name == "cad_list_faces") { ok = safe->cadService->listFacesJson(feature, value, error); if (ok) reply->setProperty("faces", juce::JSON::parse(value)); }
        else { ok = safe->cadService->createFaceSketch(feature, parsed.getProperty("face", "").toString(), value, error); if (ok) reply->setProperty("id", value); }
        reply->setProperty("ok", ok);
        if (!ok) reply->setProperty("error", error);
        if (safe->cadWorkspace != nullptr) safe->cadWorkspace->repaint();
        return juce::JSON::toString(juce::var(reply.release())).toStdString();
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) return run();
    auto done = std::make_shared<juce::WaitableEvent>();
    auto output = std::make_shared<std::string>(R"({"ok": false, "error": "DjehutiCAD did not answer within 20 s."})");
    juce::MessageManager::callAsync([run, done, output] { *output = run(); done->signal(); });
    done->wait(20000);
    return *output;
}

// Manufacturing export for automation (agent, FRust, nodes): the plan reads the document on the message thread,
// then the writer runs on the calling thread from the plan's geometry copies.
std::string DjehutiCAD::runExportCommand(const juce::Component::SafePointer<DjehutiCAD>& safe, const juce::String& arguments)
{
    const auto failure = [](const juce::String& error) { auto o = std::make_unique<juce::DynamicObject>(); o->setProperty("ok", false); o->setProperty("error", error); return juce::JSON::toString(juce::var(o.release())).toStdString(); };
    djehuti::cad::CadExportSettings settings; juce::String error;
    if (!djehuti::cad::exporting::settingsFromJson(juce::JSON::parse(arguments.isEmpty() ? juce::String("{}") : arguments), settings, error)) return failure(error);
    auto plan = std::make_shared<djehuti::cad::CadExportPlan>();
    auto planError = std::make_shared<juce::String>("DjehutiCAD is closing.");
    const auto prepare = [safe, settings, plan, planError]
    {
        if (safe == nullptr || safe->cadService == nullptr) return false;
        return safe->cadService->planExport(settings, *plan, *planError);
    };
    bool planned = false;
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) planned = prepare();
    else
    {
        auto done = std::make_shared<juce::WaitableEvent>();
        auto ok = std::make_shared<std::atomic<bool>>(false);
        juce::MessageManager::callAsync([prepare, done, ok] { *ok = prepare(); done->signal(); });
        if (!done->wait(20000)) return failure("DjehutiCAD did not answer within 20 s.");
        planned = *ok;
    }
    if (!planned) return failure(*planError);
    const auto result = djehuti::cad::CadCommandService::runExport(*plan);
    return juce::JSON::toString(result.toVar(*plan)).toStdString();
}

std::string DjehutiCAD::runIkCommand(const juce::Component::SafePointer<DjehutiCAD>& safe, const juce::String& command, const juce::String& arguments)
{
    const auto run = [safe, command, arguments]() -> std::string
    {
        if (safe == nullptr || safe->cadService == nullptr || safe->motionRuntime == nullptr) return R"({"ok": false, "error": "DjehutiCAD is closing."})";
        const auto result = djehuti::cad::ikcmd::execute(command, juce::JSON::parse(arguments.isEmpty() ? juce::String("{}") : arguments), *safe->cadService, *safe->motionRuntime);
        if (safe->cadWorkspace != nullptr) safe->cadWorkspace->repaint();
        return result.toStdString();
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) return run();
    auto done = std::make_shared<juce::WaitableEvent>();
    auto output = std::make_shared<std::string>(R"({"ok": false, "error": "DjehutiCAD did not answer within 20 s."})");
    juce::MessageManager::callAsync([run, done, output] { *output = run(); done->signal(); });
    if (!done->wait(20000)) return R"({"ok": false, "error": "DjehutiCAD did not answer within 20 s."})";
    return *output;
}


std::string DjehutiCAD::runToolCommand(const juce::Component::SafePointer<DjehutiCAD>& safeThis,
                                     const std::string& name, const juce::String& arguments, std::function<bool()> keepWaiting)
{
    // Export plans on the message thread and writes on the calling thread, so it is not wrapped below.
    if (name == "cad_export" && !juce::MessageManager::getInstance()->isThisTheMessageThread()) return runExportCommand(safeThis, arguments);
    const auto execute = [safeThis, name, arguments]() -> std::string
    {
        if (safeThis == nullptr || safeThis->cadWorkspace == nullptr) return R"({"ok":false,"error":"DjehutiCAD is closing."})";
        const auto parsedArgs = juce::JSON::parse(arguments);
        if (!parsedArgs.isObject()) return R"({"ok":false,"error":"Arguments must be an object."})";
        if (name == "cad_ui")
        {
            if (parsedArgs.getProperty("action", "").toString() == "camera" && (int)parsedArgs.getProperty("view", -1) >= 0)
                return juce::JSON::toString(safeThis->viewsPanel->controlViewCamera((int)parsedArgs["view"], parsedArgs)).toStdString();
            return juce::JSON::toString(safeThis->cadWorkspace->automation(parsedArgs)).toStdString();
        }
        if(name=="cad_sweep"||name=="cad_loft"||name=="cad_mill")
        {
            const juce::String type=name=="cad_sweep"?"sweep":name=="cad_loft"?"loft":"mill";
            return juce::JSON::toString(safeThis->cadService->modelingCommand(type,parsedArgs),false,17).toStdString();
        }
        if(name=="cad_mirror"||name=="cad_linear_pattern"||name=="cad_circular_pattern")
        {
            const juce::String type=name=="cad_mirror"?"mirror":name=="cad_linear_pattern"?"linearPattern":"circularPattern";
            const auto result=safeThis->cadService->replicationCommand(type,parsedArgs);return juce::JSON::toString(result,false,17).toStdString();
        }
        if (name == "cad_revolve" || name == "cad_lathe" || name == "cad_chamfer" || name == "cad_hole")
        {
            auto result = name == "cad_hole" ? safeThis->cadService->holeCommand(parsedArgs) : name == "cad_chamfer" ? safeThis->cadService->chamferCommand(parsedArgs) : name == "cad_lathe" ? safeThis->cadService->latheCommand(parsedArgs) : safeThis->cadService->revolveCommand(parsedArgs);
            result.getDynamicObject()->setProperty("state", safeThis->cadWorkspace->automationState());
            return juce::JSON::toString(result, false, 17).toStdString();
        }
        if (name == "cad_workspace") return runWorkspaceCommand(safeThis, arguments);
        if (name == "cad_check_interference")
        {
            if ((bool) parsedArgs.getProperty("clear", false))
            { safeThis->cadWorkspace->clearInterference(); return std::string(R"({"ok":true,"cleared":true})"); }
            const auto pose = parsedArgs.getProperty("pose", "shown").toString();
            if (pose != "shown" && pose != "reference") return std::string(R"({"ok":false,"error":"pose must be shown or reference."})");
            return juce::JSON::toString(safeThis->cadWorkspace->checkInterference(pose == "reference")).toStdString();
        }
        if (name == "cad_export") return runExportCommand(safeThis, arguments);
        if (name == "cad_list_faces" || name == "cad_create_face_sketch") return runFaceCommand(safeThis, name, arguments);
        if (name == "cad_component" || name == "cad_fixture" || name == "cad_catalog" || name == "cad_stock" || name.rfind("cad_material_", 0) == 0 || name.rfind("cad_analysis_", 0) == 0) return runEngineeringCommand(safeThis, name, arguments);
        if (const auto ik = ikCommandForTool(name); ik.isNotEmpty()) return runIkCommand(safeThis, ik, arguments);
        if (name == "cad_motion_status")
        {
            auto result = juce::JSON::parse(safeThis->motionLink->statusBoard()->get());
            if (result.isObject()) result.getDynamicObject()->setProperty("ok", true);
            return juce::JSON::toString(result).toStdString();
        }
        bool ok = true; juce::String error, id;
            if (name == "open_cad_workspace") safeThis->selectWorkspace(0);
            else if (name == "open_frust_editor") safeThis->selectWorkspace(1);
            else if (name == "open_node_editor") safeThis->selectWorkspace(2);
            else if (name == "open_3d_views") { safeThis->selectWorkspace(3); safeThis->viewsPanel->addView(); }
            else if (name.rfind("cad_", 0) == 0)
            {
                const auto parsed = juce::JSON::parse(arguments);
                auto number = [&](const char* key, double fallback = 0.0) { return parsed.getProperty(key, fallback).toString().getDoubleValue(); };
                if (name == "cad_create_box") ok = safeThis->cadService->createBox(number("width"), number("depth"), number("height"), id, error);
                else if (name == "cad_create_cylinder") ok = safeThis->cadService->createCylinder(number("radius"), number("height"), id, error);
                else if (name == "cad_create_sketch") ok = safeThis->cadService->createSketch(parsed.getProperty("plane", "XY").toString(), number("offset"), id, error);
                else if (name == "cad_sketch_line") ok = safeThis->cadService->addSketchLine(parsed.getProperty("sketch", "").toString(), number("x1"), number("y1"), number("x2"), number("y2"), id, error);
                else if (name == "cad_sketch_rectangle") ok = safeThis->cadService->addSketchRectangle(parsed.getProperty("sketch", "").toString(), number("x"), number("y"), number("width"), number("height"), id, error);
                else if (name == "cad_sketch_circle") ok = safeThis->cadService->addSketchCircle(parsed.getProperty("sketch", "").toString(), number("x"), number("y"), number("radius"), id, error);
                else if (name == "cad_sketch_dimension") ok = safeThis->cadService->setSketchEntityDimension(parsed.getProperty("sketch", "").toString(), parsed.getProperty("entity", "").toString(), number("value"), error);
                else if (name == "cad_sketch_position") ok = safeThis->cadService->setSketchEntityPosition(parsed.getProperty("sketch", "").toString(), parsed.getProperty("entity", "").toString(), number("x"), number("y"), error);
                else if (name == "cad_sketch_constraint") ok = safeThis->cadService->addSketchConstraint(parsed.getProperty("sketch", "").toString(), parsed.getProperty("type", "").toString(), parsed.getProperty("first", "").toString(), parsed.getProperty("second", "").toString(), number("value"), id, error);
                else if (name == "cad_extrude") ok = safeThis->cadService->createExtrude(parsed.getProperty("sketch", "").toString(), number("depth"), static_cast<bool>(parsed.getProperty("reverse", false)), id, error);
                else if (name == "cad_cut") ok = safeThis->cadService->createCut(parsed.getProperty("sketch", "").toString(), parsed.getProperty("target", "").toString(), number("depth"), static_cast<bool>(parsed.getProperty("throughAll", false)), static_cast<bool>(parsed.getProperty("reverse", false)), id, error);
                else if (name == "cad_fillet")
                {
                    std::vector<int> edgeIndices;
                    if (const auto* arr = parsed.getProperty("edges", {}).getArray())
                        for (const auto& item : *arr) edgeIndices.push_back((int) item);
                    ok = safeThis->cadService->createFillet(parsed.getProperty("target", "").toString(), edgeIndices, number("radius"), id, error);
                }
                else if (name == "cad_set_dimensions") ok = safeThis->cadService->setDimensions(parsed.getProperty("id", "").toString(), number("width"), number("depth"), number("height"), number("radius"), error);
                else if (name == "cad_set_placement") ok = safeThis->cadService->setPlacement(parsed.getProperty("id", "").toString(), number("x"), number("y"), number("z"), number("rx"), number("ry"), number("rz"), error);
                else if (name == "cad_boolean") ok = safeThis->cadService->booleanOp(parsed.getProperty("operation", "").toString(), parsed.getProperty("first", "").toString(), parsed.getProperty("second", "").toString(), id, error);
                else if (name == "cad_sketch_bezier") ok = safeThis->cadService->addSketchBezier(parsed.getProperty("sketch", "").toString(), number("x1"), number("y1"), number("cx1"), number("cy1"), number("cx2"), number("cy2"), number("x2"), number("y2"), id, error, parsed.getProperty("startConnection", "").toString(), parsed.getProperty("endConnection", "").toString());
                else if (name == "cad_sketch_move_point") ok = safeThis->cadService->moveSketchPoint(parsed.getProperty("sketch", "").toString(), parsed.getProperty("point", "").toString(), number("x"), number("y"), error);
                else if (name == "cad_sketch_insert_vertex") { juce::StringArray removed; ok = safeThis->cadService->insertSketchVertex(parsed.getProperty("sketch", "").toString(), parsed.getProperty("line", "").toString(), number("x"), number("y"), id, removed, error); }
                else if (name == "cad_sketch_smooth") ok = safeThis->cadService->smoothSketchJoint(parsed.getProperty("sketch", "").toString(), parsed.getProperty("curve", "").toString(), parsed.getProperty("end", "").toString(), error);
                else if (name == "cad_undo") ok = safeThis->cadService->undo(error);
                else if (name == "cad_redo") ok = safeThis->cadService->redo(error);
                else if (name == "cad_create_instance") ok = safeThis->cadService->createInstance(parsed.getProperty("feature", "").toString(), parsed.getProperty("name", "").toString(), id, error);
                else if (name == "cad_create_joint" || name == "cad_update_joint")
                {
                    djehuti::cad::CadJointRequest request;
                    auto vector = [&](const char* key, int index, double fallback) { const auto* list = parsed.getProperty(key, {}).getArray(); return list != nullptr && list->size() == 3 ? (double) (*list)[index] : fallback; };
                    request.name = parsed.getProperty("name", "").toString(); request.type = parsed.getProperty("type", "").toString();
                    request.parentInstance = parsed.getProperty("parent", "").toString(); if (request.parentInstance == "world") request.parentInstance.clear();
                    request.childInstance = parsed.getProperty("child", "").toString();
                    request.originX = vector("origin", 0, 0.0); request.originY = vector("origin", 1, 0.0); request.originZ = vector("origin", 2, 0.0);
                    request.axisX = vector("axis", 0, 0.0); request.axisY = vector("axis", 1, 0.0); request.axisZ = vector("axis", 2, 1.0);
                    request.reference = number("reference"); request.hasLimits = parsed.hasProperty("lower") && parsed.hasProperty("upper"); request.lower = number("lower"); request.upper = number("upper");
                    if (name == "cad_create_joint") ok = safeThis->cadService->createJoint(request, id, error);
                    else ok = safeThis->cadService->updateJoint(parsed.getProperty("joint", "").toString(), request, error);
                }
                else if (name == "cad_set_joint_mapping")
                {
                    djehuti::cad::CadSignalMapping mapping;
                    mapping.jointId = parsed.getProperty("joint", "").toString(); mapping.signalId = parsed.getProperty("signal", "").toString();
                    mapping.sourceQuantity = parsed.getProperty("quantity", "").toString(); mapping.sourceUnit = parsed.getProperty("unit", "").toString();
                    mapping.scale = number("scale", 1.0); mapping.offset = number("offset"); mapping.initialCoordinate = number("initialCoordinate");
                    ok = safeThis->cadService->setJointMapping(mapping, error);
                }
                else if (name == "cad_set_instance_grounded") ok = safeThis->cadService->setInstanceGrounded(parsed.getProperty("instance", "").toString(), static_cast<bool>(parsed.getProperty("grounded", true)), error);
                else if (name == "cad_set_instance_placement")
                {
                    auto vector = [&](const char* key, int index) { const auto* list = parsed.getProperty(key, {}).getArray(); return list != nullptr && list->size() == 3 ? (double) (*list)[index] : 0.0; };
                    const auto frame = djehuti::cad::mech::frameFromEulerDegrees(vector("origin", 0), vector("origin", 1), vector("origin", 2), vector("rotationDeg", 0), vector("rotationDeg", 1), vector("rotationDeg", 2));
                    ok = safeThis->cadService->setInstancePlacement(parsed.getProperty("instance", "").toString(), frame, error);
                }
                else if (name == "cad_set_joint_role")
                    ok = safeThis->cadService->setJointRole(parsed.getProperty("joint", "").toString(), parsed.getProperty("role", "").toString(), number("lockValue"), static_cast<bool>(parsed.getProperty("enforceLimits", false)), error);
                else if (name == "cad_create_coupling")
                {
                    djehuti::cad::CadCoupling coupling;
                    coupling.name = parsed.getProperty("name", "").toString(); coupling.type = parsed.getProperty("type", "").toString();
                    coupling.jointA = parsed.getProperty("jointA", "").toString(); coupling.jointB = parsed.getProperty("jointB", "").toString();
                    coupling.ratio = number("ratio", 1.0); coupling.pitchRadius = number("pitchRadius", 0.01); coupling.lead = number("lead", 0.001); coupling.offset = number("offset");
                    ok = safeThis->cadService->createCoupling(coupling, id, error);
                }
                else if (name == "cad_remove_coupling") ok = safeThis->cadService->removeCoupling(parsed.getProperty("coupling", "").toString(), error);
                else if (name == "cad_motion_preview")
                {
                    if (static_cast<bool>(parsed.getProperty("reset", false))) safeThis->motionRuntime->resetToReference();
                    else ok = safeThis->motionRuntime->setPreviewCoordinate(parsed.getProperty("joint", "").toString(), number("value"), error);
                }
                else if (name == "cad_motion_lifecycle")
                {
                    const auto command = parsed.getProperty("command", "").toString();
                    if (command == "retry") ok = safeThis->motionLink->retryLifecycle(error);
                    else ok = safeThis->motionLink->requestLifecycle(command, (std::int64_t) std::llround(number("stepMs", 10.0) * 1.0e9), error);
                }
                else if (name == "cad_motion_listen")
                {
                    if (static_cast<bool>(parsed.getProperty("listen", true))) ok = safeThis->motionLink->startListening(error);
                    else safeThis->motionLink->stopListening();
                }
                else { ok = false; error = "Unknown CAD tool."; }
                if (safeThis->cadWorkspace != nullptr) safeThis->cadWorkspace->repaint();
            }
            else { ok = false; error = "Unknown tool."; }

        auto* result = new juce::DynamicObject(); result->setProperty("ok", ok && error.isEmpty());
        if (error.isNotEmpty()) result->setProperty("error", error);
        if (id.isNotEmpty()) result->setProperty("id", id);
        result->setProperty("state", safeThis->cadWorkspace->automationState());
        return juce::JSON::toString(juce::var(result)).toStdString();
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) return execute();
    struct Pending { juce::WaitableEvent done; std::string result; };
    auto pending = std::make_shared<Pending>();
    juce::MessageManager::callAsync([execute, pending] { pending->result = execute(); pending->done.signal(); });
    if (keepWaiting)
    {
        while (!pending->done.wait(200))
            if (!keepWaiting()) return R"({"ok":false,"status":"unknown","error":"DjehutiCAD is shutting down; the command's outcome was not observed."})";
    }
    else if (!pending->done.wait(120000))
        return R"({"ok":false,"status":"pending","error":"The command has not finished after 120 s and may still complete; inspect CAD state before issuing another change."})";
    return pending->result;
}

DjehutiCAD::DjehutiCAD(bool diagnostic) : diagnosticMode(diagnostic)
{
    juce::PropertiesFile::Options storage;
    storage.applicationName = "DjehutiCAD";
    storage.filenameSuffix = ".settings";
    storage.folderName = "DjehutiCAD";
    properties.setStorageParameters(storage);
    if (auto* settings = properties.getUserSettings())
        settings->setValue("application", "DjehutiCAD");

    if (!diagnosticMode) authSession.loadFromDisk();
    profileAvatar.onSessionChanged = [this] { repaint(); };

    addAndMakeVisible(menuBar);
    addAndMakeVisible(profileAvatar);
    dockManager = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager);
    cadService = std::make_shared<djehuti::cad::CadCommandService>();
    fixtureService = std::make_shared<djehuti::cad::CadFixtureService>();
    frust_engine::setCadCommandService(cadService);
    // One runtime pose shared by every view, and the DEWB motion endpoint that can drive it.
    motionRuntime = std::make_shared<djehuti::cad::CadMotionRuntime>(cadService);
    motionLink = std::make_unique<djehuti::motion::MotionLink>(cadService, motionRuntime);
    {
        const auto board = motionLink->statusBoard();
        const juce::Component::SafePointer<DjehutiCAD> safe(this);
        frust_engine::setCadMotionHooks([board] { return board->get().toStdString(); },
                                        [safe](const std::string& joint, double value) { juce::MessageManager::callAsync([safe, joint, value] { if (safe != nullptr && safe->motionRuntime != nullptr) { juce::String e; safe->motionRuntime->setPreviewCoordinate(joint, value, e); } }); },
                                        [safe](const std::string& command, double stepMs) { juce::MessageManager::callAsync([safe, command, stepMs] { if (safe != nullptr && safe->motionLink != nullptr) { juce::String e; safe->motionLink->requestLifecycle(command, (std::int64_t) std::llround(stepMs * 1.0e9), e); } }); });
        frust_engine::setCadIkHook([safe](const std::string& command, const std::string& arguments) { return runIkCommand(safe, command, arguments); });
        frust_engine::setCadWorkspaceHook([safe](const std::string& args) { return runWorkspaceCommand(safe,args); });
        frust_engine::setCadExportHook([safe](const std::string& arguments) { return runExportCommand(safe, arguments); });
    }
    auto cadWorkspaceContent = std::make_unique<CadWorkspacePanel>(cadService);
    cadWorkspace = cadWorkspaceContent.get();
    dockManager->registerPanel("cad", "CAD Workspace", std::move(cadWorkspaceContent), CreationDock::DockTargetZone::CenterTab);
    cadWorkspace->onOpenViews = [this] { selectWorkspace(3); };
    auto craftContent = std::make_unique<CadWorkspacePanel>(cadService);
    craftWorkspace = craftContent.get();
    craftWorkspace->setMotion(motionRuntime, motionLink.get());
    craftWorkspace->onOpenViews = [this] { selectWorkspace(3); };
    dockManager->registerPanel("craft", "Craft Designer", std::move(craftContent), CreationDock::DockTargetZone::CenterTab);
    cadWorkspace->setMotion(motionRuntime, motionLink.get());
    auto frustPanelContent = std::make_unique<FrustPanel>(!diagnosticMode);
    frustPanel = frustPanelContent.get();
    dockManager->registerPanel("frust", "FRust", std::move(frustPanelContent), CreationDock::DockTargetZone::CenterTab);
    auto nodePanelContent = std::make_unique<NodeDesignerPanel>();
    nodePanel = nodePanelContent.get();
    nodePanel->onRunRequested = [this](const juce::String& script, const juce::String& label) {
        selectWorkspace(1);
        std::string error;
        if (!frustPanel->start(script, label, error))
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "FRust", error);
    };
    dockManager->registerPanel("nodes", "Node Editor", std::move(nodePanelContent), CreationDock::DockTargetZone::CenterTab);
    auto viewsPanelContent = std::make_unique<ThreeDViewsPanel>(cadService);
    viewsPanel = viewsPanelContent.get();
    { auto runtime = motionRuntime; viewsPanel->setPoseSource([runtime] { return runtime->pose(); }); }
    {
        // One body/face/edge selection shared by the model view and every 3D view.
        auto selection = std::make_shared<djehuti::cad::CadSelection>();
        cadWorkspace->setSelection(selection);
        craftWorkspace->setSelection(selection);
        viewsPanel->setSelection(selection);
    }
    dockManager->registerPanel("views", "3D Views", std::move(viewsPanelContent), CreationDock::DockTargetZone::CenterTab);
    const juce::Component::SafePointer<DjehutiCAD> safeThis(this);
    auto agentPanelContent = std::make_unique<DjehutiAgentPanel>(properties, [safeThis](const std::string& name, const std::string& arguments, std::shared_ptr<DjehutiAgentRequestState> state) {
        if (state == nullptr || state->cancelled || !state->alive) return std::string(R"({"ok":false,"error":"Request cancelled."})");
        return runToolCommand(safeThis, name, arguments);
    });
    agentPanel = agentPanelContent.get();
    dockManager->registerPanel("agent", "Agent", std::move(agentPanelContent), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("toolbox", "Toolbox", std::make_unique<CadToolbox>(*cadWorkspace), CreationDock::DockTargetZone::Left);
    auto fixtureContent = std::make_unique<CadFixturesPanel>(fixtureService, cadService);
    fixturesPanel = fixtureContent.get();
    fixturesPanel->onOpenSource = [this](const juce::File& file)
    {
        auto* args = new juce::DynamicObject(); args->setProperty("action", "document"); args->setProperty("operation", "open"); args->setProperty("path", file.getFullPathName());
        const auto reply = cadWorkspace->automation(juce::var(args));
        if (!(bool)reply["ok"]) juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Open source CAD",reply["error"].toString());
        else selectWorkspace(0);
    };
    dockManager->registerPanel("fixtures", "Test Fixtures", std::move(fixtureContent), CreationDock::DockTargetZone::CenterTab);
    auto browserContent=std::make_unique<CadComponentBrowser>(cadService);componentBrowser=browserContent.get();
    componentBrowser->selectedPart=[this]{auto id=cadWorkspace->automationState()["selected"].toString();if(const auto* instance=cadService->document().mechanicsData().findInstance(id))id=instance->featureId;return id;};
    componentBrowser->onInserted=[this](const juce::String& id){auto args=djehuti::cad::components::object();djehuti::cad::components::put(args,"action","select");djehuti::cad::components::put(args,"id",id);cadWorkspace->automation(args);};
    dockManager->registerPanel("components","Component Library",std::move(browserContent),CreationDock::DockTargetZone::CenterTab);
    auto craftStockContent=std::make_unique<CadCraftBrowser>(cadService);craftBrowser=craftStockContent.get();
    craftBrowser->selectedOccurrence=[this]{return craftWorkspace->automationState()["selected"].toString();};
    craftBrowser->setOverlaysCallback=[this](std::vector<djehuti::cad::CadOverlayLine> lines){ if(craftWorkspace) craftWorkspace->setInspectionLines(std::move(lines)); };
    craftBrowser->onInserted=[this](const juce::String& id){auto a=djehuti::cad::components::object();djehuti::cad::components::put(a,"action","select");djehuti::cad::components::put(a,"id",id);craftWorkspace->automation(a);};
    craftBrowser->onMaterials=[this]{craftWorkspace->showMaterialsManager();};
    dockManager->registerPanel("craftStock","Craft Stock",std::move(craftStockContent),CreationDock::DockTargetZone::Right);
    const auto bindDocking = [this](WorkspaceMenu& menu, const juce::String& id)
    {
        menu.changeDocking = [this,id](bool floating)
        {
            if (auto* panel = dockManager->findPanelById(id))
                if (floating) dockManager->floatPanel(panel); else dockManager->dockPanel(panel, CreationDock::DockTargetZone::CenterTab);
        };
        menu.refresh();
    };
    bindDocking(cadWorkspace->workspaceMenu,"cad"); bindDocking(frustPanel->workspaceMenu,"frust");
    bindDocking(nodePanel->workspaceMenu,"nodes"); bindDocking(viewsPanel->workspaceMenu,"views");
    bindDocking(fixturesPanel->workspaceMenu,"fixtures");
    bindDocking(componentBrowser->workspaceMenu,"components");
    bindDocking(craftWorkspace->workspaceMenu,"craft");bindDocking(craftBrowser->workspaceMenu,"craftStock");
    viewsPanel->setContextCommand([safe = juce::Component::SafePointer<CadWorkspacePanel>(cadWorkspace)](const juce::String& action, const djehuti::cad::CadPick& pick)
    { if (safe != nullptr) safe->viewContextCommand(action,pick); });
    dockManager->activatePanel("cad"); defaultDockLayout = dockManager->captureLayout();
    defaultDockLayout.getDynamicObject()->setProperty("leftWidth",176); dockManager->applyLayout(defaultDockLayout);
    if (!diagnosticMode)
    {
        restoreSettings();
        localApi = std::make_unique<LocalAgentApi>([safeThis](const juce::String& name, const juce::var& args, std::function<bool()> keepWaiting) {
            return juce::JSON::parse(juce::String(runToolCommand(safeThis, name.toStdString(), juce::JSON::toString(args), std::move(keepWaiting))));
        }, DjehutiAgentPanel::toolCatalogue());
        if (!localApi->start()) juce::Logger::writeToLog("DjehutiCAD localhost API could not start.");
    }
}

DjehutiCAD::~DjehutiCAD()
{
    localApi.reset();
    saveSettings();
    authSession.cancelLogin();
    dockManager.reset();
    cadWorkspace = nullptr; nodePanel = nullptr; frustPanel = nullptr; agentPanel = nullptr; viewsPanel = nullptr; fixturesPanel = nullptr; componentBrowser = nullptr;
    fixtureService.reset();
    frust_engine::setCadMotionHooks(nullptr, nullptr, nullptr);
    frust_engine::setCadIkHook(nullptr);
    frust_engine::setCadWorkspaceHook(nullptr);
    frust_engine::setCadExportHook(nullptr);
    motionLink.reset();
    motionRuntime.reset();
    frust_engine::setCadCommandService(nullptr);
    cadService.reset();
}

void DjehutiCAD::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff171b20));
}

void DjehutiCAD::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(42);
    menuBar.setBounds(header.removeFromLeft(420));
    profileAvatar.setBounds(header.removeFromRight(profileAvatar.getPreferredWidth()).reduced(4, 5));
    if (dockManager != nullptr) dockManager->setBounds(area);
}

void DjehutiCAD::selectWorkspace(int tabIndex)
{
    const juce::StringArray ids { "cad","frust","nodes","views","agent","fixtures","components","craft","craftStock" };
    if (dockManager != nullptr && juce::isPositiveAndBelow(tabIndex,ids.size())) { dockManager->showPanel(ids[tabIndex]); dockManager->activatePanel(ids[tabIndex]); }
    if (dockManager != nullptr && tabIndex == 7) dockManager->showPanel("craftStock");
}

void DjehutiCAD::confirmCloseThen(std::function<void()> closeCallback)
{
    saveSettings();
    if (closeCallback) { if (fixturesPanel != nullptr) fixturesPanel->confirmClose(std::move(closeCallback)); else closeCallback(); }
}

juce::StringArray DjehutiCAD::getMenuBarNames()
{
    return { "File", "Window", "Settings", "Account", "Help" };
}

juce::PopupMenu DjehutiCAD::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu menu;
    if (index == 0)
    {
        menu.addItem(kNewCadDocument, "New CAD Document");
        menu.addItem(kOpenCadDocument, "Open CAD Document...");
        menu.addItem(kSaveCadDocument, "Save CAD Document");
        menu.addItem(kSaveCadDocumentAs, "Save CAD Document As...");
        menu.addSeparator();
        menu.addItem(111, "New Test Fixture");
        menu.addItem(112, "Open Test Fixture...");
        menu.addSeparator();
        menu.addItem(kExportCad, "Export... (3MF, STL, STEP)", cadService != nullptr && !cadService->document().features().empty());
        menu.addSeparator();
        menu.addItem(10, "Close");
    }
    else if (index == 1)
    {
        menu.addItem(kCadWorkspace, "CAD Workspace");
        menu.addItem(kFrustEditor, "FRust Editor");
        menu.addItem(kNodeEditor, "Node Editor");
        menu.addItem(kAgent, "BYOK Agent");
        menu.addItem(kThreeDViews, "3D Views");
        menu.addItem(113, "Test Fixtures");
        menu.addItem(114, "Component Library");
        menu.addItem(115, "Craft Designer");
        menu.addItem(116, "Craft Stock browser");
        menu.addSeparator();
        menu.addItem(500, "Toolbox", true, dockManager->isPanelOpen("toolbox"));
        menu.addItem(501, "Restore default workspace layout");
    }
    else if (index == 2)
    {
        menu.addItem(kAgentSettings, "Agent Settings...");
    }
    else if (index == 3)
    {
        menu.addItem(20, authSession.hasValidSession() ? "Account Information" : "Sign In");
        if (authSession.hasValidSession())
            menu.addItem(21, "Sign Out");
    }
    else
    {
        menu.addItem(31, "CAD Workspace Guide");
        menu.addItem(32, "Local Automation API");
        menu.addItem(30, "About DjehutiCAD");
    }
    return menu;
}

void DjehutiCAD::menuItemSelected(int commandId, int)
{
    if (commandId == kNewCadDocument)
        cadWorkspace->newDocument();
    else if (commandId == kOpenCadDocument)
        cadWorkspace->openDocument();
    else if (commandId == kSaveCadDocument)
        cadWorkspace->saveDocument();
    else if (commandId == kSaveCadDocumentAs)
        cadWorkspace->saveDocumentAs();
    else if (commandId == kExportCad)
        cadWorkspace->showExportDialog();
    else if (commandId == 111) { selectWorkspace(5); fixturesPanel->createFixture(); }
    else if (commandId == 112) { selectWorkspace(5); fixturesPanel->openFixture(); }
    else if (commandId == 113) selectWorkspace(5);
    else if (commandId == 114) selectWorkspace(6);
    else if (commandId == 115) selectWorkspace(7);
    else if (commandId == 116) selectWorkspace(8);
    else if (commandId == kAgentSettings)
    {
        auto content = new DjehutiAgentSettings(properties, [] {});
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(content); options.dialogTitle = "Agent Settings"; options.dialogBackgroundColour = juce::Colour(0xff171b20);
        options.escapeKeyTriggersCloseButton = true; options.useNativeTitleBar = true; options.resizable = false;
        content->setSize(480, 430); options.launchAsync();
    }
    else if (commandId == 500) dockManager->togglePanel("toolbox");
    else if (commandId == 501) applyWorkspaceLayout(defaultDockLayout);
    else if (commandId == 10) { if (auto* app = juce::JUCEApplication::getInstance()) app->systemRequestedQuit(); }
    else if (commandId == kThreeDViews) selectWorkspace(3);
    else if (commandId == kCadWorkspace)
        selectWorkspace(0);
    else if (commandId == kFrustEditor)
        selectWorkspace(1);
    else if (commandId == kNodeEditor)
        selectWorkspace(2);
    else if (commandId == kAgent)
        selectWorkspace(4);
    else if (commandId == 20)
    {
        if (authSession.hasValidSession())
            profileAvatar.showAccountInfoWindow();
        else
            authSession.beginLogin();
    }
    else if (commandId == 21)
        authSession.clearSession();
    else if (commandId == 32)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Local Automation API",
            "Local tools operate the same CAD document and visible task controls. No model provider is required.\n\nDiscovery: " + LocalAgentApi::getDiscoveryFile().getFullPathName()
            + "\n\nGET /v1/status, /v1/tools and /v1/state. POST /v1/tools/call. Authenticate with the discovery file's bearer token; keep it private.\n\nUse cad_ui to inspect the task, set fields, click Apply, draw sketch previews and move cameras. Requests execute one action at a time; the client controls pauses for narration. See docs/LOCAL_AUTOMATION_API.md.");
    else if (commandId == 31)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "CAD Workspace Guide",
                                               "Workspace menus and tools:\nEach workspace owns its menus, including when floated. Window reopens panels and the dockable Toolbox. Choose Pointer, Box or Circle selection and the Body/Face/Edge filter in the Toolbox. Box left-to-right encloses; right-to-left and circle cross projected geometry, including obscured items. Right-click a 3D view for context actions and reference guides. View > Floor grid, World planes, Selected local planes and Joint frames show model/solved coordinates without editing the document. Each view keeps its own guide settings.\n\nSketch to solid:\n"
                                               "1. Create Sketch, choose the XY, XZ or YZ plane, then Create Sketch.\n"
                                               "2. Pick Line, Rectangle or Circle and drag in the canvas. Esc cancels a gesture; Select edits entities.\n"
                                               "3. Finish Sketch, keep the sketch selected, then Extrude (or Cut Sketch with a Target body).\n\n"
                                               "Turning, bevels and drilling (Modify menu):\n"
                                               "Virtual Lathe edits outside/bore station tables in axial mm and diameter mm; steps and grooves use repeated axial positions. It uses the existing editable parametric Revolve.\n"
                                               "Chamfer: click successive edges in Edge mode to add them, enter an equal distance and Preview before Apply. Select the result to edit its distance or edges.\n"
                                               "Drill Press / Hole: pick a planar face, set its local U/V centre and diameter, then blind depth or Through all. Plain, Counterbore and Countersink types are editable from the resulting body's inspector.\n\n"
                                               "Mirror and patterns: choose sources, Bodies or Modeling feature mode, a plane/direction/axis, and distribution. Hole/Cut/Union/Subtract feature mode modifies the intended target. Preview, then Apply. Select the result and Edit sources and parameters to revise it.\n\n"
                                               "Sweep: select a closed profile and connected sketch/edge path, orientation and operation; controlled twist uses straight paths. Loft: select ordered closed sketch sections, smooth/ruled and correspondence. Virtual Router / Mill: select target and accessible entry plane, a pocket region or slot/profile path, diameter and blind/through depth. All provide Preview and editable source references.\n\n"
                                               "Subtract:\n"
                                               "Choose Subtract, pick the Target (body to keep) and the Tool (cutting body) in the tree or model view, then press Subtract. "
                                               "Both sources stay in the history, hidden; Undo restores them.\n\n"
                                               "Model view: drag to orbit, Shift or right-drag to pan, wheel to zoom, click to select a body.\n\n"
                                               "Motion:\n"
                                               "1. Instance: pick a solid body to create a movable assembly instance.\n"
                                               "2. Joint: choose the parent (or World) and child instances, the type (fixed, revolute about +Z, prismatic along +Z) and the joint frame.\n"
                                               "3. Select the joint to preview a coordinate (runtime only, never saved) or map a DEWB signal to it.\n"
                                               "4. Motion Link: Start Listening and give DEWB the pipe path; DEWB validates the profile, synchronizes, then streams motion.\n"
                                               "5. Mechanisms: ground at least one instance (or joint parts to the world), give each joint a role (driver = input, dependent = solved, locked = held), add Couplings for gears, linear ratios, rack and pinion or screws; closed loops such as slider-cranks are allowed. Preview a driver and the dependent joints follow; the status shows remaining freedom and constraint errors.\n"
                                               "6. Simulation controls in the Motion Link panel (Start/Resume, Pause, Stop Simulation, Reset, Single Step) ask DEWB to change its run state; they need a DEWB that advertises lifecycle control, and the model moves only when DEWB reports the new state.\n"
                                               "6a. Faces and edges: the Body, Face and Edge buttons choose what a click in a 3-D view picks. Hover highlights in light cyan; the selection is magenta in every view and is described in the right panel. A planar face with a persistent name (e.g. box-1/top) can carry a sketch: select it and press Create Sketch. The sketch lies in the face, follows it when the body's dimensions change, and an edit that removes or splits that face is refused instead of moving the sketch. Cuts from a face sketch go into the body unless reversed.\n"
                                               "7. Inverse Kinematics: choose Assembly > Inverse kinematics, then choose the driver joints the solve may adjust, then add targets: the instance carrying the endpoint, a tool frame on it (mm, degrees), the reference frame (World or another instance), the mode (position, orientation or full pose) and the desired pose. Capture Current Pose copies the shown pose without moving anything. Solve moves only the selected drivers; dependent joints follow the mechanism, locked joints and grounding hold, hard bounds are respected. A result is shown only if every constraint and target holds; otherwise the last valid pose stays and the target errors are listed. Drag Target moves a target's position on the shown plane (the yellow diamond). Save Task stores the definition (undoable); solving never changes the document. Local IK is unavailable while DEWB drives the mechanism.\n"
                                               "7a. Interference: Assembly > Check interference inspects the pose shown (motion previews and IK results included). Interfering parts turn red and the shared volume is outlined in yellow; the right panel lists each pair with its common volume. Touching faces are contact, not interference. Moving the mechanism or editing the model marks the result out of date; Check again or Clear.\n"
                                               "8. Export: File > Export..., or right-click a body or instance in the tree. Choose 3MF (preferred for 3-D printing: millimetres, one named object per part), binary STL (millimetres; one file per part) or STEP AP214 (exact solids with names and placements). Tick the parts; all ticked exports the visible model, with instanced bodies exported as their instances and construction operands left out. Placements are the authored reference configuration, never a simulation or IK pose. Mesh quality: Standard 0.05 mm / 15 deg, Fine 0.01 mm / 5 deg, or Custom. Solids and meshes are checked before writing; a failed or cancelled export leaves existing files untouched, and export never changes the document.");
    else if (commandId == 30)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "About DjehutiCAD",
                                               "DjehutiCAD 0.1.0\nA FRust-programmable CAD foundation.");
}

void DjehutiCAD::applyWorkspaceLayout(const juce::var& saved)
{
    if (!saved.isObject()) return;
    auto layout = saved.clone();
    const auto zones = layout.getProperty("zones", {});
    juce::StringArray visible;
    for (const auto& zone : juce::StringArray { "left", "right", "bottom", "center" })
        if (const auto* ids = zones.getProperty(zone, {}).getProperty("panels", {}).getArray())
            for (const auto& id : *ids) visible.addIfNotAlreadyThere(id.toString());
    if (auto* floating = layout.getProperty("floating", {}).getArray())
        for (auto& entry : *floating)
        {
            visible.addIfNotAlreadyThere(entry.getProperty("id", "").toString());
            auto* object = entry.getDynamicObject();
            if (object == nullptr) continue;
            juce::Rectangle<int> bounds((int)entry.getProperty("x",100), (int)entry.getProperty("y",100),
                                        (int)entry.getProperty("w",600), (int)entry.getProperty("h",450));
            const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds);
            if (display == nullptr || display->userArea.isEmpty()) continue;
            const auto area = display->userArea;
            bounds.setSize(juce::jlimit(1,area.getWidth(),juce::jmax(200,bounds.getWidth())),
                           juce::jlimit(1,area.getHeight(),juce::jmax(150,bounds.getHeight())));
            bounds.setPosition(juce::jlimit(area.getX(),area.getRight()-bounds.getWidth(),bounds.getX()),
                               juce::jlimit(area.getY(),area.getBottom()-bounds.getHeight(),bounds.getY()));
            object->setProperty("x",bounds.getX()); object->setProperty("y",bounds.getY());
            object->setProperty("w",bounds.getWidth()); object->setProperty("h",bounds.getHeight());
        }
    // CreationDock moves the listed panels; omitted panels must explicitly stay hidden on restore.
    for (const auto& id : juce::StringArray { "cad", "frust", "nodes", "views", "agent", "toolbox", "fixtures", "components", "craft", "craftStock" })
        if (!visible.contains(id)) dockManager->hidePanel(id);
    dockManager->applyLayout(layout);
}

void DjehutiCAD::restoreSettings()
{
    const auto savedDock = juce::JSON::parse(properties.getUserSettings()->getValue("workspaceDockLayout"));
    if (savedDock.isObject()) applyWorkspaceLayout(savedDock);
    else selectWorkspace(juce::jlimit(0, 8, properties.getUserSettings()->getIntValue("selectedTab",0)));
    cadWorkspace->restoreViewState(properties.getUserSettings()->getValue("cadReferenceGuides"));
    viewsPanel->restoreViewState(properties.getUserSettings()->getValue("multiReferenceGuides"));
    cadWorkspace->restoreLayoutState(properties.getUserSettings()->getValue("cadWorkspaceLayout"));
    craftWorkspace->restoreViewState(properties.getUserSettings()->getValue("craftReferenceGuides"));
    craftWorkspace->restoreLayoutState(properties.getUserSettings()->getValue("craftWorkspaceLayout"));
}

void DjehutiCAD::saveSettings()
{
    if (diagnosticMode) return;
    if (auto* settings = properties.getUserSettings())
    {
        if (dockManager != nullptr) settings->setValue("workspaceDockLayout", juce::JSON::toString(dockManager->captureLayout()));
        if (cadWorkspace != nullptr) settings->setValue("cadReferenceGuides",cadWorkspace->viewState());
        if (viewsPanel != nullptr) settings->setValue("multiReferenceGuides",viewsPanel->viewState());
        if (cadWorkspace != nullptr) settings->setValue("cadWorkspaceLayout", cadWorkspace->layoutState());
        if (craftWorkspace != nullptr) {settings->setValue("craftReferenceGuides",craftWorkspace->viewState());settings->setValue("craftWorkspaceLayout",craftWorkspace->layoutState());}
        settings->saveIfNeeded();
    }
}

std::string DjehutiCAD::runWorkspaceCommand(const juce::Component::SafePointer<DjehutiCAD>& safe,const juce::String& arguments)
{
    const auto execute=[safe,arguments]() -> std::string
    {
        auto out=std::make_unique<juce::DynamicObject>(); juce::String error;
        if(safe==nullptr || safe->cadWorkspace==nullptr) error="DjehutiCAD is closing.";
        auto args=juce::JSON::parse(arguments.isEmpty()?"{}":arguments);
        if(!args.isObject()) error="Arguments must be an object.";
        const auto action=args.getProperty("action","state").toString(); const auto workspace=args.getProperty("workspace","cad").toString();
        if(error.isEmpty())
        {
            const juce::StringArray ids{"cad","frust","nodes","views","agent","toolbox","fixtures","components","craft","craftStock"};
            if(!ids.contains(workspace)) error="Unknown workspace.";
            else if(action=="tool")
            {
                const auto tool=args.getProperty("tool","").toString();
                if(!juce::StringArray{"pointer","boxSelect","circleSelect","bodyMode","faceMode","edgeMode","sketchSelect","line","rectangle","circle"}.contains(tool)) error="Unknown selection or sketch tool.";
                else if(!safe->cadWorkspace->executeUiCommand(tool)) error="That tool is unavailable in the current editing mode.";
            }
            else if(action=="guides")
            {
                const auto values=args.getProperty("guides",{});
                if(!values.isObject()) error="guides must be an object of floor/world/local/joints booleans.";
                else
                {
                    for(const auto& field:values.getDynamicObject()->getProperties())
                        if(!juce::StringArray{"floor","world","local","joints"}.contains(field.name.toString()) || !field.value.isBool()) error="Unknown guide or non-boolean value.";
                    if(error.isEmpty())
                    {
                        if(workspace=="cad")
                        {
                            auto state=juce::JSON::parse(safe->cadWorkspace->viewState());
                            for(const auto& field:values.getDynamicObject()->getProperties()) state.getDynamicObject()->setProperty(field.name,field.value);
                            safe->cadWorkspace->restoreViewState(juce::JSON::toString(state));
                        }
                        else if(workspace=="views")
                        {
                            auto state=juce::JSON::parse(safe->viewsPanel->viewState()); auto* all=state.getArray(); const int view=(int)args.getProperty("view",-1);
                            if(all==nullptr || view < -1 || view>=all->size()) error="view must be -1 for all views or a valid zero-based view index.";
                            else { for(int i=0;i<all->size();++i) if(view<0 || view==i) for(const auto& field:values.getDynamicObject()->getProperties()) all->getReference(i).getDynamicObject()->setProperty(field.name,field.value); safe->viewsPanel->restoreViewState(juce::JSON::toString(state)); }
                        }
                        else error="Reference guides belong to cad or views.";
                    }
                }
            }
            else if(action=="ui")
                return juce::JSON::toString(safe->cadWorkspace->automation(args.getProperty("ui",{}))).toStdString();
            else if(action=="float" || action=="dock" || action=="activate")
            {
                auto* panel=safe->dockManager->findPanelById(workspace);
                if(panel==nullptr) error="Workspace unavailable.";
                else if(action=="float") safe->dockManager->floatPanel(panel);
                else if(action=="dock") safe->dockManager->dockPanel(panel,workspace=="toolbox"?CreationDock::DockTargetZone::Left:CreationDock::DockTargetZone::CenterTab);
                else { safe->dockManager->showPanel(workspace); safe->dockManager->activatePanel(workspace); }
            }
            else if(action!="state") error="Unknown workspace action.";
        }
        out->setProperty("ok",error.isEmpty()); if(error.isNotEmpty()) out->setProperty("error",error);
        if(safe!=nullptr && safe->cadWorkspace!=nullptr)
        {
            out->setProperty("cadGuides",juce::JSON::parse(safe->cadWorkspace->viewState()));
            out->setProperty("viewGuides",juce::JSON::parse(safe->viewsPanel->viewState()));
            out->setProperty("documentRevision",(juce::int64)safe->cadService->document().revision());
            auto tools=std::make_unique<juce::DynamicObject>(); for(const auto& name:juce::StringArray{"pointer","boxSelect","circleSelect","bodyMode","faceMode","edgeMode","sketchSelect","line","rectangle","circle"}) tools->setProperty(name,safe->cadWorkspace->uiToolActive(name));
            out->setProperty("activeTools",juce::var(tools.release()));
        }
        return juce::JSON::toString(juce::var(out.release())).toStdString();
    };
    if(juce::MessageManager::getInstance()->isThisTheMessageThread()) return execute();
    auto done=std::make_shared<juce::WaitableEvent>(); auto output=std::make_shared<std::string>();
    juce::MessageManager::callAsync([execute,done,output] { *output=execute(); done->signal(); });
    if(!done->wait(20000)) return R"({"ok":false,"error":"Workspace did not answer within 20 seconds."})";
    return *output;
}

juce::var DjehutiCAD::runWorkspaceCheck()
{
    auto root=std::make_unique<juce::DynamicObject>(); juce::Array<juce::var> checks; int failed=0;
    const auto record=[&](bool ok,const juce::String& name) { auto entry=std::make_unique<juce::DynamicObject>(); entry->setProperty("name",name); entry->setProperty("ok",ok); checks.add(juce::var(entry.release())); if(!ok) ++failed; };
    const auto revision=cadService->document().revision(); setSize(1280,800);
    const juce::Component::SafePointer<DjehutiCAD> safe(this);
    const auto command=[&](const char* json) { return juce::JSON::parse(juce::String(runWorkspaceCommand(safe,json))); };
    record(cadWorkspace->workspaceMenu.sections().size()==6,"CAD owns six local command menus");
    record(frustPanel->workspaceMenu.sections().size()==1 && nodePanel->workspaceMenu.sections().size()==2,"FRust and nodes own their command menus");
    record(cadWorkspace->executeUiCommand("boxSelect") && cadWorkspace->uiToolActive("boxSelect"),"Toolbox selects persistent box tool");
    record(cadWorkspace->executeUiCommand("circleSelect") && cadWorkspace->uiToolActive("circleSelect"),"Toolbox selects persistent circle tool");
    record(cadWorkspace->executeUiCommand("pointer") && cadWorkspace->uiToolActive("pointer"),"Pointer restored");
    record(!(bool)command(R"({"action":"tool","tool":"line"})").getProperty("ok",false),"Sketch drawing unavailable outside sketch mode");
    record((bool)command(R"({"action":"guides","guides":{"floor":false,"local":true,"joints":true}})").getProperty("ok",false),"Shared guide command accepted");
    auto status=command("{}"); record(!(bool)status["cadGuides"]["floor"] && (bool)status["cadGuides"]["local"] && (bool)status["cadGuides"]["joints"],"Guide state returned accurately");
    record(!(bool)command(R"({"action":"guides","guides":{"floor":"false"}})").getProperty("ok",false),"Invalid guide type rejected");
    record((bool)command(R"({"action":"guides","workspace":"views","view":1,"guides":{"world":true}})").getProperty("ok",false),"One 3D view can change independently");
    auto views=juce::JSON::parse(viewsPanel->viewState()); record(!(bool)views[0]["world"] && (bool)views[1]["world"],"Other view settings retained");
    record((bool)command(R"({"action":"float","workspace":"cad"})").getProperty("ok",false) && cadWorkspace->workspaceMenu.findParentComponentOfClass<juce::DocumentWindow>()!=nullptr,"Local CAD menu travels into floating workspace");
    record((bool)command(R"({"action":"dock","workspace":"cad"})").getProperty("ok",false) && cadWorkspace->workspaceMenu.findParentComponentOfClass<juce::DocumentWindow>()==nullptr,"CAD workspace docks again with menu intact");
    record((bool)command(R"({"action":"activate","workspace":"components"})").getProperty("ok",false)&&componentBrowser!=nullptr,"Component Library is registered and activated through CreationDock");
    record((bool)command(R"({"action":"float","workspace":"components"})").getProperty("ok",false)&&componentBrowser->workspaceMenu.findParentComponentOfClass<juce::DocumentWindow>()!=nullptr,"Component Library floats with its own local menu");
    record((bool)command(R"({"action":"dock","workspace":"components"})").getProperty("ok",false)&&componentBrowser->workspaceMenu.findParentComponentOfClass<juce::DocumentWindow>()==nullptr,"Component Library docks again without changing the CAD document");
    dockManager->hidePanel("toolbox"); const auto hiddenToolbox=dockManager->captureLayout();
    applyWorkspaceLayout(defaultDockLayout); applyWorkspaceLayout(hiddenToolbox);
    record(!dockManager->isPanelOpen("toolbox"),"Hidden Toolbox stays hidden when restoring the saved layout");
    applyWorkspaceLayout(defaultDockLayout);
    record(cadService->document().revision()==revision,"Tools, guides and docking do not alter the CAD document");
    record(std::find(frust_engine::cadHostFunctions().begin(),frust_engine::cadHostFunctions().end(),"djehuti_cad_workspace")!=frust_engine::cadHostFunctions().end(),"FRust host exposes shared workspace command");
    juce::String detail; const bool regionOk=runViewportInteractionCheck(detail);
    record(regionOk,"Production region selection and grid checks: "+detail);
    root->setProperty("ok",failed==0); root->setProperty("passed",checks.size()-failed); root->setProperty("failed",failed); root->setProperty("checks",checks);
    return juce::var(root.release());
}
