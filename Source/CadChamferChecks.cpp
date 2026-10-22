#include "CadDocument.h"
#include "CadWorkspacePanel.h"
#include "CadSelection.h"
#include "NodeCompiler.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>

namespace djehuti::cad
{
bool runCadChamferCheck(juce::var& report)
{
    juce::Array<juce::var> checks;int failed=0;
    const auto check=[&](bool ok,const char* name){auto* o=new juce::DynamicObject();o->setProperty("name",name);o->setProperty("ok",ok);checks.add(o);if(!ok)++failed;};
    const auto json=[](const juce::String& s){return juce::JSON::parse(s);};
    const auto request=[&](const char* action,const juce::String& id,const juce::var& params){auto* a=new juce::DynamicObject();a->setProperty("action",action);if(id.isNotEmpty())a->setProperty("id",id);if(params.isObject())a->setProperty("parameters",params);return juce::var(a);};
    const auto close=[](double a,double b){return std::abs(a-b)<std::max(1e-7,std::abs(b)*1e-8);};
    auto service=std::make_shared<CadCommandService>();juce::String box,error;service->createBox(20,30,40,box,error);
    const auto volume=[&](const juce::String& id){GProp_GProps p;const auto* f=service->document().find(id);if(f)BRepGProp::VolumeProperties(f->shape,p);return p.Mass();};
    auto listArgs=json(R"({"action":"edges"})");listArgs.getDynamicObject()->setProperty("target",box);auto list=service->chamferCommand(listArgs);
    check((bool)list["ok"]&&list["edges"].size()==12,"API discovers edge indices, geometry and persistent references");
    const double length=(double)list["edges"][0]["length_mm"];
    auto params=json(R"({"distance":2,"edges":[0]})");params.getDynamicObject()->setProperty("target",box);
    const auto revision=service->document().revision();auto preview=service->chamferCommand(request("preview",{},params));
    check((bool)preview["ok"]&&(bool)preview["preview"]&&!(bool)preview["committed"]&&close((double)preview["volume_mm3"],24000-2*length)&&service->document().revision()==revision&&service->document().features().size()==1,"Preview has analytic geometry and leaves the live document unchanged");
    check(service->undo(error)&&service->document().find(box)==nullptr&&service->redo(error),"Preview adds no live Undo entries");
    auto created=service->chamferCommand(request("create",{},params));const auto id=created["id"].toString();
    check((bool)created["ok"]&&close(volume(id),24000-2*length),"Chamfer removes the expected triangular-prism volume");
    if(id.isNotEmpty())
    {
        auto changed=service->chamferCommand(request("update",id,json(R"({"distance":4})")));
        check((bool)changed["ok"]&&close(volume(id),24000-8*length),"Distance editing keeps the same feature and edge references");
        check(service->undo(error)&&close(volume(id),24000-2*length)&&service->redo(error)&&close(volume(id),24000-8*length),"Chamfer parameter Undo/Redo restores geometry");
        check(service->setDimensions(box,25,35,45,5,error),"Source dimensions rebuild the dependent chamfer");
        auto get=service->chamferCommand(request("get",id,{}));auto current=service->chamferCommand(listArgs);const int edge=(int)get["parameters"]["edges"][0];
        const double newLength=(double)current["edges"][edge]["length_mm"];
        check(close(volume(id),25*35*45-8*newLength),"Persistent edge references follow source dimension changes");
        const auto before=volume(id);const auto rev=service->document().revision();
        auto invalid=service->chamferCommand(request("update",id,json(R"({"distance":1000})")));
        check(!(bool)invalid["ok"]&&service->document().revision()==rev&&close(volume(id),before),"Oversize chamfer fails atomically");
        changed=service->chamferCommand(request("update",id,json(R"({"distance":1,"edges":[0,2]})")));
        check((bool)changed["ok"]&&changed["edgeReferences"].size()==2&&volume(id)<25*35*45,"Multiple-edge selection is editable and produces valid geometry");
        const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/chamfer-check");folder.createDirectory();
        for(const auto* ext:{"djo","djc"})
        {CadCommandService back;const auto path=folder.getChildFile(juce::String("beveled.")+ext);const bool ok=service->save(path,error)&&back.load(path,error);const auto r=back.chamferCommand(request("get",id,{}));check(ok&&(bool)r["ok"]&&r["edgeReferences"].size()==2&&(double)r["parameters"]["distance"]==1,"Native/JSON preserves chamfer parameters and persistent references");}
        check(!(bool)service->chamferCommand(request("create",{},json(R"({"target":"missing","edges":[0],"distance":1})")))["ok"],"Invalid targets return controlled errors");
        check((bool)service->chamferCommand(request("delete",id,{}))["ok"]&&service->undo(error),"Chamfer deletion is undoable");
    }
    auto gui=std::make_shared<CadCommandService>();gui->createBox(20,30,40,box,error);auto panel=std::make_unique<CadWorkspacePanel>(gui);panel->setSize(1400,900);auto selection=std::make_shared<CadSelection>();panel->setSelection(selection);
    panel->automation(json(R"({"action":"command","command":"chamfer"})"));selection->setSelectedMany({{box,box,-1,0},{box,box,-1,2}});
    panel->automation(json(R"({"action":"set","field":"chamferDistance","value":2})"));const auto guiRevision=gui->document().revision();
    auto state=panel->automation(json(R"({"action":"click","button":"chamferPreview"})"));
    check((bool)state["ok"]&&(bool)state["state"]["chamferPreview"]&&gui->document().revision()==guiRevision,"GUI Preview displays a candidate without committing");
    state=panel->automation(json(R"({"action":"set","field":"chamferDistance","value":3})"));
    check(!(bool)state["state"]["chamferPreview"],"Changing GUI parameters clears the old preview");
    state=panel->automation(json(R"({"action":"click","button":"apply"})"));juce::String guiId;for(const auto& f:gui->document().features())if(f.type=="chamfer")guiId=f.id;
    check((bool)state["ok"]&&guiId.isNotEmpty(),"GUI applies a multi-edge chamfer");
    if(guiId.isNotEmpty())
    {
        panel->automation(json(R"({"action":"set","field":"chamferDistance","value":4})"));state=panel->automation(json(R"({"action":"click","button":"apply"})"));
        check((bool)state["ok"]&&gui->document().find(guiId)->width==4,"GUI body inspector edits the chamfer distance");
        panel->automation(json(R"({"action":"click","button":"chamferPreview"})"));state=panel->automation(json(R"({"action":"click","button":"cancel"})"));
        check((bool)state["ok"]&&!(bool)state["state"]["chamferPreview"],"Cancel restores the authored model from GUI preview");
    }
    const auto node=node_compiler::CompileGraphToSource(R"({"functionName":"bevel","params":[{"name":"args","type":"String"}],"output":"r","nodes":[{"id":"r","type":"cad_chamfer","inputs":[{"param":"args"}]}]})");check(node.ok&&node.source.find("djehuti_cad_chamfer")!=std::string::npos,"Chamfer node generates the shared FRust command");
    auto* out=new juce::DynamicObject();out->setProperty("ok",failed==0);out->setProperty("passed",checks.size()-failed);out->setProperty("failed",failed);out->setProperty("checks",checks);report=out;return failed==0;
}
}
