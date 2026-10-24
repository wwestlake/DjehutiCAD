#pragma once
#include <JuceHeader.h>
// Native GL peers cover child popups; desktop tips also constrain to the active display.
// Scope lookup to this workspace so hidden workspaces do not create duplicate tips.
class CadTooltipWindow final : public juce::TooltipWindow
{
public:
    explicit CadTooltipWindow(juce::Component& owner):juce::TooltipWindow(nullptr,600),scope(&owner){}
    juce::String getTipFor(juce::Component& c) override
    {return scope!=nullptr&&scope->isShowing()&&(scope==&c||scope->isParentOf(&c))?juce::TooltipWindow::getTipFor(c):juce::String();}
private:
    juce::Component::SafePointer<juce::Component> scope;
};
