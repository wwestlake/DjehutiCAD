// Context-specific workspace actions; geometry algorithms and OCAF mutations remain in CadCommandService.
std::vector<int> CadWorkspacePanel::eligibleEdges(const juce::String& id,int* skipped) const
{
    std::vector<int> result;if(skipped)*skipped=0;const auto* feature=service->document().find(id);if(!feature||feature->shape.IsNull())return result;
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(feature->shape,TopAbs_EDGE,edges);TopTools_IndexedDataMapOfShapeListOfShape adjacency;TopExp::MapShapesAndAncestors(feature->shape,TopAbs_EDGE,TopAbs_FACE,adjacency);
    for(int i=1;i<=edges.Extent();++i){const auto edge=TopoDS::Edge(edges(i));TopTools_IndexedMapOfShape faces;if(adjacency.Contains(edge))for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromKey(edge));it.More();it.Next())faces.Add(it.Value());
        if(!BRep_Tool::Degenerated(edge)&&faces.Extent()==2)result.push_back(i-1);else if(skipped)++*skipped;
    }return result;
}
void CadWorkspacePanel::chooseAll(bool clear)
{
    if(!selection)return;
    if(clear){filletEdges.clear();selection->clear();if(!isCommandTask())selectFeature({});modelView.markDirty();rebuildTaskPanel(true);return;}
    if(task==Task::fillet||task==Task::chamfer){
        if(targetId.isEmpty()&&isSolid(selectedId))targetId=selectedId;
        if(targetId.isEmpty()){setMessage("Choose the target body before selecting all edges.",1);return;}
        int skipped=0;const auto edges=eligibleEdges(targetId,&skipped);selection->setMode(djehuti::cad::CadSelection::Mode::edge);std::vector<djehuti::cad::CadPick> picks;for(int edge:edges)picks.push_back({targetId,targetId,-1,edge});selection->setSelectedMany(std::move(picks));
        setMessage("Selected "+juce::String((int)edges.size())+" edges; excluded "+juce::String(skipped)+" seam, degenerate, or non-manifold edges. Preview/Apply validates the chosen radius or distance.",skipped?1:0);return;
    }
    selection->setSelectedMany(modelView.allPicks());modelView.markDirty();
}
bool CadWorkspacePanel::handleModelKey(const juce::KeyPress& key)
{
    if(editingSketchId.isNotEmpty())return false;
    if(auto* focus=juce::Component::getCurrentlyFocusedComponent();focus&&(dynamic_cast<juce::TextEditor*>(focus)||focus->findParentComponentOfClass<juce::TextEditor>()))return false;
    const auto mods=key.getModifiers();auto code=key.getKeyCode();if(code>=1&&code<=26)code+='A'-1;code=(int)juce::CharacterFunctions::toUpperCase((juce::juce_wchar)code);
    if(mods.isCommandDown()&&!mods.isAltDown()){
        if(code=='A'){chooseAll(mods.isShiftDown());return true;}
        if(code=='S'){if(mods.isShiftDown())saveDocumentAs();else saveDocument();return true;}
        if(code=='F'&&mods.isShiftDown())return executeUiCommand("flyMode");
        if(code=='O'&&mods.isShiftDown())return executeUiCommand("orbitPoint");
        return false; // Copy/paste are supplied by Sketch and text editors only.
    }
    if(mods.isAltDown()||mods.isCommandDown())return false;
    if(modelView.isFlyMode()||modelView.isChoosingOrbitPoint())return false;
    if(key==juce::KeyPress::escapeKey){if(task!=Task::none&&task!=Task::body&&task!=Task::subshape)cancelTask();else chooseAll(true);if(selection)selection->setTool(djehuti::cad::CadSelection::Tool::pointer);return true;}
    if(code=='F')return executeUiCommand(mods.isShiftDown()?"focusSelected":"fit");
    if(key==juce::KeyPress::homeKey)return executeUiCommand("reset");
    if(key==juce::KeyPress::deleteKey){
        if(task==Task::fillet||task==Task::chamfer){chooseAll(true);return true;}
        juce::StringArray ids;if(selection)for(const auto& p:selection->items())if(p.face<0&&p.edge<0)ids.addIfNotAlreadyThere(p.drawId);
        if(ids.isEmpty()&&selectedId.isNotEmpty())ids.add(selectedId);for(const auto& id:ids)deleteFeature(id);if(selection)selection->clear();return !ids.isEmpty();
    }
    if(mods.isAnyModifierKeyDown())return false;
    const std::pair<int,const char*> commands[]{{'1',"bodyMode"},{'2',"faceMode"},{'3',"edgeMode"},{'V',"pointer"},{'B',"boxSelect"},{'C',"circleSelect"},{'M',"multipleSelect"},{'T',"transform"}};
    for(const auto& pair:commands)if(code==pair.first)return executeUiCommand(pair.second);return false;
}
bool CadWorkspacePanel::sketchShown(const juce::String& id) const
{
    const auto* f=service->document().find(id);if(!f)return false;
    return editingSketchId.isNotEmpty()&&id!=editingSketchId?sketchReferenceShown(id):f->visible;
}
bool CadWorkspacePanel::sketchReferenceApplicable(const juce::String& id) const
{
    // Mirrors referenceInputs(): every source it can draw, and nothing else.
    if(editingSketchId.isEmpty()||id.isEmpty()||id==editingSketchId)return false;
    const auto& d=service->document();
    if(const auto* instance=d.mechanicsData().findInstance(id)){const auto* f=d.find(instance->featureId);return f!=nullptr&&!f->shape.IsNull();}
    const auto* f=d.find(id);if(f==nullptr)return false;
    if(f->type=="sketch")return true;
    if(f->shape.IsNull()||f->sketchId==editingSketchId)return false;
    for(const auto& instance:d.mechanicsData().instances)if(instance.featureId==id)return false;   // shown through its instances
    return true;
}
bool CadWorkspacePanel::sketchReferenceShown(const juce::String& id) const
{
    const auto* sketch=service->document().findSketch(editingSketchId);
    if(sketch==nullptr||!sketch->references.contains(id)||referenceOpacity<=0)return false;
    const auto* f=service->document().find(id);
    return f!=nullptr&&f->type=="sketch"?showReferenceSketches:showReferenceSolids;
}
bool CadWorkspacePanel::manipulable(const juce::String& id,juce::String& reason) const
{
    const auto& d=service->document();
    if(editingSketchId.isNotEmpty()){reason="Finish the sketch to move bodies.";return false;}
    if(const auto* instance=d.mechanicsData().findInstance(id))
    {
        if(!instance->grounded&&d.mechanicsData().instanceHasJoints(id)){reason=instance->name+" is positioned by its joints. Ground it, or edit the joint frames, to move it.";return false;}
        const auto* f=d.find(instance->featureId);if(f==nullptr||f->shape.IsNull()){reason="This instance has no geometry.";return false;}
        return true;
    }
    const auto* f=d.find(id);
    if(f==nullptr||f->type=="sketch"||f->shape.IsNull()){reason="Select a body or an assembly component to move or rotate it.";return false;}
    for(const auto& instance:d.mechanicsData().instances)if(instance.featureId==id){reason=f->name+" is shown through its assembly instances; select an instance to move it.";return false;}
    return true;
}
void CadWorkspacePanel::updateManipulator(bool force)
{
    const auto& d=service->document();const auto pose=motionRuntime!=nullptr?motionRuntime->pose():nullptr;
    const auto key=juce::String((int)transformTool)+(transformLocal?"L":"W")+"|"+selectedId+"|"+juce::String(d.documentInstance())+"|"+juce::String(d.revision())+"|"+juce::String(pose?pose->serial:0)+"|"+editingSketchId;
    if(!force&&key==manipulatorKey)return;manipulatorKey=key;
    Viewport3D::Manipulator m;juce::String reason;
    if(transformTool&&selectedId.isNotEmpty()&&manipulable(selectedId,reason))
    {
        // Pivot: the centre of the body's world bounds. Local axes follow the body's orientation.
        const auto* instance=d.mechanicsData().findInstance(selectedId);const auto* f=d.find(instance?instance->featureId:selectedId);
        gp_Trsf world,rotation;
        if(instance)
        {
            gp_Trsf frame=instance->placement.toTrsf();
            if(motionRuntime!=nullptr){const auto& shown=motionRuntime->shownInstanceWorld();if(const auto found=shown.find(instance->id);found!=shown.end())frame=found->second;}
            world=d.instanceShapeTransform(f->id,frame);rotation=frame;rotation.SetTranslationPart(gp_Vec());
        }
        else{rotation.SetRotation(gp::OX(),f->rx*juce::MathConstants<double>::pi/180);gp_Trsf ry,rz;ry.SetRotation(gp::OY(),f->ry*juce::MathConstants<double>::pi/180);rz.SetRotation(gp::OZ(),f->rz*juce::MathConstants<double>::pi/180);rotation=rz.Multiplied(ry).Multiplied(rotation);}
        Bnd_Box box;BRepBndLib::Add(f->shape.Moved(TopLoc_Location(world)),box,true);
        if(!box.IsVoid())
        {
            double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
            m.active=true;m.drawId=selectedId;m.label=(transformLocal?"Move / Rotate ":"Move / Rotate ")+featureName(selectedId);m.local=transformLocal;
            m.pivot={(float)((x0+x1)/2),(float)((y0+y1)/2),(float)((z0+z1)/2)};
            if(transformLocal)for(int i=0;i<3;++i){const auto a=gp_Dir(i==0?1:0,i==1?1:0,i==2?1:0).Transformed(rotation);m.axes[(size_t)i]={(float)a.X(),(float)a.Y(),(float)a.Z()};}
            if(const auto* pivotRef=d.findConstruction(manipulatorPivotRef))   // pivot and (local) axes taken from a construction reference
            {
                const auto resolved=d.resolveConstruction(pivotRef->id);
                if(resolved.ok)
                {
                    m.pivot={(float)resolved.world.x,(float)resolved.world.y,(float)resolved.world.z};m.label+=" (pivot: "+pivotRef->name+")";
                    if(transformLocal){const auto t=resolved.world.toTrsf();for(int i=0;i<3;++i){const auto a=gp_Dir(i==0?1:0,i==1?1:0,i==2?1:0).Transformed(t);m.axes[(size_t)i]={(float)a.X(),(float)a.Y(),(float)a.Z()};}}
                }
            }
        }
    }
    else if(transformTool&&selectedId.isNotEmpty()&&reason.isNotEmpty())setMessage(reason,1);
    modelView.setManipulator(m);modelView.markDirty();wakeFrames();
}
bool CadWorkspacePanel::applyManipulation(const Viewport3D::Manipulation& m)
{
    const auto& man=modelView.manipulator();const auto id=man.drawId;const auto& d=service->document();juce::String reason;
    if(!manipulable(id,reason)){showError(reason);return false;}
    const gp_Dir axis(man.axes[(size_t)m.axis][0],man.axes[(size_t)m.axis][1],man.axes[(size_t)m.axis][2]);const gp_Pnt pivot(man.pivot[0],man.pivot[1],man.pivot[2]);
    gp_Trsf delta,turn;if(m.rotate){delta.SetRotation(gp_Ax1(pivot,axis),m.amount);turn.SetRotation(gp_Ax1(gp::Origin(),axis),m.amount);}else delta.SetTranslation(gp_Vec(axis)*m.amount);
    juce::String e;bool ok=false;const auto name=featureName(id);
    if(const auto* instance=d.mechanicsData().findInstance(id))ok=service->setInstancePlacement(id,djehuti::cad::CadFrame::fromTrsf(delta.Multiplied(instance->placement.toTrsf())),e);
    else if(const auto* f=d.find(id))
    {
        // Feature placement: point p, then rotations about X, Y, Z through p. The new frame is delta applied to it.
        const auto p=gp_Pnt(f->x,f->y,f->z).Transformed(delta);double rx=f->rx,ry=f->ry,rz=f->rz;
        if(m.rotate)
        {
            gp_Trsf a,b,c;a.SetRotation(gp::OX(),f->rx*juce::MathConstants<double>::pi/180);b.SetRotation(gp::OY(),f->ry*juce::MathConstants<double>::pi/180);c.SetRotation(gp::OZ(),f->rz*juce::MathConstants<double>::pi/180);
            djehuti::cad::mech::eulerDegreesFromFrame(djehuti::cad::CadFrame::fromTrsf(turn.Multiplied(c.Multiplied(b).Multiplied(a))),rx,ry,rz);
        }
        ok=service->setPlacement(id,p.X(),p.Y(),p.Z(),rx,ry,rz,e);
    }
    if(!ok){showError(e.isNotEmpty()?e:"The placement could not be changed.");return false;}
    refreshFromDocument(true);if(task==Task::body||task==Task::instance)rebuildTaskPanel(false);updateManipulator(true);
    const auto axisName=juce::String::charToString("XYZ"[m.axis]);
    setMessage((m.rotate?"Rotated "+name+" "+juce::String(juce::radiansToDegrees(m.amount),2)+" deg about "+axisName:"Moved "+name+" "+juce::String(m.amount,3)+" mm along "+axisName)+(man.local?" (local)":"")+". Placement only; the part geometry is unchanged. Undo reverts.",0);
    return true;
}
void CadWorkspacePanel::focusInsertedComponent(const juce::String& instanceId)
{
    // A freshly inserted component is selected with the Move / Rotate tool ready, so it can be placed at once.
    refreshFromDocument(true);selectFeature(instanceId);transformTool=true;updateManipulator(true);updateToolbar();
    setMessage("Inserted "+featureName(instanceId)+". Drag the arrows to move it and the rings to rotate it, or type its placement in the inspector.",0);
}
juce::PopupMenu CadWorkspacePanel::sketchReferenceMenu(const juce::String& id) const
{
    juce::PopupMenu menu;
    if(sketchReferenceApplicable(id))menu.addItem(1,sketchReferenceShown(id)?"Hide in this sketch":"Show in this sketch");
    else menu.addItem(1,"Not displayable as a sketch reference",false);
    menu.addItem(2,"Reference geometry settings...");
    return menu;
}
void CadWorkspacePanel::runSketchReferenceMenu(const juce::String& sketch,const juce::String& id,int result)
{
    // The menu is asynchronous: the sketch may have closed or the item been deleted while it was open.
    if(result==0||sketch.isEmpty()||editingSketchId!=sketch)return;
    if(result==1&&sketchReferenceApplicable(id))toggleSketchReference(id);
    else if(result==2)showReferenceControls();
    tree.repaint();
}
void CadWorkspacePanel::toggleSketchReference(const juce::String& id)
{
    const auto& d=service->document();const auto* sketch=d.findSketch(editingSketchId);
    if(sketch==nullptr||!sketchReferenceApplicable(id))return;
    juce::StringArray list;for(const auto& existing:sketch->references)if(existing!=id&&(d.find(existing)!=nullptr||d.mechanicsData().findInstance(existing)!=nullptr))list.add(existing);
    const bool show=!sketchReferenceShown(id);if(show)list.add(id);
    const auto* f=d.find(id);const bool isSketch=f!=nullptr&&f->type=="sketch";const auto name=featureName(id);
    juce::String error;if(!service->setSketchReferences(editingSketchId,list,error)){showError(error);return;}
    if(show){(isSketch?showReferenceSketches:showReferenceSolids)=true;if(referenceOpacity<=0)referenceOpacity=.4f;}
    referencePreferencesChanged();
    setMessage(name+(show?" is shown as a reference in this sketch.":" is hidden in this sketch. Its 3D visibility is unchanged."),0);
}
void CadWorkspacePanel::showSketches(bool visible)
{
    juce::StringArray ids;for(const auto& feature:service->document().features())if(feature.type=="sketch")ids.add(feature.id);
    juce::String error;for(const auto& id:ids){const auto* f=service->document().find(id);if(f&&f->visible!=visible&&!service->setVisible(id,visible,error)){showError(error);return;}}
    showReferenceSketches=visible;referencePreferencesChanged();refreshFromDocument(true);modelView.markDirty();
}
