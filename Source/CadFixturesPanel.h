#pragma once
#include "CadTestFixture.h"
#include "WorkspaceMenu.h"
#include "CadSaveProtection.h"

// Each tab owns an independently persisted engineering fixture; this workspace docks/floats with CreationDock.
class CadFixturesPanel final : public juce::Component, private juce::Timer
{
public:
    CadFixturesPanel(std::shared_ptr<djehuti::cad::CadFixtureService>,std::shared_ptr<djehuti::cad::CadCommandService>);
    ~CadFixturesPanel() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void createFixture();
    void openFixture();
    void synchronize(const juce::String& activate={});
    void confirmClose(std::function<void()>,std::function<void()> cancelled={});
    std::function<void(const juce::File&)> onOpenSource;
    WorkspaceMenu workspaceMenu;
    bool smokeCheck(const juce::File& folder,juce::var& report);
private:
    friend bool runFixtureSaveProtectionCheck(juce::var&);
    djehuti::cad::saveprotection::Prompt unsavedPrompt;
    void confirmFixture(const juce::String&,djehuti::cad::saveprotection::Completion);
    void confirmNext(const juce::StringArray&,int,std::function<void()>,std::function<void()>);
    void timerCallback() override;
    void manage(const juce::String& action);
    std::shared_ptr<djehuti::cad::CadFixtureService> service;
    std::shared_ptr<djehuti::cad::CadCommandService> cad;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    juce::TextButton newButton{"New fixture"},openButton{"Open..."},duplicate{"Duplicate"},close{"Close fixture"},remove{"Delete fixture..."};
    juce::Label message;
    juce::StringArray tabIds;
    std::unique_ptr<juce::FileChooser> chooser;
};
bool runCadFixtureUiCheck(juce::var& report,const juce::File& folder,bool keepOpen=false);
