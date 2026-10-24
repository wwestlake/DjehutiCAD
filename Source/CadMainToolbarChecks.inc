// Renders production UI and captures the existing GPU viewport diagnostic.
// Synthetic component events do not claim native-pointer interaction.
class CadMainToolbarCheck final : private juce::Timer
{
public:
    explicit CadMainToolbarCheck(std::function<void(bool,juce::var)> done)
        : service(std::make_shared<djehuti::cad::CadCommandService>()),panel(service),window("CAD toolbar and lighting validation",juce::Colours::darkgrey,0),completion(std::move(done))
    {
        folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/main-toolbar-check");folder.createDirectory();
        check(!panel.modifyPalette->isHorizontal()&&!panel.snapPalette->isHorizontal(),"Main 3D toolbars default to vertical");
        for(const auto size:std::vector<juce::Point<int>>{{1100,720},{1500,950},{1920,1080}}){panel.setSize(size.x,size.y);panel.refreshFromDocument(true);validateButtons(*panel.modifyPalette);validateButtons(*panel.snapPalette);check(!panel.modifyPalette->getBounds().intersects(panel.snapPalette->getBounds()),"Default toolbars do not overlap at "+juce::String(size.x));}
        panel.setSize(1280,850);
        {juce::String error;for(auto* bar:{panel.modifyPalette.get(),panel.snapPalette.get()}){bar->configure(juce::JSON::parse(R"({"horizontal":true})"),error);validateButtons(*bar);bar->configure(juce::JSON::parse(R"({"horizontal":false})"),error);}}

        {juce::String error;panel.modifyPalette->configure(juce::JSON::parse(R"({"docked":false,"x":100,"y":100})"),error);panel.snapPalette->configure(juce::JSON::parse(R"({"docked":false,"x":100,"y":100})"),error);check(!panel.modifyPalette->getBounds().intersects(panel.snapPalette->getBounds()),"Overlapping legacy floating positions recover without obscuring icons");panel.modifyPalette->configure(juce::JSON::parse(R"({"docked":true,"edge":"left"})"),error);panel.snapPalette->configure(juce::JSON::parse(R"({"docked":true,"edge":"right"})"),error);}
        for(auto* toolbar:{panel.modifyPalette.get(),panel.snapPalette.get()}){
            const auto original=toolbar->command;juce::String invoked;toolbar->command=[&](const auto& key){invoked=key;};toolbar->refresh([](const auto&){return true;},[](const auto&){return false;});
            const auto controls=toolbar->state()["controls"];for(const auto& c:*controls.getArray()){invoked.clear();check(toolbar->invoke(c["command"].toString())&&invoked==c["command"].toString(),"Existing icon binding: "+c["command"].toString());}
            toolbar->command=original;
            const auto expanded=toolbar->getBounds();const auto revision=service->document().revision();toolbar->toggleCollapsed();
            check(toolbar->isVisible()&&toolbar->getWidth()==36&&toolbar->getHeight()==36,"Collapse leaves compact launcher: "+toolbar->getName());
            toolbar->mouseDown(event(*toolbar,{16,16},juce::ModifierKeys::leftButtonModifier,{16,16}));toolbar->mouseUp(event(*toolbar,{16,16},0,{16,16}));
            check(!toolbar->isCollapsed()&&toolbar->getBounds()==expanded&&service->document().revision()==revision,"Launcher click restores bounds without changing document");
        }
        auto& toolbar=*panel.modifyPalette;juce::String error;
        toolbar.configure(juce::JSON::parse(R"({"docked":false,"x":130,"y":50})"),error);const auto floating=toolbar.getBounds();
        toolbar.toggleCollapsed();toolbar.toggleCollapsed();check(toolbar.getBounds()==floating,"Floating position and dimensions survive collapse");
        toolbar.toggleCollapsed();const auto old=toolbar.getPosition();toolbar.mouseDown(event(toolbar,{16,16},juce::ModifierKeys::leftButtonModifier,{16,16}));toolbar.mouseDrag(event(toolbar,{50,60},juce::ModifierKeys::leftButtonModifier,{16,16}));toolbar.mouseUp(event(toolbar,{50,60},0,{16,16}));check(toolbar.isCollapsed()&&toolbar.getPosition()!=old,"Collapsed launcher drags without expanding");
        for(const auto& edge:juce::StringArray{"left","right","top","bottom"}){
            const auto area=panel.modelView.getLocalBounds().withTrimmedTop(30).withTrimmedBottom(28);
            toolbar.mouseDown(event(toolbar,{16,16},juce::ModifierKeys::leftButtonModifier,{16,16}));toolbar.mouseDrag(event(toolbar,{50,60},juce::ModifierKeys::leftButtonModifier,{16,16}));
            toolbar.setTopLeftPosition(edge=="left"?area.getX():edge=="right"?area.getRight()-36:area.getCentreX(),edge=="top"?area.getY():edge=="bottom"?area.getBottom()-36:area.getCentreY());toolbar.mouseUp(event(toolbar,{50,60},0,{16,16}));
            check(toolbar.isDocked()&&toolbar.state()["edge"].toString()==edge,"Launcher docks at "+edge);
            toolbar.toggleCollapsed();validateButtons(toolbar);check(area.contains(toolbar.getBounds()),"Expanded toolbar remains accessible at "+edge);toolbar.toggleCollapsed();
        }
        const auto saved=panel.viewState();{CadWorkspacePanel restored(service);restored.setSize(1280,850);restored.restoreViewState(saved);check(restored.modifyPalette->isCollapsed()&&restored.modifyPalette->state()["edge"].toString()=="bottom","Collapsed state and edge persist");}
        panel.executeUiCommand("modifyToolbar");check(!toolbar.isVisible(),"View can intentionally hide toolbar");panel.executeUiCommand("modifyToolbar");check(toolbar.isVisible(),"View restores toolbar without restart");
        toolbar.configure(juce::JSON::parse(R"({"collapsed":false,"docked":true,"edge":"left"})"),error);panel.snapPalette->configure(juce::JSON::parse(R"({"collapsed":false,"docked":true,"edge":"right"})"),error);
        check(service->createCylinder(30,60,body,error),"Create curved object with ordinary CAD command");panel.refreshFromDocument(true);panel.executeUiCommand("extrude");const auto task=panel.task;toolbar.toggleCollapsed();toolbar.toggleCollapsed();check(panel.task==task,"Active modeling operation survives collapse");panel.cancelTask();
        panel.modelView.setHighlights({{body,juce::Colour(0xffe9ad62)}});panel.modelView.fitAll();
        saveImage(panel.createComponentSnapshot(panel.getLocalBounds()),"workspace-components.png");toolbar.toggleCollapsed();panel.snapPalette->toggleCollapsed();saveImage(panel.createComponentSnapshot(panel.getLocalBounds()),"collapsed-components.png");toolbar.toggleCollapsed();panel.snapPalette->toggleCollapsed();
        window.setUsingNativeTitleBar(true);window.setContentNonOwned(&panel,false);window.setBounds(70,70,1280,880);window.setVisible(true);
        startTimer(350);
    }
private:
    static juce::MouseEvent event(juce::Component& c,juce::Point<float> p,int mods,juce::Point<float> down){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys(mods),1,0,0,0,0,&c,&c,juce::Time::getCurrentTime(),down,juce::Time::getCurrentTime(),1,true);}
    void check(bool ok,const juce::String& name){auto* v=new juce::DynamicObject();v->setProperty("name",name);v->setProperty("ok",ok);checks.add(v);if(!ok)++failures;}
    void saveImage(const juce::Image& image,const juce::String& name){if(auto out=folder.getChildFile(name).createOutputStream()){out->setPosition(0);out->truncate();juce::PNGImageFormat().writeImageToStream(image,*out);}}
    void validateButtons(CadFloatingToolbar& toolbar)
    {
        bool full=true,overlap=false;int count=0;juce::Array<juce::Rectangle<int>> bounds;const auto controls=toolbar.state()["controls"];
        for(auto* c:toolbar.getChildren())if(dynamic_cast<juce::Button*>(c)&&controls.size()>0){bool tool=false;for(const auto& v:*controls.getArray())if(v["command"].toString()==c->getName())tool=true;if(!tool)continue;++count;
            full=full&&c->getWidth()==32&&c->getHeight()==32&&toolbar.getLocalBounds().contains(c->getBounds())&&dynamic_cast<juce::Button*>(c)->getTooltip().isNotEmpty();for(const auto& b:bounds)overlap=overlap||b.intersects(c->getBounds());bounds.add(c->getBounds());}
        check(full&&!overlap&&count==(toolbar.getName()=="Modify toolbar"?23:9),"Square, contained, spaced icons and tooltips: "+toolbar.getName());
        const auto image=toolbar.createComponentSnapshot(toolbar.getLocalBounds());bool opaque=true;for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x)opaque=opaque&&image.getPixelAt(x,y).getAlpha()==255;check(opaque&&toolbar.isOpaque(),"Rendered toolbar background is fully opaque");
    }
    void timerCallback() override
    {
        if(++ticks>60){check(false,"GPU capture completed within bounded wait: "+error);finish();return;}
        panel.modelView.prepareFrame(0,(std::uint64_t)ticks);
        if(stage==0){
            if(!panel.modelView.diagnostics().contains("OpenGL "))return;
            auto camera=juce::JSON::parse("{\"yawDeg\":"+juce::String(angle*90)+",\"pitchDeg\":25}");
            if(!panel.modelView.controlCamera(camera,error)){check(false,"Camera orbit accepted: "+error);finish();return;}stage=1;return;
        }
        const auto file=folder.getChildFile(juce::StringArray{"front.png","side.png","back.png","other-side.png"}[angle]);
        if(stage==1){file.deleteFile();auto args=juce::JSON::parse(R"({"operation":"capture"})");args.getDynamicObject()->setProperty("path",file.getFullPathName());
            if(!panel.modelView.controlCamera(args,error))return;stage=2;return;
        }
        const auto image=juce::ImageFileFormat::loadFrom(file);if(!image.isValid())return;
        std::vector<int> brightness;for(int y=1;y<image.getHeight()-1;++y)for(int x=1;x<image.getWidth()-1;++x){const auto c=image.getPixelAt(x,y);if(c.getRed()>1.15*c.getGreen()&&c.getGreen()>1.4*c.getBlue()&&c.getRed()>70)brightness.push_back(c.getRed());}
        std::sort(brightness.begin(),brightness.end());const int low=brightness.empty()?0:brightness[brightness.size()/20],high=brightness.empty()?0:brightness[brightness.size()*19/20];
        check(brightness.size()>1000&&low>=90&&high-low>=12,"GPU illumination retains visible fill and curved gradients at yaw "+juce::String(angle*90)+" (P5="+juce::String(low)+", P95="+juce::String(high)+")");
        if(angle==0){
            // Diagnostic composition of actual GL model pass and actual component overlay.
            auto combined=panel.createComponentSnapshot(panel.getLocalBounds());juce::Graphics g(combined);const auto bounds=panel.getLocalArea(&panel.modelView,panel.modelView.getLocalBounds());g.drawImage(image,bounds.toFloat());g.drawImageAt(panel.modelView.createComponentSnapshot(panel.modelView.getLocalBounds()),bounds.getX(),bounds.getY());saveImage(combined,"gpu-and-components.png");
            panel.tooltipWindow.displayTip(window.getScreenPosition()+juce::Point<int>{150,150},panel.modifyPalette->state()["controls"][0]["tooltip"].toString());saveImage(panel.tooltipWindow.createComponentSnapshot(panel.tooltipWindow.getLocalBounds()),"tooltip.png");panel.tooltipWindow.hideTip();
        }
        if(++angle==4){check(!panel.modelView.diagnostics().containsIgnoreCase("shader error"),"Both depth shaders compile with common lighting");finish();return;}stage=0;
    }
    void finish()
    {
        stopTimer();window.clearContentComponent();window.setVisible(false);auto* result=new juce::DynamicObject();result->setProperty("ok",failures==0);result->setProperty("passed",checks.size()-failures);result->setProperty("failed",failures);result->setProperty("checks",checks);result->setProperty("nativePointer",false);result->setProperty("renderFolder",folder.getFullPathName());juce::var report(result);
        auto done=std::move(completion);const bool ok=failures==0;juce::MessageManager::callAsync([this,done=std::move(done),report,ok]{delete this;done(ok,report);});
    }
    std::shared_ptr<djehuti::cad::CadCommandService> service;CadWorkspacePanel panel;juce::DocumentWindow window;std::function<void(bool,juce::var)> completion;
    juce::File folder;juce::Array<juce::var> checks;int failures=0,ticks=0,stage=0,angle=0;juce::String error,body;
};
void startMainToolbarCheck(std::function<void(bool,juce::var)> done){new CadMainToolbarCheck(std::move(done));}
