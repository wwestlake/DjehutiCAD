#include "CadDocument.h"
#include "CadWorkspacePanel.h"
#include "NodeCompiler.h"
#include "FrustEngine.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <limits>

extern "C" const char* djehuti_cad_revolve(const char* argsJson);

namespace djehuti::cad
{
namespace
{
double volume(const CadCommandService& s, const juce::String& id)
{
    const auto* f = s.document().find(id); if (f == nullptr || f->shape.IsNull()) return -1;
    GProp_GProps p; BRepGProp::VolumeProperties(f->shape, p); return p.Mass();
}
bool closeVolume(double a, double b) { return std::abs(a-b) < std::max(1e-7, std::abs(b)*1e-8); }
juce::var obj(std::initializer_list<std::pair<juce::Identifier, juce::var>> fields)
{
    auto* o = new juce::DynamicObject(); for (const auto& f : fields) o->setProperty(f.first, f.second); return o;
}
struct Checks
{
    juce::Array<juce::var> rows; int failed = 0;
    void check(bool ok, const juce::String& name, const juce::String& detail = {})
    { rows.add(obj({{"name",name},{"ok",ok},{"detail",detail}})); if (!ok) ++failed; }
    bool finish(juce::var& report) { report = obj({{"ok",failed==0},{"passed",rows.size()-failed},{"failed",failed},{"checks",rows}}); return failed == 0; }
};
}

bool runCadRevolveCheck(juce::var& report)
{
    Checks checks; const auto pi = juce::MathConstants<double>::pi;
    CadCommandService s; juce::String sketch, group, full, partial, error;
    CadRevolveParameters p;
    bool setup = s.createSketch("XY",0,sketch,error) && s.addSketchRectangle(sketch,5,0,5,10,group,error);
    checks.check(setup && s.createRevolve(sketch,p,full,error) && closeVolume(volume(s,full),750*pi), "Full 360 degree annular solid has analytic volume",error);
    if (full.isEmpty()) return checks.finish(report);
    checks.check(BRepCheck_Analyzer(s.document().find(full)->shape).IsValid(),"Full solid passes OCCT geometry validation");
    p.angleDegrees=90;
    checks.check(s.createRevolve(sketch,p,partial,error) && closeVolume(volume(s,partial),187.5*pi),"Partial 90 degree revolve has analytic volume",error);
    p.reverse=true;
    checks.check(s.updateRevolve(partial,sketch,p,error) && closeVolume(volume(s,partial),187.5*pi),"Reverse rotation preserves volume and stable feature id",error);
    Bnd_Box bounds; BRepBndLib::Add(s.document().find(partial)->shape,bounds);
    double x0,y0,z0,x1,y1,z1; bounds.Get(x0,y0,z0,x1,y1,z1);
    checks.check(z0>=-1e-6 && closeVolume(z1,10),"Reverse quarter-turn occupies the opposite side of the sketch plane");
    const auto revision=s.document().revision(); const auto before=volume(s,partial);
    auto invalid=p; invalid.angleDegrees=0;
    checks.check(!s.updateRevolve(partial,sketch,invalid,error)&&s.document().revision()==revision&&closeVolume(volume(s,partial),before),"Zero angle edit rolls back atomically",error);
    invalid=p;invalid.axisType="custom";invalid.direction={0,0,0};
    checks.check(!s.updateRevolve(partial,sketch,invalid,error)&&s.document().revision()==revision,"Zero axis direction is rejected",error);
    invalid=p;invalid.axisType="worldZ";
    checks.check(!s.updateRevolve(partial,sketch,invalid,error)&&s.document().revision()==revision&&closeVolume(volume(s,partial),before),"Out-of-plane axis rejects without altering the valid solid",error);
    invalid=p;invalid.angleDegrees=std::numeric_limits<double>::quiet_NaN();
    checks.check(!s.updateRevolve(partial,sketch,invalid,error),"Nonfinite angle is rejected",error);
    invalid=p;invalid.angleDegrees=361;
    checks.check(!s.updateRevolve(partial,sketch,invalid,error),"Angles over one full turn are rejected",error);

    const auto widthEntity=s.document().findSketch(sketch)->entities.front().id;
    checks.check(s.setSketchEntityDimension(sketch,widthEntity,7,error)&&closeVolume(volume(s,full),1190*pi)&&closeVolume(volume(s,partial),297.5*pi),"Sketch dimension edits recompute full and partial dependents",error);
    checks.check(s.undo(error)&&closeVolume(volume(s,full),750*pi)&&s.redo(error)&&closeVolume(volume(s,full),1190*pi),"Sketch rebuild Undo/Redo restores revolve geometry",error);
    p.angleDegrees=180;
    checks.check(s.updateRevolve(partial,sketch,p,error)&&closeVolume(volume(s,partial),595*pi)&&s.undo(error)&&closeVolume(volume(s,partial),297.5*pi)&&s.redo(error)&&closeVolume(volume(s,partial),595*pi),"Revolve parameter edits have OCAF Undo/Redo",error);
    checks.check(!s.remove(sketch,error),"Deleting a referenced source sketch is refused",error);
    const auto sourceRevision=s.document().revision();
    checks.check(!s.setSketchEntityPosition(sketch,widthEntity,-1,0,error)&&s.document().revision()==sourceRevision&&closeVolume(volume(s,full),1190*pi),"A source edit that crosses the axis restores the valid sketch and all dependents",error);
    std::vector<CadFaceInfo> capFaces; s.listFaces(partial,capFaces,error);
    juce::String cap;
    for(const auto& f:capFaces)if(f.planar&&f.token.startsWith(partial+"/start:"))cap=f.token;
    juce::String capSketch;
    checks.check(cap.isNotEmpty()&&s.createFaceSketch(partial,cap,capSketch,error),"Partial Revolve cap has a persistent face reference usable by face sketching",error);

    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/revolve-check"); folder.createDirectory();
    for (const auto* ext : {"djo","djc"})
    {
        const auto path=folder.getChildFile(juce::String("roundtrip.")+ext); CadCommandService back;
        const bool saved=s.save(path,error)&&back.load(path,error);
        const auto* restored=back.document().find(partial);
        checks.check(saved&&restored&&restored->sketchId==sketch&&restored->revolve.angleDegrees==180&&restored->revolve.reverse&&closeVolume(volume(back,partial),595*pi),juce::String(ext)+" roundtrip preserves parameters, ids, dependencies and geometry",error);
        checks.check(saved&&back.setSketchEntityDimension(sketch,widthEntity,8,error)&&closeVolume(volume(back,full),1440*pi),juce::String(ext)+" reopened feature remains parametric",error);
    }

    CadCommandService api; juce::String apiSketch;
    api.createSketch("XY",0,apiSketch,error); api.addSketchRectangle(apiSketch,0,0,5,10,group,error);
    auto created=api.revolveCommand(obj({{"action","create"},{"parameters",obj({{"sketch",apiSketch}})}}));
    const auto apiId=created["id"].toString();
    checks.check((bool)created["ok"]&&closeVolume(volume(api,apiId),250*pi),"API create returns stable id and complete default parameters");
    auto read=api.revolveCommand(obj({{"action","get"},{"id",apiId}}));
    checks.check((bool)read["ok"]&&read["parameters"]["axis"]["type"].toString()=="sketchV"&&read["parameters"]["sketch"].toString()==apiSketch,"API get exposes structured parameters");
    auto changed=api.revolveCommand(obj({{"action","update"},{"id",apiId},{"parameters",obj({{"angleDegrees",120.0}})}}));
    checks.check((bool)changed["ok"]&&changed["id"].toString()==apiId&&closeVolume(volume(api,apiId),250*pi/3),"API partial update merges parameters through the document command");
    const auto apiRevision=api.document().revision();
    auto rejected=api.revolveCommand(obj({{"action","update"},{"id",apiId},{"parameters",obj({{"angleDegrees","invalid"}})}}));
    checks.check(!(bool)rejected["ok"]&&rejected["code"].toString()=="invalid_arguments"&&api.document().revision()==apiRevision,"API rejects malformed parameters with structured errors");
    checks.check((bool)api.revolveCommand(obj({{"action","delete"},{"id",apiId}}))["ok"]&&api.document().find(apiId)==nullptr&&api.undo(error)&&api.document().find(apiId)!=nullptr&&api.redo(error)&&api.document().find(apiId)==nullptr,"API delete and Undo/Redo share document history",error);

    CadCommandService custom; juce::String customSketch, customId;
    custom.createSketch("XY",0,customSketch,error); custom.addSketchRectangle(customSketch,5,0,5,10,group,error);
    p={};p.axisType="custom";p.origin={5,0,0};p.direction={0,2,0};
    checks.check(custom.createRevolve(customSketch,p,customId,error)&&closeVolume(volume(custom,customId),250*pi),"Custom offset axis, non-unit direction and an axis-touching profile work",error);
    CadCommandService customBack; const auto customFile=folder.getChildFile("custom-axis.djo");
    checks.check(custom.save(customFile,error)&&customBack.load(customFile,error)&&customBack.document().find(customId)->revolve.origin==p.origin&&customBack.document().find(customId)->revolve.direction==p.direction&&closeVolume(volume(customBack,customId),250*pi),"Native persistence preserves custom axis coordinates and direction",error);
    const auto customRevision=custom.document().revision();p.origin={7,0,0};juce::String rejectedId;
    checks.check(!custom.createRevolve(customSketch,p,rejectedId,error)&&rejectedId.isEmpty()&&custom.document().revision()==customRevision,"Axis-crossing profile creation rolls back and publishes no id",error);
    p={};
    for (const auto* plane : {"XY","XZ","YZ"})
    {
        CadCommandService rotated;juce::String profile,torus,entity;
        const bool ok=rotated.createSketch(plane,12,profile,error)&&rotated.addSketchCircle(profile,10,0,2,entity,error)&&rotated.createRevolve(profile,p,torus,error);
        checks.check(ok&&closeVolume(volume(rotated,torus),80*pi*pi),juce::String("Offset ")+plane+" circle revolves into a valid torus",error);
    }
    CadCommandService nested;juce::String nestedSketch,nestedId;
    nested.createSketch("XY",0,nestedSketch,error);nested.addSketchRectangle(nestedSketch,1,0,5,6,group,error);nested.addSketchRectangle(nestedSketch,2,2,1,2,group,error);
    checks.check(nested.createRevolve(nestedSketch,p,nestedId,error)&&closeVolume(volume(nested,nestedId),200*pi),"Nested profiles preserve enclosed holes instead of filling them",error);

    CadCommandService attached;juce::String box,faceSketch,torus,entity;
    const bool attachedOk=attached.createBox(30,30,10,box,error)&&attached.createFaceSketch(box,box+"/top",faceSketch,error)&&attached.addSketchCircle(faceSketch,10,0,2,entity,error)&&attached.createRevolve(faceSketch,p,torus,error);
    checks.check(attachedOk&&attached.setDimensions(box,30,30,20,5,error)&&closeVolume(volume(attached,torus),80*pi*pi),"Face-attached source sketch and its Revolve rebuild after support changes",error);
    if(attachedOk){GProp_GProps props;BRepGProp::VolumeProperties(attached.document().find(torus)->shape,props);checks.check(closeVolume(props.CentreOfMass().Z(),20),"Sketch axis follows the moving support plane");}

    CadCommandService bad;juce::String open,line;
    bad.createSketch("XY",0,open,error);bad.addSketchLine(open,1,0,4,0,line,error);
    const auto badRevision=bad.document().revision();
    checks.check(!bad.createRevolve(open,p,rejectedId,error)&&bad.document().revision()==badRevision&&bad.document().find(open)->visible,"Open profile fails without hiding or corrupting the source",error);
    juce::String bow;bad.createSketch("XY",0,bow,error);
    bad.addSketchLine(bow,1,0,5,4,line,error);bad.addSketchLine(bow,5,4,1,4,line,error);bad.addSketchLine(bow,1,4,5,0,line,error);bad.addSketchLine(bow,5,0,1,0,line,error);
    checks.check(!bad.createRevolve(bow,p,rejectedId,error),"Self-intersecting closed profile is rejected",error);
    const auto node=node_compiler::CompileGraphToSource(R"({"functionName":"revolve_command","params":[{"name":"args","type":"String"}],"output":"result","nodes":[{"id":"result","type":"cad_revolve","inputs":[{"param":"args"}]}]})");
    checks.check(node.ok&&node.source.find("extern fn djehuti_cad_revolve")!=std::string::npos,"CAD Revolve node compiles to the shared typed FRust host command",node.errorMessage);
    auto host=std::make_shared<CadCommandService>();juce::String hostSketch;
    host->createSketch("XY",0,hostSketch,error);host->addSketchRectangle(hostSketch,0,0,5,10,group,error);
    frust_engine::setCadCommandService(host);
    const auto hostArgs=juce::JSON::toString(obj({{"action","create"},{"parameters",obj({{"sketch",hostSketch}})}}));
    const auto hostReply=juce::JSON::parse(djehuti_cad_revolve(hostArgs.toRawUTF8()));
    checks.check((bool)hostReply["ok"]&&closeVolume(volume(*host,hostReply["id"].toString()),250*pi),"FRust host function executes the shared Revolve command");
    frust_engine::setCadCommandService({});
    return checks.finish(report);
}

bool runCadRevolveUiCheck(juce::var& report)
{
    Checks checks; const auto pi=juce::MathConstants<double>::pi;
    auto service=std::make_shared<CadCommandService>();juce::String sketch,group,error;
    service->createSketch("XY",0,sketch,error);service->addSketchRectangle(sketch,5,0,5,10,group,error);
    auto panel=std::make_unique<CadWorkspacePanel>(service);panel->setSize(1400,900);
    const auto ui=[&](const juce::var& args){return panel->automation(args);};
    ui(obj({{"action","select"},{"id",sketch}}));
    auto started=ui(obj({{"action","command"},{"command","revolve"}}));
    checks.check((bool)started["ok"]&&started["state"]["task"].toString()=="Revolve","GUI Revolve command opens the creation task");
    ui(obj({{"action","set"},{"field","axisType"},{"value","World Y"}}));
    ui(obj({{"action","set"},{"field","angleDegrees"},{"value",90.0}}));
    auto applied=ui(obj({{"action","click"},{"button","apply"}}));
    juce::String id;for(const auto& f:service->document().features())if(f.type=="revolve")id=f.id;
    checks.check((bool)applied["ok"]&&!id.isEmpty()&&closeVolume(volume(*service,id),187.5*pi),"GUI profile, axis and angle controls create the quarter-turn solid",juce::JSON::toString(applied["error"]));
    if(id.isEmpty())return checks.finish(report);
    ui(obj({{"action","set"},{"field","angleDegrees"},{"value",180.0}}));
    applied=ui(obj({{"action","click"},{"button","apply"}}));
    checks.check((bool)applied["ok"]&&closeVolume(volume(*service,id),375*pi)&&service->document().find(id)->revolve.axisType=="worldY","GUI body inspector edits existing Revolve parameters");
    ui(obj({{"action","command"},{"command","undo"}}));
    checks.check(closeVolume(volume(*service,id),187.5*pi),"GUI Undo restores the prior angle");
    ui(obj({{"action","command"},{"command","redo"}}));
    checks.check(closeVolume(volume(*service,id),375*pi),"GUI Redo restores the edited angle");
    const auto revision=service->document().revision();
    auto invalid=ui(obj({{"action","set"},{"field","angleDegrees"},{"value",0.0}}));
    checks.check(!(bool)invalid["state"]["canApply"]&&service->document().revision()==revision,"GUI disables Apply for an invalid angle");
    ui(obj({{"action","set"},{"field","angleDegrees"},{"value",180.0}}));
    ui(obj({{"action","set"},{"field","axisType"},{"value","Custom"}}));
    ui(obj({{"action","set"},{"field","axisY"},{"value",0.0}}));
    ui(obj({{"action","set"},{"field","axisX"},{"value",5.0}}));
    ui(obj({{"action","set"},{"field","axisDX"},{"value",0.0}}));
    ui(obj({{"action","set"},{"field","axisDY"},{"value",2.0}}));
    ui(obj({{"action","set"},{"field","axisDZ"},{"value",0.0}}));
    applied=ui(obj({{"action","click"},{"button","apply"}}));
    checks.check((bool)applied["ok"]&&closeVolume(volume(*service,id),125*pi),"GUI custom origin and direction controls update the solid");
    service->save(juce::File::getCurrentWorkingDirectory().getChildFile("build/revolve-check/gui-revolve.djo"),error);
    return checks.finish(report);
}
}
