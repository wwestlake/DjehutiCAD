#pragma once

#include <JuceHeader.h>

#include "Auth/DesktopAuthSession.h"
#include "Auth/ProfileAvatarComponent.h"
#include "DjehutiMenuBarComponent.h"
#include "CadDocument.h"
#include "CadMotionRuntime.h"
#include "MotionLink.h"

class FrustPanel;
class NodeDesignerPanel;
class DjehutiAgentPanel;
class ThreeDViewsPanel;
class CadWorkspacePanel;

class DjehutiCAD final : public juce::Component,
                         private juce::MenuBarModel
{
public:
    DjehutiCAD();
    ~DjehutiCAD() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void selectWorkspace(int tabIndex);
    void confirmCloseThen(std::function<void()> closeCallback);

private:
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int, const juce::String&) override;
    void menuItemSelected(int commandId, int) override;

    void restoreSettings();
    void saveSettings();

    juce::ApplicationProperties properties;
    DesktopAuthSession authSession { "djehuti-cad" };
    ProfileAvatarComponent profileAvatar { authSession };
    DjehutiMenuBarComponent menuBar { this };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<FrustPanel> frustPanel;
    std::unique_ptr<NodeDesignerPanel> nodePanel;
    std::unique_ptr<DjehutiAgentPanel> agentPanel;
    std::unique_ptr<ThreeDViewsPanel> viewsPanel;
    std::unique_ptr<CadWorkspacePanel> cadWorkspace;
    std::shared_ptr<djehuti::cad::CadCommandService> cadService;
    std::shared_ptr<djehuti::cad::CadMotionRuntime> motionRuntime;
    static std::string runIkCommand(const juce::Component::SafePointer<DjehutiCAD>& safe, const juce::String& command, const juce::String& arguments);
    std::unique_ptr<djehuti::motion::MotionLink> motionLink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DjehutiCAD)
};
