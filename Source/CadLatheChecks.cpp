#include "CadDocument.h"
#include "CadWorkspacePanel.h"
#include "NodeCompiler.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>

namespace djehuti::cad
{
bool runCadLatheCheck(juce::var& report)
{
    juce::Array<juce::var> rows;int failed=0;
    const auto check=[&](bool ok,const char* name){auto* o=new juce::DynamicObject();o->setProperty("name",name);o->setProperty("ok",ok);rows.add(o);if(!ok)++failed;};
    const auto json=[](const juce::String& s){return juce::JSON::parse(s);};
    auto service=std::make_shared<CadCommandService>();
    const auto volume=[&](const juce::String& id){const auto* f=service->document().find(id);GProp_GProps p;if(f)BRepGProp::VolumeProperties(f->shape,p);return p.Mass();};
    const auto close=[](double a,double b){return std::abs(a-b)<std::max(1e-7,std::abs(b)*1e-8);};
    const auto pi=juce::MathConstants<double>::pi;
    auto created=service->latheCommand(json(R"({"action":"create","parameters":{"outer":[[0,20],[10,20],[10,16],[12,16],[12,20],[30,20]],"inner":[[0,8],[30,8]]}})"));
    const auto id=created["id"].toString();check((bool)created["ok"]&&close(volume(id),2448*pi),"Turn stepped/grooved hollow profile with analytic volume");
    if(id.isNotEmpty())
    {
        const auto sketch=created["sketch"].toString();juce::String error;
        check(service->undo(error)&&service->document().find(id)==nullptr&&service->document().find(sketch)==nullptr&&service->redo(error)&&service->document().find(id)!=nullptr,"Lathe creation and its sketch are one OCAF Undo/Redo operation");
        auto args=json(R"({"action":"update","parameters":{"angleDegrees":180}})");args.getDynamicObject()->setProperty("id",id);
        check((bool)service->latheCommand(args)["ok"]&&close(volume(id),1224*pi),"Lathe uses existing partial Revolve behavior");
        args=json(R"({"action":"update","parameters":{"outer":[[0,24],[10,24],[10,16],[12,16],[12,24],[30,24]]}})");args.getDynamicObject()->setProperty("id",id);
        check((bool)service->latheCommand(args)["ok"]&&close(volume(id),1840*pi),"Station updates rebuild the shared source sketch");
        const auto revision=service->document().revision();args=json(R"({"action":"update","parameters":{"inner":[[0,30],[30,30]]}})");args.getDynamicObject()->setProperty("id",id);
        check(!(bool)service->latheCommand(args)["ok"]&&service->document().revision()==revision&&close(volume(id),1840*pi),"Invalid bore fails without changing the valid document");
        const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/lathe-check");folder.createDirectory();
        for(const auto* ext:{"djo","djc"})
        {CadCommandService back;const auto path=folder.getChildFile(juce::String("turned.")+ext);const bool ok=service->save(path,error)&&back.load(path,error);auto get=json(R"({"action":"get"})");get.getDynamicObject()->setProperty("id",id);const auto r=back.latheCommand(get);check(ok&&(bool)r["ok"]&&(bool)r["profileEditable"]&&r["sketch"].toString()==sketch,"Native/JSON retains lathe workflow, profile and Revolve parameters");}
        auto get=json(R"({"action":"get"})");get.getDynamicObject()->setProperty("id",id);
        const auto body=service->latheCommand(get);check((bool)body["profileEditable"]&&body["parameters"]["outer"].size()==6,"Inspection derives stations from the authoritative sketch");
        auto del=json(R"({"action":"delete"})");del.getDynamicObject()->setProperty("id",id);check((bool)service->latheCommand(del)["ok"]&&service->undo(error),"Lathe delete is undoable");
    }
    auto guiService=std::make_shared<CadCommandService>();auto panel=std::make_unique<CadWorkspacePanel>(guiService);panel->setSize(1400,900);
    auto state=panel->automation(json(R"({"action":"command","command":"lathe"})"));check((bool)state["ok"]&&state["state"]["task"].toString()=="Virtual Lathe","GUI opens the dedicated Virtual Lathe task");
    panel->automation(json(R"({"action":"set","field":"latheOuter","value":"0, 20\n10, 20\n10, 16\n12, 16\n12, 20\n30, 20"})"));
    panel->automation(json(R"({"action":"set","field":"latheInner","value":"0, 8\n30, 8"})"));
    state=panel->automation(json(R"({"action":"click","button":"apply"})"));juce::String guiId;
    for(const auto& f:guiService->document().features())if(f.lathe)guiId=f.id;
    check((bool)state["ok"]&&guiId.isNotEmpty(),"GUI creates a hollow stepped/grooved turned part");
    if(guiId.isNotEmpty())
    {
        panel->automation(json(R"({"action":"click","button":"latheProfile"})"));panel->automation(json(R"({"action":"set","field":"latheOuter","value":"0, 24\n10, 24\n10, 16\n12, 16\n12, 24\n30, 24"})"));
        state=panel->automation(json(R"({"action":"click","button":"apply"})"));GProp_GProps p;BRepGProp::VolumeProperties(guiService->document().find(guiId)->shape,p);
        check((bool)state["ok"]&&close(p.Mass(),3680*pi),"GUI station editing updates the existing feature");
    }
    const auto node=node_compiler::CompileGraphToSource(R"({"functionName":"turn","params":[{"name":"args","type":"String"}],"output":"r","nodes":[{"id":"r","type":"cad_lathe","inputs":[{"param":"args"}]}]})");
    check(node.ok&&node.source.find("djehuti_cad_lathe")!=std::string::npos,"Lathe node generates the shared FRust host call");
    auto* out=new juce::DynamicObject();out->setProperty("ok",failed==0);out->setProperty("passed",rows.size()-failed);out->setProperty("failed",failed);out->setProperty("checks",rows);report=out;return failed==0;
}
}
