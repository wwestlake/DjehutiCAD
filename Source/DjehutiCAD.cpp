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
    cadWorkspace = std::make_unique<CadWorkspacePanel>(cadService);
    tabs.addTab("CAD Workspace", juce::Colour(0xff11161b), cadWorkspace.get(), true);
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
    tabs.addTab("3D Views", juce::Colour(0xff11161b), viewsPanel.get(), false);
    const juce::Component::SafePointer<DjehutiCAD> safeThis(this);
    agentPanel = std::make_unique<DjehutiAgentPanel>(properties, [safeThis](const std::string& name, const std::string& arguments, std::shared_ptr<DjehutiAgentRequestState> state) {
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
    else if (commandId == 30)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "About DjehutiCAD",
                                               "DjehutiCAD 0.1.0\nA FRust-programmable CAD foundation.");
}

void DjehutiCAD::restoreSettings()
{
    const auto selected = properties.getUserSettings()->getIntValue("selectedTab", 0);
    selectWorkspace(juce::jlimit(0, 4, selected));
}

void DjehutiCAD::saveSettings()
{
    if (auto* settings = properties.getUserSettings())
    {
        settings->setValue("selectedTab", tabs.getCurrentTabIndex());
        settings->saveIfNeeded();
    }
}
