#pragma once
#include <JuceHeader.h>

class CadReferenceControls final : public juce::Component
{
public:
    std::function<void(const juce::var&)> changed;
    explicit CadReferenceControls(const juce::var& state)
    {
        for(const auto& spec:std::vector<std::pair<juce::String,juce::String>>{{"showSketches","Other sketches"},{"showSolids","Solid bodies"},{"snap","Snap to visible references"},{"select","Select references (read only)"}})
        {
            auto t=std::make_unique<juce::ToggleButton>(spec.second);t->setToggleState((bool)state.getProperty(juce::Identifier(spec.first),{}),juce::dontSendNotification);t->onClick=[this,key=spec.first,b=t.get()]{auto* v=new juce::DynamicObject();v->setProperty(key,b->getToggleState());if(changed)changed(v);};addAndMakeVisible(*t);toggles.push_back(std::move(t));
        }
        opacity.setRange(0,1,.01);opacity.setValue((double)state["opacity"],juce::dontSendNotification);opacity.setSliderStyle(juce::Slider::LinearHorizontal);opacity.setTextBoxStyle(juce::Slider::TextBoxRight,false,45,22);opacity.setTooltip("Reference opacity (zero also disables reference snaps)");opacity.onValueChange=[this]{auto* v=new juce::DynamicObject();v->setProperty("opacity",opacity.getValue());if(changed)changed(v);};addAndMakeVisible(opacity);
        title.setText("Reference geometry",juce::dontSendNotification);addAndMakeVisible(title);
        scroll.setViewedComponent(&sources,false);scroll.setScrollBarsShown(true,false);addAndMakeVisible(scroll);
        if(const auto* list=state["sources"].getArray())for(const auto& source:*list)
        {
            auto t=std::make_unique<juce::ToggleButton>(source["label"].toString());t->setTooltip(source["id"].toString());t->setToggleState((bool)source["enabled"],juce::dontSendNotification);t->onClick=[this,id=source["id"].toString(),b=t.get()]{auto* v=new juce::DynamicObject();auto* vis=new juce::DynamicObject();vis->setProperty(id,b->getToggleState());v->setProperty("visibility",vis);if(changed)changed(v);};sources.addAndMakeVisible(*t);sourceToggles.push_back(std::move(t));
        }
        setSize(300,juce::jmin(500,185+(int)sourceToggles.size()*26));
    }
    void resized() override
    {auto a=getLocalBounds().reduced(8);title.setBounds(a.removeFromTop(26));for(auto& t:toggles)t->setBounds(a.removeFromTop(25));opacity.setBounds(a.removeFromTop(30));scroll.setBounds(a);sources.setSize(juce::jmax(1,a.getWidth()-14),(int)sourceToggles.size()*26);int y=0;for(auto& t:sourceToggles){t->setBounds(0,y,sources.getWidth(),26);y+=26;}}
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff1b2933));}
private:
    juce::Label title;juce::Slider opacity;juce::Viewport scroll;juce::Component sources;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles,sourceToggles;
};
