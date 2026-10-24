// Production component checks: synthetic JUCE events, not native pointer automation.
bool runSketchToolboxCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;int failures=0;juce::String error;
    const auto check=[&](bool ok,const juce::String& name){auto* v=new juce::DynamicObject();v->setProperty("name",name);v->setProperty("ok",ok);checks.add(v);if(!ok)++failures;};
    auto service=std::make_shared<CadCommandService>();juce::String sketch,line;
    check(service->createSketch("XY",0,sketch,error)&&service->addSketchLine(sketch,0,0,20,10,line,error),"Create representative sketch");
    CadWorkspacePanel panel(service);panel.setSize(1500,950);panel.refreshFromDocument(true);panel.enterSketch(sketch);panel.selectSketchEntity(line,false);panel.handleUpdateNowIfNeeded();
    auto& drawing=*panel.sketchPalette;auto& selection=*panel.selectionPalette;
    const auto event=[](juce::Component& c,juce::Point<float> p,int mods,juce::Point<float> down){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys(mods),1,0,0,0,0,&c,&c,juce::Time::getCurrentTime(),down,juce::Time::getCurrentTime(),1,true);};
    const juce::StringArray keys{"sketchSelect","sketchBox","sketchCircleSelect","sketchFreehand","sketchMultiple","sketchCopy","sketchPaste","sketchRotate","sketchMirror","sketchSplit","ellipse"};
    // Verify real buttons retain their command identity, tooltip and compact bounds.
    for(const auto& key:keys){juce::Button* found=nullptr;for(auto* c:selection.getChildren())if(c->getName()==key)found=dynamic_cast<juce::Button*>(c);check(found&&found->isVisible()&&found->getTooltip().isNotEmpty()&&found->getWidth()<=48,"Icon and tooltip: "+key);}
    const auto actualCommand=selection.command;juce::String invoked;
    selection.command=[&](const juce::String& key){invoked=key;};
    selection.refresh([](const auto&){return true;},[](const auto&){return false;});
    for(const auto& key:keys){invoked.clear();check(selection.invoke(key)&&invoked==key,"Icon dispatch: "+key);}
    selection.command=actualCommand;
    panel.executeUiCommand("sketchRotate");panel.handleUpdateNowIfNeeded();
    const auto active=panel.sketchCanvas->tool();const auto selected=panel.sketchSelection;const auto revision=service->document().revision();
    const auto dimensions=selection.getBounds();
    juce::Button* collapse=nullptr;for(auto* c:selection.getChildren())if(auto* b=dynamic_cast<juce::Button*>(c))if(b->getTitle()=="Collapse toolbox")collapse=b;
    check(collapse!=nullptr,"Header has a labelled collapse control");if(collapse)collapse->onClick();
    check(selection.isVisible()&&selection.isCollapsed()&&selection.getWidth()==36&&selection.getHeight()==36,"Collapse leaves a compact visible launcher");
    check(!drawing.isCollapsed(),"Toolboxes collapse independently");
    selection.mouseDown(event(selection,{16,16},juce::ModifierKeys::leftButtonModifier,{16,16}));selection.mouseUp(event(selection,{16,16},0,{16,16}));
    check(!selection.isCollapsed()&&selection.getBounds()==dimensions,"Launcher click restores dimensions and floating position");
    check(panel.sketchCanvas->tool()==active&&panel.sketchSelection==selected&&service->document().revision()==revision,"Active tool, selection and document survive collapse");
    drawing.restore(juce::JSON::parse(R"({"visible":true,"docked":false,"x":140,"y":30,"symmetryExpanded":true})"));
    const auto expanded=drawing.getBounds();drawing.toggleCollapsed();check((bool)drawing.state()["symmetryExpanded"],"Symmetry settings survive collapse");drawing.toggleCollapsed();
    check(drawing.getBounds()==expanded,"Expanded symmetry toolbox dimensions and position restore");
    selection.restore(juce::JSON::parse(R"({"visible":true,"docked":false,"x":170,"y":45,"collapsed":true})"));
    const auto oldPosition=selection.getPosition();selection.mouseDown(event(selection,{16,16},juce::ModifierKeys::leftButtonModifier,{16,16}));selection.mouseDrag(event(selection,{66,56},juce::ModifierKeys::leftButtonModifier,{16,16}));selection.mouseUp(event(selection,{66,56},0,{16,16}));
    check(selection.isCollapsed()&&!selection.isDocked()&&selection.getPosition()!=oldPosition,"Collapsed launcher drags without expanding");
    // Dock through the same header/launcher drag path used in the workspace.
    for(const auto& edge:juce::StringArray{"left","right","top","bottom"}){
        selection.restore(juce::JSON::parse(R"({"visible":true,"docked":false,"x":170,"y":45,"collapsed":true})"));
        auto area=panel.centre.getLocalBounds();panel.snapPalette->reserve(area);
        const juce::Point<int> target{edge=="left"?area.getX():edge=="right"?area.getRight()-36:area.getCentreX(),edge=="top"?area.getY():edge=="bottom"?area.getBottom()-36:area.getCentreY()};
        selection.mouseDown(event(selection,{16,16},juce::ModifierKeys::leftButtonModifier,{16,16}));selection.mouseDrag(event(selection,{46,46},juce::ModifierKeys::leftButtonModifier,{16,16}));selection.setTopLeftPosition(target);selection.mouseUp(event(selection,{46,46},0,{16,16}));
        check(selection.isDocked()&&selection.state()["edge"].toString()==edge&&selection.isCollapsed(),"Collapsed drag docks at "+edge);
        selection.toggleCollapsed();check(selection.isDocked()&&selection.isVisible()&&panel.centre.getLocalBounds().contains(selection.getBounds()),"Expanded toolbox stays accessible at "+edge);
        check(!selection.getBounds().intersects(panel.sketchCanvas->getBounds()),"Docked toolbox reserves canvas at "+edge);
    }
    selection.toggleCollapsed();const auto preferences=panel.viewState();
    {CadWorkspacePanel restored(service);restored.setSize(1500,950);restored.restoreViewState(preferences);restored.enterSketch(sketch);check(restored.selectionPalette->isCollapsed()&&restored.selectionPalette->isVisible()&&restored.selectionPalette->state()["edge"].toString()=="bottom","Collapse and edge survive workspace preference restoration");}
    panel.executeUiCommand("selectionToolbox");check(!selection.isVisible(),"View menu can intentionally hide Selection toolbox");panel.executeUiCommand("selectionToolbox");check(selection.isVisible(),"View menu restores Selection toolbox without restarting");
    selection.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));check(!selection.isCollapsed(),"Keyboard expands collapsed launcher");
    panel.executeUiCommand("line");panel.handleUpdateNowIfNeeded();check(panel.uiToolActive("line"),"Existing drawing command remains active");
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/sketch-toolbox-check");folder.createDirectory();
    drawing.restore(juce::JSON::parse(R"({"visible":true,"docked":true})"));selection.restore(juce::JSON::parse(R"({"visible":true,"docked":true,"edge":"right"})"));panel.sketchCanvas->fitToContent();
    const auto capture=[&](const juce::String& name){const auto image=panel.createComponentSnapshot(panel.getLocalBounds());if(auto stream=folder.getChildFile(name).createOutputStream()){stream->setPosition(0);stream->truncate();juce::PNGImageFormat().writeImageToStream(image,*stream);}};
    capture("expanded.png");drawing.toggleCollapsed();selection.toggleCollapsed();capture("collapsed.png");
    auto* result=new juce::DynamicObject();result->setProperty("ok",failures==0);result->setProperty("passed",checks.size()-failures);result->setProperty("failed",failures);result->setProperty("checks",checks);report=result;return failures==0;
}
