#include "CadDocument.h"
#include "CadWorkspacePanel.h"
#include "CadSelection.h"
#include "NodeCompiler.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>

namespace djehuti::cad
{
bool runCadHoleCheck(juce::var& report)
{
    juce::Array<juce::var> checks;int failed=0;
    const auto check=[&](bool ok,const char* name){auto* o=new juce::DynamicObject();o->setProperty("name",name);o->setProperty("ok",ok);checks.add(o);if(!ok)++failed;};
    const auto json=[](const juce::String& s){return juce::JSON::parse(s);};
    const auto request=[&](const char* action,const juce::String& id,const juce::var& params){auto* a=new juce::DynamicObject();a->setProperty("action",action);if(id.isNotEmpty())a->setProperty("id",id);if(params.isObject())a->setProperty("parameters",params);return juce::var(a);};
    const auto close=[](double a,double b){return std::abs(a-b)<std::max(1e-7,std::abs(b)*1e-8);};const auto pi=juce::MathConstants<double>::pi;
    auto service=std::make_shared<CadCommandService>();juce::String box,error;service->createBox(40,30,20,box,error);
    const auto volume=[&](const juce::String& id){GProp_GProps p;const auto* f=service->document().find(id);if(f)BRepGProp::VolumeProperties(f->shape,p);return p.Mass();};
    auto frameArgs=json(R"({"action":"face"})");frameArgs.getDynamicObject()->setProperty("target",box);frameArgs.getDynamicObject()->setProperty("face",box+"/top");const auto frame=service->holeCommand(frameArgs);
    check((bool)frame["ok"]&&close((double)frame["centreUV"][0],20)&&close((double)frame["centreUV"][1],15)&&(double)frame["frame"]["normal"][2]>0.99,"API describes the planar support frame and default centre");
    auto params=json(R"({"diameter":6,"depth":5})");params.getDynamicObject()->setProperty("target",box);params.getDynamicObject()->setProperty("face",box+"/top");
    auto created=service->holeCommand(request("create",{},params));const auto id=created["id"].toString();
    check((bool)created["ok"]&&close(volume(id),24000-45*pi),"Blind hole removes the expected cylinder volume");
    if(id.isNotEmpty())
    {
        std::vector<CadFaceInfo> faces;service->listFaces(id,faces,error);bool bottom=false,top=false;
        for(const auto& f:faces){if(f.token==id+"/bottom"&&f.planar)bottom=true;if(f.token==box+"/top")top=true;}
        check(bottom&&top,"Blind bottom and inherited support face have persistent names");
        auto changed=service->holeCommand(request("update",id,json(R"({"u":10,"v":10,"depth":8,"style":"counterbore","headDiameter":12,"headDepth":3})")));
        check((bool)changed["ok"]&&close(volume(id),24000-153*pi),"Counterbore dimensions and numeric positioning update the existing feature");
        changed=service->holeCommand(request("update",id,json(R"({"style":"countersink","countersinkAngleDegrees":90})")));
        check((bool)changed["ok"]&&close(volume(id),24000-108*pi),"Countersink removes the exact frustum-plus-shaft volume");
        changed=service->holeCommand(request("update",id,json(R"({"throughAll":true})")));
        check((bool)changed["ok"]&&close(volume(id),24000-216*pi),"Through-all countersink reaches the far side of the target");
        check(service->undo(error)&&close(volume(id),24000-108*pi)&&service->redo(error)&&close(volume(id),24000-216*pi),"Hole edits support OCAF Undo/Redo");
        check(service->setDimensions(box,40,30,30,5,error)&&close(volume(id),36000-306*pi),"Through-all hole follows support movement and target thickness changes");
        const auto revision=service->document().revision();const auto before=volume(id);
        auto rejected=service->holeCommand(request("update",id,json(R"({"u":1000})")));
        check(!(bool)rejected["ok"]&&service->document().revision()==revision&&close(volume(id),before),"Off-face positioning fails without changing the valid model");
        rejected=service->holeCommand(request("update",id,json(R"({"countersinkAngleDegrees":180})")));
        check(!(bool)rejected["ok"]&&service->document().revision()==revision,"Invalid countersink angle is rejected");
        rejected=service->holeCommand(request("update",id,json(R"({"style":"counterbore","headDepth":100})")));
        check(!(bool)rejected["ok"]&&service->document().revision()==revision,"Oversize head depth is a controlled atomic failure");
        rejected=service->holeCommand(request("update",id,json(R"({"diameter":-2})")));
        check(!(bool)rejected["ok"]&&service->document().revision()==revision,"Negative diameter is rejected");
        const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/hole-check");folder.createDirectory();
        for(const auto* ext:{"djo","djc"})
        {CadCommandService back;const auto path=folder.getChildFile(juce::String("drilled.")+ext);const bool ok=service->save(path,error)&&back.load(path,error);const auto r=back.holeCommand(request("get",id,{}));check(ok&&(bool)r["ok"]&&r["parameters"]["face"].toString()==box+"/top"&&(bool)r["parameters"]["throughAll"]&&r["parameters"]["style"].toString()=="countersink","Native/JSON retains face references and complete hole parameters");}
        changed=service->holeCommand(request("update",id,json(R"({"style":"plain"})")));
        auto secondParams=json(R"({"diameter":3,"throughAll":true,"u":30,"v":20})");secondParams.getDynamicObject()->setProperty("target",id);secondParams.getDynamicObject()->setProperty("face",box+"/top");const auto second=service->holeCommand(request("create",{},secondParams));
        changed=service->holeCommand(request("update",id,json(R"({"diameter":8})")));
        check((bool)second["ok"]&&(bool)changed["ok"]&&close(volume(second["id"].toString()),36000-547.5*pi),"Chained holes rebuild through inherited face references");
        check(!(bool)service->holeCommand(request("delete",id,{}))["ok"],"Deletion refuses dependent holes");
        check((bool)service->holeCommand(request("delete",second["id"].toString(),{}))["ok"]&&service->undo(error),"Hole deletion is undoable");
    }
    for(const auto* faceName:{"right","bottom"})
    {
        auto p=json(R"({"diameter":4,"depth":5})");p.getDynamicObject()->setProperty("target",box);p.getDynamicObject()->setProperty("face",box+"/"+faceName);const auto r=service->holeCommand(request("create",{},p));
        check((bool)r["ok"]&&close(volume(r["id"].toString()),36000-20*pi),juce::String(faceName)=="right"?"Side drilling follows the inward support normal":"Bottom drilling follows the inward support normal");
    }
    juce::String cylinder;service->createCylinder(10,20,cylinder,error);params=json(R"({"diameter":4,"depth":5})");params.getDynamicObject()->setProperty("target",cylinder);params.getDynamicObject()->setProperty("face",cylinder+"/side");const auto revision=service->document().revision();
    check(!(bool)service->holeCommand(request("create",{},params))["ok"]&&service->document().revision()==revision,"Curved support faces are refused without mutation");

    auto gui=std::make_shared<CadCommandService>();gui->createBox(40,30,20,box,error);std::vector<CadFaceInfo> faces;gui->listFaces(box,faces,error);int top=-1,right=-1;for(const auto& f:faces){if(f.token==box+"/top")top=f.faceIndex;if(f.token==box+"/right")right=f.faceIndex;}
    auto panel=std::make_unique<CadWorkspacePanel>(gui);panel->setSize(1400,900);auto selection=std::make_shared<CadSelection>();panel->setSelection(selection);
    panel->automation(json(R"({"action":"command","command":"hole"})"));selection->setSelected({box,box,top,-1});
    auto state=panel->automation(json(R"({"action":"state"})"));check((bool)state["state"]["canApply"],"GUI picks a planar face and fills usable U/V defaults");
    panel->automation(json(R"({"action":"set","field":"holeStyle","value":"Counterbore"})"));panel->automation(json(R"({"action":"set","field":"holeDepth","value":8})"));panel->automation(json(R"({"action":"set","field":"holeHeadDepth","value":2})"));panel->automation(json(R"({"action":"set","field":"holeU","value":10})"));panel->automation(json(R"({"action":"set","field":"holeV","value":10})"));
    state=panel->automation(json(R"({"action":"click","button":"apply"})"));juce::String guiId;for(const auto& f:gui->document().features())if(f.type=="hole")guiId=f.id;
    check((bool)state["ok"]&&guiId.isNotEmpty(),"GUI creates a positioned counterbore");
    if(guiId.isNotEmpty())
    {
        GProp_GProps p;BRepGProp::VolumeProperties(gui->document().find(guiId)->shape,p);check(close(p.Mass(),24000-126*pi),"GUI counterbore dimensions produce the expected volume");
        panel->automation(json(R"({"action":"set","field":"holeStyle","value":"Countersink"})"));state=panel->automation(json(R"({"action":"click","button":"apply"})"));BRepGProp::VolumeProperties(gui->document().find(guiId)->shape,p);
        check((bool)state["ok"]&&close(p.Mass(),24000-108*pi),"GUI inspector changes an existing hole to a countersink");
        panel->automation(json(R"({"action":"set","field":"holeThroughAll","value":true})"));state=panel->automation(json(R"({"action":"click","button":"apply"})"));BRepGProp::VolumeProperties(gui->document().find(guiId)->shape,p);
        check((bool)state["ok"]&&close(p.Mass(),24000-216*pi),"GUI toggles through-all drilling");
        panel->automation(json(R"({"action":"click","button":"holeEditFace"})"));selection->setSelected({box,box,right,-1});state=panel->automation(json(R"({"action":"click","button":"apply"})"));
        check((bool)state["ok"]&&gui->document().find(guiId)->hole.face==box+"/right","GUI reselects the source support face for an existing hole");
    }
    const auto node=node_compiler::CompileGraphToSource(R"({"functionName":"drill","params":[{"name":"args","type":"String"}],"output":"r","nodes":[{"id":"r","type":"cad_hole","inputs":[{"param":"args"}]}]})");check(node.ok&&node.source.find("djehuti_cad_hole")!=std::string::npos,"Hole node generates the shared FRust host command");
    auto* out=new juce::DynamicObject();out->setProperty("ok",failed==0);out->setProperty("passed",checks.size()-failed);out->setProperty("failed",failed);out->setProperty("checks",checks);report=out;return failed==0;
}
}
