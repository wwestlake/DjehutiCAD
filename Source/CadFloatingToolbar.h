#pragma once
#include <JuceHeader.h>

// Workspace-owned presentation. Commands remain bound to existing CAD handlers.
class CadFloatingToolbar final : public juce::Component, public juce::TooltipClient
{
public:
    struct Tool { juce::String command, label; };
    std::function<void(const juce::String&)> command;
    std::function<void()> layoutChanged;
    CadFloatingToolbar(const juce::String& title,std::vector<Tool> tools,bool canRotate,juce::Point<int> start)
        : rotatable(canRotate),floating(start)
    {
        setName(title);setTitle(title);setOpaque(true);setWantsKeyboardFocus(true);
        for(const auto& tool:tools){auto b=std::make_unique<Icon>(tool.command);b->setTitle(tool.label);b->setTooltip(tool.label);b->onClick=[this,key=tool.command]{if(command)command(key);};addAndMakeVisible(*b);buttons.push_back(std::move(b));}
        close.setTooltip("Collapse "+title+"; click the compact launcher to expand");close.setTitle("Collapse "+title);close.onClick=[this]{toggleCollapsed();};addAndMakeVisible(close);
        dock.setTooltip("Dock or float; drag the grip or compact launcher to any workspace edge");dock.onClick=[this]{docked=!docked;changed();};addAndMakeVisible(dock);
        orientation.setTooltip("Switch horizontal / vertical layout");orientation.onClick=[this]{horizontal=!horizontal;changed();};addAndMakeVisible(orientation);
    }
    void setSketchContext(bool value){sketchContext=value;}
    juce::String getTooltip() override {return collapsed?"Expand "+getName()+" (Enter/Space); drag to move or dock":getName();}
    bool isCollapsed() const{return collapsed;}
    void toggleCollapsed(){collapsed=!collapsed;wanted=true;changed();}
    struct Layout {int columns=1,rows=1;juce::Point<int> size;};
    Layout layout(juce::Rectangle<int> area) const
    {
        if(collapsed)return {1,1,{36,36}};
        constexpr int pitch=36,padding=4;const int chrome=sketchContext?28:52;
        const int n=juce::jmax(1,(int)buttons.size());Layout result;
        if(isHorizontal()){
            result.columns=juce::jmax(1,juce::jmin(n,(area.getWidth()-padding*2)/pitch));result.rows=(n+result.columns-1)/result.columns;
        }else{
            const int maxRows=juce::jmax(1,(area.getHeight()-chrome)/pitch);result.columns=(n+maxRows-1)/maxRows;result.rows=(n+result.columns-1)/result.columns;
        }
        result.size={padding*2+result.columns*pitch,chrome+result.rows*pitch};return result;
    }
    juce::Point<int> preferredSize(juce::Rectangle<int> area) const{return layout(area).size;}
    void setAvailable(bool a){available=a;setVisible(available&&wanted);}
    bool isAvailable() const{return available;}
    juce::String dockingEdge() const{return sketchContext&&(edge=="left"||edge=="right")?"bottom":edge;}
    void place(juce::Rectangle<int> area,bool avail)
    {
        available=avail;placement=area;setVisible(available&&wanted);if(!isVisible()||area.isEmpty())return;
        auto size=preferredSize(area);size.x=juce::jmin(size.x,area.getWidth());size.y=juce::jmin(size.y,area.getHeight());
        auto at=floating;if(docked){at={0,0};const auto e=dockingEdge();if(e=="bottom")at.y=area.getHeight()-size.y;if(e=="right")at.x=area.getWidth()-size.x;}
        at.x=juce::jlimit(0,juce::jmax(0,area.getWidth()-size.x),at.x);at.y=juce::jlimit(0,juce::jmax(0,area.getHeight()-size.y),at.y);
        setBounds(area.getX()+at.x,area.getY()+at.y,size.x,size.y);toFront(false);
    }
    void avoid(juce::Rectangle<int> obstruction)
    {
        if(!isVisible()||docked||!getBounds().intersects(obstruction))return;
        for(const auto point:std::vector<juce::Point<int>>{{getX(),obstruction.getBottom()+6},{obstruction.getRight()+6,getY()},{obstruction.getX()-getWidth()-6,getY()},{getX(),obstruction.getY()-getHeight()-6}}){
            const auto candidate=getBounds().withPosition(point);if(placement.contains(candidate)&&!candidate.intersects(obstruction)){setTopLeftPosition(point);floating=point-placement.getPosition();return;}
        }
    }
    void reserve(juce::Rectangle<int>& area) const
    {if(!isVisible()||!docked)return;const auto e=dockingEdge();if(e=="left")area.removeFromLeft(getWidth()+4);else if(e=="right")area.removeFromRight(getWidth()+4);else if(e=="bottom")area.removeFromBottom(getHeight()+4);else area.removeFromTop(getHeight()+4);}
    bool visiblePreference() const{return wanted;}
    bool isDocked() const{return docked;}
    bool isHorizontal() const{return sketchContext||horizontal;}
    void toggleVisible(){wanted=!wanted;changed();}
    void toggleDocked(){docked=!docked;changed();}
    void refresh(const std::function<bool(const juce::String&)>& enabled,const std::function<bool(const juce::String&)>& active)
    {for(auto& b:buttons){b->setEnabled(available&&enabled(b->getName()));b->setToggleState(active(b->getName()),juce::dontSendNotification);}}
    bool invoke(const juce::String& key)
    {for(auto& b:buttons)if(b->getName()==key&&b->isEnabled()){b->onClick();return true;}return false;}
    juce::var state() const
    {
        auto* v=new juce::DynamicObject();v->setProperty("layoutVersion",2);v->setProperty("collapsed",collapsed);v->setProperty("visible",wanted);v->setProperty("showing",isVisible());v->setProperty("docked",docked);v->setProperty("horizontal",horizontal);v->setProperty("edge",edge);v->setProperty("x",floating.x);v->setProperty("y",floating.y);
        juce::Array<juce::var> controls;for(const auto& b:buttons){auto* t=new juce::DynamicObject();t->setProperty("command",b->getName());t->setProperty("label",b->getTitle());t->setProperty("tooltip",b->getTooltip());t->setProperty("enabled",b->isEnabled());t->setProperty("active",b->getToggleState());controls.add(t);}v->setProperty("controls",controls);return v;
    }
    bool configure(const juce::var& value,juce::String& error)
    {
        if(!value.isObject()){error="Toolbar preferences must be an object.";return false;}
        for(const auto& p:value.getDynamicObject()->getProperties()){
            const auto key=p.name.toString();if(!juce::StringArray{"visible","docked","horizontal","collapsed","x","y","edge"}.contains(key)){error="Unknown toolbar preference: "+key;return false;}
            if(juce::StringArray{"visible","docked","horizontal","collapsed"}.contains(key)&&!p.value.isBool()){error="Toolbar toggles must be booleans.";return false;}
            if((key=="x"||key=="y")&&(!(p.value.isInt()||p.value.isInt64()||p.value.isDouble())||!std::isfinite((double)p.value)||(double)p.value<0||(double)p.value>100000)){error="Toolbar positions must be finite nonnegative pixels.";return false;}
            if(key=="edge"&&(!p.value.isString()||!juce::StringArray{"top","bottom","left","right"}.contains(p.value.toString()))){error="Choose a viewport docking edge.";return false;}
        }
        wanted=value.getProperty("visible",wanted);docked=value.getProperty("docked",docked);horizontal=value.getProperty("horizontal",horizontal);collapsed=value.getProperty("collapsed",collapsed);floating={(int)value.getProperty("x",floating.x),(int)value.getProperty("y",floating.y)};
        edge=value.getProperty("edge",edge).toString();changed();return true;
    }
    void restore(const juce::var& value)
    {
        if(!value.isObject())return;auto copy=value.clone();auto* o=copy.getDynamicObject();const bool legacy=(int)copy.getProperty("layoutVersion",0)<2;
        for(const auto* key:{"showing","controls","layoutVersion"})o->removeProperty(key);
        if(legacy)o->setProperty("horizontal",false);juce::String error;configure(copy,error);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff19262e));g.setColour(juce::Colour(0xff627b87));g.drawRect(getLocalBounds(),1);
        if(collapsed){g.setColour(juce::Colour(isMouseOver()?0xff78dcca:0xffdcecf3));g.drawRect(9,9,18,18,1);if(rotatable){g.drawLine(9,9,18,4,1.5f);g.drawLine(18,4,27,9,1.5f);g.drawLine(18,4,18,21,1.5f);}else for(int x:{12,18,24})for(int y:{12,18,24})g.fillEllipse((float)x-1,(float)y-1,2,2);return;}
        for(int x:{8,14,20})for(int y:{7,13})g.fillEllipse((float)x,(float)y,2,2);
    }
    void resized() override
    {
        close.setVisible(!collapsed);dock.setVisible(!collapsed);orientation.setVisible(!collapsed&&!sketchContext);for(auto& b:buttons)b->setVisible(!collapsed);if(collapsed)return;
        close.setBounds(getWidth()-23,2,18,20);dock.setButtonText(sketchContext?(docked?"Float":"Dock"):(docked?"F":"D"));dock.setBounds(sketchContext?getWidth()-67:4,sketchContext?2:getHeight()-24,sketchContext?42:18,20);orientation.setButtonText(horizontal?"V":"H");orientation.setBounds(24,getHeight()-24,18,20);
        const auto grid=layout(placement.isEmpty()?getLocalBounds():placement);
        for(size_t i=0;i<buttons.size();++i){const int index=(int)i;const int col=isHorizontal()?index%grid.columns:index/grid.rows,row=isHorizontal()?index/grid.columns:index%grid.rows;buttons[i]->setBounds(6+col*36,26+row*36,32,32);}
    }
    void mouseEnter(const juce::MouseEvent&) override{repaint();}
    void mouseExit(const juce::MouseEvent&) override{repaint();}
    bool keyPressed(const juce::KeyPress& key) override{if(collapsed&&(key==juce::KeyPress::returnKey||key==juce::KeyPress::spaceKey)){toggleCollapsed();return true;}return false;}
    void mouseDown(const juce::MouseEvent& e) override
    {dragging=e.mods.isLeftButtonDown()&&(collapsed||(e.y<24&&e.x<getWidth()-23));moved=false;if(dragging)dragger.startDraggingComponent(this,e);}
    void mouseDrag(const juce::MouseEvent& e) override
    {if(!dragging||e.getDistanceFromDragStart()<4)return;moved=true;docked=false;dragger.dragComponent(this,e,nullptr);setTopLeftPosition(juce::jlimit(placement.getX(),juce::jmax(placement.getX(),placement.getRight()-getWidth()),getX()),juce::jlimit(placement.getY(),juce::jmax(placement.getY(),placement.getBottom()-getHeight()),getY()));}
    void mouseUp(const juce::MouseEvent&) override
    {
        if(!dragging)return;dragging=false;if(!moved){if(collapsed)toggleCollapsed();return;}floating=getPosition()-placement.getPosition();
        const int d[]{std::abs(getX()-placement.getX()),std::abs(getRight()-placement.getRight()),std::abs(getY()-placement.getY()),std::abs(getBottom()-placement.getBottom())};int nearest=0;for(int i=1;i<4;++i)if(d[i]<d[nearest])nearest=i;docked=d[nearest]<=8;if(docked)edge=juce::StringArray{"left","right","top","bottom"}[nearest];changed();
    }
private:
    class Icon final:public juce::Button
    {
    public:
        explicit Icon(const juce::String& key):juce::Button(key){}
        void paintButton(juce::Graphics& g,bool over,bool down) override
        {
            juce::Graphics::ScopedSaveState saved(g);
            g.setColour(getToggleState()?juce::Colour(0xff32796c):juce::Colour(over||down?0xff3b5260:0xff243540));g.fillRoundedRectangle(getLocalBounds().toFloat(),4);
            g.setColour(juce::Colour(0xffdcecf3).withAlpha(isEnabled()?1.0f:.28f));g.addTransform(juce::AffineTransform::translation((getWidth()-24)*.5f,(getHeight()-24)*.5f));
            const auto k=getName();const auto line=[&](float a,float b,float c,float d){g.drawLine(a,b,c,d,1.5f);};const auto circle=[&](float x,float y,float r){g.drawEllipse(x-r,y-r,r*2,r*2,1.5f);};
            const auto cube=[&]{line(3,8,12,3);line(12,3,21,8);line(21,8,12,13);line(12,13,3,8);line(3,8,3,19);line(3,19,12,23);line(12,23,21,19);line(21,19,21,8);line(12,13,12,23);};
            if(k=="pointer"){juce::Path p;p.startNewSubPath(4,2);p.lineTo(21,13);p.lineTo(12,14);p.lineTo(8,23);p.closeSubPath();g.strokePath(p,juce::PathStrokeType(1.5f));}
            else if(k=="boxSelect"){g.drawRect(3.f,3.f,18.f,18.f,1.5f);g.fillRect(1,1,4,4);g.fillRect(19,19,4,4);}
            else if(k=="circleSelect"){circle(12,12,9);g.fillRect(1,1,4,4);g.fillRect(19,19,4,4);}
            else if(k=="multipleSelect"){g.drawRect(2,2,13,13,1);g.drawRect(9,9,13,13,1);line(17,3,23,3);line(20,0,20,6);}
            else if(k=="bodyMode")cube();
            else if(k=="faceMode"){cube();g.fillRect(13,14,6,6);}
            else if(k=="edgeMode"){cube();g.setColour(juce::Colour(0xffff716b));line(3,8,12,3);line(12,3,21,8);}
            else if(k=="selectAll"){g.drawRect(1,1,22,22,1);for(int x:{5,14})for(int y:{5,14})g.fillRect(x,y,5,5);}
            else if(k=="clearSelection"){g.drawRect(1,1,22,22,1);line(5,5,19,19);line(5,19,19,5);}
            else if(k=="grid"){for(int x:{4,12,20})for(int y:{4,12,20})g.fillEllipse((float)x-1,(float)y-1,2,2);}
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
            else if(k=="transform"){line(12,2,12,22);line(2,12,22,12);line(12,2,9,5);line(12,2,15,5);line(12,22,9,19);line(12,22,15,19);line(2,12,5,9);line(2,12,5,15);line(22,12,19,9);line(22,12,19,15);circle(12,12,5);}
            else if(k=="mirror"){line(12,1,12,23);juce::Path p;p.addTriangle(3,6,3,19,9,19);p.addTriangle(21,6,21,19,15,19);g.strokePath(p,juce::PathStrokeType(1.5f));}
            else if(k=="linearPattern"){for(int x:{1,9,17})g.drawRect(x,8,6,8,1);line(2,21,22,21);}
            else if(k=="circularPattern"){circle(12,12,8);for(const auto p:std::vector<juce::Point<int>>{{10,1},{1,15},{18,15}})g.drawRect(p.x,p.y,5,5,1);}
            else if(k=="sweep"||k=="mill"){juce::Path p;p.startNewSubPath(3,20);p.lineTo(3,12);p.quadraticTo(3,4,13,4);p.lineTo(21,4);g.strokePath(p,juce::PathStrokeType(k=="mill"?4.f:2.f));if(k=="sweep")g.drawEllipse(0,16,8,6,1.5f);else{line(18,10,18,23);line(14,17,22,17);}}
            else if(k=="loft"){g.drawEllipse(5,2,14,5,1.5f);g.drawRect(1,17,22,6,1);line(5,5,1,20);line(19,5,23,20);}
            else if(k=="surfacePlanar"){juce::Path p;p.startNewSubPath(2,8);p.lineTo(17,3);p.lineTo(22,16);p.lineTo(7,21);p.closeSubPath();g.strokePath(p,juce::PathStrokeType(1.7f));line(5,10,19,6);line(7,15,20,11);}
            else if(k=="surfaceExtrude"){line(3,21,8,16);line(8,16,15,20);line(15,20,22,15);line(3,21,3,7);line(8,16,8,2);line(15,20,15,6);line(22,15,22,1);line(3,7,8,2);line(8,2,15,6);line(15,6,22,1);}
            else if(k=="surfaceRevolve"){line(4,1,4,23);g.drawEllipse(7,4,15,5,1.5f);g.drawEllipse(7,17,15,5,1.5f);line(7,6,7,19);line(22,6,22,19);}
            else if(k=="surfaceRuled"){juce::Path p;p.startNewSubPath(2,5);p.quadraticTo(12,11,22,2);g.strokePath(p,juce::PathStrokeType(1.8f));p.clear();p.startNewSubPath(2,18);p.quadraticTo(12,24,22,15);g.strokePath(p,juce::PathStrokeType(1.8f));for(int x:{2,7,12,17,22})line((float)x,5,(float)x,18);}
            else if(k.startsWith("surface")){for(int i=0;i<4;++i){line(2,(float)(4+i*4),21,(float)(8+i*4));line((float)(2+i*6),4,(float)(3+i*6),21);}if(k=="surface_trim"){line(2,22,22,2);}else if(k=="surface_extend"){line(12,2,12,11);line(8,6,12,2);line(16,6,12,2);}else if(k=="surface_split"){line(3,4,19,20);line(19,4,3,20);}else if(k=="surface_sew"){line(7,2,15,6);line(15,6,7,10);line(7,10,15,14);}else if(k=="surface_thicken"){line(2,1,21,5);line(21,5,21,20);}else if(k=="surface_solidify")g.drawRect(7,8,10,10,2);}
            else if(k=="subtract"||k=="union"||k=="intersect"){g.drawRect(2,3,13,13,1);g.drawRect(9,10,13,13,1);if(k=="intersect")g.fillRect(9,10,6,6);else{line(8,12,17,12);if(k=="union")line(12,8,12,17);}}
            else{cube();if(k=="cut"){line(5,2,19,2);line(12,0,12,10);}else{line(18,14,18,1);line(15,5,18,1);line(21,5,18,1);}}
        }
    };
    void changed(){resized();repaint();if(layoutChanged)layoutChanged();}
    bool available=true,rotatable=false,horizontal=false,wanted=true,docked=true,dragging=false,collapsed=false,moved=false,sketchContext=false;
    juce::String edge="left";juce::Point<int> floating;juce::Rectangle<int> placement;juce::ComponentDragger dragger;
    juce::TextButton close{"-"},dock{"F"},orientation{"H"};std::vector<std::unique_ptr<Icon>> buttons;
};
