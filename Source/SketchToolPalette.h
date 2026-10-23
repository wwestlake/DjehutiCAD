#pragma once
#include <JuceHeader.h>

// Workspace-owned presentation only. All tools invoke the existing CAD command handlers.
class SketchToolPalette final : public juce::Component
{
public:
    std::function<void(const juce::String&)> command;
    std::function<void()> layoutChanged;
    SketchToolPalette()
    {
        setName("Sketch toolbox"); setTitle("Sketch toolbox");
        const std::pair<const char*,const char*> specs[]{
            {"sketchSelect","Select geometry (Esc returns to Select; Shift-click selects a second entity)"},
            {"line","Line: drag between endpoints"},{"rectangle","Rectangle: drag between opposite corners"},
            {"circle","Circle: drag from centre to radius"},{"curve","Bezier curve: drag endpoints, then edit handles"},
            {"constrain","Constrain selected geometry"},{"deleteSketchEntities","Delete selected sketch entities (Delete; Undo restores them)"},
            {"fitSketch","Fit all sketch geometry"},{"finishSketch","Finish sketch and keep completed geometry"}};
        for(const auto& spec:specs)
        {
            auto button=std::make_unique<Icon>(spec.first); button->setTooltip(spec.second); button->setTitle(spec.second);
            button->onClick=[this,key=juce::String(spec.first)]{if(command)command(key);};
            addAndMakeVisible(*button); buttons.push_back(std::move(button));
        }
        close.setTooltip("Hide sketch toolbox; restore it from View > Sketch toolbox"); close.setTitle("Hide sketch toolbox");
        close.onClick=[this]{preferredVisible=false;changed();}; addAndMakeVisible(close);
        dock.setTooltip("Dock at the left edge or float over the sketch"); dock.setTitle("Dock or float sketch toolbox");
        dock.onClick=[this]{docked=!docked;changed();}; addAndMakeVisible(dock);
        symmetry.setTooltip("Expand symmetry and mirror controls");symmetry.onClick=[this]{expanded=!expanded;changed();};addAndMakeVisible(symmetry);
        setTooltipText();
    }
    void setSymmetryPanel(juce::Component* panel){symmetryPanel=panel;if(panel)addAndMakeVisible(panel);}
    void setSymmetryMode(const juce::String& mode){symmetry.setButtonText(mode=="off"?"Sym":mode=="both"?"H+V":mode=="horizontal"?"Sym H":"Sym V");symmetry.setColour(juce::TextButton::buttonColourId,juce::Colour(mode=="off"?0xff293c48:0xff356d66));}
    bool wanted() const { return preferredVisible; }
    bool isDocked() const { return docked; }
    void toggleVisible() { preferredVisible=!preferredVisible;changed(); }
    void toggleDocked() { docked=!docked;changed(); }
    void setAvailable(bool editing) { setVisible(editing&&preferredVisible); }
    void refresh(const std::function<bool(const juce::String&)>& enabled,const std::function<bool(const juce::String&)>& active)
    {
        for(auto& button:buttons){button->setEnabled(enabled(button->getName()));button->setToggleState(active(button->getName()),juce::dontSendNotification);}
    }
    void place(juce::Rectangle<int> area)
    {
        placement=area;if(area.isEmpty())return;
        const int width=juce::jmin(expanded?328:58,area.getWidth()),height=juce::jmin(expanded?510:395,area.getHeight());
        const auto p=docked?juce::Point<int>{0,0}:juce::Point<int>{juce::jlimit(0,area.getWidth()-width,floating.x),juce::jlimit(0,area.getHeight()-height,floating.y)};
        setBounds(area.getX()+p.x,area.getY()+p.y,width,height); if(!docked)floating=p;toFront(false);
    }
    juce::var state() const
    {
        auto* o=new juce::DynamicObject();o->setProperty("visible",preferredVisible);o->setProperty("docked",docked);
        o->setProperty("x",floating.x);o->setProperty("y",floating.y);o->setProperty("showing",isVisible());o->setProperty("symmetryExpanded",expanded);return o;
    }
    void restore(const juce::var& value)
    {
        if(!value.isObject())return;
        expanded=value.getProperty("symmetryExpanded",false);
        preferredVisible=value.getProperty("visible",true);docked=value.getProperty("docked",true);
        floating={juce::jlimit(0,100000,(int)value.getProperty("x",12)),juce::jlimit(0,100000,(int)value.getProperty("y",12))};changed();
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff19262e));g.setColour(juce::Colour(0xff557581));g.drawRect(getLocalBounds(),1);
        for(int x:{14,22,30})for(int y:{8,14})g.fillEllipse((float)x,(float)y,2,2);
    }
    void resized() override
    {
        auto area=getLocalBounds().reduced(5);if(symmetryPanel){symmetryPanel->setVisible(expanded);symmetryPanel->setBounds(62,24,juce::jmax(1,getWidth()-66),getHeight()-28);}
        area.setWidth(48);close.setBounds(35,2,18,20);area.removeFromTop(20);symmetry.setBounds(area.removeFromBottom(26));symmetry.setToggleState(expanded,juce::dontSendNotification);
        dock.setButtonText(docked?"Float":"Dock");dock.setBounds(area.removeFromBottom(24));
        const int row=juce::jmax(1,area.getHeight()/(int)buttons.size());
        for(auto& button:buttons)button->setBounds(area.removeFromTop(row).reduced(1,2));
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging=e.mods.isLeftButtonDown()&&e.y<24&&e.x<getWidth()-23;
        if(dragging){docked=false;dragger.startDraggingComponent(this,e);}
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if(!dragging||getParentComponent()==nullptr)return;
        dragger.dragComponent(this,e,nullptr);
        auto area=placement;setTopLeftPosition(juce::jlimit(area.getX(),juce::jmax(area.getX(),area.getRight()-getWidth()),getX()),juce::jlimit(area.getY(),juce::jmax(area.getY(),area.getBottom()-getHeight()),getY()));
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if(!dragging)return;dragging=false;floating=getPosition()-placement.getPosition();docked=getX()<=placement.getX()+8;changed();
    }
private:
    class Icon final : public juce::Button
    {
    public:
        explicit Icon(const juce::String& key):juce::Button(key){}
        void paintButton(juce::Graphics& g,bool over,bool down) override
        {
            g.setColour(getToggleState()?juce::Colour(0xff326f65):juce::Colour(over||down?0xff344c58:0xff22333e));g.fillRoundedRectangle(getLocalBounds().toFloat(),4);
            auto box=getLocalBounds().toFloat().reduced(10,6);const float x=box.getX(),y=box.getY(),w=box.getWidth(),h=box.getHeight();
            g.setColour(juce::Colour(0xffd8eeee).withAlpha(isEnabled()?1.0f:.3f));const auto key=getName();juce::Path path;
            if(key=="sketchSelect"){path.startNewSubPath(x,y);path.lineTo(x+w,y+h*.55f);path.lineTo(x+w*.45f,y+h*.6f);path.lineTo(x+w*.25f,y+h);path.closeSubPath();g.strokePath(path,juce::PathStrokeType(1.6f));}
            else if(key=="line")g.drawLine(x,y+h,x+w,y,2);
            else if(key=="rectangle")g.drawRect(box,1.8f);
            else if(key=="circle")g.drawEllipse(box,1.8f);
            else if(key=="curve"){path.startNewSubPath(x,y+h);path.cubicTo(x+w*.8f,y+h,x+w*.2f,y,x+w,y);g.strokePath(path,juce::PathStrokeType(1.8f));}
            else if(key=="deleteSketchEntities"){g.drawRect(box.reduced(3,2),1.6f);g.drawLine(x,y+1,x+w,y+1,2);g.drawLine(x+w*.5f,y+5,x+w*.5f,y+h-4,1.6f);}
            else if(key=="fitSketch"){g.drawRect(box,1.4f);g.drawEllipse(box.reduced(5),1.3f);}
            else if(key=="finishSketch"){path.startNewSubPath(x,y+h*.5f);path.lineTo(x+w*.35f,y+h);path.lineTo(x+w,y);g.strokePath(path,juce::PathStrokeType(2));}
            else {g.drawLine(x,y+h*.3f,x+w,y+h*.3f,2);g.drawLine(x,y+h*.7f,x+w,y+h*.7f,2);}
        }
    };
    void changed(){dock.setButtonText(docked?"Float":"Dock");setTooltipText();if(layoutChanged)layoutChanged();}
    void setTooltipText(){setMouseCursor(juce::MouseCursor::DraggingHandCursor);}
    bool preferredVisible=true,docked=true,dragging=false,expanded=false;juce::Component* symmetryPanel=nullptr;juce::TextButton symmetry{"Sym"};
    juce::Rectangle<int> placement;juce::Point<int> floating{12,12};juce::ComponentDragger dragger;
    juce::TextButton close{"x"},dock{"Float"};std::vector<std::unique_ptr<Icon>> buttons;
};
