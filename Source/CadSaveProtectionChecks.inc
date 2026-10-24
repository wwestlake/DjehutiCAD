bool runFixtureSaveProtectionCheck(juce::var&);
bool runCadSaveProtectionCheck(juce::var& report)
{
    using namespace djehuti::cad;juce::Array<juce::var> checks;int failures=0;juce::String error,id;
    const auto check=[&](bool ok,const juce::String& name){auto* v=new juce::DynamicObject();v->setProperty("name",name);v->setProperty("ok",ok);checks.add(v);if(!ok)++failures;};
    auto service=std::make_shared<CadCommandService>();CadWorkspacePanel panel(service);panel.setSize(1280,850);int prompts=0,answer=0;bool closed=false;
    panel.unsavedPrompt=[&](const auto&,auto reply){++prompts;reply(answer);};
    panel.confirmClose([&](bool ok){closed=ok;});check(closed&&prompts==0,"Clean document proceeds without prompting");
    check(service->createBox(10,20,30,id,error)&&service->document().modified(),"Geometry edit marks document dirty");
    const auto revision=service->document().revision();panel.refreshFromDocument(true);panel.selectFeature(id);panel.executeUiCommand("reset");panel.modifyPalette->toggleCollapsed();
    check(service->document().modified()&&service->document().revision()==revision,"Selection, camera and toolbar state do not alter document state");
    panel.confirmClose([&](bool ok){closed=ok;});check(!closed&&prompts==1&&service->document().features().size()==1,"Cancel blocks close and preserves document");
    answer=2;panel.confirmClose([&](bool ok){closed=ok;});check(closed&&service->document().modified()&&service->document().file()==juce::File(),"Don't Save permits close without writing or clearing dirty state");
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/save-protection-check");folder.createDirectory();const auto file=folder.getChildFile("saved.djo");service->document().setFile(file);
    answer=1;panel.confirmClose([&](bool ok){closed=ok&&file.existsAsFile()&&!service->document().modified();});
    check(closed,"Save writes native document before allowing close");
    CadCommandService reopened;check(reopened.load(file,error)&&reopened.document().features().size()==1&&!reopened.document().modified(),"Saved changes reopen cleanly");
    const auto before=prompts;panel.confirmClose([&](bool ok){closed=ok;});check(closed&&prompts==before,"Saved document closes without prompting");
    service->createCylinder(5,10,id,error);check(service->document().modified(),"Subsequent edit dirties saved document");
    check(service->undo(error)&&!service->document().modified(),"Undo to the saved OCAF state clears dirty flag");
    check(service->redo(error)&&service->document().modified(),"Redo away from saved state marks dirty");
    check(service->save(file,error)&&!service->document().modified(),"Saving after redo clears dirty flag");
    check(service->undo(error)&&service->document().modified(),"Undo away from a newly saved state is dirty");
    check(service->redo(error)&&!service->document().modified(),"Redo back to a saved state is clean");
    panel.selectFeature(id);panel.executeUiCommand("reset");check(!service->document().modified(),"Selection and camera preserve a clean document");
    service->createBox(2,3,4,id,error);const auto dirtyRevision=service->document().revision();const auto dirtyCount=service->document().features().size();
    const auto blocker=folder.getChildFile("blocker");blocker.replaceWithText("not a directory");service->document().setFile(blocker.getChildFile("failed.djo"));
    answer=1;panel.confirmClose([&](bool ok){closed=ok;});check(!closed&&service->document().modified()&&service->document().revision()==dirtyRevision&&service->document().features().size()==dirtyCount,"Failed save blocks closure and preserves content");
    panel.finishSaveAs({},service->document().documentInstance(),[&](bool ok){closed=ok;});check(!closed&&service->document().modified()&&service->document().features().size()==dirtyCount,"Canceled Save As does not form a filename or close document");
    panel.finishSaveAs(folder.getChildFile("obsolete.djo"),service->document().documentInstance()+1,[&](bool ok){closed=ok;});check(!closed&&!folder.getChildFile("obsolete.djo").existsAsFile(),"Obsolete Save As callback cannot save another document");
    answer=0;panel.newDocument();check(service->document().features().size()==dirtyCount,"New document respects Cancel");
    answer=2;panel.newDocument();check(service->document().features().empty()&&!service->document().modified(),"New document replaces only after explicit discard");
    auto independent=std::make_shared<CadCommandService>();independent->createBox(1,1,1,id,error);check(independent->document().modified()&&!service->document().modified(),"Independent CAD services retain independent dirty state");
    juce::var fixtureReport;check(runFixtureSaveProtectionCheck(fixtureReport),"Multi-document fixture close/exit protection");
    auto* result=new juce::DynamicObject();result->setProperty("ok",failures==0);result->setProperty("passed",checks.size()-failures);result->setProperty("failed",failures);result->setProperty("checks",checks);result->setProperty("fixtures",fixtureReport);report=result;return failures==0;
}
