#pragma once
#include <JuceHeader.h>

// Presentation-only overlay, following SketchToolPalette's in-workspace docking/drag conventions.
class CadFloatingToolbar final : public juce::Component
{
public:
    struct Tool { juce::String command, label; };
    std::function<void(const juce::String&)> command;
    std::function<void()> layoutChanged;
    CadFloatingToolbar(const juce::String& title,std::vector<Tool> tools,bool canRotate,juce::Point<int> start)
        : rotatable(canRotate),floating(start)
    {
        setName(title);setTitle(title);
        for(const auto& tool:tools){auto b=std::make_unique<Icon>(tool.command);b->setTitle(tool.label);b->setTooltip(tool.label);b->onClick=[this,key=tool.command]{if(command)command(key);};addAndMakeVisible(*b);buttons.push_back(std::move(b));}
        close.setTooltip("Hide "+title+"; restore it from View");close.onClick=[this]{wanted=false;changed();};addAndMakeVisible(close);
        dock.setTooltip("Dock to the viewport edge or float; dragging to an edge also docks");dock.onClick=[this]{docked=!docked;changed();};addAndMakeVisible(dock);
        orientation.setTooltip("Switch horizontal / vertical orientation");orientation.onClick=[this]{horizontal=!horizontal;edge=horizontal?"top":"left";changed();};if(rotatable)addAndMakeVisible(orientation);
    }
    juce::Point<int> preferredSize(juce::Rectangle<int> area) const
    {
        const int n=(int)buttons.size();
        if(horizontal){const int columns=juce::jmax(1,juce::jmin(n,(area.getWidth()-8)/32));return {juce::jmin(area.getWidth(),juce::jmax(112,columns*32+8)),24+((n+columns-1)/columns)*34+4};}
        const int rows=juce::jmax(1,juce::jmin(n,(area.getHeight()-28)/34));return {juce::jmax(96,((n+rows-1)/rows)*34+8),24+rows*34+4};
    }
    void setAvailable(bool a) { available=a; setVisible(available && wanted); }
    bool isAvailable() const { return available; }
    void place(juce::Rectangle<int> area,bool avail)
    {
        available=avail;placement=area;setVisible(available&&wanted);if(!isVisible()||area.isEmpty())return;
        auto size=preferredSize(area);size.x=juce::jmin(size.x,area.getWidth());size.y=juce::jmin(size.y,area.getHeight());
        auto at=floating;if(docked){at={0,0};if(edge=="bottom")at.y=area.getHeight()-size.y;if(edge=="right")at.x=area.getWidth()-size.x;}
        at.x=juce::jlimit(0,juce::jmax(0,area.getWidth()-size.x),at.x);at.y=juce::jlimit(0,juce::jmax(0,area.getHeight()-size.y),at.y);
        setBounds(area.getX()+at.x,area.getY()+at.y,size.x,size.y);toFront(false);
    }
    void reserve(juce::Rectangle<int>& area) const
    {
        if(!isVisible()||!docked)return;
        if(edge=="left")area.removeFromLeft(getWidth()+4);else if(edge=="right")area.removeFromRight(getWidth()+4);else if(edge=="bottom")area.removeFromBottom(getHeight()+4);else area.removeFromTop(getHeight()+4);
    }
    bool visiblePreference() const{return wanted;}
    bool isDocked() const{return docked;}
    bool isHorizontal() const{return horizontal;}
    void toggleVisible(){wanted=!wanted;changed();}
    void toggleDocked(){docked=!docked;changed();}
    void refresh(const std::function<bool(const juce::String&)>& enabled,const std::function<bool(const juce::String&)>& active)
    {for(auto& b:buttons){b->setEnabled(available&&enabled(b->getName()));b->setToggleState(active(b->getName()),juce::dontSendNotification);}}
    bool invoke(const juce::String& key)
    {for(auto& b:buttons)if(b->getName()==key&&b->isEnabled()){b->onClick();return true;}return false;}
    juce::var state() const
    {
        auto* v=new juce::DynamicObject();v->setProperty("visible",wanted);v->setProperty("showing",isVisible());v->setProperty("docked",docked);v->setProperty("horizontal",horizontal);v->setProperty("edge",edge);v->setProperty("x",floating.x);v->setProperty("y",floating.y);
        juce::Array<juce::var> controls;for(const auto& b:buttons){auto* t=new juce::DynamicObject();t->setProperty("command",b->getName());t->setProperty("label",b->getTitle());t->setProperty("enabled",b->isEnabled());t->setProperty("active",b->getToggleState());controls.add(t);}v->setProperty("controls",controls);return v;
    }
    bool configure(const juce::var& value,juce::String& error)
    {
        if(!value.isObject()){error="Toolbar preferences must be an object.";return false;}
        for(const auto& p:value.getDynamicObject()->getProperties())
        {
            const auto key=p.name.toString();if(!juce::StringArray{"visible","docked","horizontal","x","y","edge"}.contains(key)){error="Unknown toolbar preference: "+key;return false;}
            if(juce::StringArray{"visible","docked","horizontal"}.contains(key)&&!p.value.isBool()){error="Toolbar toggles must be booleans.";return false;}
            if((key=="x"||key=="y")&&(!(p.value.isInt()||p.value.isInt64()||p.value.isDouble())||!std::isfinite((double)p.value)||(double)p.value<0||(double)p.value>100000)){error="Toolbar positions must be finite nonnegative pixels.";return false;}
            if(key=="edge"&&(!p.value.isString()||!juce::StringArray{"top","bottom","left","right"}.contains(p.value.toString()))){error="Choose a viewport docking edge.";return false;}
        }
        if(!rotatable&&((value.hasProperty("horizontal")&&!(bool)value["horizontal"])||(value.hasProperty("edge")&&juce::StringArray{"left","right"}.contains(value["edge"].toString())))){error="Snap toolbar is horizontal and docks at the top or bottom.";return false;}
        wanted=value.getProperty("visible",wanted);docked=value.getProperty("docked",docked);horizontal=value.getProperty("horizontal",horizontal);floating={(int)value.getProperty("x",floating.x),(int)value.getProperty("y",floating.y)};
        edge=value.getProperty("edge",horizontal?"top":"left").toString();changed();return true;
    }
    void restore(const juce::var& value)
    {if(!value.isObject())return;auto copy=juce::JSON::parse(juce::JSON::toString(value));copy.getDynamicObject()->removeProperty("showing");copy.getDynamicObject()->removeProperty("controls");juce::String error;configure(copy,error);}
    void paint(juce::Graphics& g) override
    {g.setColour(juce::Colour(0xf018252e));g.fillRoundedRectangle(getLocalBounds().toFloat(),5);g.setColour(juce::Colour(0xff627b87));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f),5,1);for(int x:{8,14,20})for(int y:{7,13})g.fillEllipse((float)x,(float)y,2,2);}
    void resized() override
    {
        close.setBounds(getWidth()-21,2,18,20);dock.setButtonText(horizontal?(docked?"Float":"Dock"):(docked?"F":"D"));dock.setBounds(getWidth()-(horizontal?65:43),2,horizontal?42:20,20);orientation.setButtonText(horizontal?"V":"H");orientation.setBounds(getWidth()-(horizontal?87:65),2,20,20);
        const auto area=getLocalBounds().withTrimmedTop(24).reduced(4,2);const int columns=horizontal?juce::jmax(1,area.getWidth()/32):juce::jmax(1,(int)std::ceil((double)buttons.size()/juce::jmax(1,area.getHeight()/34)));
        for(size_t i=0;i<buttons.size();++i)buttons[i]->setBounds(area.getX()+(horizontal?0:(area.getWidth()-columns*32)/2)+(int)(i%(size_t)columns)*32,area.getY()+(int)(i/(size_t)columns)*34,30,32);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {dragging=e.mods.isLeftButtonDown()&&e.y<24&&e.x<getWidth()-(horizontal?(rotatable?90:68):68);if(dragging){docked=false;dragger.startDraggingComponent(this,e);}}
    void mouseDrag(const juce::MouseEvent& e) override
    {if(!dragging)return;dragger.dragComponent(this,e,nullptr);setTopLeftPosition(juce::jlimit(placement.getX(),juce::jmax(placement.getX(),placement.getRight()-getWidth()),getX()),juce::jlimit(placement.getY(),juce::jmax(placement.getY(),placement.getBottom()-getHeight()),getY()));}
    void mouseUp(const juce::MouseEvent&) override
    {if(!dragging)return;dragging=false;floating=getPosition()-placement.getPosition();docked=false;if(horizontal){if(floating.y<=8){edge="top";docked=true;}else if(getBottom()>=placement.getBottom()-8){edge="bottom";docked=true;}}else{if(floating.x<=8){edge="left";docked=true;}else if(getRight()>=placement.getRight()-8){edge="right";docked=true;}}changed();}
private:
    class Icon final:public juce::Button
    {
    public:
        explicit Icon(const juce::String& key):juce::Button(key){}
        void paintButton(juce::Graphics& g,bool over,bool down) override
        {
            g.setColour(getToggleState()?juce::Colour(0xff32796c):juce::Colour(over||down?0xff3b5260:0xff243540));g.fillRoundedRectangle(getLocalBounds().toFloat(),4);
            g.setColour(juce::Colour(0xffdcecf3).withAlpha(isEnabled()?1.0f:.28f));g.addTransform(juce::AffineTransform::translation((getWidth()-24)*.5f,(getHeight()-24)*.5f));
            const auto k=getName();const auto line=[&](float a,float b,float c,float d){g.drawLine(a,b,c,d,1.5f);};const auto circle=[&](float x,float y,float r){g.drawEllipse(x-r,y-r,r*2,r*2,1.5f);};
            const auto cube=[&]{line(3,8,12,3);line(12,3,21,8);line(21,8,12,13);line(12,13,3,8);line(3,8,3,19);line(3,19,12,23);line(12,23,21,19);line(21,19,21,8);line(12,13,12,23);};
            if(k=="grid"){for(int x:{4,12,20})for(int y:{4,12,20})g.fillEllipse((float)x-1,(float)y-1,2,2);}
            else if(k=="vertex"){line(3,20,12,6);line(12,6,22,18);g.drawRect(9,3,6,6,1);}
            else if(k=="edge"){line(2,20,22,4);circle(12,12,3);}
            else if(k=="midpoint"){line(2,17,22,17);juce::Path p;p.addTriangle(12,7,7,15,17,15);g.strokePath(p,juce::PathStrokeType(1.5f));}
            else if(k=="center"){circle(12,12,8);line(12,2,12,22);line(2,12,22,12);}
            else if(k=="intersection"){line(2,3,22,21);line(2,21,22,3);circle(12,12,3);}
            else if(k=="face"){juce::Path p;p.startNewSubPath(2,7);p.lineTo(18,3);p.lineTo(23,17);p.lineTo(7,22);p.closeSubPath();g.strokePath(p,juce::PathStrokeType(1.5f));circle(12,12,2);}
            else if(k=="off"){circle(12,12,8);line(3,21,21,3);}
            else if(k=="references"){juce::Path p;p.startNewSubPath(1,12);p.quadraticTo(12,-2,23,12);p.quadraticTo(12,26,1,12);g.strokePath(p,juce::PathStrokeType(1.5f));circle(12,12,4);}
            else if(k=="fillet"||k=="chamfer"){juce::Path p;p.startNewSubPath(3,22);p.lineTo(3,9);if(k=="fillet")p.quadraticTo(3,3,9,3);else p.lineTo(9,3);p.lineTo(22,3);g.strokePath(p,juce::PathStrokeType(2));line(9,22,9,9);line(9,9,22,9);}
            else if(k=="revolve"||k=="lathe"){line(12,1,12,23);if(k=="lathe"){line(4,5,20,5);line(4,19,20,19);line(4,5,7,8);line(7,8,7,16);line(7,16,4,19);line(20,5,17,8);line(17,8,17,16);line(17,16,20,19);}else{juce::Path p;p.addCentredArc(12,12,9,8,0,-2.5f,2.5f,true);g.strokePath(p,juce::PathStrokeType(1.6f));line(19,6,22,9);line(22,9,17,9);}}
            else if(k=="hole"){g.drawRect(2,12,20,10,1);circle(12,16,4);line(12,1,12,12);line(8,7,12,12);line(16,7,12,12);}
            else if(k=="mirror"){line(12,1,12,23);juce::Path p;p.addTriangle(3,6,3,19,9,19);p.addTriangle(21,6,21,19,15,19);g.strokePath(p,juce::PathStrokeType(1.5f));}
            else if(k=="linearPattern"){for(int x:{1,9,17})g.drawRect(x,8,6,8,1);line(2,21,22,21);}
            else if(k=="circularPattern"){circle(12,12,8);for(const auto p:std::vector<juce::Point<int>>{{10,1},{1,15},{18,15}})g.drawRect(p.x,p.y,5,5,1);}
            else if(k=="sweep"||k=="mill"){juce::Path p;p.startNewSubPath(3,20);p.lineTo(3,12);p.quadraticTo(3,4,13,4);p.lineTo(21,4);g.strokePath(p,juce::PathStrokeType(k=="mill"?4.f:2.f));if(k=="sweep")g.drawEllipse(0,16,8,6,1.5f);else{line(18,10,18,23);line(14,17,22,17);}}
            else if(k=="loft"){g.drawEllipse(5,2,14,5,1.5f);g.drawRect(1,17,22,6,1);line(5,5,1,20);line(19,5,23,20);}
            else if(k.startsWith("surface")){for(int i=0;i<4;++i){line(2,(float)(4+i*4),21,(float)(8+i*4));line((float)(2+i*6),4,(float)(3+i*6),21);}if(k=="surface_trim"){line(2,22,22,2);}else if(k=="surface_extend"){line(12,2,12,11);line(8,6,12,2);line(16,6,12,2);}else if(k=="surface_sew"){line(7,2,15,6);line(15,6,7,10);line(7,10,15,14);}else if(k=="surface_thicken"){line(2,1,21,5);line(21,5,21,20);}else if(k=="surface_solidify")g.drawRect(7,8,10,10,2);}
            else if(k=="subtract"||k=="union"||k=="intersect"){g.drawRect(2,3,13,13,1);g.drawRect(9,10,13,13,1);if(k=="intersect")g.fillRect(9,10,6,6);else{line(8,12,17,12);if(k=="union")line(12,8,12,17);}}
            else{cube();if(k=="cut"){line(5,2,19,2);line(12,0,12,10);}else{line(18,14,18,1);line(15,5,18,1);line(21,5,18,1);}}
        }
    };
    void changed(){resized();if(layoutChanged)layoutChanged();}
    bool available=true,rotatable=false,horizontal=true,wanted=true,docked=false,dragging=false;juce::String edge="top";juce::Point<int> floating;juce::Rectangle<int> placement;juce::ComponentDragger dragger;juce::TextButton close{"x"},dock{"Dock"},orientation{"V"};std::vector<std::unique_ptr<Icon>> buttons;
};
