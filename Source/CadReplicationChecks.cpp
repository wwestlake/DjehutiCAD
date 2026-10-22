#include "CadDocument.h"
#include "CadWorkspacePanel.h"
#include "CadSelection.h"
#include "FrustEngine.h"
#include "NodeCompiler.h"
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <cmath>

namespace djehuti::cad
{
bool runCadReplicationCheck(juce::var& report)
{
    juce::Array<juce::var> checks;int failed=0;
    const auto check=[&](bool ok,const juce::String& name,const juce::String& detail=juce::String())
    {auto* o=new juce::DynamicObject();o->setProperty("name",name);o->setProperty("ok",ok);if(!ok)o->setProperty("detail",detail);checks.add(o);if(!ok)++failed;};
    const auto json=[](const juce::String& s){return juce::JSON::parse(s);};const double pi=juce::MathConstants<double>::pi;
    const auto isClose=[](double a,double b){return std::abs(a-b)<std::max(1e-6,std::abs(b)*1e-8);};
    auto service=std::make_shared<CadCommandService>();juce::String box,error;service->createBox(100,100,10,box,error);
    const auto request=[&](const char* action,const juce::String& id,const juce::var& p){auto* o=new juce::DynamicObject();o->setProperty("action",action);if(id.isNotEmpty())o->setProperty("id",id);if(p.isObject())o->setProperty("parameters",p);return juce::var(o);};
    const auto command=[&](const juce::String& type,const char* action,const juce::String& id,const juce::var& p){return service->replicationCommand(type,request(action,id,p));};
    const auto volume=[&](const juce::String& id){const auto* f=service->document().find(id);GProp_GProps p;if(f)BRepGProp::VolumeProperties(f->shape,p);return p.Mass();};
    const auto bounds=[&](const juce::String& id){Bnd_Box b;BRepBndLib::AddOptimal(service->document().find(id)->shape,b,false,false);double a,c,d,e,f,g;b.Get(a,c,d,e,f,g);return std::array<double,6>{a,c,d,e,f,g};};
    const auto seed=[&](const juce::String& source){auto p=juce::var(new juce::DynamicObject());p.getDynamicObject()->setProperty("sources",juce::Array<juce::var>{source});return p;};
    for(const auto* plane:{"XY","XZ","YZ"})
    {
        auto p=seed(box);auto r=json("{\"type\":\""+juce::String(plane)+"\"}");p.getDynamicObject()->setProperty("plane",r);
        const auto result=command("mirror","create",{},p);check((bool)result["ok"]&&isClose(volume(result["id"].toString()),200000),"Mirror principal plane "+juce::String(plane),result["error"].toString());
    }
    auto p=seed(box);p.getDynamicObject()->setProperty("includeSource",false);p.getDynamicObject()->setProperty("plane",json(R"({"type":"custom","origin":[150,0,0],"vector":[1,0,0]})"));
    auto r=command("mirror","create",{},p);const auto mirror=r["id"].toString();
    check((bool)r["ok"]&&isClose(volume(mirror),100000)&&isClose(bounds(mirror)[0],200)&&isClose(bounds(mirror)[3],300),"Custom mirror plane positions an oriented solid correctly",r["error"].toString());
    if(mirror.isNotEmpty())
    {
        auto plane=json(R"({"type":"face"})");plane.getDynamicObject()->setProperty("feature",box);plane.getDynamicObject()->setProperty("face",box+"/right");auto edit=juce::var(new juce::DynamicObject());edit.getDynamicObject()->setProperty("plane",plane);
        r=command("mirror","update",mirror,edit);check((bool)r["ok"]&&isClose(bounds(mirror)[0],100),"Mirror uses a selected planar face",r["error"].toString());
        check(service->setDimensions(box,120,100,10,5,error)&&isClose(bounds(mirror)[0],120)&&isClose(bounds(mirror)[3],240),"Mirror follows source and face-plane changes",error);
        check(service->undo(error)&&isClose(bounds(mirror)[0],100)&&service->redo(error)&&isClose(bounds(mirror)[0],120),"Mirror supports Undo/Redo");
        service->setDimensions(box,100,100,10,5,error);
    }
    auto holeArgs=json(R"({"action":"create","parameters":{"u":10,"v":10,"diameter":4,"throughAll":true}})");holeArgs["parameters"].getDynamicObject()->setProperty("target",box);holeArgs["parameters"].getDynamicObject()->setProperty("face",box+"/top");const auto hole=service->holeCommand(holeArgs)["id"].toString();
    p=seed(hole);p.getDynamicObject()->setProperty("operation","feature");p.getDynamicObject()->setProperty("plane",json(R"({"type":"custom","origin":[50,0,0],"vector":[1,0,0]})"));
    r=command("mirror","create",{},p);const auto mirroredHole=r["id"].toString();check((bool)r["ok"]&&isClose(volume(mirroredHole),100000-80*pi),"Mirror reapplies a Hole to its original target",r["error"].toString());
    p=seed(hole);p.getDynamicObject()->setProperty("operation","feature");p.getDynamicObject()->setProperty("count1",3);p.getDynamicObject()->setProperty("count2",2);p.getDynamicObject()->setProperty("spacing1",20);p.getDynamicObject()->setProperty("spacing2",30);
    const auto revision=service->document().revision();r=command("linearPattern","preview",{},p);
    check((bool)r["ok"]&&(bool)r["preview"]&&!(bool)r["committed"]&&isClose((double)r["volume_mm3"],100000-240*pi)&&service->document().revision()==revision,"Linear hole preview is exact and leaves the live document unchanged",r["error"].toString());
    r=command("linearPattern","create",{},p);const auto linear=r["id"].toString();check((bool)r["ok"]&&(int)r["instanceCount"]==6&&isClose(volume(linear),100000-240*pi),"Two-direction hole array modifies one intended target",r["error"].toString());
    auto editHole=json(R"({"action":"update","parameters":{"diameter":6}})");editHole.getDynamicObject()->setProperty("id",hole);r=service->holeCommand(editHole);
    check((bool)r["ok"]&&isClose(volume(linear),100000-540*pi)&&isClose(volume(mirroredHole),100000-180*pi),"Hole diameter edits rebuild both repeated feature results",r["error"].toString());
    r=command("linearPattern","update",linear,json(R"({"includeSource":false,"count2":1})"));check((bool)r["ok"]&&(int)r["instanceCount"]==2&&(int)r["instanceIndices"][0]==1&&isClose(volume(linear),100000-180*pi),"Excluded source retains predictable grid indices",r["error"].toString());
    check(service->undo(error)&&isClose(volume(linear),100000-540*pi)&&service->redo(error)&&isClose(volume(linear),100000-180*pi),"Pattern parameter Undo/Redo restores material");
    auto circularHoleArgs=json(R"({"action":"create","parameters":{"u":70,"v":50,"diameter":4,"throughAll":true}})");circularHoleArgs["parameters"].getDynamicObject()->setProperty("target",box);circularHoleArgs["parameters"].getDynamicObject()->setProperty("face",box+"/top");const auto radialHole=service->holeCommand(circularHoleArgs)["id"].toString();
    p=seed(radialHole);p.getDynamicObject()->setProperty("operation","feature");p.getDynamicObject()->setProperty("axis",json(R"({"type":"custom","origin":[50,50,0],"vector":[0,0,1]})"));
    r=command("circularPattern","create",{},p);const auto circular=r["id"].toString();check((bool)r["ok"]&&(int)r["instanceCount"]==6&&isClose(volume(circular),100000-240*pi),"Circular bolt-hole array excludes the duplicate 360-degree endpoint",r["error"].toString());
    r=command("circularPattern","update",circular,json(R"({"count":4,"angleDegrees":180,"reverse":true})"));check((bool)r["ok"]&&isClose(volume(circular),100000-160*pi),"Partial circular array includes both ends and supports reverse",r["error"].toString());
    auto resized=json(R"({"action":"update","parameters":{"diameter":5}})");resized.getDynamicObject()->setProperty("id",radialHole);r=service->holeCommand(resized);check((bool)r["ok"]&&isClose(volume(circular),100000-250*pi),"Circular pattern stays linked to its Hole operation",r["error"].toString());

    // Repeat a sketch Cut and a Boolean additive operation, preserving their source relationships.
    juce::String sketch,group,cut;service->createSketch("XY",10,sketch,error);service->addSketchRectangle(sketch,10,60,5,5,group,error);service->createCut(sketch,box,3,false,true,cut,error);
    p=seed(cut);p.getDynamicObject()->setProperty("operation","feature");p.getDynamicObject()->setProperty("count1",3);p.getDynamicObject()->setProperty("spacing1",20);
    r=command("linearPattern","create",{},p);const auto cutArray=r["id"].toString();check((bool)r["ok"]&&isClose(volume(cutArray),100000-225),"Linear pattern reapplies a sketch Cut",r["error"].toString());
    if(cutArray.isNotEmpty()){service->setDimensions(cut,10,4,4,5,error);check(isClose(volume(cutArray),100000-300),"Sketch-cut depth changes rebuild patterned cuts",error);}
    juce::String boss,addition;service->createBox(5,5,5,boss,error);service->setPlacement(boss,10,10,10,0,0,0,error);service->booleanOp("union",box,boss,addition,error);
    p=seed(addition);p.getDynamicObject()->setProperty("operation","feature");p.getDynamicObject()->setProperty("count1",3);p.getDynamicObject()->setProperty("spacing1",20);r=command("linearPattern","create",{},p);const auto bosses=r["id"].toString();check((bool)r["ok"]&&isClose(volume(bosses),100375),"Additive Union feature repeats bosses on its base target",r["error"].toString());
    service->setDimensions(boss,5,5,8,5,error);check(bosses.isNotEmpty()&&isClose(volume(bosses),100600),"Additive source edits rebuild its pattern",error);
    p=seed(boss);p.getDynamicObject()->setProperty("operation","add");p.getDynamicObject()->setProperty("target",box);p.getDynamicObject()->setProperty("plane",json(R"({"type":"custom","origin":[50,0,0],"vector":[1,0,0]})"));r=command("mirror","create",{},p);check((bool)r["ok"]&&isClose(volume(r["id"].toString()),100400),"Mirror can add source solids into a chosen target",r["error"].toString());

    juce::String extension,stepped;service->createBox(40,100,10,extension,error);service->setPlacement(extension,20,0,-10,0,0,0,error);service->booleanOp("union",box,extension,stepped,error);
    auto steppedArgs=json(R"({"action":"create","parameters":{"u":10,"v":10,"diameter":4,"throughAll":true}})");steppedArgs["parameters"].getDynamicObject()->setProperty("target",stepped);steppedArgs["parameters"].getDynamicObject()->setProperty("face",box+"/top");const auto steppedHole=service->holeCommand(steppedArgs)["id"].toString();
    for(const auto& type:juce::StringArray{"mirror","linearPattern","circularPattern"})
    {
        auto definition=seed(steppedHole);definition.getDynamicObject()->setProperty("operation","feature");
        if(type=="mirror")definition.getDynamicObject()->setProperty("plane",json(R"({"type":"custom","origin":[20,0,0],"vector":[1,0,0]})"));
        if(type=="linearPattern")definition.getDynamicObject()->setProperty("spacing1",20);
        if(type=="circularPattern"){definition.getDynamicObject()->setProperty("axis",json(R"({"type":"custom","origin":[20,10,0],"vector":[0,0,1]})"));definition.getDynamicObject()->setProperty("count",2);}
        const auto result=command(type,"create",{},definition);check((bool)result["ok"]&&isClose(volume(result["id"].toString()),140000-120*pi),type+" through-all cutter reaches a thicker repeated position",result["error"].toString());
    }
    juce::String marker;service->createBox(2,2,2,marker,error);service->setPlacement(marker,19,-1,-1,0,0,0,error);
    p=seed(marker);p.getDynamicObject()->setProperty("count",3);p.getDynamicObject()->setProperty("angleDegrees",180);r=command("circularPattern","create",{},p);
    GProp_GProps mass;if((bool)r["ok"])BRepGProp::VolumeProperties(service->document().find(r["id"].toString())->shape,mass);
    check((bool)r["ok"]&&isClose(mass.CentreOfMass().Y(),20.0/3)&&isClose(mass.Mass(),24),"Partial circular distribution has exact endpoint and middle positions",r["error"].toString());

    // Reference directions and axes follow the same persistent edge tokens as Chamfer.
    auto refs=json(R"({"action":"references"})");refs.getDynamicObject()->setProperty("target",box);r=service->replicationCommand("linearPattern",refs);check((bool)r["ok"]&&r["edges"].size()==12,"API discovers reference edges");
    p=seed(boss);p.getDynamicObject()->setProperty("direction1",json(R"({"type":"edge","edge":0})"));p["direction1"].getDynamicObject()->setProperty("feature",box);p.getDynamicObject()->setProperty("reverse1",true);r=command("linearPattern","create",{},p);check((bool)r["ok"]&&isClose(volume(r["id"].toString()),400)&&r["parameters"]["direction1"]["reference"].toString().isNotEmpty(),"Linear direction uses a persistent straight edge and reverse",r["error"].toString());
    juce::String cylinder;service->createCylinder(5,10,cylinder,error);refs.getDynamicObject()->setProperty("target",cylinder);const auto edges=service->replicationCommand("circularPattern",refs);int circle=-1;for(const auto& e:*edges["edges"].getArray())if(e["curve"].toString()=="circle"){circle=(int)e["index"];break;}
    p=seed(boss);p.getDynamicObject()->setProperty("axis",json("{\"type\":\"edge\",\"edge\":"+juce::String(circle)+"}"));p["axis"].getDynamicObject()->setProperty("feature",cylinder);r=command("circularPattern","create",{},p);check((bool)r["ok"]&&isClose(volume(r["id"].toString()),1200),"Circular pattern uses a geometric circle axis",r["error"].toString());
    for(const auto& type:juce::StringArray{"mirror","linearPattern","circularPattern"})
    {
        const auto id=type=="mirror"?mirroredHole:type=="linearPattern"?linear:circular;if(id.isEmpty())continue;
        const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/replication-check");folder.createDirectory();
        for(const auto* ext:{"djo","djc"}){CadCommandService back;const auto file=folder.getChildFile(type+"."+ext);const bool ok=service->save(file,error)&&back.load(file,error);const auto restored=back.replicationCommand(type,request("get",id,{}));GProp_GProps props;if(back.document().find(id))BRepGProp::VolumeProperties(back.document().find(id)->shape,props);check(ok&&(bool)restored["ok"]&&isClose(props.Mass(),volume(id)),type+" persists definitions/references and material through "+ext,error);}
        const auto rev=service->document().revision();auto invalid=seed("missing");auto rejected=command(type,"update",id,invalid);check(!(bool)rejected["ok"]&&service->document().revision()==rev,type+" rejects missing sources atomically");
        check(!(bool)service->remove(hole,error),"Source deletion is blocked by repeated feature dependencies");
        check((bool)command(type,"delete",id,{})["ok"]&&service->undo(error),type+" deletion is undoable");
    }
    const auto rev=service->document().revision();
    for(const auto& invalid:juce::StringArray{R"({"count1":1.5})",R"({"count1":0})",R"({"spacing1":0})",R"({"count1":256,"count2":256})",R"({"direction1":{"type":"custom","vector":[0,0,0]}})",R"({"reverse1":1})"})check(!(bool)command("linearPattern","update",linear,json(invalid))["ok"]&&service->document().revision()==rev,"Invalid linear count/spacing/direction is controlled: "+invalid);
    check(!(bool)command("circularPattern","update",circular,json(R"({"axis":{"type":"custom","vector":[0,0,0]}})"))["ok"],"Zero circular axis is refused");
    check(!(bool)command("circularPattern","update",circular,json(R"({"angleDegrees":361})"))["ok"],"Circular angles above 360 are refused");
    check(!(bool)command("linearPattern","update",linear,json(R"({"spacing1":1000})"))["ok"]&&service->document().revision()==rev,"An off-target patterned cut fails without corruption");
    auto self=seed(linear);check(!(bool)command("linearPattern","update",linear,self)["ok"],"Self dependencies are refused");

    juce::String alternate;service->createBox(120,100,20,alternate,error);
    auto reattach=json(R"({"action":"update","parameters":{}})");reattach.getDynamicObject()->setProperty("id",hole);reattach["parameters"].getDynamicObject()->setProperty("target",alternate);reattach["parameters"].getDynamicObject()->setProperty("face",alternate+"/top");r=service->holeCommand(reattach);
    check((bool)r["ok"]&&isClose(volume(linear),240000-360*pi)&&isClose(volume(mirroredHole),240000-360*pi),"Automatic targets follow a source Hole reassigned to a different body",r["error"].toString());
    const auto binding=command("linearPattern","get",linear,{});check(binding["parameters"]["target"].toString().isEmpty()&&binding["resolvedTarget"].toString()==alternate,"API distinguishes automatic target binding from resolved target ID");
    check(service->undo(error)&&isClose(volume(linear),100000-180*pi),"Undo restores the source target and dependent patterns",error);

    // GUI uses the real task controls and shared selection callbacks.
    auto gui=std::make_shared<CadCommandService>();gui->createBox(10,8,6,box,error);auto panel=std::make_unique<CadWorkspacePanel>(gui);panel->setSize(1400,900);auto selection=std::make_shared<CadSelection>();panel->setSelection(selection);
    for(const auto& type:juce::StringArray{"mirror","linearPattern","circularPattern"})
    {
        auto a=json(R"({"action":"select"})");a.getDynamicObject()->setProperty("id",box);panel->automation(a);a=json(R"({"action":"command"})");a.getDynamicObject()->setProperty("command",type);panel->automation(a);
        if(type=="mirror")
        {
            panel->automation(json(R"({"action":"set","field":"planeType","value":"face"})"));panel->automation(json(R"({"action":"click","button":"planePick"})"));
            std::vector<CadFaceInfo> faces;gui->listFaces(box,faces,error);for(const auto& face:faces)if(face.token==box+"/right")selection->setSelected({box,box,face.faceIndex,-1});
        }
        if(type=="linearPattern")
        {
            panel->automation(json(R"({"action":"set","field":"direction1Type","value":"edge"})"));panel->automation(json(R"({"action":"click","button":"direction1Pick"})"));selection->setSelected({box,box,-1,0});
        }
        if(type=="mirror")panel->automation(json(R"({"action":"set","field":"repInclude","value":false})"));
        if(type=="linearPattern"){panel->automation(json(R"({"action":"set","field":"rep_count1","value":3})"));panel->automation(json(R"({"action":"set","field":"rep_count2","value":2})"));}
        auto state=panel->automation(json(R"({"action":"click","button":"repPreview"})"));check((bool)state["ok"]&&(bool)state["state"]["replicationPreview"],type+" GUI displays an isolated preview");
        state=panel->automation(json(R"({"action":"click","button":"apply"})"));juce::String id;for(const auto& f:gui->document().features())if(f.type==type)id=f.id;check((bool)state["ok"]&&id.isNotEmpty(),type+" GUI creates a feature");
        panel->automation(json(R"({"action":"click","button":"repEdit"})"));panel->automation(json(R"({"action":"set","field":"repInclude","value":true})"));state=panel->automation(json(R"({"action":"click","button":"apply"})"));check((bool)state["ok"]&&(bool)gui->document().find(id)->replication["includeSource"],type+" GUI reopens and edits the same feature");
        panel->automation(json(R"({"action":"click","button":"repEdit"})"));panel->automation(json(R"({"action":"click","button":"repPreview"})"));state=panel->automation(json(R"({"action":"click","button":"cancel"})"));check(!(bool)state["state"]["replicationPreview"],type+" GUI cancel restores the committed view");
    }
    panel.reset();
    // Execute real FRust and compiled CAD-node code, in addition to checking generated names.
    frust_engine::setCadCommandService(service);
    for(const auto& item:std::vector<std::pair<juce::String,juce::String>>{{"mirror",mirroredHole},{"linear_pattern",linear},{"circular_pattern",circular}})
    {
        const auto args=juce::JSON::toString(request("get",item.second,{}),true).toStdString();const auto literal=juce::JSON::toString(juce::var(args),true).toStdString();
        const auto script="pub fn run() -> String = { djehuti_cad_"+item.first.toStdString()+"("+literal+") }";
        frust_engine::RunOptions options;options.prelude=frust_engine::cadPrelude();const auto run=frust_engine::runScript(script,options);
        check(run.ok&&(bool)json(run.output)["ok"],item.first+" FRust host executes the shared command",juce::String(run.report()));
        const auto graph="{\"functionName\":\"inspect\",\"params\":[{\"name\":\"args\",\"type\":\"String\"}],\"output\":\"r\",\"nodes\":[{\"id\":\"r\",\"type\":\"cad_"+item.first.toStdString()+"\",\"inputs\":[{\"param\":\"args\"}]}]}";
        const auto node=node_compiler::CompileGraphToSource(graph);const auto nodeRun=node.ok?frust_engine::runScript(node.source+"\npub fn run() -> String = { inspect("+literal+") }"):frust_engine::Result();
        check(node.ok&&nodeRun.ok&&(bool)json(nodeRun.output)["ok"],item.first+" CAD node compiles and executes the shared host",juce::String(nodeRun.report()));
    }
    frust_engine::setCadCommandService({});auto* out=new juce::DynamicObject();out->setProperty("ok",failed==0);out->setProperty("passed",checks.size()-failed);out->setProperty("failed",failed);out->setProperty("checks",checks);report=out;return failed==0;
}
}
