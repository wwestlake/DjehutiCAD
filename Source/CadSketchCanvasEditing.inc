// Members of SketchCanvas: transient editing gestures commit through the document's existing history.
bool copySelection()
{
    const auto* sketch=owner.service->document().findSketch(sketchId);
    if(!sketch||owner.sketchSelection.isEmpty())return false;
    juce::Array<juce::var> entities,constraints;bool first=true;
    for(const auto& entity:sketch->entities)if(owner.sketchSelection.contains(entity.id))
    {entities.add(djehuti::cad::sketch_edit::entityJson(entity));if(first){clipboardAnchor={entity.x1,entity.y1};first=false;}}
    const auto selected=[&](juce::String ref){return owner.sketchSelection.contains(ref.upToFirstOccurrenceOf(":",false,false));};
    for(const auto& c:sketch->constraints)if(selected(c.firstEntity)&&(c.secondEntity.isEmpty()||selected(c.secondEntity)))
    {auto* value=new juce::DynamicObject();value->setProperty("type",c.type);value->setProperty("first",c.firstEntity);value->setProperty("second",c.secondEntity);value->setProperty("value",c.value);constraints.add(value);}
    auto* value=new juce::DynamicObject();value->setProperty("entities",entities);value->setProperty("constraints",constraints);clipboard=value;
    owner.setMessage("Copied sketch objects. Ctrl+V previews at the pointer; click repeatedly to paste independent copies.",0);return true;
}
static juce::var values(std::initializer_list<std::pair<const char*,double>> entries)
{auto* value=new juce::DynamicObject();for(const auto& [key,n]:entries)value->setProperty(key,n);return value;}
bool edit(const juce::String& operation,const juce::var& parameters,const juce::StringArray& selected={})
{
    juce::String error;juce::StringArray result;
    if(!owner.service->document().editSketchGeometry(sketchId,selected.isEmpty()?owner.sketchSelection:selected,operation,parameters,result,error)){owner.showError(error);return false;}
    owner.refreshFromDocument(true);owner.sketchSelection=result;
    selectedEntity=owner.sketchEntityId=result.isEmpty()?juce::String():result[0];
    secondEntity=owner.sketchSecondEntityId=result.size()>1?result[1]:juce::String();
    owner.task=owner.sketchEntityId.isEmpty()?Task::sketchEdit:Task::sketchEntity;owner.rebuildTaskPanel(false);owner.syncTreeSelection();if(operation=="rotate")owner.setMessage("Rotated selection. Absolute horizontal/vertical drivers were released; dimensions and joins are retained.",0);repaint();return true;
}
bool editKey(const juce::KeyPress& key)
{
    auto* focus=juce::Component::getCurrentlyFocusedComponent();if(dynamic_cast<juce::TextEditor*>(focus)!=nullptr)return false;
    const auto code=juce::CharacterFunctions::toLowerCase((juce::juce_wchar)key.getKeyCode());
    if(key.getModifiers().isCtrlDown()&&(code=='c'||code==3))return copySelection();
    if(key.getModifiers().isCtrlDown()&&(code=='v'||code==22)){if(clipboard.isVoid())return false;owner.setSketchTool("paste");return true;}
    if((mode=="line"||pointDragActive)&&(code=='h'||code=='v')){directionDegrees=code=='h'?0.:90.;owner.setMessage(code=='h'?"Horizontal constraint":"Vertical constraint",0);repaint();return true;}
    if(code=='a'&&(mode=="line"||mode=="rotate"))
    {
        auto* dialog=new juce::AlertWindow("Exact angle","Angle in degrees; rotation requires a centre picked first.",juce::AlertWindow::NoIcon);
        dialog->addTextEditor("angle","0","Degrees");dialog->addButton("Apply",1,juce::KeyPress(juce::KeyPress::returnKey));dialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SketchCanvas> safe(this);juce::Component::SafePointer<juce::AlertWindow> prompt(dialog);
        dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe,prompt](int result){
            if(result!=1||!safe||!prompt)return;
            const auto text=prompt->getTextEditor("angle")->getText();char* end=nullptr;const auto a=std::strtod(text.toRawUTF8(),&end);
            if(!std::isfinite(a)||end==text.toRawUTF8()||*end){safe->owner.showError("Enter a finite angle.");return;}
            if(safe->mode=="rotate"){if(!safe->transformAnchor){safe->owner.showError("Click a rotation centre first.");return;}safe->edit("rotate",values({{"x",safe->start.x},{"y",safe->start.y},{"angle",a}}));safe->transformAnchor=false;}
            else safe->directionDegrees=a;
        }),true);return true;
    }
    return false;
}
Point constrainDrawing(Point origin,Point point,const juce::ModifierKeys& mods) const
{
    auto angle=directionDegrees;
    if(mods.isShiftDown())angle=std::round(std::atan2(point.y-origin.y,point.x-origin.x)*4/juce::MathConstants<double>::pi)*45;
    return std::isfinite(angle)?djehuti::cad::sketch_edit::constrained(origin,point,angle):point;
}
bool beginPrimitiveResize(juce::Point<float> pixel)
{
    const auto* sketch=owner.service->document().findSketch(sketchId);if(!sketch)return false;
    for(const auto& entity:sketch->entities)if((owner.sketchSelection.contains(entity.id)||entity.groupId.isNotEmpty())&&entity.mirrorOwner.isEmpty())
    {
        if(entity.type=="circle"||entity.type=="ellipse")for(int handle=0;handle<4;++handle)
        {const auto p=djehuti::cad::sketch_edit::ellipsePoint(entity,handle*juce::MathConstants<double>::halfPi);if(toPoint(p.x,p.y).getDistanceFrom(pixel)<9){primitiveEntity=entity;primitiveHandle=handle%2;primitiveGroup.clear();primitiveActive=true;return true;}}
        if(entity.groupId.isNotEmpty())
        {
            std::vector<const djehuti::cad::CadSketchEntity*> rectangle;for(const auto& e:sketch->entities)if(e.groupId==entity.groupId)rectangle.push_back(&e);
            if(rectangle.size()!=4)continue;
            for(int handle=0;handle<8;++handle){const auto& edge=*rectangle[(size_t)(handle%4)];const Point p=handle<4?Point{(edge.x1+edge.x2)*.5,(edge.y1+edge.y2)*.5}:Point{edge.x1,edge.y1};if(toPoint(p.x,p.y).getDistanceFrom(pixel)<8){primitiveEntity=entity;primitiveGroup=entity.groupId;primitiveHandle=handle;primitiveActive=true;if(!owner.sketchSelection.contains(entity.id))owner.selectSketchEntity(entity.id,false);return true;}}
        }
    }
    return false;
}
void updatePrimitive(Point p)
{
    primitivePoint=p;primitivePreview=primitiveEntity;if(primitiveGroup.isNotEmpty())return;
    const auto a=primitiveEntity.type=="ellipse"?primitiveEntity.angle:0.,dx=p.x-primitiveEntity.x1,dy=p.y-primitiveEntity.y1;
    primitivePreview.type="ellipse";primitivePreview.radiusY=primitiveEntity.type=="ellipse"?primitiveEntity.radiusY:primitiveEntity.radius;
    if(primitiveHandle==0)primitivePreview.radius=std::max(1e-6,std::abs(dx*std::cos(a)+dy*std::sin(a)));
    else primitivePreview.radiusY=std::max(1e-6,std::abs(-dx*std::sin(a)+dy*std::cos(a)));
}
void finishAreaSelection(bool add)
{
    const auto* sketch=owner.service->document().findSketch(sketchId);if(!sketch)return;juce::Path region;
    if(mode=="box_select")region.addRectangle(juce::Rectangle<float>(toPoint(start.x,start.y),toPoint(current.x,current.y)));
    else if(mode=="circle_select"){const auto a=toPoint(start.x,start.y),b=toPoint(current.x,current.y);const auto r=a.getDistanceFrom(b);region.addEllipse(a.x-r,a.y-r,r*2,r*2);}
    else{if(lasso.size()<3)return;region.startNewSubPath(lasso[0]);for(const auto& p:lasso)region.lineTo(p);region.closeSubPath();}
    juce::StringArray hits;
    for(const auto& entity:sketch->entities){const auto points=djehuti::cad::sketch_edit::outline(entity);bool hit=false;for(size_t i=0;i<points.size();++i){const auto a=toPoint(points[i].x,points[i].y);if(region.contains(a)){hit=true;break;}if(i){const auto b=toPoint(points[i-1].x,points[i-1].y);if(region.intersectsLine(juce::Line<float>(a,b))){hit=true;break;}}}if(hit)hits.add(entity.id);}
    if(!add)owner.selectSketchEntity({},false);for(const auto& id:hits)if(!owner.sketchSelection.contains(id))owner.selectSketchEntity(id,true);
}
void paintEditing(juce::Graphics& g)
{
    g.setColour(selectedColour.withAlpha(.8f));
    if(gestureActive&&mode.endsWith("_select"))
    {
        const auto a=toPoint(start.x,start.y),b=toPoint(current.x,current.y);
        if(mode=="box_select")g.drawRect(juce::Rectangle<float>(a,b),1.5f);
        else if(mode=="circle_select"){const auto r=a.getDistanceFrom(b);g.drawEllipse(a.x-r,a.y-r,r*2,r*2,1.5f);}
        else if(!lasso.empty()){juce::Path path;path.startNewSubPath(lasso[0]);for(const auto& p:lasso)path.lineTo(p);g.strokePath(path,juce::PathStrokeType(1.5f));}
    }
    if((mode=="rotate"||mode=="mirror")&&transformAnchor){const auto a=toPoint(start.x,start.y),b=toPoint(cursor.x,cursor.y);g.drawEllipse(a.x-5,a.y-5,10,10,2);g.drawLine(juce::Line<float>(a,b),1.5f);}
    if((mode=="rotate"||mode=="mirror")&&transformAnchor&&hasCursor)
    {
        const auto* sketch=owner.service->document().findSketch(sketchId);const auto angle=std::atan2(cursor.y-start.y,cursor.x-start.x),c=std::cos(angle),sn=std::sin(angle);
        const auto dx=cursor.x-start.x,dy=cursor.y-start.y,q=dx*dx+dy*dy;
        if(sketch)for(const auto& entity:sketch->entities)if(owner.sketchSelection.contains(entity.id))
        {const auto points=djehuti::cad::sketch_edit::outline(entity);juce::Path path;for(size_t i=0;i<points.size();++i){const auto u=points[i].x-start.x,v=points[i].y-start.y;Point point;if(mode=="rotate")point={start.x+c*u-sn*v,start.y+sn*u+c*v};else{if(q<1e-12)continue;const auto t=(u*dx+v*dy)/q;point={start.x+2*t*dx-u,start.y+2*t*dy-v};}const auto pixel=toPoint(point.x,point.y);if(i)path.lineTo(pixel);else path.startNewSubPath(pixel);}g.strokePath(path,juce::PathStrokeType(1.5f));}
    }
    if(mode=="paste"&&!clipboard.isVoid()&&hasCursor)
        if(const auto* list=clipboard["entities"].getArray())for(const auto& value:*list){const auto points=djehuti::cad::sketch_edit::outline(djehuti::cad::sketch_edit::entityFromJson(value));juce::Path path;for(size_t i=0;i<points.size();++i){const auto p=points[i]+cursor-clipboardAnchor;const auto pixel=toPoint(p.x,p.y);if(i)path.lineTo(pixel);else path.startNewSubPath(pixel);}g.strokePath(path,juce::PathStrokeType(1.5f));}
    if(std::isfinite(directionDegrees))g.drawText("Direction "+juce::String(directionDegrees,2)+" deg | Shift: 45 deg | A: exact",10,26,getWidth()-20,20,juce::Justification::left);
}
juce::var clipboard;Point clipboardAnchor;
std::vector<juce::Point<float>> lasso;
bool transformAnchor=false,transformDragged=false,primitiveActive=false,primitiveMoved=false;
juce::Point<float> primitivePress;
int primitiveHandle=0;juce::String primitiveGroup;
djehuti::cad::CadSketchEntity primitiveEntity,primitivePreview;Point primitivePoint;
double directionDegrees=std::numeric_limits<double>::quiet_NaN();
