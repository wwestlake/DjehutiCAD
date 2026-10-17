#include "DjehutiCAD.h"

#include "FrustPanel.h"
#include "NodeDesignerPanel.h"
#include "DjehutiAgent.h"
#include "Viewport3D.h"
#include "CadWorkspacePanel.h"
#include "FrustEngine.h"

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
                                        [safe](const std::string& joint, double value) { juce::MessageManager::callAsync([safe, joint, value] { if (safe != nullptr && safe->motionRuntime != nullptr) { juce::String e; safe->motionRuntime->setPreviewCoordinate(joint, value, e); } }); });
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
    tabs.addTab("3D Views", juce::Colour(0xff11161b), viewsPanel.get(), false);
    const juce::Component::SafePointer<DjehutiCAD> safeThis(this);
    const auto motionBoard = motionLink->statusBoard();
    agentPanel = std::make_unique<DjehutiAgentPanel>(properties, [safeThis, motionBoard](const std::string& name, const std::string& arguments, std::shared_ptr<DjehutiAgentRequestState> state) {
        // Link status is read from the thread-safe status board so the agent sees it immediately.
        if (name == "cad_motion_status") return motionBoard->get().toStdString();
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
                else if (name == "cad_motion_preview")
                {
                    if (static_cast<bool>(parsed.getProperty("reset", false))) safeThis->motionRuntime->resetToReference();
                    else safeThis->motionRuntime->setPreviewCoordinate(parsed.getProperty("joint", "").toString(), number("value"), error);
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
    frust_engine::setCadMotionHooks(nullptr, nullptr);
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
                                               "4. Motion Link: Start Listening and give DEWB the pipe path; DEWB validates the profile, synchronizes, then streams motion.");
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
