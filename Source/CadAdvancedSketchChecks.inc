// Focused geometry and production-widget regression; never starts the mechanical runtime.
bool runAdvancedSketchCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks;int failures=0;juce::String error;
    const auto check=[&](bool ok,const juce::String& name){auto* value=new juce::DynamicObject();value->setProperty("name",name);value->setProperty("ok",ok);if(!ok){++failures;value->setProperty("error",error);}checks.add(value);};
    const auto approximately=[](double a,double b){return std::abs(a-b)<1e-8;};
    CadSketchEntity curve;curve.type="bezier";curve.x1=-8;curve.y1=3;curve.cx1=2;curve.cy1=37;curve.cx2=32;curve.cy2=-19;curve.x2=42;curve.y2=7;
    for(const double t:{.1,.37,.5,.91})
    {
        const auto halves=sketch_edit::subdivide(curve,t);bool exact=true;
        for(int i=0;i<=100;++i){const auto u=i/100.;exact=exact&&sketch_edit::evaluate(halves.first,u).getDistanceFrom(sketch_edit::evaluate(curve,u*t))<1e-10&&sketch_edit::evaluate(halves.second,u).getDistanceFrom(sketch_edit::evaluate(curve,t+u*(1-t)))<1e-10;}
        check(exact,"Exact de Casteljau subdivision t="+juce::String(t));
    }
    const auto constrained=sketch_edit::constrained({4,7},{15,20},45);
    check(approximately(constrained.x-4,constrained.y-7),"Exact 45-degree drawing projection");
    auto service=std::make_shared<CadCommandService>();juce::String sketch,line,bezier,circle,rectangle;
    check(service->createSketch("XY",0,sketch,error),"Create sketch");
    check(service->addSketchLine(sketch,0,0,20,0,line,error),"Create line");
    check(service->addSketchBezier(sketch,curve.x1,curve.y1,curve.cx1,curve.cy1,curve.cx2,curve.cy2,curve.x2,curve.y2,bezier,error),"Create editable Bezier");
    check(service->addSketchCircle(sketch,60,20,10,circle,error),"Create circle");
    check(service->addSketchRectangle(sketch,80,10,20,15,rectangle,error),"Create rectangle group");
    const auto parameters=[](const juce::String& json){return juce::JSON::parse(json);};
    juce::StringArray result;
    const auto op=[&](juce::String operation,juce::StringArray ids,juce::String json){return service->document().editSketchGeometry(sketch,ids,operation,parameters(json),result,error);};
    check(op("split",{bezier},R"({"t":0.37})"),"Split curve in existing undoable document path");
    check(result.size()==2,"Curve split produces independently editable segments");
    const auto revision=service->document().revision();
    check(!op("split",{line},R"({"t":1})")&&service->document().revision()==revision,"Reject endpoint split without a transaction");
    check(op("split",{line},R"({"t":0.4})"),"Split line into joined segments");
    check(service->undo(error)&&service->redo(error),"Split participates in undo/redo");
    check(op("resize_ellipse",{circle},R"({"rx":14,"ry":6})"),"Stretch circle into ellipse");
    const auto entity=[&](const juce::String& id){for(const auto& e:service->document().findSketch(sketch)->entities)if(e.id==id)return e;return CadSketchEntity();};
    check(entity(circle).type=="ellipse"&&approximately(entity(circle).radiusY,6),"Ellipse radii retained");
    juce::StringArray rect;
    for(const auto& e:service->document().findSketch(sketch)->entities)if(e.groupId==rectangle)rect.add(e.id);
    check(op("rotate",rect,R"({"x":80,"y":10,"angle":30})"),"Rotate constrained rectangle as one group around arbitrary centre");
    check(approximately(entity(rect[0]).x2,80+20*std::cos(juce::MathConstants<double>::pi/6)),"Rotation preserves geometry instead of realigning rectangle");
    const auto beforeMirror=service->document().findSketch(sketch)->entities.size();
    check(op("mirror",{circle,bezier},R"({"ax":0,"ay":0,"bx":1,"by":1})"),"Mirror mixed selection about arbitrary line");
    check(service->document().findSketch(sketch)->entities.size()==beforeMirror+2,"Mirror creates independent editable geometry");
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/advanced-sketch-check");folder.createDirectory();const auto file=folder.getChildFile("roundtrip.djc");
    check(service->save(file,error),"Save advanced sketch through native document persistence");
    CadCommandService reopened;check(reopened.load(file,error),"Reopen advanced sketch");
    const auto* reopenedSketch=reopened.document().findSketch(sketch);bool preserved=false;
    if(reopenedSketch)for(const auto& e:reopenedSketch->entities)if(e.id==circle)preserved=e.type=="ellipse"&&approximately(e.radius,14)&&approximately(e.radiusY,6);
    check(preserved,"Save/reopen preserves ellipse type, identity and semiaxes");
    {
        CadCommandService profile;juce::String plane,solid;juce::StringArray ellipseIds;
        check(profile.createSketch("XY",0,plane,error)&&profile.document().editSketchGeometry(plane,{},"ellipse",parameters(R"({"x":0,"y":0,"rx":12,"ry":5,"angle":0.3})"),ellipseIds,error),"Create standalone ellipse profile");
        const auto extruded=profile.createExtrude(plane,7,false,solid,error);check(extruded,"Ellipse remains an exact closed OCCT profile");
        if(extruded){GProp_GProps properties;BRepGProp::VolumeProperties(profile.document().find(solid)->shape,properties);check(std::abs(properties.Mass()-juce::MathConstants<double>::pi*12*5*7)<1e-6,"Ellipse extrusion has exact analytic volume");}
    }
    CadWorkspacePanel panel(service);panel.setSize(1500,950);panel.refreshFromDocument(true);panel.enterSketch(sketch);panel.sketchCanvas->fitToContent();
    const auto event=[&](juce::Point<float> p,int mods,juce::Point<float> down){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys(mods),1,0,0,0,0,panel.sketchCanvas.get(),panel.sketchCanvas.get(),juce::Time::getCurrentTime(),down,juce::Time::getCurrentTime(),1,true);};
    const auto region=[&](juce::String command,juce::Point<double> a,juce::Point<double> b){panel.executeUiCommand(command);const auto p=panel.sketchCanvas->toPoint(a.x,a.y),q=panel.sketchCanvas->toPoint(b.x,b.y);panel.sketchCanvas->mouseDown(event(p,juce::ModifierKeys::leftButtonModifier,p));panel.sketchCanvas->mouseDrag(event(q,juce::ModifierKeys::leftButtonModifier,p));panel.sketchCanvas->mouseUp(event(q,0,p));};
    region("sketchBox",{-30,-40},{130,80});check(panel.sketchSelection.size()>2,"Box selection selects arbitrary mixed geometry through production callbacks");
    region("sketchCircleSelect",{40,15},{150,15});check(panel.sketchSelection.size()>2,"Circle region selects mixed objects");
    panel.executeUiCommand("sketchFreehand");
    const std::array<juce::Point<double>,5> boundary{{{-30,-40},{130,-40},{130,80},{-30,80},{-30,-40}}};const auto down=panel.sketchCanvas->toPoint(boundary[0].x,boundary[0].y);
    panel.sketchCanvas->mouseDown(event(down,juce::ModifierKeys::leftButtonModifier,down));for(size_t i=1;i<boundary.size();++i)panel.sketchCanvas->mouseDrag(event(panel.sketchCanvas->toPoint(boundary[i].x,boundary[i].y),juce::ModifierKeys::leftButtonModifier,down));panel.sketchCanvas->mouseUp(event(down,0,down));
    check(panel.sketchSelection.size()>2,"Freehand region selects groups and mixed geometry");
    panel.selectSketchEntity(circle,false);panel.selectSketchEntity(bezier,true);panel.selectSketchEntity(line,true);check(panel.sketchSelection.size()==3,"Multiple selection supports more than two objects");
    panel.selectSketchEntity(bezier,true);check(!panel.sketchSelection.contains(bezier)&&panel.sketchSelection.size()==2,"Shift selection removes individual object");
    panel.selectSketchEntity(rect[0],false);check(panel.sketchSelection.size()==4,"Selecting one rectangle member selects its group");
    check(panel.sketchCanvas->copySelection(),"Copy snapshots geometry and internal constraints");
    const auto beforePaste=service->document().findSketch(sketch)->entities.size();
    auto paste=parameters(R"({"x":15,"y":25})");paste.getDynamicObject()->setProperty("clipboard",panel.sketchCanvas->clipboard);
    check(panel.sketchCanvas->edit("paste",paste),"Paste clipboard via existing transaction");
    check(panel.sketchCanvas->edit("paste",paste),"Repeat paste independently");
    const auto* afterPaste=service->document().findSketch(sketch);juce::StringArray unique;bool noCollision=true;
    for(const auto& e:afterPaste->entities){noCollision=noCollision&&!unique.contains(e.id);unique.add(e.id);}
    check(noCollision&&afterPaste->entities.size()==beforePaste+8,"Repeated paste remaps entity and rectangle identities");
    check(service->undo(error)&&service->document().findSketch(sketch)->entities.size()==beforePaste+4,"Paste is one undo step");
    check(service->redo(error),"Paste redo");
    {
        const auto snapshot=panel.sketchCanvas->clipboard;juce::String other;check(service->createSketch("YZ",3,other,error),"Create a different destination sketch");
        auto parameters=juce::JSON::parse(R"({"x":30,"y":-20})");parameters.getDynamicObject()->setProperty("clipboard",snapshot);
        check(service->document().editSketchGeometry(other,{},"paste",parameters,result,error)&&service->document().findSketch(other)->entities.size()==4,"Clipboard group pastes into another sketch with independent identities");
    }
    panel.selectSketchEntity(circle,false);panel.handleUpdateNowIfNeeded();
    check(panel.form&&panel.form->control("ellipseRx")&&panel.form->control("ellipseRy"),"Ellipse precise properties integrated in existing form");
    panel.setSketchTool("select");auto pixel=panel.sketchCanvas->toPoint(74,20),destination=panel.sketchCanvas->toPoint(78,20);
    panel.sketchCanvas->mouseDown(event(pixel,juce::ModifierKeys::leftButtonModifier,pixel));panel.sketchCanvas->mouseDrag(event(destination,juce::ModifierKeys::leftButtonModifier,pixel));panel.sketchCanvas->mouseUp(event(destination,0,pixel));
    check(std::abs(entity(circle).radius-18)<1e-4,"Interactive ellipse handle updates document radius");
    panel.selectSketchEntity(rect[0],false);panel.setSketchTool("select");
    auto edge=entity(rect[1]);auto firstEdge=entity(rect[0]);
    const auto length=std::hypot(firstEdge.x2-firstEdge.x1,firstEdge.y2-firstEdge.y1);
    const juce::Point<double> u{(firstEdge.x2-firstEdge.x1)/length,(firstEdge.y2-firstEdge.y1)/length};
    auto edgePixel=panel.sketchCanvas->toPoint((edge.x1+edge.x2)*.5,(edge.y1+edge.y2)*.5),edgeTarget=panel.sketchCanvas->toPoint((edge.x1+edge.x2)*.5+8*u.x,(edge.y1+edge.y2)*.5+8*u.y);
    panel.sketchCanvas->mouseDown(event(edgePixel,juce::ModifierKeys::leftButtonModifier,edgePixel));panel.sketchCanvas->mouseDrag(event(edgeTarget,juce::ModifierKeys::leftButtonModifier,edgePixel));panel.sketchCanvas->mouseUp(event(edgeTarget,0,edgePixel));
    firstEdge=entity(rect[0]);check(std::abs(std::hypot(firstEdge.x2-firstEdge.x1,firstEdge.y2-firstEdge.y1)-(length+8))<1e-5,"Interactive edge handle resizes a rotated rectangle without flattening it");
    panel.selectSketchEntity(rect[0],false);panel.executeUiCommand("sketchRotate");
    auto centre=panel.sketchCanvas->toPoint(80,10),anglePoint=panel.sketchCanvas->toPoint(80,35);
    const auto beforeRotation=service->document().revision();
    panel.sketchCanvas->mouseDown(event(centre,juce::ModifierKeys::leftButtonModifier,centre));panel.sketchCanvas->mouseDrag(event(anglePoint,juce::ModifierKeys::leftButtonModifier,centre));panel.sketchCanvas->mouseUp(event(anglePoint,0,centre));
    check(service->document().revision()==beforeRotation+1,"Interactive group rotation commits once on drag release");
    check(service->undo(error),"Interactive rotation undoes as a group");
    panel.lineClickAndDrag=true; // These gestures explicitly validate the optional legacy drag interaction.
    panel.setSketchTool("line");panel.handleSketchKey(juce::KeyPress('h'));auto a=panel.sketchCanvas->toPoint(-30,-30),b=panel.sketchCanvas->toPoint(-10,-12);
    panel.sketchCanvas->mouseDown(event(a,juce::ModifierKeys::leftButtonModifier,a));panel.sketchCanvas->mouseDrag(event(b,juce::ModifierKeys::leftButtonModifier,a));panel.sketchCanvas->mouseUp(event(b,0,a));
    auto drawn=service->document().findSketch(sketch)->entities.back();check(approximately(drawn.y1,drawn.y2),"H drawing shortcut produces exact horizontal line");
    panel.setSketchTool("line");panel.handleSketchKey(juce::KeyPress('v'));a=panel.sketchCanvas->toPoint(-40,-35);b=panel.sketchCanvas->toPoint(-24,-12);
    panel.sketchCanvas->mouseDown(event(a,juce::ModifierKeys::leftButtonModifier,a));panel.sketchCanvas->mouseDrag(event(b,juce::ModifierKeys::leftButtonModifier,a));panel.sketchCanvas->mouseUp(event(b,0,a));
    drawn=service->document().findSketch(sketch)->entities.back();check(approximately(drawn.x1,drawn.x2),"V drawing shortcut produces exact vertical line");
    panel.setSketchTool("line");a=panel.sketchCanvas->toPoint(-60,-30);b=panel.sketchCanvas->toPoint(-42,-15);
    panel.sketchCanvas->mouseDown(event(a,juce::ModifierKeys::leftButtonModifier,a));panel.sketchCanvas->mouseDrag(event(b,juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier,a));panel.sketchCanvas->mouseUp(event(b,juce::ModifierKeys::shiftModifier,a));
    drawn=service->document().findSketch(sketch)->entities.back();check(approximately(std::abs(drawn.x2-drawn.x1),std::abs(drawn.y2-drawn.y1)),"Shift drawing snaps to an exact 45-degree increment");
    panel.setSketchTool("select");panel.selectSketchEntity(circle,false);panel.handleUpdateNowIfNeeded();
    {
        const auto finalFile=folder.getChildFile("edited-roundtrip.djc");CadCommandService loaded;
        bool equal=service->save(finalFile,error)&&loaded.load(finalFile,error);
        if(equal)for(const auto& source:service->document().sketchData())
        {const auto* target=loaded.document().findSketch(source.id);if(!target||target->entities.size()!=source.entities.size()){equal=false;break;}for(size_t i=0;i<source.entities.size();++i)equal=equal&&juce::JSON::toString(sketch_edit::entityJson(source.entities[i]),false,17)==juce::JSON::toString(sketch_edit::entityJson(target->entities[i]),false,17);}
        check(equal,"Save/reopen preserves transformed, split, resized and repeatedly pasted geometry exactly");
    }
    const auto* buttons=panel.sketchPalette.get();check(buttons&&buttons->isVisible(),"Selection tools live in existing dockable sketch toolbox");
    auto image=panel.createComponentSnapshot(panel.getLocalBounds());juce::FileOutputStream stream(folder.getChildFile("workspace.png"));if(stream.openedOk())juce::PNGImageFormat().writeImageToStream(image,stream);
    auto* summary=new juce::DynamicObject();summary->setProperty("ok",failures==0);summary->setProperty("passed",checks.size()-failures);summary->setProperty("failed",failures);summary->setProperty("checks",checks);report=summary;return failures==0;
}
