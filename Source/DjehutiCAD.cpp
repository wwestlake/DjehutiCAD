#include "DjehutiCAD.h"

#include "FrustPanel.h"
#include "NodeDesignerPanel.h"
#include "DjehutiAgent.h"
#include "Viewport3D.h"
#include "CadWorkspacePanel.h"
#include "FrustEngine.h"
#include "CadIkCommands.h"

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

DjehutiCAD::DjehutiCAD()
{
    juce::PropertiesFile::Options storage;
    storage.applicationName = "DjehutiCAD";
    storage.filenameSuffix = ".settings";
    storage.folderName = "DjehutiCAD";
    properties.setStorageParameters(storage);
    if (auto* settings = properties.getUserSettings())
        settings->setValue("application", "DjehutiCAD");

    authSession.loadFromDisk();
    profileAvatar.onSessionChanged = [this] { repaint(); };

    addAndMakeVisible(menuBar);
    addAndMakeVisible(profileAvatar);
    addAndMakeVisible(tabs);

    tabs.setTabBarDepth(36);
    cadService = std::make_shared<djehuti::cad::CadCommandService>();
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
    }
    cadWorkspace = std::make_unique<CadWorkspacePanel>(cadService);
    tabs.addTab("CAD Workspace", juce::Colour(0xff11161b), cadWorkspace.get(), true);
    cadWorkspace->onOpenViews = [this] { selectWorkspace(3); };
    cadWorkspace->setMotion(motionRuntime, motionLink.get());
    frustPanel = std::make_unique<FrustPanel>();
    tabs.addTab("FRust", juce::Colour(0xff11161b), frustPanel.get(), false);
    nodePanel = std::make_unique<NodeDesignerPanel>();
    nodePanel->onRunRequested = [this](const juce::String& script, const juce::String& label) {
        selectWorkspace(1);
        std::string error;
        if (!frustPanel->start(script, label, error))
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "FRust", error);
    };
    tabs.addTab("Node Editor", juce::Colour(0xff11161b), nodePanel.get(), false);
    viewsPanel = std::make_unique<ThreeDViewsPanel>(cadService);
    { auto runtime = motionRuntime; viewsPanel->setPoseSource([runtime] { return runtime->pose(); }); }
    {
        // One body/face/edge selection shared by the model view and every 3D view.
        auto selection = std::make_shared<djehuti::cad::CadSelection>();
        cadWorkspace->setSelection(selection);
        viewsPanel->setSelection(selection);
    }
    tabs.addTab("3D Views", juce::Colour(0xff11161b), viewsPanel.get(), false);
    const juce::Component::SafePointer<DjehutiCAD> safeThis(this);
    const auto motionBoard = motionLink->statusBoard();
    agentPanel = std::make_unique<DjehutiAgentPanel>(properties, [safeThis, motionBoard](const std::string& name, const std::string& arguments, std::shared_ptr<DjehutiAgentRequestState> state) {
        // Link status is read from the thread-safe status board so the agent sees it immediately.
        if (name == "cad_motion_status") return motionBoard->get().toStdString();
        if (name == "cad_list_faces" || name == "cad_create_face_sketch")
            return (state == nullptr || state->cancelled) ? std::string(R"({"ok": false, "error": "Request cancelled."})") : runFaceCommand(safeThis, name, arguments);
        if (const auto ikCommand = ikCommandForTool(name); ikCommand.isNotEmpty())
            return (state == nullptr || state->cancelled) ? std::string(R"({"ok": false, "error": "Request cancelled."})") : runIkCommand(safeThis, ikCommand, arguments);
        juce::MessageManager::callAsync([safeThis, name, arguments, state] {
            if (safeThis == nullptr || state == nullptr || state->cancelled || !state->alive) return;
            if (name == "open_cad_workspace") safeThis->selectWorkspace(0);
            else if (name == "open_frust_editor") safeThis->selectWorkspace(1);
            else if (name == "open_node_editor") safeThis->selectWorkspace(2);
            else if (name == "open_3d_views") { safeThis->selectWorkspace(3); safeThis->viewsPanel->addView(); }
            else if (name.rfind("cad_", 0) == 0)
            {
                const auto parsed = juce::JSON::parse(arguments);
                auto number = [&](const char* key, double fallback = 0.0) { return parsed.getProperty(key, fallback).toString().getDoubleValue(); };
                juce::String error, id;
                if (name == "cad_create_box") safeThis->cadService->createBox(number("width"), number("depth"), number("height"), id, error);
                else if (name == "cad_create_cylinder") safeThis->cadService->createCylinder(number("radius"), number("height"), id, error);
                else if (name == "cad_create_sketch") safeThis->cadService->createSketch(parsed.getProperty("plane", "XY").toString(), number("offset"), id, error);
                else if (name == "cad_sketch_line") safeThis->cadService->addSketchLine(parsed.getProperty("sketch", "").toString(), number("x1"), number("y1"), number("x2"), number("y2"), id, error);
                else if (name == "cad_sketch_rectangle") safeThis->cadService->addSketchRectangle(parsed.getProperty("sketch", "").toString(), number("x"), number("y"), number("width"), number("height"), id, error);
                else if (name == "cad_sketch_circle") safeThis->cadService->addSketchCircle(parsed.getProperty("sketch", "").toString(), number("x"), number("y"), number("radius"), id, error);
                else if (name == "cad_sketch_dimension") safeThis->cadService->setSketchEntityDimension(parsed.getProperty("sketch", "").toString(), parsed.getProperty("entity", "").toString(), number("value"), error);
                else if (name == "cad_sketch_position") safeThis->cadService->setSketchEntityPosition(parsed.getProperty("sketch", "").toString(), parsed.getProperty("entity", "").toString(), number("x"), number("y"), error);
                else if (name == "cad_sketch_constraint") safeThis->cadService->addSketchConstraint(parsed.getProperty("sketch", "").toString(), parsed.getProperty("type", "").toString(), parsed.getProperty("first", "").toString(), parsed.getProperty("second", "").toString(), number("value"), id, error);
                else if (name == "cad_extrude") safeThis->cadService->createExtrude(parsed.getProperty("sketch", "").toString(), number("depth"), static_cast<bool>(parsed.getProperty("reverse", false)), id, error);
                else if (name == "cad_cut") safeThis->cadService->createCut(parsed.getProperty("sketch", "").toString(), parsed.getProperty("target", "").toString(), number("depth"), static_cast<bool>(parsed.getProperty("throughAll", false)), static_cast<bool>(parsed.getProperty("reverse", false)), id, error);
                else if (name == "cad_set_dimensions") safeThis->cadService->setDimensions(parsed.getProperty("id", "").toString(), number("width"), number("depth"), number("height"), number("radius"), error);
                else if (name == "cad_set_placement") safeThis->cadService->setPlacement(parsed.getProperty("id", "").toString(), number("x"), number("y"), number("z"), number("rx"), number("ry"), number("rz"), error);
                else if (name == "cad_boolean") safeThis->cadService->booleanOp(parsed.getProperty("operation", "").toString(), parsed.getProperty("first", "").toString(), parsed.getProperty("second", "").toString(), id, error);
                else if (name == "cad_undo") safeThis->cadService->undo(error);
                else if (name == "cad_redo") safeThis->cadService->redo(error);
                else if (name == "cad_create_instance") safeThis->cadService->createInstance(parsed.getProperty("feature", "").toString(), parsed.getProperty("name", "").toString(), id, error);
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
                    if (name == "cad_create_joint") safeThis->cadService->createJoint(request, id, error);
                    else safeThis->cadService->updateJoint(parsed.getProperty("joint", "").toString(), request, error);
                }
                else if (name == "cad_set_joint_mapping")
                {
                    djehuti::cad::CadSignalMapping mapping;
                    mapping.jointId = parsed.getProperty("joint", "").toString(); mapping.signalId = parsed.getProperty("signal", "").toString();
                    mapping.sourceQuantity = parsed.getProperty("quantity", "").toString(); mapping.sourceUnit = parsed.getProperty("unit", "").toString();
                    mapping.scale = number("scale", 1.0); mapping.offset = number("offset"); mapping.initialCoordinate = number("initialCoordinate");
                    safeThis->cadService->setJointMapping(mapping, error);
                }
                else if (name == "cad_set_instance_grounded") safeThis->cadService->setInstanceGrounded(parsed.getProperty("instance", "").toString(), static_cast<bool>(parsed.getProperty("grounded", true)), error);
                else if (name == "cad_set_instance_placement")
                {
                    auto vector = [&](const char* key, int index) { const auto* list = parsed.getProperty(key, {}).getArray(); return list != nullptr && list->size() == 3 ? (double) (*list)[index] : 0.0; };
                    const auto frame = djehuti::cad::mech::frameFromEulerDegrees(vector("origin", 0), vector("origin", 1), vector("origin", 2), vector("rotationDeg", 0), vector("rotationDeg", 1), vector("rotationDeg", 2));
                    safeThis->cadService->setInstancePlacement(parsed.getProperty("instance", "").toString(), frame, error);
                }
                else if (name == "cad_set_joint_role")
                    safeThis->cadService->setJointRole(parsed.getProperty("joint", "").toString(), parsed.getProperty("role", "").toString(), number("lockValue"), static_cast<bool>(parsed.getProperty("enforceLimits", false)), error);
                else if (name == "cad_create_coupling")
                {
                    djehuti::cad::CadCoupling coupling;
                    coupling.name = parsed.getProperty("name", "").toString(); coupling.type = parsed.getProperty("type", "").toString();
                    coupling.jointA = parsed.getProperty("jointA", "").toString(); coupling.jointB = parsed.getProperty("jointB", "").toString();
                    coupling.ratio = number("ratio", 1.0); coupling.pitchRadius = number("pitchRadius", 0.01); coupling.lead = number("lead", 0.001); coupling.offset = number("offset");
                    safeThis->cadService->createCoupling(coupling, id, error);
                }
                else if (name == "cad_remove_coupling") safeThis->cadService->removeCoupling(parsed.getProperty("coupling", "").toString(), error);
                else if (name == "cad_motion_preview")
                {
                    if (static_cast<bool>(parsed.getProperty("reset", false))) safeThis->motionRuntime->resetToReference();
                    else safeThis->motionRuntime->setPreviewCoordinate(parsed.getProperty("joint", "").toString(), number("value"), error);
                }
                else if (name == "cad_motion_lifecycle")
                {
                    const auto command = parsed.getProperty("command", "").toString();
                    if (command == "retry") safeThis->motionLink->retryLifecycle(error);
                    else safeThis->motionLink->requestLifecycle(command, (std::int64_t) std::llround(number("stepMs", 10.0) * 1.0e9), error);
                }
                else if (name == "cad_motion_listen")
                {
                    if (static_cast<bool>(parsed.getProperty("listen", true))) safeThis->motionLink->startListening(error);
                    else safeThis->motionLink->stopListening();
                }
                if (safeThis->cadWorkspace != nullptr) safeThis->cadWorkspace->repaint();
            }
        });
        return std::string("Operation scheduled in DjehutiCAD.");
    });
    tabs.addTab("Agent", juce::Colour(0xff11161b), agentPanel.get(), false);
    restoreSettings();
}

DjehutiCAD::~DjehutiCAD()
{
    saveSettings();
    authSession.cancelLogin();
    tabs.clearTabs();
    nodePanel.reset();
    frustPanel.reset();
    agentPanel.reset();
    viewsPanel.reset();
    frust_engine::setCadMotionHooks(nullptr, nullptr, nullptr);
    frust_engine::setCadIkHook(nullptr);
    motionLink.reset();
    motionRuntime.reset();
    cadWorkspace.reset();
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
    tabs.setBounds(area);
}

void DjehutiCAD::selectWorkspace(int tabIndex)
{
    tabs.setCurrentTabIndex(tabIndex, true);
}

void DjehutiCAD::confirmCloseThen(std::function<void()> closeCallback)
{
    saveSettings();
    if (closeCallback)
        closeCallback();
}

juce::StringArray DjehutiCAD::getMenuBarNames()
{
    return { "File", "View", "Settings", "Account", "Help" };
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
        menu.addItem(10, "Close");
    }
    else if (index == 1)
    {
        menu.addItem(kCadWorkspace, "CAD Workspace");
        menu.addItem(kFrustEditor, "FRust Editor");
        menu.addItem(kNodeEditor, "Node Editor");
        menu.addItem(kAgent, "BYOK Agent");
        menu.addItem(kThreeDViews, "New 3D View");
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
    else if (commandId == kAgentSettings)
    {
        auto content = new DjehutiAgentSettings(properties, [] {});
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(content); options.dialogTitle = "Agent Settings"; options.dialogBackgroundColour = juce::Colour(0xff171b20);
        options.escapeKeyTriggersCloseButton = true; options.useNativeTitleBar = true; options.resizable = false;
        content->setSize(480, 430); options.launchAsync();
    }
    else if (commandId == kThreeDViews)
    {
        selectWorkspace(3);
        if (viewsPanel != nullptr && !viewsPanel->addView())
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "3D Views", "The maximum of six views is already open.");
    }
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
    else if (commandId == 31)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "CAD Workspace Guide",
                                               "Sketch to solid:\n"
                                               "1. Create Sketch, choose the XY, XZ or YZ plane, then Create Sketch.\n"
                                               "2. Pick Line, Rectangle or Circle and drag in the canvas. Esc cancels a gesture; Select edits entities.\n"
                                               "3. Finish Sketch, keep the sketch selected, then Extrude (or Cut Sketch with a Target body).\n\n"
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
                                               "7. Inverse Kinematics: press IK, choose the driver joints the solve may adjust, then add targets: the instance carrying the endpoint, a tool frame on it (mm, degrees), the reference frame (World or another instance), the mode (position, orientation or full pose) and the desired pose. Capture Current Pose copies the shown pose without moving anything. Solve moves only the selected drivers; dependent joints follow the mechanism, locked joints and grounding hold, hard bounds are respected. A result is shown only if every constraint and target holds; otherwise the last valid pose stays and the target errors are listed. Drag Target moves a target's position on the shown plane (the yellow diamond). Save Task stores the definition (undoable); solving never changes the document. Local IK is unavailable while DEWB drives the mechanism.");
    else if (commandId == 30)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "About DjehutiCAD",
                                               "DjehutiCAD 0.1.0\nA FRust-programmable CAD foundation.");
}

void DjehutiCAD::restoreSettings()
{
    const auto selected = properties.getUserSettings()->getIntValue("selectedTab", 0);
    selectWorkspace(juce::jlimit(0, 4, selected));
    cadWorkspace->restoreLayoutState(properties.getUserSettings()->getValue("cadWorkspaceLayout"));
}

void DjehutiCAD::saveSettings()
{
    if (auto* settings = properties.getUserSettings())
    {
        settings->setValue("selectedTab", tabs.getCurrentTabIndex());
        if (cadWorkspace != nullptr) settings->setValue("cadWorkspaceLayout", cadWorkspace->layoutState());
        settings->saveIfNeeded();
    }
}
