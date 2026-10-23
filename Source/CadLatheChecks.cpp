#include "CadDocument.h"
#include "CadWorkspacePanel.h"
#include "NodeCompiler.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

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
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/lathe-check");folder.createDirectory();juce::String error;
    const double rim[8][4]={{50,-13,50,13},{50,-13,55,-13},{50,13,55,13},{55,-13,55,-16},{55,-16,47,-16},{47,-16,47,16},{47,16,55,16},{55,16,55,13}};
    const auto buildRim=[&](const std::shared_ptr<CadCommandService>& target,const char* plane,juce::String& sketch)
    {target->createSketch(plane,0,sketch,error);target->rename(sketch,"Wheel Profile for Lathe",error);juce::StringArray ids;for(const auto& segment:rim){juce::String entity;target->addSketchLine(sketch,segment[0],segment[1],segment[2],segment[3],entity,error);ids.add(entity);}return ids;};
    const auto request=[&](const char* action,const juce::String& sketch,const CadRevolveParameters& p,const juce::String& id=juce::String())
    {auto* args=new juce::DynamicObject();args->setProperty("action",action);args->setProperty("id",id);auto params=revolveParametersToJson(p);params.getDynamicObject()->setProperty("sketch",sketch);args->setProperty("parameters",params);return juce::var(args);};
    const auto measure=[](const CadCommandService& target,const juce::String& id){GProp_GProps props;const auto* f=target.document().find(id);if(f&&!f->shape.IsNull())BRepGProp::VolumeProperties(f->shape,props);return props.Mass();};
    auto rimService=std::make_shared<CadCommandService>();juce::String rimSketch;CadRevolveParameters parameters;parameters.axisType="worldZ";parameters.entities=buildRim(rimService,"XZ",rimSketch);
    juce::String openLine;rimService->addSketchLine(rimSketch,75,0,80,0,openLine,error);
    check(rimService->document().latheContours(rimSketch).size()==1,"Contour discovery ignores unrelated open sketch entities");
    const auto previewRevision=rimService->document().revision();auto preview=rimService->latheCommand(request("preview",rimSketch,parameters));
    check((bool)preview["ok"]&&(bool)preview["preview"]&&!(bool)preview["committed"]&&close((double)preview["volumeMm3"],12462*pi)&&rimService->document().features().size()==1&&rimService->document().revision()==previewRevision,"Isolated rim preview uses the shared command without live edits or allocation");
    auto turned=rimService->latheCommand(request("create",rimSketch,parameters));const auto rimId=turned["id"].toString();check((bool)turned["ok"]&&rimId.isNotEmpty()&&rimService->document().features().size()==2,"Create Lathe from the existing rim contour without creating another sketch");
    if(rimId.isNotEmpty())
    {
        const auto* feature=rimService->document().find(rimId);int solids=0;for(TopExp_Explorer e(feature->shape,TopAbs_SOLID);e.More();e.Next())++solids;
        check(BRepCheck_Analyzer(feature->shape,true).IsValid()&&solids==1&&close(measure(*rimService,rimId),12462*pi),"DUB wheel rim is one valid annular solid with analytic volume 12462*pi mm3");
        Bnd_Box bounds;BRepBndLib::AddOptimal(feature->shape,bounds,false,false);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
        check(close(x0,-55)&&close(x1,55)&&close(y0,-55)&&close(y1,55)&&close(z0,-16)&&close(z1,16),"Rim outer flange diameter 110 mm and overall axial width 32 mm");
        int seat=0,inner=0,flanges=0;
        for(TopExp_Explorer ex(feature->shape,TopAbs_FACE);ex.More();ex.Next())
        {BRepAdaptor_Surface surface(TopoDS::Face(ex.Current()),true);if(surface.GetType()!=GeomAbs_Cylinder)continue;Bnd_Box faceBounds;BRepBndLib::AddOptimal(ex.Current(),faceBounds,false,false);double a,b,c,d,e,f;faceBounds.Get(a,b,c,d,e,f);const auto radius=surface.Cylinder().Radius();if(close(radius,50)&&close(f-c,26))++seat;if(close(radius,47)&&close(f-c,32))++inner;if(close(radius,55)&&close(f-c,3))++flanges;}
        check(seat==1&&inner==1&&flanges==2,"Exact cylindrical faces verify seat diameter 100 / width 26, bore diameter 94 and both raised flanges");
        check(rimService->undo(error)&&rimService->document().find(rimId)==nullptr&&rimService->document().findSketch(rimSketch)->entities.size()==9&&rimService->redo(error),"Undo/Redo restores the feature while preserving the original sketch and unrelated line");
        parameters.angleDegrees=180;turned=rimService->latheCommand(request("update",rimSketch,parameters,rimId));check((bool)turned["ok"]&&close(measure(*rimService,rimId),6231*pi)&&BRepCheck_Analyzer(rimService->document().find(rimId)->shape).IsValid(),"Editable 180-degree rim is capped, solid and half the full volume");
        parameters.angleDegrees=90;parameters.reverse=true;turned=rimService->latheCommand(request("update",rimSketch,parameters,rimId));check((bool)turned["ok"]&&close(measure(*rimService,rimId),3115.5*pi),"Reverse 90-degree rotation produces the correct quarter volume");
        parameters.angleDegrees=360;parameters.reverse=false;rimService->latheCommand(request("update",rimSketch,parameters,rimId));
        check(rimService->setSketchLineGeometry(rimSketch,parameters.entities[0],51,-13,51,13,error)&&close(measure(*rimService,rimId),15088*pi),"Editing the authoritative seat line regenerates the lathe through its dependency");
        check(rimService->undo(error)&&close(measure(*rimService,rimId),12462*pi),"Undo of a source sketch edit restores the original rim");
        const auto oldRevision=rimService->document().revision();auto invalid=parameters;invalid.entities.remove(0);check(!(bool)rimService->latheCommand(request("update",rimSketch,invalid,rimId))["ok"]&&rimService->document().revision()==oldRevision&&close(measure(*rimService,rimId),12462*pi),"Open selected contour fails without corrupting the committed rim");
        invalid=parameters;invalid.axisType="worldY";check(!(bool)rimService->latheCommand(request("update",rimSketch,invalid,rimId))["ok"]&&rimService->document().revision()==oldRevision,"An out-of-plane world axis is rejected atomically");
        invalid=parameters;invalid.angleDegrees=0;check(!(bool)rimService->latheCommand(request("update",rimSketch,invalid,rimId))["ok"],"Zero revolution angle is rejected");
        juce::String axisSketch,axisLine;rimService->createSketch("XZ",0,axisSketch,error);rimService->addSketchLine(axisSketch,0,-30,0,30,axisLine,error);parameters.axisType="sketchLine";parameters.axisSketch=axisSketch;parameters.axisEntity=axisLine;
        check((bool)rimService->latheCommand(request("update",rimSketch,parameters,rimId))["ok"]&&close(measure(*rimService,rimId),12462*pi),"Referenced line in another sketch resolves to the correct document Z axis");
        check(rimService->setSketchLineGeometry(axisSketch,axisLine,2,-30,2,30,error)&&close(measure(*rimService,rimId),11958*pi),"Axis-line edits invalidate the dependency cache and regenerate the solid");
        check(!rimService->remove(axisSketch,error),"Deleting a referenced axis sketch is blocked by existing dependencies");
        for(const auto* extension:{"djo","djc"})
        {const auto file=folder.getChildFile(juce::String("dub-wheel-rim.")+extension);CadCommandService reopened;const bool loaded=rimService->save(file,error)&&reopened.load(file,error);const auto get=reopened.latheCommand(juce::JSON::parse("{\"action\":\"get\",\"id\":\""+rimId+"\"}"));check(loaded&&(bool)get["ok"]&&get["parameters"]["entities"].size()==8&&get["parameters"]["axis"]["entity"].toString()==axisLine&&close(measure(reopened,rimId),11958*pi),"Native/JSON retains Lathe identity, selected contour, axis references, angle and evaluated geometry");}
        parameters.axisType="worldZ";parameters.axisSketch.clear();parameters.axisEntity.clear();rimService->latheCommand(request("update",rimSketch,parameters,rimId));
        rimService->save(folder.getChildFile("DUB-Wheel-Rim.djo"),error);
    }
    for(const auto* plane:{"XY","YZ"})
    {auto target=std::make_shared<CadCommandService>();juce::String sketch;auto p=parameters;p.entities=buildRim(target,plane,sketch);p.axisType=juce::String(plane)=="XY"?"worldY":"worldZ";const auto result=target->latheCommand(request("create",sketch,p));check((bool)result["ok"]&&close(measure(*target,result["id"].toString()),12462*pi),"World-axis interpretation preserves the rim in XY/YZ sketches");}
    auto custom=std::make_shared<CadCommandService>();juce::String sketch;auto p=parameters;p.entities=buildRim(custom,"XZ",sketch);juce::String circle;custom->addSketchCircle(sketch,70,0,2,circle,error);
    auto ambiguous=request("create",sketch,p);ambiguous["parameters"].getDynamicObject()->removeProperty("entities");check(!(bool)custom->latheCommand(ambiguous)["ok"]&&custom->document().features().size()==1,"Multiple closed contours require explicit selection");
    check((bool)custom->latheCommand(request("create",sketch,p))["ok"],"Explicit contour selection excludes a separate closed circle in the same sketch");
    auto bad=std::make_shared<CadCommandService>();juce::String bs,id1;bad->createSketch("XZ",0,bs,error);CadRevolveParameters bp;bp.axisType="worldZ";for(const auto& segment:std::vector<std::array<double,4>>{{2,0,4,2},{4,2,2,2},{2,2,4,0},{4,0,2,0}}){bad->addSketchLine(bs,segment[0],segment[1],segment[2],segment[3],id1,error);bp.entities.add(id1);}const auto badRevision=bad->document().revision();check(!(bool)bad->latheCommand(request("create",bs,bp))["ok"]&&bad->document().revision()==badRevision,"Self-intersecting closed contour fails without committing");

    auto guiService=std::make_shared<CadCommandService>();juce::String guiSketch;const auto guiEntities=buildRim(guiService,"XZ",guiSketch);auto panel=std::make_unique<CadWorkspacePanel>(guiService);panel->setSize(1400,900);
    panel->automation(json("{\"action\":\"select\",\"id\":\""+guiSketch+"\"}"));
    auto state=panel->automation(json(R"({"action":"command","command":"lathe"})"));check((bool)state["ok"]&&state["state"]["task"].toString()=="Virtual Lathe","GUI opens Virtual Lathe with the selected source sketch");
    bool table=false,profile=false,contour=false,axis=false;for(const auto& field:*state["state"]["fields"].getArray()){const auto key=field["id"].toString();table|=key=="latheOuter"||key=="latheInner";profile|=key=="latheProfile";contour|=key=="latheContour";axis|=key=="latheAxis";}
    check(!table&&profile&&contour&&axis,"GUI exposes sketch/contour/axis selectors and removes outside/bore station fields");
    panel->featureClicked(guiSketch);check(panel->task==CadWorkspacePanel::Task::lathe&&panel->profileId==guiSketch,"Model-tree selection assigns a sketch without leaving the Lathe task");
    panel->sketchEntityClicked(guiSketch,guiEntities[3],false);check(panel->task==CadWorkspacePanel::Task::lathe&&panel->latheSettings.entities.size()==8,"Tree entity selection resolves its closed contour without entering Sketch mode");
    panel->modelView.controlCamera(json(R"({"yawDeg":0,"pitchDeg":0,"distance":180})"),error);
    CadPick hit;for(int y=5;y<panel->modelView.getHeight()&&!hit.valid();y+=10)for(int x=5;x<panel->modelView.getWidth();x+=10){auto candidate=panel->modelView.pick({(float)x,(float)y},CadSelection::Mode::body);if(candidate.featureId==guiSketch){hit=candidate;break;}}
    check(hit.valid()&&hit.hasPoint,"Native viewport picking resolves displayed sketch outline geometry");if(hit.valid())panel->pickLatheProfile(hit);
    check(panel->profileId==guiSketch&&panel->latheSettings.entities.size()==8,"Viewport pick callback resolves the selected contour");
    const auto guiRevision=guiService->document().revision();state=panel->automation(json(R"({"action":"click","button":"lathePreview"})"));
    check((bool)state["ok"]&&panel->latheViewActive&&panel->lathePreviewWanted&&guiService->document().revision()==guiRevision,"GUI Preview shows candidate and document-axis overlay without editing the document");
    {const auto capture=panel->createComponentSnapshot(panel->getLocalBounds());if(auto stream=folder.getChildFile("lathe-controls.png").createOutputStream())juce::PNGImageFormat().writeImageToStream(capture,*stream);}
    state=panel->automation(json(R"({"action":"set","field":"latheAngle","value":180})"));check((bool)state["ok"]&&panel->lathePreviewWanted&&guiService->document().revision()==guiRevision,"Angle edits refresh the active isolated preview");
    panel->automation(json(R"({"action":"click","button":"cancel"})"));check(guiService->document().revision()==guiRevision&&guiService->document().features().size()==1&&!panel->latheViewActive,"Cancel restores the view and leaves source document and Undo unchanged");
    panel->automation(json(R"({"action":"command","command":"lathe"})"));state=panel->automation(json(R"({"action":"click","button":"apply"})"));juce::String guiId;for(const auto& f:guiService->document().features())if(f.lathe)guiId=f.id;
    check((bool)state["ok"]&&!guiId.isEmpty()&&close(measure(*guiService,guiId),12462*pi),"GUI Turn part commits the exact DUB rim using the same modeling command");
    state=panel->automation(json(R"({"action":"click","button":"apply"})"));check((bool)state["ok"]&&guiService->document().find(guiId)!=nullptr&&guiService->document().find(guiId)->revolve.entities.size()==8,"Body property Apply preserves Lathe contour and axis references");
    if(guiId.isNotEmpty())
    {panel->automation(json(R"({"action":"click","button":"latheProfile"})"));panel->automation(json(R"({"action":"set","field":"latheAngle","value":90})"));state=panel->automation(json(R"({"action":"click","button":"apply"})"));check((bool)state["ok"]&&close(measure(*guiService,guiId),3115.5*pi),"Existing Lathe GUI edits the same feature with a partial revolution");}
    const auto node=node_compiler::CompileGraphToSource(R"({"functionName":"turn","params":[{"name":"args","type":"String"}],"output":"r","nodes":[{"id":"r","type":"cad_lathe","inputs":[{"param":"args"}]}]})");
    check(node.ok&&node.source.find("djehuti_cad_lathe")!=std::string::npos,"Lathe node generates the shared FRust host call");
    auto* out=new juce::DynamicObject();out->setProperty("ok",failed==0);out->setProperty("passed",rows.size()-failed);out->setProperty("failed",failed);out->setProperty("checks",rows);report=out;return failed==0;
}
}
