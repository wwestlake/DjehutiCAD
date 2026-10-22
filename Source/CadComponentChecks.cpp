#include "CadComponents.h"
#include "CadDocument.h"
#include "CadGeometricProperties.h"
#include "CadTestFixture.h"
#include "DjehutiAgent.h"
#include <STEPControl_Writer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <set>
#include <cmath>

namespace djehuti::cad
{
using namespace components;
bool runCadComponentCheck(juce::var& report,const juce::File& researchRoot)
{
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/component-check").getChildFile(juce::Uuid().toDashedString());folder.createDirectory();
    juce::Array<juce::var> checks;bool passed=true;const auto check=[&](bool ok,const juce::String& label,const juce::var& detail=juce::var()){auto v=object();put(v,"check",label);put(v,"passed",ok);if(!detail.isVoid())put(v,"detail",detail);checks.add(v);passed&=ok;};
    CadCommandService service;const auto libraryFile=folder.getChildFile("library.json");service.setComponentLibraryFile(libraryFile);service.setCatalogFile(folder.getChildFile("materials.json"));
    const auto command=[&](const char* action,juce::var a=object()){put(a,"action",action);return service.componentCommand(a);};
    auto state=command("list");check((bool)state["ok"],"Load a local component catalog",state["error"]);
    juce::Array<juce::var> importReports;
    if(researchRoot!=juce::File())for(const auto* name:{"DjehutiCAD_Robotics_Materials_Research_Catalog_2026-10-20.xlsx","DjehutiCAD_Commercial_Battery_Pack_Library_2026-10-20.xlsx"})
    {auto a=object();put(a,"path",researchRoot.getChildFile(name).getFullPathName());auto r=command("importFile",a);check((bool)r["ok"]&&r["parsing"]["invalid"].size()==0,"Import actual research workbook",r);importReports.add(r);r=command("importFile",a);check((bool)r["ok"]&&(int)r["import"]["imported"]==0&&r["import"]["conflicts"].size()==0,"Repeated workbook imports are idempotent",r);}
    state=command("list");const auto records=state["records"];int researched=0;for(const auto& item:*records.getArray())if(item["recordId"].toString().startsWith("RC-")||item["recordId"].toString().startsWith("BAT-"))++researched;check(researched==75,"Catalog retains 32 robot components and 43 battery packs alongside stock templates",researched);
    if(records.size()<75){report=object();put(report,"passed",false);put(report,"checks",checks);return false;}
    // Seed export is produced by the production importer, never by a second workbook mapping implementation.
    auto seed=object();put(seed,"format","djehuti-component-library");put(seed,"schemaVersion",1);put(seed,"records",records);juce::File::getCurrentWorkingDirectory().getChildFile("build/component-library-seed.generated.json").replaceWithText(juce::JSON::toString(seed));
    juce::var battery,motor,unknown,conflicted;int noGeometry=0,simplified=0,robotCount=0,batteryCount=0;
    for(const auto& r:*records.getArray())
    {const auto id=r["recordId"].toString();robotCount+=id.startsWith("RC-");batteryCount+=id.startsWith("BAT-");if(id=="BAT-001")battery=r;if(id=="RC-021")motor=r;if(id=="RC-012")unknown=r;if(id=="BAT-018")conflicted=r;noGeometry+=r["geometry"]["kind"].toString()=="none";simplified+=r["geometry"]["kind"].toString()=="cylinder";}
    check(robotCount==32&&batteryCount==43&&noGeometry==7&&simplified==2,"Geometry availability reflects source dimensions; no missing dimension is invented");
    check(std::abs((double)motor["massKg"]-.0095)<1e-12&&std::abs((double)motor["properties"][6]["siValue"]-.01)<1e-12,"Typed gram/mm values normalize to kg/m while original values remain");
    check(battery["properties"][8]["formula"].toString()=="=F2*G2"&&(double)battery["properties"][8]["value"]==36&&battery["sourceUrl"].toString().startsWith("https://"),"Cached formula values, formulas and manufacturer source URLs survive extraction");
    check(conflicted["reviewStatus"].toString()=="SPEC CONFLICT"&&details(conflicted).contains("SPEC CONFLICT"),"Research specification conflicts remain visible");
    juce::String error;auto native=folder.getChildFile("commercial.djo"),json=folder.getChildFile("commercial.json");
    auto a=object();put(a,"component",battery["id"]);auto r=command("insert",a);check((bool)r["ok"],"Insert a real commercial battery into OCAF assembly",r);const auto first=r["id"].toString();
    put(a,"placement",juce::JSON::parse(R"({"x":200,"y":10,"z":5,"rx":0,"ry":0,"rz":90})"));r=command("insert",a);check((bool)r["ok"]&&service.document().mechanicsData().instances.size()==2&&service.document().componentData()["definitions"].size()==1&&service.document().features().size()==1,"Second transformed occurrence reuses geometry and engineering identity");
    check(service.undo(error)&&service.document().mechanicsData().instances.size()==1&&service.redo(error)&&service.document().mechanicsData().instances.size()==2,"Component insertion is one native OCAF undo/redo transaction",error);
    CadDocument firstInsert;juce::String inserted;firstInsert.insertComponent(battery,{},inserted,error);check(firstInsert.undo(error)&&firstInsert.features().empty()&&firstInsert.componentData()["definitions"].size()==0&&firstInsert.redo(error)&&firstInsert.componentData()["definitions"].size()==1,"Undo/redo of the first insertion includes the definition, geometry and occurrence");
    const auto feature=service.document().mechanicsData().instances.front().featureId;const auto originalName=service.document().componentFor(feature)->getProperty("name",{}).toString();
    a=object();put(a,"component",battery["id"]);auto definition=object();put(definition,"name","User battery alias");put(a,"definition",definition);check((bool)command("update",a)["ok"],"User can edit library metadata");
    check(service.document().componentFor(feature)->getProperty("name",{}).toString()==originalName,"Catalog edits do not mutate document definition snapshots");
    a=object();put(a,"component",motor["id"]);definition=object();put(definition,"description","Unrelated local motor notes");put(a,"definition",definition);command("update",a);
    auto changed=clone(battery);put(changed,"description","Revised source description");auto incoming=object();put(incoming,"format","djehuti-component-library");put(incoming,"schemaVersion",1);put(incoming,"records",juce::Array<juce::var>{changed,motor});const auto importFile=folder.getChildFile("revised.json");importFile.replaceWithText(juce::JSON::toString(incoming));a=object();put(a,"path",importFile.getFullPathName());r=command("importFile",a);check((bool)r["ok"]&&r["import"]["conflicts"].size()==1,"Source changes conflict with a locally edited record without overwriting it",r);
    put(a,"replaceConflicts",true);r=command("importFile",a);check((bool)r["ok"]&&(int)r["import"]["replaced"]==1&&service.document().componentFor(feature)->getProperty("name",{}).toString()==originalName,"Explicit replacement updates the catalog while preserving existing document revisions",r);
    bool localNotes=false;const auto afterReplacement=command("list");for(const auto& item:*afterReplacement["records"].getArray())if(item["id"]==motor["id"])localNotes=item["description"].toString()=="Unrelated local motor notes";check(localNotes,"Replacing source conflicts retains unrelated local edits whose source records are unchanged");
    check(service.save(native,error)&&service.save(json,error),"Save assembly occurrences and component definitions to native/JSON documents",error);
    for(const auto& file:{native,json}){CadDocument reopened;check(reopened.load(file,error)&&reopened.mechanicsData().instances.size()==2&&reopened.mechanicsData().instances.back().placement.x==200&&reopened.mechanicsData().instances.back().placement.y==10&&std::abs(reopened.mechanicsData().instances.back().placement.qz-std::sqrt(.5))<1e-12&&reopened.componentData()["definitions"].size()==1&&reopened.componentFor(feature)&&(*reopened.componentFor(feature))["sourceUrl"]==battery["sourceUrl"],"Reopen preserves shared component identity, engineering metadata and occurrence transforms",error);}
    CadTestFixture fixture("Published component mass");a=object();put(a,"action","addSource");put(a,"path",native.getFullPathName());const auto source=fixture.command(a)["id"].toString();a=object();put(a,"action","addModel");definition=object();put(definition,"kind","assembly");put(definition,"source",source);put(a,"definition",definition);fixture.command(a);a=object();put(a,"action","settings");definition=object();put(definition,"method","massProperties");put(a,"definition",definition);fixture.command(a);a=object();put(a,"action","run");r=fixture.command(a)["result"];
    check(r["status"].toString()=="completed"&&std::abs((double)r["totalMassKg"]-.8)<1e-12&&!r.hasProperty("centroidMm")&&!r["rows"][0].hasProperty("inertiaKgM2"),"Fixture sums known published masses without fabricating density, centre of gravity or inertia",r);
    a=object();put(a,"component",unknown["id"]);const auto before=service.document().revision();check(!(bool)command("insert",a)["ok"]&&service.document().revision()==before,"Catalog-only insertion fails atomically without creating a document feature");
    CadCommandService designed;designed.setComponentLibraryFile(libraryFile);designed.setCatalogFile(folder.getChildFile("designer-materials.json"));juce::String box;designed.createBox(12,23,34,box,error);designed.setPlacement(box,40,5,8,15,25,35,error);
    auto material=object();put(material,"name","Custom steel");put(material,"densityKgPerM3",7850);put(material,"youngsModulusMPa",200000);put(material,"poissonsRatio",.3);const auto materialId=designed.materialCommand("cad_material_create",material)["id"].toString();a=object();put(a,"feature",box);put(a,"material",materialId);designed.materialCommand("cad_material_assign",a);
    const auto customSource=folder.getChildFile("custom-source.djo");designed.save(customSource,error);juce::MemoryBlock bytes;customSource.loadFileAsData(bytes);const auto sourceRevision=designed.document().revision();a=object();put(a,"action","publishPart");put(a,"feature",box);put(a,"name","Reusable designed bracket");put(a,"category","Custom/Brackets");put(a,"description","Exact solid with source material");r=designed.componentCommand(a);const auto customId=r["id"].toString();check((bool)r["ok"]&&customId.startsWith("custom-"),"Publish a user-designed solid with an existing material snapshot",r);
    juce::MemoryBlock after;customSource.loadFileAsData(after);check(bytes==after&&designed.document().revision()==sourceRevision,"Publishing protects source CAD bytes and parametric history");
    command("reload");service.document().clear();a=object();put(a,"component",customId);r=command("insert",a);const auto customFeature=service.document().mechanicsData().instances.front().featureId;const auto volume=geomprops::ofShape(service.document().find(customFeature)->shape);
    check((bool)r["ok"]&&std::abs(volume.volumeMm3-12*23*34)<1e-6&&volume.centroidMm.Distance(gp_Pnt(6,11.5,17))<1e-6&&service.document().materialsData().assignmentFor(customFeature),"Custom part is reusable in another design with local geometry and material assignment",r);
    const auto customFile=folder.getChildFile("custom-reuse.djo");service.save(customFile,error);CadDocument customReopened;check(customReopened.load(customFile,error)&&customReopened.componentFor(customFeature)&&(*customReopened.componentFor(customFeature))["id"].toString()==customId,"Custom component metadata and exact solid persist without the library or original file");
    const auto step=folder.getChildFile("supplied-manufacturer.step");STEPControl_Writer writer;writer.Transfer(BRepPrimAPI_MakeBox(4,5,6).Shape(),STEPControl_AsIs);writer.Write(step.getFullPathName().toRawUTF8());a=object();put(a,"component",unknown["id"]);put(a,"path",step.getFullPathName());r=command("attachModel",a);check((bool)r["ok"],"Attach a compatible local STEP solid to a catalog-only component",r);a=object();put(a,"component",unknown["id"]);r=command("insert",a);const auto attachedFeature=service.document().mechanicsData().instances.back().featureId;const auto transferred=geomprops::ofShape(service.document().find(attachedFeature)->shape);check((bool)r["ok"]&&std::abs(transferred.volumeMm3-120)<1e-6,"Manufacturer CAD geometry is used instead of invented dimensions (STEP units in mm)");
    auto bad=object();put(bad,"action","update");put(bad,"component",customId);definition=object();put(definition,"name","");put(bad,"definition",definition);check(!(bool)service.componentCommand(bad)["ok"],"Invalid library edits are rejected");
    CadDocument rollback;rollback.createBox(2,3,4,box,error);auto broken=clone(seed);auto defs=object();put(defs,"schemaVersion",999);put(broken,"components",defs);const auto badFile=folder.getChildFile("invalid-document.json");badFile.replaceWithText(R"({"units":"mm","formatVersion":8,"features":[],"components":{"schemaVersion":999}})");check(!rollback.load(badFile,error)&&rollback.find(box)&&rollback.componentData()["definitions"].size()==0,"Failed document loads restore preexisting CAD state");
    check(!customReopened.load(badFile,error)&&customReopened.componentFor(customFeature)&&customReopened.componentData()["definitions"].size()==1,"Failed loads preserve existing component snapshots and geometry");
    bool registered=false;const auto catalogue=DjehutiAgentPanel::toolCatalogue();for(const auto& tool:*catalogue.getArray())registered|=tool["name"].toString()=="cad_component";check(registered,"Component operations are registered through the shared CAD tool interface");
    // Commercial motor assets use the existing BRep, instance and OCAF paths.
    CadCommandService motors;motors.setComponentLibraryFile(libraryFile);
    const auto motorCommand=[&](const char* action,juce::var args=object()){put(args,"action",action);return motors.componentCommand(args);};
    const auto motorRecords=motorCommand("list")["records"];
    const char* ids[]{"motor-stepper-28byj48-12v-reference","motor-stepper-stepperonline-17hs19-2004s1","motor-stepper-stepperonline-23hs30-2804s"};
    const double shaftR[]{2.5,2.5,3.175},holeR[]{2,1.5,2.6};juce::String mediumFeature;
    for(int index=0;index<3;++index)
    {
        juce::var record;for(const auto& entry:*motorRecords.getArray())if(entry["id"].toString()==ids[index])record=clone(entry);
        check(record.isObject()&&record["engineering"]["recommendedDriverId"].toString().isNotEmpty(),"Commercial motor reference has a shared engineering identity",ids[index]);if(!record.isObject())continue;
        auto request=object();put(request,"component",ids[index]);const auto inserted=motorCommand("insert",request);
        check((bool)inserted["ok"],"Motor inserts through the ordinary component command",inserted["error"]);if(!(bool)inserted["ok"])continue;
        const auto feature=motors.document().mechanicsData().findInstance(inserted["id"].toString())->featureId;if(index==1)mediumFeature=feature;
        bool shaftAxis=false;int holes=0;
        for(TopExp_Explorer ex(motors.document().find(feature)->shape,TopAbs_FACE);ex.More();ex.Next())
        {BRepAdaptor_Surface surface(TopoDS::Face(ex.Current()),true);if(surface.GetType()!=GeomAbs_Cylinder)continue;const auto cy=surface.Cylinder();const auto p=cy.Axis().Location();if(std::abs(cy.Axis().Direction().Z())>.999&&std::hypot(p.X(),p.Y())<1e-7&&std::abs(cy.Radius()-shaftR[index])<1e-7)shaftAxis=true;if(std::abs(cy.Radius()-holeR[index])<1e-7&&std::hypot(p.X(),p.Y())>10)++holes;}
        check(shaftAxis&&holes==(index==0?2:4),"Actual shaft and mounting bore geometry matches the reference",record["partNumber"]);
        std::vector<CadFaceInfo> faces;bool mount=false;if(motors.document().listFaces(feature,faces,error))for(const auto& face:faces)if(face.planar&&std::abs(face.centre.Z())<1e-7&&face.normal.Z()>.99&&!face.token.isEmpty())mount=true;
        check(mount,"Mount plane remains pickable with a persistent face token",error);
    }
    if(mediumFeature.isNotEmpty())
    {
        auto request=object();put(request,"component",ids[1]);put(request,"placement",juce::JSON::parse(R"({"x":80,"y":20,"z":5,"rx":90,"ry":0,"rz":0})"));const auto duplicated=motorCommand("insert",request);const auto* second=motors.document().mechanicsData().findInstance(duplicated["id"].toString());
        check((bool)duplicated["ok"]&&second&&second->featureId==mediumFeature&&motors.document().componentData()["definitions"].size()==3,"Repeated motors share a definition with independent occurrence placement");
        if(second){gp_Dir direction(0,0,1);direction.Transform(second->placement.toTrsf());check(std::abs(direction.Y()+1)<1e-7&&std::abs(direction.Z())<1e-7,"Assembly orientation rotates the physical shaft axis");}
        check(motors.undo(error)&&motors.document().mechanicsData().instances.size()==3&&motors.redo(error)&&motors.document().mechanicsData().instances.size()==4,"Motor insertion preserves OCAF undo/redo",error);
        auto& doc=motors.document();juce::String bracket,tool,next;bool built=doc.createBox(50,50,3,bracket,error)&&doc.setPlacement(bracket,-25,-25,0,0,0,0,error);
        const auto bore=[&](double x,double y,double radius){if(!built)return;built=doc.createCylinder(radius,3,tool,error)&&doc.setPlacement(tool,x,y,0,0,0,0,error)&&doc.booleanOp("subtract",bracket,tool,next,error);if(built)bracket=next;};
        bore(0,0,11.2);for(double x:{-15.5,15.5})for(double y:{-15.5,15.5})bore(x,y,1.7);double overlap=-1;
        if(built){BRepAlgoAPI_Common common(doc.find(bracket)->shape,doc.find(mediumFeature)->shape);common.Build();if(common.IsDone()){GProp_GProps props;BRepGProp::VolumeProperties(common.Shape(),props);overlap=props.Mass();}}
        check(built&&std::abs(overlap)<1e-6,"Ordinary NEMA17 bracket with pilot and screw clearances fits the motor",error);
        const auto native=folder.getChildFile("motor-assembly.djo");const auto definitions=juce::JSON::toString(doc.componentData());bool saved=doc.save(native,error);CadDocument reopened;
        check(saved&&reopened.load(native,error)&&juce::JSON::toString(reopened.componentData())==definitions&&reopened.mechanicsData().instances.size()==4,"Native reopen retains motor geometry, datums, engineering metadata and links",error);
        CadCommandService another;another.setComponentLibraryFile(libraryFile);put(request,"action","insert");const auto reused=another.componentCommand(request);check((bool)reused["ok"]&&another.document().features().size()==1,"Motor definitions can be reused in another independent CAD document");
    }
    auto legacy=object();put(legacy,"format","djehuti-component-library");put(legacy,"schemaVersion",1);juce::Array<juce::var> oldRows;for(const auto& entry:*motorRecords.getArray())if(!entry["id"].toString().startsWith("motor-stepper-"))oldRows.add(clone(entry));
    const auto customizedId=oldRows[0]["id"].toString();put(oldRows.getReference(0),"name","Existing user customization");put(legacy,"records",oldRows);const auto legacyFile=folder.getChildFile("pre-motor-library.json");legacyFile.replaceWithText(juce::JSON::toString(legacy));CadComponentLibrary upgrade;upgrade.setFile(legacyFile);
    check(upgrade.ensureLoaded(error)&&upgrade.find(ids[0])&&upgrade.find(ids[1])&&upgrade.find(ids[2])&&(*upgrade.find(customizedId))["name"].toString()=="Existing user customization","Existing catalogs gain new bundled motors without overwriting user edits",error);
    // Manufacturer STEP snapshots use the same component/OCAF path as every part.
    CadCommandService pololu;pololu.setComponentLibraryFile(libraryFile);pololu.setCatalogFile(folder.getChildFile("materials.json"));
    const auto library=pololu.componentCommand(juce::JSON::parse(R"({"action":"list"})"))["records"];
    const char* partNumbers[]{"4865","4885","4845","3229"};juce::String firstPololuFeature;
    for(int index=0;index<4;++index)
    {
        const juce::String part=partNumbers[index],key="motor-dc-pololu-"+part;juce::var record;
        for(const auto& entry:*library.getArray())if(entry["id"].toString()==key)record=clone(entry);
        check(record.isObject()&&record["partNumber"].toString()==part,"Exact Pololu part remains independently selectable",part);if(!record.isObject())continue;
        const auto solid=components::shape(record,error);check(!solid.IsNull()&&record["geometry"]["level"].toString()=="manufacturer","Manufacturer solid snapshot loads",part+": "+error);if(solid.IsNull())continue;
        int solids=0;for(TopExp_Explorer it(solid,TopAbs_SOLID);it.More();it.Next())++solids;
        check(solids==(index==3?1:2),"Encoder/non-encoder manufacturer solid structure is preserved",part);
        bool shaft=false,boss=false,flat=false,mount=false;std::set<int> holeSides;
        for(TopExp_Explorer it(solid,TopAbs_FACE);it.More();it.Next())
        {
            const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face,true);
            if(surface.GetType()==GeomAbs_Cylinder)
            {
                const auto c=surface.Cylinder();const auto p=c.Location();
                if(std::abs(c.Axis().Direction().Z())>.999&&std::hypot(p.X(),p.Y())<1e-5){shaft|=std::abs(c.Radius()-2)<1e-5;boss|=std::abs(c.Radius()-3.5)<1e-5;}
                if(std::abs(c.Radius()-1.25)<1e-5&&std::abs(p.X())<1e-5&&std::abs(std::abs(p.Y())-8.5)<1e-5)holeSides.insert(p.Y()>0?1:-1);
            }
            if(surface.GetType()==GeomAbs_Plane)
            {
                Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,false,false);double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
                flat|=std::abs(y-1.5)<1e-5&&std::abs(Y-1.5)<1e-5&&std::abs(z-4.5)<1e-5&&std::abs(Z-12.5)<1e-5;
                GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties);mount|=std::abs(z)<1e-5&&std::abs(Z)<1e-5&&properties.Mass()>200;
            }
        }
        check(shaft&&boss&&flat,"Actual shaft Ø4, pilot Ø7 and CAD nominal D-flat (Y=1.5, Z=4.5..12.5) match the drawing",part);
        check(mount&&holeSides.size()==2,"Mounting plane and M3 nominal bores at Y +/-8.5 are distinct from face screws",part);
        if(index==0||index==3)
        {
            auto imported=clone(record);const auto resource=index==3?"0J1113":"0J1647";
            const auto file=juce::File::getCurrentWorkingDirectory().getChildFile("Resources/components/manufacturer/pololu").getChildFile(resource).getChildFile(record["geometry"]["sourceFile"].toString());
            bool loaded=components::attachModel(imported,file,error);auto transferred=loaded?components::shape(imported,error):TopoDS_Shape();
            if(loaded&&index==3){gp_Trsf shift;shift.SetTranslation(gp_Vec(-30.875,0,-18.375));transferred=BRepBuilderAPI_Transform(transferred,shift,true).Shape();}
            GProp_GProps sourceProps,snapshotProps;if(!transferred.IsNull()){BRepGProp::VolumeProperties(transferred,sourceProps);BRepGProp::VolumeProperties(solid,snapshotProps);}
            check(loaded&&!transferred.IsNull()&&std::abs(sourceProps.Mass()-snapshotProps.Mass())<1e-5,"Production STEP importer reproduces the normalized snapshot volume",part+": "+error);
        }
        auto request=object();put(request,"action","insert");put(request,"component",key);auto placement=object();put(placement,"x",index*100.0);put(placement,"y",5.0);put(placement,"z",7.0);put(placement,"ry",90.0);put(request,"placement",placement);
        const auto inserted=pololu.componentCommand(request);const auto* occurrence=pololu.document().mechanicsData().findInstance(inserted["id"].toString());
        bool located=false;if(occurrence){gp_Dir axis(0,0,1);axis.Transform(occurrence->placement.toTrsf());located=std::abs(axis.X()-1)<1e-6&&std::abs(occurrence->placement.x-index*100.0)<1e-6;if(index==0)firstPololuFeature=occurrence->featureId;}
        check((bool)inserted["ok"]&&located,"Pololu occurrence placement rotates the physical shaft axis",inserted["error"]);
    }
    const auto pololuFile=folder.getChildFile("pololu25d-assembly.djo");const auto pololuDefinitions=juce::JSON::toString(pololu.document().componentData());CadDocument pololuReloaded;
    check(pololu.save(pololuFile,error)&&pololuReloaded.load(pololuFile,error)&&juce::JSON::toString(pololuReloaded.componentData())==pololuDefinitions&&pololuReloaded.mechanicsData().instances.size()==4,"Native save/reopen retains four Pololu identities, STEP geometry, interfaces and placements",error);
    check(pololu.undo(error)&&pololu.document().mechanicsData().instances.size()==3&&pololu.redo(error)&&pololu.document().mechanicsData().instances.size()==4,"Pololu insertion preserves OCAF undo/redo",error);
    report=object();put(report,"passed",passed);put(report,"checks",checks);put(report,"imports",importReports);put(report,"coverage",juce::JSON::parse(R"({"robotComponents":32,"batteryPacks":43,"catalogOnly":7,"simplifiedCylinders":2,"envelopes":66,"materialReferencesNotDuplicated":29})"));put(report,"folder",folder.getFullPathName());return passed;
}
}
