bool runFixtureSaveProtectionCheck(juce::var& report)
{
    using namespace djehuti::cad;juce::Array<juce::var> checks;int failed=0;juce::String error;
    const auto check=[&](bool ok,const juce::String& name){auto* v=new juce::DynamicObject();v->setProperty("name",name);v->setProperty("ok",ok);checks.add(v);if(!ok)++failed;};
    auto service=std::make_shared<CadFixtureService>();auto cad=std::make_shared<CadCommandService>();CadFixturesPanel panel(service,cad);panel.setSize(1280,850);panel.createFixture();panel.createFixture();
    const auto first=service->documents()[0],second=service->documents()[1];const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/save-protection-check");
    check(first->save(folder.getChildFile("first.djfixture"),true,error)&&second->save(folder.getChildFile("second.djfixture"),true,error),"Save two independent fixtures");
    auto rename=juce::JSON::parse(R"({"action":"rename","name":"Changed"})");first->command(rename);check(first->modified()&&!second->modified(),"Fixture dirty state is independent per document");
    first->command(juce::JSON::parse(R"({"action":"undo"})"));check(!first->modified(),"Fixture undo returns to saved state");first->command(juce::JSON::parse(R"({"action":"redo"})"));check(first->modified(),"Fixture redo leaves saved state");second->command(rename);
    int prompts=0;panel.unsavedPrompt=[&](const auto&,auto answer){answer(++prompts==1?2:0);};bool closed=false,cancelled=false;
    panel.confirmClose([&]{closed=true;},[&]{cancelled=true;});check(!closed&&cancelled&&prompts==2&&service->documents().size()==2,"Exit checks every modified document; Cancel in second aborts entire exit");
    panel.unsavedPrompt=[&](const auto&,auto answer){answer(1);};panel.confirmClose([&]{closed=true;});check(closed&&!first->modified()&&!second->modified(),"Exit Save writes each modified fixture before continuing");
    panel.unsavedPrompt=[&](const auto&,auto answer){++prompts;answer(0);};const int before=prompts;panel.confirmClose([&]{closed=true;});check(prompts==before,"Clean fixture documents need no prompt");
    first->command(rename);panel.synchronize(first->id());panel.manage("close");check(service->find(first->id())!=nullptr,"Individual fixture tab Cancel preserves document");
    const auto original=first->file().loadFileAsString();panel.unsavedPrompt=[](const auto&,auto answer){answer(2);};panel.manage("close");check(service->find(first->id())==nullptr&&first->file().loadFileAsString()==original,"Individual tab Don't Save closes without writing");
    second->command(rename);panel.synchronize(second->id());panel.unsavedPrompt=[](const auto&,auto answer){answer(1);};panel.manage("close");check(service->find(second->id())==nullptr&&!second->modified(),"Individual tab Save completes before removal");
    auto* result=new juce::DynamicObject();result->setProperty("ok",failed==0);result->setProperty("passed",checks.size()-failed);result->setProperty("failed",failed);result->setProperty("checks",checks);report=result;return failed==0;
}
