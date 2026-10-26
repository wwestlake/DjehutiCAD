#include "CadDocument.h"
#include "CadWorkspacePanel.h"
#include "CadSelection.h"
#include <cmath>

namespace djehuti::cad
{
// Opt-in Phase-1 checks: no trimming, sewing, thickening or broad regression suite.
bool runCadSurfaceCreationCheck(juce::var& report)
{
    juce::Array<juce::var> checks;int failed=0;juce::String error;
    const auto progress=juce::File::getCurrentWorkingDirectory().getChildFile("build/surface-creation-check.progress.log");progress.replaceWithText("Phase-1 surface checks\n");
    const auto json=[](const juce::String& text){return juce::JSON::parse(text);};
    const auto check=[&](bool ok,const juce::String& name,const juce::String& detail=juce::String())
    {progress.appendText((ok?"PASS ":"FAIL ")+name+" "+detail+"\n");auto* c=new juce::DynamicObject();c->setProperty("name",name);c->setProperty("ok",ok);if(!ok){c->setProperty("detail",detail);++failed;}checks.add(c);};
    const auto closeEnough=[](double a,double b){return std::isfinite(a)&&std::abs(a-b)<std::max(1e-5,std::abs(b)*1e-6);};
    const auto command=[&](const std::shared_ptr<CadCommandService>& s,const char* action,const juce::String& id,juce::var p=juce::var())
    {auto r=json("{}");r.getDynamicObject()->setProperty("action",action);if(id.isNotEmpty())r.getDynamicObject()->setProperty("id",id);if(p.isObject())r.getDynamicObject()->setProperty("parameters",p);return s->modelingCommand("surface",r);};
    const auto create=[&](const std::shared_ptr<CadCommandService>& s,const char* method,const juce::String& sketch,juce::var p=juce::var())
    {if(!p.isObject())p=json("{}");p.getDynamicObject()->setProperty("method",method);p.getDynamicObject()->setProperty("profile",sketch);return command(s,"create",{},p);};
    const auto rectangle=[&](const std::shared_ptr<CadCommandService>& s,double z)
    {juce::String id,group;s->createSketch("XY",z,id,error);s->addSketchRectangle(id,0,0,10,10,group,error);return id;};
    const auto line=[&](const std::shared_ptr<CadCommandService>& s,const char* plane,double offset,double x0,double y0,double x1,double y1,juce::String& entity)
    {juce::String id;s->createSketch(plane,offset,id,error);s->addSketchLine(id,x0,y0,x1,y1,entity,error);return id;};
    const auto metric=[&](const std::shared_ptr<CadCommandService>& s,const juce::String& id,const char* key)
    {const auto* f=s->document().find(id);return f?(double)surfaceTopology(f->shape)[key]:-1.0;};
    const double pi=juce::MathConstants<double>::pi;
    auto s=std::make_shared<CadCommandService>();juce::String entity;
    const auto square=rectangle(s,0);auto r=create(s,"planar",square);const auto plane=r["id"].toString();
    check((bool)r["ok"]&&closeEnough(metric(s,plane,"area_mm2"),100)&&metric(s,plane,"solids")==0,"Planar rectangular boundary creates an independent face",r["error"].toString());
    bool twoSided=false;for(const auto& mesh:s->snapshot()->meshes)if(mesh.featureId==plane)twoSided=mesh.doubleSided;
    check(twoSided,"Surface viewport mesh renders on both sides");
    juce::String circle,ring;s->createSketch("XY",0,circle,error);s->addSketchCircle(circle,20,0,3,ring,error);r=create(s,"planar",circle);const auto disc=r["id"].toString();
    check((bool)r["ok"]&&closeEnough(metric(s,disc,"area_mm2"),9*pi),"Planar circular boundary",r["error"].toString());
    juce::String triangle;s->createSketch("XY",0,triangle,error);s->addSketchLine(triangle,0,0,8,0,entity,error);s->addSketchLine(triangle,8,0,2,5,entity,error);s->addSketchLine(triangle,2,5,0,0,entity,error);r=create(s,"planar",triangle);
    check((bool)r["ok"]&&closeEnough((double)r["topology"]["area_mm2"],20),"Planar arbitrary closed polygon",r["error"].toString());
    const auto selected=rectangle(s,5);juce::Array<juce::var> contour;for(const auto& e:s->document().findSketch(selected)->entities)contour.add(e.id);s->addSketchLine(selected,30,0,40,0,entity,error);
    auto subset=json(R"({"source":{}})");subset["source"].getDynamicObject()->setProperty("sketch",selected);subset["source"].getDynamicObject()->setProperty("entities",contour);r=create(s,"planar",{},subset);
    check((bool)r["ok"]&&closeEnough((double)r["topology"]["area_mm2"],100),"Selected contour ignores unrelated ordinary sketch curves",r["error"].toString());
    const auto open=line(s,"XY",0,0,0,10,0,entity);const auto openEntity=entity;auto rev=s->document().revision();r=create(s,"planar",open);
    check(!(bool)r["ok"]&&s->document().revision()==rev,"Open planar boundary fails without document mutation");
    r=create(s,"extrude",open,json(R"({"distance":5})"));const auto extruded=r["id"].toString();
    check((bool)r["ok"]&&closeEnough(metric(s,extruded,"area_mm2"),50)&&metric(s,extruded,"solids")==0,"Open curve extrusion produces an uncapped surface",r["error"].toString());
    check(s->setSketchEntityDimension(open,openEntity,20,error)&&closeEnough(metric(s,extruded,"area_mm2"),100),"Extrusion rebuilds after source edit",error);
    r=command(s,"update",extruded,json(R"({"distance":7,"direction":[0,0,2]})"));
    check((bool)r["ok"]&&r["id"].toString()==extruded&&closeEnough(metric(s,extruded,"area_mm2"),140),"Extrusion distance/direction edit preserves identity",r["error"].toString());
    check(s->undo(error)&&closeEnough(metric(s,extruded,"area_mm2"),100)&&s->redo(error)&&closeEnough(metric(s,extruded,"area_mm2"),140),"Surface edit Undo/Redo",error);
    r=create(s,"extrude",circle,json(R"({"distance":-10})"));const auto cylinder=r["id"].toString();
    check((bool)r["ok"]&&closeEnough(metric(s,cylinder,"area_mm2"),60*pi)&&metric(s,cylinder,"solids")==0&&metric(s,cylinder,"unmatchedEdges")==2,"Closed curve extrusion has two uncapped circular boundaries",r["error"].toString());
    rev=s->document().revision();r=command(s,"update",extruded,json(R"({"direction":[0,0,0]})"));check(!(bool)r["ok"]&&s->document().revision()==rev,"Zero extrusion direction fails atomically");
    r=command(s,"update",extruded,json(R"({"direction":[1,0,0]})"));check(!(bool)r["ok"]&&s->document().revision()==rev,"Tangent extrusion direction fails atomically");
    const auto radial=line(s,"XZ",0,3,0,3,10,entity);const auto radialEntity=entity;r=create(s,"revolve",radial);const auto revolved=r["id"].toString();
    check((bool)r["ok"]&&closeEnough(metric(s,revolved,"area_mm2"),60*pi)&&metric(s,revolved,"solids")==0,"Full curve revolution produces cylinder surface",r["error"].toString());
    r=command(s,"update",revolved,json(R"({"angleDegrees":180})"));check((bool)r["ok"]&&closeEnough(metric(s,revolved,"area_mm2"),30*pi),"Partial revolution angle editing",r["error"].toString());
    rev=s->document().revision();r=command(s,"update",revolved,json(R"({"axis":{"type":"worldY"}})"));check(!(bool)r["ok"]&&s->document().revision()==rev,"Noncoplanar sketch axis fails atomically");
    r=command(s,"update",revolved,json(R"({"angleDegrees":0})"));check(!(bool)r["ok"]&&s->document().revision()==rev,"Zero revolution angle rejected");
    const auto axisSketch=line(s,"XZ",0,0,0,0,10,entity);const auto axisEntity=entity;auto axis=json(R"({"axis":{"type":"sketchLine"}})");axis["axis"].getDynamicObject()->setProperty("sketch",axisSketch);axis["axis"].getDynamicObject()->setProperty("entity",axisEntity);r=command(s,"update",revolved,axis);
    check((bool)r["ok"]&&closeEnough(metric(s,revolved,"area_mm2"),30*pi),"Selected sketch-line axis supports later document references",r["error"].toString());
    check(s->setSketchLineGeometry(axisSketch,axisEntity,1,0,1,10,error)&&closeEnough(metric(s,revolved,"area_mm2"),20*pi),"Axis-line dependency regenerates revolved surface",error);
    check(s->setSketchEntityDimension(radial,radialEntity,20,error)&&closeEnough(metric(s,revolved,"area_mm2"),40*pi),"Profile dependency regenerates revolved surface",error);
    const auto lower=line(s,"XY",0,0,0,10,0,entity),upper=line(s,"XY",10,0,0,10,0,entity);const auto upperEntity=entity;
    auto ruled=json(R"({"secondSource":{}})");ruled["secondSource"].getDynamicObject()->setProperty("sketch",upper);r=create(s,"ruled",lower,ruled);const auto ruledId=r["id"].toString();
    check((bool)r["ok"]&&closeEnough(metric(s,ruledId,"area_mm2"),100)&&metric(s,ruledId,"solids")==0,"Ruled surface joins two compatible open curves",r["error"].toString());
    check(s->setSketchEntityDimension(upper,upperEntity,20,error)&&closeEnough(metric(s,ruledId,"area_mm2"),150),"Ruled surface follows second curve dependency",error);
    auto reversed=json(R"({"source":{"reverse":true},"secondSource":{"reverse":true}})");reversed["source"].getDynamicObject()->setProperty("sketch",lower);reversed["secondSource"].getDynamicObject()->setProperty("sketch",upper);r=command(s,"update",ruledId,reversed);
    check((bool)r["ok"]&&r["id"].toString()==ruledId&&closeEnough(metric(s,ruledId,"area_mm2"),150),"Ruled correspondence reversal remains editable",r["error"].toString());
    auto bad=json(R"({"secondSource":{}})");bad["secondSource"].getDynamicObject()->setProperty("sketch",circle);rev=s->document().revision();r=command(s,"update",ruledId,bad);check(!(bool)r["ok"]&&s->document().revision()==rev&&r["error"].toString().contains("open"),"Ruled rejects mixed open/closed sources with feedback",r["error"].toString());
    bad["secondSource"].getDynamicObject()->setProperty("sketch",triangle);r=create(s,"ruled",square,bad);check(!(bool)r["ok"]&&r["error"].toString().contains("segment"),"Ruled rejects mismatched segment counts",r["error"].toString());
    const auto elevated=rectangle(s,15);ruled["secondSource"].getDynamicObject()->setProperty("sketch",elevated);r=create(s,"ruled",square,ruled);check((bool)r["ok"]&&closeEnough((double)r["topology"]["area_mm2"],600),"Ruled closed wires create side faces without caps",r["error"].toString());
    auto refs=json(R"({"action":"references"})");refs.getDynamicObject()->setProperty("target",plane);const auto edges=s->modelingCommand("surface",refs);auto fromEdges=json(R"({"source":{"edges":[]}})");fromEdges["source"].getDynamicObject()->setProperty("feature",plane);if(const auto* values=edges["edges"].getArray())for(const auto& e:*values)fromEdges["source"]["edges"].getArray()->add(e["reference"]);r=create(s,"planar",{},fromEdges);
    check((bool)r["ok"]&&closeEnough((double)r["topology"]["area_mm2"],100),"Planar source accepts persistent model-edge wire",r["error"].toString());
    check(!s->remove(upper,error),"Referenced ruled curve cannot be deleted");
    check(s->setVisible(plane,false,error)&&!s->document().find(plane)->visible&&s->setVisible(plane,true,error),"Independent surface visibility uses normal document control",error);
    for(const auto& id:juce::StringArray{disc,extruded,revolved,ruledId})
    {
        const auto definition=command(s,"get",id);rev=s->document().revision();r=command(s,"preview",id,definition["parameters"]);
        check((bool)r["ok"]&&(bool)r["preview"]&&!(bool)r["committed"]&&s->document().revision()==rev,"Isolated preview: "+definition["parameters"]["method"].toString(),r["error"].toString());
        check((bool)command(s,"delete",id)["ok"]&&s->document().find(id)==nullptr&&s->undo(error)&&s->document().find(id)&&s->redo(error)&&s->undo(error),"Deletion and Undo/Redo: "+definition["parameters"]["method"].toString(),error);
    }
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/surface-creation-check");folder.createDirectory();
    for(const auto* ext:{"djo","djc"})
    {
        CadCommandService back;const auto file=folder.getChildFile(juce::String("creation.")+ext);bool same=s->save(file,error)&&back.load(file,error);
        for(const auto& id:juce::StringArray{plane,extruded,revolved,ruledId})
        {const auto before=command(s,"get",id);auto q=json(R"({"action":"get"})");q.getDynamicObject()->setProperty("id",id);const auto after=back.modelingCommand("surface",q);same=same&&(bool)after["ok"]&&juce::JSON::toString(before["parameters"],true,17)==juce::JSON::toString(after["parameters"],true,17)&&closeEnough((double)before["topology"]["area_mm2"],(double)after["topology"]["area_mm2"]);}
        check(same,juce::String("All four definitions/identities/geometry survive ")+ext+" save/reopen",error);
    }
    {
        CadWorkspacePanel panel(s);panel.setSize(1400,900);auto choose=json(R"({"action":"select"})");choose.getDynamicObject()->setProperty("id",revolved);panel.automation(choose);
        auto reply=panel.automation(json(R"({"action":"click","button":"modelingEdit"})"));
        check((bool)reply["ok"],"Property editor accepts API axis references with omitted optional vectors",reply["error"].toString());
        panel.automation(json(R"({"action":"click","button":"cancel"})"));
    }
    // Real form callbacks and selection callbacks, without claiming native pointer interaction.
    for(const auto& method:juce::StringArray{"planar","extrude","revolve","ruled"})
    {
        progress.appendText("GUI "+method+" setup\n");
        auto ui=std::make_shared<CadCommandService>();juce::String curveId;
        const auto source=method=="planar"?rectangle(ui,0):line(ui,method=="revolve"?"XZ":"XY",0,method=="revolve"?3:0,0,method=="revolve"?3:10,method=="revolve"?10:0,curveId);
        const auto second=line(ui,method=="revolve"?"XZ":"XY",method=="revolve"?0:10,0,0,method=="revolve"?0:10,method=="revolve"?10:0,entity);const auto axisLine=entity;
        auto panel=std::make_unique<CadWorkspacePanel>(ui);panel->setSize(1400,900);auto selection=std::make_shared<CadSelection>();panel->setSelection(selection);
        const auto call=[&](const juce::String& text){progress.appendText(text+"\n");return panel->automation(json(text));};
        const auto select=[&](const juce::String& id,const juce::String& e=juce::String()){progress.appendText("select "+id+" "+e+"\n");auto a=json(R"({"action":"select"})");a.getDynamicObject()->setProperty("id",id);if(e.isNotEmpty())a.getDynamicObject()->setProperty("entity",e);return panel->automation(a);};
        const auto alias=method=="planar"?"surfacePlanar":method=="extrude"?"surfaceExtrude":method=="revolve"?"surfaceRevolve":"surfaceRuled";
        auto a=json(R"({"action":"command"})");a.getDynamicObject()->setProperty("command",alias);r=panel->automation(a);select(source,curveId);
        if(method=="ruled"){call(R"({"action":"click","button":"surfSecondSource"})");select(second);}
        if(method=="revolve"){call(R"({"action":"click","button":"surfAxisLine"})");select(second,axisLine);}
        rev=ui->document().revision();r=call(R"({"action":"click","button":"modelingPreview"})");check((bool)r["ok"]&&(bool)r["state"]["modelingPreview"]&&ui->document().revision()==rev,method+" GUI source/axis callbacks and isolated Preview",juce::JSON::toString(r["state"]["message"]));
        r=call(R"({"action":"click","button":"apply"})");juce::String id;for(const auto& f:ui->document().features())if(f.type=="surface")id=f.id;
        check((bool)r["ok"]&&id.isNotEmpty(),method+" GUI Apply creates persistent feature",r["error"].toString());
        if(id.isEmpty())continue;
        select(id);call(R"({"action":"click","button":"modelingEdit"})");
        if(method=="extrude")call(R"({"action":"set","field":"adv_distance","value":12})");
        else if(method=="revolve")call(R"({"action":"set","field":"adv_angleDegrees","value":180})");
        else call(R"({"action":"set","field":"adv_flipNormals","value":true})");
        r=call(R"({"action":"click","button":"apply"})");const auto* f=ui->document().find(id);
        check((bool)r["ok"]&&f&&(method=="extrude"?closeEnough((double)f->modeling["distance"],12):method=="revolve"?closeEnough((double)f->modeling["angleDegrees"],180):(bool)f->modeling["flipNormals"]),method+" normal property edit keeps identity",r["error"].toString());
        call(R"({"action":"click","button":"modelingEdit"})");rev=ui->document().revision();call(R"({"action":"click","button":"modelingPreview"})");r=call(R"({"action":"click","button":"cancel"})");check(!(bool)r["state"]["modelingPreview"]&&ui->document().revision()==rev,method+" GUI Cancel discards draft");
    }
    auto* out=new juce::DynamicObject();out->setProperty("ok",failed==0);out->setProperty("passed",checks.size()-failed);out->setProperty("failed",failed);out->setProperty("checks",checks);report=out;return failed==0;
}
}
