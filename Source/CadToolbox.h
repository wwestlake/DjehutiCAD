#pragma once
#include "CadWorkspacePanel.h"

// Persistent interaction modes. Modeling operations remain in the workspace menus.
class CadToolbox final : public juce::Component, private juce::Timer
{
public:
    explicit CadToolbox(CadWorkspacePanel& owner) : workspace(&owner)
    {
        title.setText("Toolbox", juce::dontSendNotification); title.setFont(juce::Font(18.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colours::white); addAndMakeVisible(title);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xff9bb4c1));
        hint.setText("CAD tools\nBox: left to right encloses; right to left crosses. Circle crosses. Area selection includes obscured geometry. Right-drag pans.", juce::dontSendNotification);
        hint.setFont(juce::Font(12.0f)); addAndMakeVisible(hint);
        const std::pair<const char*, const char*> specs[] = {
            {"Pointer", "pointer"}, {"Box select", "boxSelect"}, {"Circle select", "circleSelect"},
            {"Bodies", "bodyMode"}, {"Faces", "faceMode"}, {"Edges", "edgeMode"},
            {"Sketch select", "sketchSelect"}, {"Line", "line"}, {"Rectangle", "rectangle"}, {"Circle", "circle"}, {"Curve", "curve"}
        };
        for (const auto& s : specs)
        {
            auto b = std::make_unique<juce::TextButton>(s.first); b->setTooltip(s.first); b->setClickingTogglesState(false);
            b->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff22333e)); b->setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff30675e)); b->setColour(juce::TextButton::textColourOffId,juce::Colours::white); b->setColour(juce::TextButton::textColourOnId,juce::Colours::white);
            const auto command = juce::String(s.second);
            b->onClick = [safe = workspace, command] { if (safe != nullptr) safe->executeUiCommand(command); };
            addAndMakeVisible(*b); buttons.push_back(std::move(b)); commands.add(command);
        }
        startTimerHz(4); timerCallback();
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff151e25)); }
    void resized() override
    {
        auto b = getLocalBounds().reduced(10); title.setBounds(b.removeFromTop(32));
        for (size_t i = 0; i < buttons.size(); ++i)
        {
            if (i == 0 || i == 3 || i == 6) b.removeFromTop(12);
            buttons[i]->setBounds(b.removeFromTop(30)); b.removeFromTop(4);
        }
        b.removeFromTop(12); hint.setBounds(b.removeFromTop(115));
    }
private:
    void timerCallback() override
    {
        if (!isShowing() || workspace == nullptr) return;
        for (size_t i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setToggleState(workspace->uiToolActive(commands[(int) i]), juce::dontSendNotification);
            buttons[i]->setEnabled(workspace->uiCommandEnabled(commands[(int) i]));
        }
    }
    juce::Component::SafePointer<CadWorkspacePanel> workspace;
    juce::Label title, hint;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    juce::StringArray commands;
};
