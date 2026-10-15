#include "DjehutiCAD.h"

#include "FrustPanel.h"
#include "NodeDesignerPanel.h"

namespace
{
constexpr int kCadWorkspace = 100;
constexpr int kFrustEditor = 101;
constexpr int kNodeEditor = 102;
}

class DjehutiCAD::EmptyCadWorkspace final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff11161b));
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(24.0f, juce::Font::bold));
        g.drawText("DjehutiCAD", getLocalBounds().reduced(48).removeFromTop(44),
                   juce::Justification::centredLeft, false);
        g.setColour(juce::Colour(0xff8fa4ad));
        g.setFont(juce::Font("Segoe UI", 15.0f, juce::Font::plain));
        g.drawFittedText("CAD workspace ready. The modeling viewport and geometry engine will be added in a later assignment.",
                         getLocalBounds().reduced(48).withTrimmedTop(64),
                         juce::Justification::topLeft, 3, 1.0f);
    }
};

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
    tabs.addTab("CAD Workspace", juce::Colour(0xff11161b), new EmptyCadWorkspace(), true);
    frustPanel = std::make_unique<FrustPanel>();
    tabs.addTab("FRust", juce::Colour(0xff11161b), frustPanel.get(), false);
    nodePanel = std::make_unique<NodeDesignerPanel>();
    tabs.addTab("Node Editor", juce::Colour(0xff11161b), nodePanel.get(), false);
    restoreSettings();
}

DjehutiCAD::~DjehutiCAD()
{
    saveSettings();
    authSession.cancelLogin();
    tabs.clearTabs();
    nodePanel.release();
    frustPanel.release();
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
    return { "File", "View", "Account", "Help" };
}

juce::PopupMenu DjehutiCAD::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu menu;
    if (index == 0)
    {
        menu.addItem(kCadWorkspace, "New CAD Workspace");
        menu.addSeparator();
        menu.addItem(10, "Close");
    }
    else if (index == 1)
    {
        menu.addItem(kCadWorkspace, "CAD Workspace");
        menu.addItem(kFrustEditor, "FRust Editor");
        menu.addItem(kNodeEditor, "Node Editor");
    }
    else if (index == 2)
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
    if (commandId >= kCadWorkspace && commandId <= kNodeEditor)
        selectWorkspace(commandId - kCadWorkspace);
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
    selectWorkspace(juce::jlimit(0, 2, selected));
}

void DjehutiCAD::saveSettings()
{
    if (auto* settings = properties.getUserSettings())
    {
        settings->setValue("selectedTab", tabs.getCurrentTabIndex());
        settings->saveIfNeeded();
    }
}
