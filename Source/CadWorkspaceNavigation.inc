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
    const std::pair<int,const char*> commands[]{{'1',"bodyMode"},{'2',"faceMode"},{'3',"edgeMode"},{'V',"pointer"},{'B',"boxSelect"},{'C',"circleSelect"},{'M',"multipleSelect"}};
    for(const auto& pair:commands)if(code==pair.first)return executeUiCommand(pair.second);return false;
}
bool CadWorkspacePanel::sketchShown(const juce::String& id) const
{
    const auto* f=service->document().find(id);if(!f)return false;
    return editingSketchId.isNotEmpty()&&id!=editingSketchId?showReferenceSketches&&f->visible:f->visible;
}
void CadWorkspacePanel::showSketches(bool visible)
{
    juce::StringArray ids;for(const auto& feature:service->document().features())if(feature.type=="sketch")ids.add(feature.id);
    juce::String error;for(const auto& id:ids){const auto* f=service->document().find(id);if(f&&f->visible!=visible&&!service->setVisible(id,visible,error)){showError(error);return;}referenceVisibility.getDynamicObject()->setProperty(id,visible);}
    showReferenceSketches=visible;referencePreferencesChanged();refreshFromDocument(true);modelView.markDirty();
}
