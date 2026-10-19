#pragma once

#include <JuceHeader.h>

#include "Auth/DesktopAuthSession.h"
#include "Auth/ProfileAvatarComponent.h"
#include "DjehutiMenuBarComponent.h"
#include "CadDocument.h"
#include "CadMotionRuntime.h"
#include "MotionLink.h"
#include <CreationDock/DockManager.h>

class FrustPanel;
class NodeDesignerPanel;
class DjehutiAgentPanel;
class ThreeDViewsPanel;
class CadWorkspacePanel;

class DjehutiCAD final : public juce::Component,
                         private juce::MenuBarModel
{
public:
    explicit DjehutiCAD(bool diagnostic = false);
    juce::var runWorkspaceCheck();
    ~DjehutiCAD() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void selectWorkspace(int tabIndex);
    void confirmCloseThen(std::function<void()> closeCallback);

private:
    bool diagnosticMode = false;
    static std::string runWorkspaceCommand(const juce::Component::SafePointer<DjehutiCAD>&, const juce::String&);
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int, const juce::String&) override;
    void menuItemSelected(int commandId, int) override;

    void restoreSettings();
    void saveSettings();
    void applyWorkspaceLayout(const juce::var&);

    juce::ApplicationProperties properties;
    DesktopAuthSession authSession { "djehuti-cad" };
    ProfileAvatarComponent profileAvatar { authSession };
    DjehutiMenuBarComponent menuBar { this };
    std::unique_ptr<CreationDock::DockManager> dockManager;
    juce::var defaultDockLayout;
    FrustPanel* frustPanel = nullptr; // owned by DockManager
    NodeDesignerPanel* nodePanel = nullptr; // owned by DockManager
    DjehutiAgentPanel* agentPanel = nullptr; // owned by DockManager
    ThreeDViewsPanel* viewsPanel = nullptr; // owned by DockManager
    CadWorkspacePanel* cadWorkspace = nullptr; // owned by DockManager
    std::shared_ptr<djehuti::cad::CadCommandService> cadService;
    std::shared_ptr<djehuti::cad::CadMotionRuntime> motionRuntime;
    static std::string runFaceCommand(const juce::Component::SafePointer<DjehutiCAD>& safe, const std::string& name, const juce::String& arguments);
    static std::string runExportCommand(const juce::Component::SafePointer<DjehutiCAD>& safe, const juce::String& arguments);
    static std::string runIkCommand(const juce::Component::SafePointer<DjehutiCAD>& safe, const juce::String& command, const juce::String& arguments);
    std::unique_ptr<djehuti::motion::MotionLink> motionLink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DjehutiCAD)
};
