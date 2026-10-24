#pragma once
#include <JuceHeader.h>

// Workspace-owned presentation only. All tools invoke the existing CAD command handlers.
class SketchToolPalette final : public juce::Component, public juce::TooltipClient
{
public:
    std::function<void(const juce::String&)> command;
    std::function<void()> layoutChanged;
    explicit SketchToolPalette(bool selection = false) : selectionTools(selection)
    {
        if(selectionTools){docked=false;floating={76,12};}
        setWantsKeyboardFocus(true);
        setName(selectionTools?"Selection toolbox":"Sketch toolbox"); setTitle(getName());
        const std::pair<const char*,const char*> specs[]{
            {"sketchSelect","Select geometry (Esc returns to Select; Shift-click selects a second entity)"},
            {"line","Line: drag between endpoints"},{"rectangle","Rectangle: drag between opposite corners"},
            {"circle","Circle: drag from centre to radius"},{"curve","Bezier curve: drag endpoints, then edit handles"},
            {"constrain","Constrain selected geometry"},{"deleteSketchEntities","Delete selected sketch entities (Delete; Undo restores them)"},
            {"fitSketch","Fit all sketch geometry"},{"finishSketch","Finish sketch and keep completed geometry"}};
        if(!selectionTools)for(const auto& spec:specs)
        {
            auto button=std::make_unique<Icon>(spec.first); button->setTooltip(spec.second); button->setTitle(spec.second);
            button->onClick=[this,key=juce::String(spec.first)]{if(command)command(key);};
            addAndMakeVisible(*button); buttons.push_back(std::move(button));
        }
        const std::pair<const char*,const char*> editing[]{
            {"sketchSelect","Single selection (Esc); Shift-click adds or removes geometry"},
            {"sketchBox","Box selection: drag a rectangular region"},
            {"sketchCircleSelect","Circle-region selection: drag centre to radius"},
            {"sketchFreehand","Freehand selection: draw a closed region"},
            {"sketchMultiple","Multiple selection: click to add or remove geometry"},
            {"sketchCopy","Copy selected geometry (Ctrl+C)"},{"sketchPaste","Paste copied geometry (Ctrl+V)"},
            {"sketchRotate","Rotate selection: pick centre and direction; A enters exact degrees"},
            {"sketchMirror","Mirror selection: pick two axis points"},
            {"sketchSplit","Split / Insert Node: click a line or curve"},
            {"ellipse","Ellipse: drag from centre to set horizontal and vertical radii"}};
        if(selectionTools)for(const auto& spec:editing){auto button=std::make_unique<Icon>(spec.first);button->setTooltip(spec.second);button->setTitle(spec.second);button->onClick=[this,key=juce::String(spec.first)]{if(command)command(key);};addAndMakeVisible(*button);buttons.push_back(std::move(button));}
        close.setButtonText("-");close.setTooltip("Collapse toolbox; click the compact icon to expand");close.setTitle("Collapse toolbox");
        close.onClick=[this]{toggleCollapsed();};addAndMakeVisible(close);
        dock.setTooltip("Dock or float; drag to a workspace edge to dock"); dock.setTitle("Dock or float sketch toolbox");
        dock.onClick=[this]{docked=!docked;changed();}; addAndMakeVisible(dock);
        symmetry.setTooltip("Expand symmetry and mirror controls");symmetry.onClick=[this]{expanded=!expanded;changed();};addAndMakeVisible(symmetry);
        setTooltipText();
    }
    void setSymmetryPanel(juce::Component* panel){symmetryPanel=panel;if(panel)addAndMakeVisible(panel);}
    void setSymmetryMode(const juce::String& mode){symmetry.setButtonText(mode=="off"?"Sym":mode=="both"?"H+V":mode=="horizontal"?"Sym H":"Sym V");symmetry.setColour(juce::TextButton::buttonColourId,juce::Colour(mode=="off"?0xff293c48:0xff356d66));}
    juce::String getTooltip() override {return collapsed?"Expand "+getName()+"; drag to move or dock":getName();}
    bool isCollapsed() const { return collapsed; }
    void toggleCollapsed(){collapsed=!collapsed;preferredVisible=true;changed();}
    bool invoke(const juce::String& key){for(auto& b:buttons)if(b->getName()==key&&b->isEnabled()){b->onClick();return true;}return false;}
    void reserve(juce::Rectangle<int>& area) const {if(!isVisible()||!docked)return;if(edge=="right")area.removeFromRight(getWidth()+6);else if(edge=="top")area.removeFromTop(getHeight()+6);else if(edge=="bottom")area.removeFromBottom(getHeight()+6);else area.removeFromLeft(getWidth()+6);}
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
        const bool horizontal=(edge=="top"||edge=="bottom")&&!expanded;
        const int columns=juce::jmax(1,juce::jmin((int)buttons.size(),(area.getWidth()-10)/34));
        const int width=juce::jmin(collapsed?36:expanded&&!selectionTools?322:horizontal?columns*34+10:60,area.getWidth());
        const int height=juce::jmin(collapsed?36:horizontal?24+((int)buttons.size()+columns-1)/columns*34+28+(selectionTools?0:26):expanded&&!selectionTools?510:selectionTools?428:386,area.getHeight());
        auto p=docked?juce::Point<int>{0,0}:juce::Point<int>{juce::jlimit(0,area.getWidth()-width,floating.x),juce::jlimit(0,area.getHeight()-height,floating.y)};
        if(docked){if(edge=="right")p.x=area.getWidth()-width;else if(edge=="bottom")p.y=area.getHeight()-height;}
        setBounds(area.getX()+p.x,area.getY()+p.y,width,height); if(!docked&&!collapsed)floating=p;toFront(false);
    }
    juce::var state() const
    {
        auto* o=new juce::DynamicObject();o->setProperty("collapsed",collapsed);o->setProperty("edge",edge);o->setProperty("visible",preferredVisible);o->setProperty("docked",docked);
        o->setProperty("x",floating.x);o->setProperty("y",floating.y);o->setProperty("showing",isVisible());o->setProperty("symmetryExpanded",expanded);return o;
    }
    void restore(const juce::var& value)
    {
        if(!value.isObject())return;
        collapsed=value.getProperty("collapsed",false);edge=value.getProperty("edge","left").toString();if(!juce::StringArray{"left","right","top","bottom"}.contains(edge))edge="left";
        expanded=value.getProperty("symmetryExpanded",false);
        preferredVisible=value.getProperty("visible",true);docked=value.getProperty("docked",true);
        floating={juce::jlimit(0,100000,(int)value.getProperty("x",12)),juce::jlimit(0,100000,(int)value.getProperty("y",12))};changed();
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(collapsed&&isMouseOver()?0xff344c58:0xff19262e));g.setColour(juce::Colour(0xff557581));g.drawRect(getLocalBounds(),1);
        if(collapsed){g.setColour(juce::Colour(0xffd8eeee));juce::Path p;if(selectionTools){p.startNewSubPath(10,8);p.lineTo(25,19);p.lineTo(17,20);p.lineTo(13,28);p.closeSubPath();}else{p.startNewSubPath(10,26);p.lineTo(24,10);p.lineTo(28,14);p.lineTo(14,29);p.closeSubPath();}g.strokePath(p,juce::PathStrokeType(1.6f));return;}
        for(int x:{10,18,26})for(int y:{8,14})g.fillEllipse((float)x,(float)y,2,2);
    }
    void resized() override
    {
        auto area=getLocalBounds().reduced(5);if(symmetryPanel){symmetryPanel->setVisible(expanded&&!collapsed);symmetryPanel->setBounds(64,24,juce::jmax(1,getWidth()-68),getHeight()-28);}
        close.setVisible(!collapsed);dock.setVisible(!collapsed);symmetry.setVisible(!collapsed&&!selectionTools);for(auto& button:buttons)button->setVisible(!collapsed);
        if(collapsed)return;
        if((edge=="top"||edge=="bottom")&&!expanded){
            close.setBounds(getWidth()-23,2,18,20);auto footer=area.removeFromBottom(24);dock.setBounds(footer.removeFromRight(48));if(!selectionTools)symmetry.setBounds(area.removeFromBottom(26));area.removeFromTop(20);
            const int columns=juce::jmax(1,(getWidth()-10)/34);for(size_t i=0;i<buttons.size();++i)buttons[i]->setBounds(5+(int)i%columns*34,25+(int)i/columns*34,32,30);return;
        }
        area.setWidth(48);close.setBounds(35,2,18,20);area.removeFromTop(20);if(!selectionTools)symmetry.setBounds(area.removeFromBottom(26));symmetry.setToggleState(expanded,juce::dontSendNotification);
        dock.setButtonText(docked?"Float":"Dock");dock.setBounds(area.removeFromBottom(24));
        const int row=juce::jmin(34,juce::jmax(1,area.getHeight()/(int)buttons.size()));
        for(auto& button:buttons)button->setBounds(area.removeFromTop(row).reduced(1,2));
    }
    void mouseEnter(const juce::MouseEvent&) override {repaint();}
    void mouseExit(const juce::MouseEvent&) override {repaint();}
    bool keyPressed(const juce::KeyPress& key) override {if(collapsed&&(key==juce::KeyPress::returnKey||key==juce::KeyPress::spaceKey)){toggleCollapsed();return true;}return false;}
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging=e.mods.isLeftButtonDown()&&(collapsed||(e.y<24&&e.x<getWidth()-23));moved=false;
        if(dragging){dragger.startDraggingComponent(this,e);}
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if(!dragging||getParentComponent()==nullptr)return;
        if(e.getDistanceFromDragStart()<4)return;moved=true;docked=false;dragger.dragComponent(this,e,nullptr);
        auto area=placement;setTopLeftPosition(juce::jlimit(area.getX(),juce::jmax(area.getX(),area.getRight()-getWidth()),getX()),juce::jlimit(area.getY(),juce::jmax(area.getY(),area.getBottom()-getHeight()),getY()));
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if(!dragging)return;dragging=false;if(!moved){if(collapsed)toggleCollapsed();return;}floating=getPosition()-placement.getPosition();
        const int distances[]{std::abs(getX()-placement.getX()),std::abs(getRight()-placement.getRight()),std::abs(getY()-placement.getY()),std::abs(getBottom()-placement.getBottom())};int nearest=0;for(int i=1;i<4;++i)if(distances[i]<distances[nearest])nearest=i;docked=distances[nearest]<=8;if(docked)edge=juce::StringArray{"left","right","top","bottom"}[nearest];changed();
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
            else if(key=="sketchBox"||key=="sketchCircleSelect"||key=="ellipse"){if(key=="sketchBox")g.drawRect(box,1.6f);else g.drawEllipse(box,1.6f);if(key!="ellipse"){g.fillRect(x-2,y-2,4.f,4.f);g.fillRect(x+w-2,y+h-2,4.f,4.f);}}
            else if(key=="sketchFreehand"){path.startNewSubPath(x,y+h*.5f);path.cubicTo(x-4,y-4,x+w,y-4,x+w,y+h*.5f);path.cubicTo(x+w+3,y+h,x+4,y+h+4,x,y+h*.5f);g.strokePath(path,juce::PathStrokeType(1.6f));}
            else if(key=="sketchMultiple"){g.drawRect(x,y,w*.65f,h*.65f,1.5f);g.drawRect(x+w*.35f,y+h*.35f,w*.65f,h*.65f,1.5f);g.drawLine(x+w*.65f,y,x+w,y,1.5f);g.drawLine(x+w*.82f,y-3,x+w*.82f,y+3,1.5f);}
            else if(key=="sketchCopy"||key=="sketchPaste"){g.drawRect(x+5,y+5,w-5,h-5,1.6f);g.drawRect(x,y,w-5,h-5,1.6f);if(key=="sketchPaste"){g.fillRect(x+6,y-2,w-10,4.f);g.drawLine(x+9,y+10,x+w-2,y+10,1.5f);}}
            else if(key=="sketchRotate"){path.addCentredArc(x+w*.5f,y+h*.5f,w*.45f,h*.45f,0,-2.7f,2.f,true);g.strokePath(path,juce::PathStrokeType(1.6f));g.drawLine(x+w,y+h*.5f,x+w-6,y+h*.5f-3,1.6f);g.drawLine(x+w,y+h*.5f,x+w-1,y+h*.5f-7,1.6f);}
            else if(key=="sketchMirror"){g.drawLine(x+w*.5f,y,x+w*.5f,y+h,1.3f);path.addTriangle(x,y+3,x,y+h-3,x+w*.35f,y+h-3);path.addTriangle(x+w,y+3,x+w,y+h-3,x+w*.65f,y+h-3);g.strokePath(path,juce::PathStrokeType(1.5f));}
            else if(key=="sketchSplit"){g.drawLine(x,y+h,x+w,y,1.6f);g.setColour(juce::Colour(0xff78dcca));g.fillEllipse(x+w*.5f-3,y+h*.5f-3,6,6);}
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
    void changed(){resized();repaint();dock.setButtonText(docked?"Float":"Dock");setTooltipText();if(layoutChanged)layoutChanged();}
    void setTooltipText(){setTitle(collapsed?"Expand "+getName()+" (drag to move or dock)":getName());setMouseCursor(juce::MouseCursor::DraggingHandCursor);}
    bool selectionTools=false,collapsed=false,moved=false;juce::String edge="left";
    bool preferredVisible=true,docked=true,dragging=false,expanded=false;juce::Component* symmetryPanel=nullptr;juce::TextButton symmetry{"Sym"};
    juce::Rectangle<int> placement;juce::Point<int> floating{12,12};juce::ComponentDragger dragger;
    juce::TextButton close{"x"},dock{"Float"};std::vector<std::unique_ptr<Icon>> buttons;
};
