#pragma once

#include <JuceHeader.h>

#include "Auth/DesktopAuthSession.h"
#include "Auth/ProfileAvatarComponent.h"
#include "DjehutiMenuBarComponent.h"

class FrustPanel;
class NodeDesignerPanel;
class DjehutiAgentPanel;

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
    class EmptyCadWorkspace;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DjehutiCAD)
};
