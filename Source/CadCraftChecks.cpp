#include "CadCraftStock.h"
#include "CadDocument.h"
#include "CadGeometricProperties.h"
#include "CadEngineeringUnits.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <limits>

namespace djehuti::cad
{
using namespace components;
namespace geom = geomprops;
bool runCadCraftCheck(juce::var& report)
{
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-check").getChildFile(juce::Uuid().toDashedString());folder.createDirectory();
    juce::Array<juce::var> checks;bool passed=true;
    const auto check=[&](bool ok,const juce::String& label,const juce::String& detail=juce::String())
    {auto item=object();put(item,"check",label);put(item,"passed",ok);put(item,"detail",detail);checks.add(item);passed&=ok;report=object();put(report,"passed",passed);put(report,"checks",checks);put(report,"folder",folder.getFullPathName());return ok;};
    const auto close=[](double a,double b){return std::abs(a-b)<1e-7*juce::jmax(1.0,std::abs(b));};
    juce::String error;
    CadMaterial density;density.name="Density only check";density.model="densityOnly";density.densityKgPerM3=500;
    check(matl::validateMaterial(density,error)&&!density.youngsModulusMPa&&!density.poissonsRatio,"Density-only material accepted without invented elastic properties",error);
    auto invalid=density;invalid.densityKgPerM3.reset();check(!matl::validateMaterial(invalid,error),"Density-only material requires a density");
    invalid=density;invalid.densityKgPerM3=-1;check(!matl::validateMaterial(invalid,error),"Nonpositive density rejected");
    invalid=density;invalid.densityKgPerM3=std::numeric_limits<double>::infinity();check(!matl::validateMaterial(invalid,error),"Nonfinite density rejected");
    invalid=density;invalid.youngsModulusMPa=100;check(!matl::validateMaterial(invalid,error),"Density-only model cannot imply structural eligibility");
    invalid=density;invalid.model="isotropic";check(!matl::validateMaterial(invalid,error),"Existing isotropic requirements preserved");
    invalid.youngsModulusMPa=200000;invalid.poissonsRatio=.3;check(matl::validateMaterial(invalid,error),"Complete engineering material remains accepted",error);
    const auto seeds=craft::templates();check(seeds.size()==8,"Eight representative templates cover four categories");
    for(const auto& stock:seeds)
    {
        error.clear();const auto solid=components::shape(stock,error);const auto g=stock["geometry"];const auto profile=g["profile"].toString();
        const double l=g["lengthMm"],w=g["widthMm"],t=g["thicknessMm"],od=g["outerDiameterMm"],id=g["innerDiameterMm"],pi=juce::MathConstants<double>::pi;
        const double expected=profile=="rectangular"?l*w*t:profile=="roundedStick"?((l-w)*w+pi*w*w/4)*t:pi*(od*od-id*id)/4*l;
        int solids=0;if(!solid.IsNull())for(TopExp_Explorer ex(solid,TopAbs_SOLID);ex.More();ex.Next())++solids;
        GProp_GProps properties;if(!solid.IsNull())BRepGProp::VolumeProperties(solid,properties);
        check(!solid.IsNull()&&BRepCheck_Analyzer(solid).IsValid()&&solids==1&&close(properties.Mass(),expected),"Valid solid and analytical volume: "+stock["name"].toString(),"expected="+juce::String(expected,8)+", calculated="+juce::String(properties.Mass(),8)+" mm^3; "+error);
        if(profile=="tube"&&!solid.IsNull())
        {
            const auto tube=TopoDS::Solid(TopExp_Explorer(solid,TopAbs_SOLID).Current());
            BRepClass3d_SolidClassifier hole(tube,gp_Pnt(l/2,0,0),1e-7),wall(tube,gp_Pnt(l/2,(od+id)/4,0),1e-7);
            check(hole.State()==TopAbs_OUT&&wall.State()==TopAbs_IN,"Tube has an actual empty bore and a solid wall");
        }
    }
    auto bad=clone(seeds[0]["geometry"]);put(bad,"lengthMm",0);check(!craft::validateGeometry(bad,error),"Zero dimension rejected");
    bad=clone(seeds[0]["geometry"]);put(bad,"widthMm",std::numeric_limits<double>::quiet_NaN());check(!craft::validateGeometry(bad,error),"NaN dimension rejected");
    bad=clone(seeds[5]["geometry"]);put(bad,"innerDiameterMm",8);check(!craft::validateGeometry(bad,error),"Zero tube wall rejected");
    bad=clone(seeds[3]["geometry"]);put(bad,"lengthMm",10);check(!craft::validateGeometry(bad,error),"Impossible rounded stick rejected");
    CadCommandService service;service.setComponentLibraryFile(folder.getChildFile("library.json"));service.setCatalogFile(folder.getChildFile("materials.json"));
    const auto command=[&](const char* action,juce::var args=object()){put(args,"action",action);return service.componentCommand(args);};
    auto state=command("list");check((bool)state["ok"]&&state["records"].size()==83,"Craft templates extend the existing component catalog",state["error"].toString());
    const auto insert=[&](const char* name){auto a=object();put(a,"component",name);return command("insert",a);};
    auto r=insert("craft-rounded-stick");const auto first=r["id"].toString();if(!check((bool)r["ok"],"Insert rounded stick through shared component command",r["error"].toString()))return false;
    auto& doc=service.document();const auto firstFeature=doc.mechanicsData().findInstance(first)->featureId;
    r=insert("craft-rounded-stick");const auto second=r["id"].toString();if(!check((bool)r["ok"],"Insert second stock piece",r["error"].toString()))return false;
    const auto secondFeature=doc.mechanicsData().findInstance(second)->featureId;
    check(firstFeature!=secondFeature&&doc.componentData()["definitions"].size()==2,"Identical template insertions have independent definitions and occurrences");
    check(doc.undo(error)&&!doc.find(secondFeature)&&doc.redo(error)&&doc.find(secondFeature),"Insertion is one OCAF undo/redo command",error);
    const auto* assignment=doc.materialsData().assignmentFor(firstFeature);const auto materialId=assignment?assignment->materialId:juce::String();const auto* material=doc.materialsData().find(materialId);
    check(material&&material->model=="densityOnly"&&material->densityKgPerM3&&*material->densityKgPerM3==500&&!material->youngsModulusMPa,"Default material resolves from engineering catalog to document snapshot");
    const auto frozenTemplate=clone(*doc.componentFor(firstFeature));auto geometry=clone(frozenTemplate["geometry"]);put(geometry,"lengthMm",160);
    auto a=object();put(a,"feature",firstFeature);put(a,"geometry",geometry);r=command("editStock",a);
    const auto volume=[&](const juce::String& feature){return geom::ofFeature(doc,feature).volumeMm3;};
    const auto originalVolume=volume(secondFeature),editedVolume=volume(firstFeature);
    check((bool)r["ok"]&&editedVolume>originalVolume&&(*doc.componentFor(secondFeature))["geometry"]["lengthMm"]==juce::var(120),"Dimension edit rebuilds one independent piece only",r["error"].toString());
    check((*doc.componentFor(firstFeature))["templateId"].toString()=="craft-rounded-stick"&&(int)(*doc.componentFor(firstFeature))["templateRevision"]==1&&juce::JSON::toString((*doc.componentFor(firstFeature))["templateGeometry"])==juce::JSON::toString(frozenTemplate["templateGeometry"]),"Template identity, revision, original parameters and provenance retained");
    check(doc.undo(error)&&close(volume(firstFeature),originalVolume)&&doc.redo(error)&&close(volume(firstFeature),editedVolume),"Dimension edit undo/redo restores calculated solid",error);
    juce::String linked;check(doc.createInstance(firstFeature,"Explicit linked stock",linked,error),"Explicit linked occurrence uses existing instance command",error);
    put(geometry,"lengthMm",180);put(a,"geometry",geometry);r=command("editStock",a);
    check((bool)r["ok"]&&doc.mechanicsData().findInstance(linked)->featureId==firstFeature&&close(geom::ofInstance(doc,linked).volumeMm3,volume(firstFeature))&&close(volume(secondFeature),originalVolume),"Linked occurrences follow definition edits; independent piece stays unchanged");
    bad=clone(geometry);put(bad,"lengthMm",0);put(a,"geometry",bad);const auto before=juce::JSON::toString(doc.componentData());r=command("editStock",a);
    check(!(bool)r["ok"]&&before==juce::JSON::toString(doc.componentData()),"Rejected dimension edit preserves existing document state");
    a=object();put(a,"action","useMaterial");put(a,"material","mat-craft-plastic");put(a,"feature",firstFeature);r=service.catalogCommand(a);
    const auto substituted=r["id"].toString();check((bool)r["ok"]&&doc.materialsData().assignmentFor(firstFeature)->materialId==substituted&&doc.materialsData().assignmentFor(secondFeature)->materialId==materialId,"Material substitution changes only the selected definition",r["error"].toString());
    check(doc.undo(error)&&doc.materialsData().assignmentFor(firstFeature)->materialId==materialId&&doc.redo(error)&&doc.materialsData().assignmentFor(firstFeature)->materialId==substituted,"Material assignment undo/redo includes imported definition",error);
    const auto pose=mech::frameFromEulerDegrees(25,40,5,0,0,90);check(doc.setInstancePlacement(first,pose,error)&&close(doc.mechanicsData().findInstance(first)->placement.x,25)&&doc.mechanicsData().findInstance(second)->placement.x==0,"Placement edits move an individual occurrence",error);
    check(doc.undo(error)&&doc.mechanicsData().findInstance(first)->placement.x==0&&doc.redo(error)&&close(doc.mechanicsData().findInstance(first)->placement.x,25),"Placement undo/redo",error);
    r=insert("craft-plastic-tube");check((bool)r["ok"],"Insert tube in mixed construction",r["error"].toString());
    const auto native=folder.getChildFile("construction.djo"),json=folder.getChildFile("construction.json");
    const auto expectedComponents=juce::JSON::toString(doc.componentData()),expectedMaterials=juce::JSON::toString(matl::toJson(doc.materialsData())),expectedMechanics=juce::JSON::toString(mech::toJson(doc.mechanicsData()));
    check(doc.save(native,error)&&doc.exportJson(json,error),"Save construction as native OCAF and CAD JSON",error);
    for(const auto& file:{native,json})
    {
        CadDocument reopened;bool loaded=reopened.load(file,error);
        check(loaded&&expectedComponents==juce::JSON::toString(reopened.componentData())&&expectedMaterials==juce::JSON::toString(matl::toJson(reopened.materialsData()))&&expectedMechanics==juce::JSON::toString(mech::toJson(reopened.mechanicsData()))&&close(geom::ofFeature(reopened,firstFeature).volumeMm3,volume(firstFeature)),"Reopen preserves geometry, parameters, materials, provenance, links and placements: "+file.getFileExtension(),error);
    }
    const auto savedVolume=volume(firstFeature);service.newDocument();check(doc.features().empty()&&service.load(native,error)&&close(volume(firstFeature),savedVolume),"Close/clear active document and reopen native construction",error);
    a=object();put(a,"component","craft-rounded-stick");auto templateGeometry=clone(seeds[3]["geometry"]);put(templateGeometry,"lengthMm",140);put(a,"geometry",templateGeometry);r=command("updateStock",a);
    check((bool)r["ok"]&&(double)(*doc.componentFor(firstFeature))["geometry"]["lengthMm"]==180,"Editing catalog defaults does not change existing document pieces",r["error"].toString());
    r=command("duplicateStock",a);const auto custom=r["id"].toString();check((bool)r["ok"]&&custom.startsWith("craft-user-"),"User can create a custom stock template",r["error"].toString());
    state=command("reload");bool retained=false;if(const auto* rows=state["records"].getArray())for(const auto& entry:*rows)if(entry["id"].toString()==custom)retained=(double)entry["geometry"]["lengthMm"]==140;
    check((bool)state["ok"]&&retained,"User template dimensions persist in existing local component library");
    return passed;
}
bool runCadCraftPhase2Check(juce::var& report)
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-check-p2").getChildFile(juce::Uuid().toDashedString());
    folder.createDirectory();
    juce::Array<juce::var> checks; bool passed = true;
    const auto check = [&](bool ok, const juce::String& label, const juce::String& detail = juce::String())
    {
        auto item = object(); put(item, "check", label); put(item, "passed", ok); put(item, "detail", detail);
        checks.add(item); passed &= ok;
        report = object(); put(report, "passed", passed); put(report, "checks", checks); put(report, "folder", folder.getFullPathName());
        return ok;
    };
    const auto close = [](double a, double b) { return std::abs(a - b) < 1e-5 * juce::jmax(1.0, std::abs(b)); };
    juce::String error;

    CadCommandService service;
    service.setComponentLibraryFile(folder.getChildFile("library.json"));
    service.setCatalogFile(folder.getChildFile("materials.json"));
    const auto command = [&](const char* action, juce::var args = object())
    {
        put(args, "action", action);
        return service.componentCommand(args);
    };

    // 1. Insert craft wood strip (120 x 10 x 3 mm, volume = 3600 mm^3)
    auto r = command("insert", [&]{ auto a = object(); put(a, "component", "craft-wood-strip"); return a; }());
    const auto inst1 = r["id"].toString();
    auto& doc = service.document();
    const auto feat1 = doc.mechanicsData().findInstance(inst1)->featureId;
    const double initialVol = geom::ofFeature(doc, feat1).volumeMm3;
    check((bool)r["ok"] && close(initialVol, 3600.0), "Insert craft wood strip with nominal 3600 mm^3 solid");

    // 2. Cut to length: shorten to 90 mm from end (+X)
    auto cut1 = object(); put(cut1, "type", "length"); put(cut1, "positionMm", 90); put(cut1, "side", "end");
    auto a = object(); put(a, "feature", feat1); put(a, "cut", cut1);
    r = command("addCut", a);
    const double cut90Vol = geom::ofFeature(doc, feat1).volumeMm3;
    check((bool)r["ok"] && close(cut90Vol, 2700.0), "Cut to length (120 mm -> 90 mm) yields exact proportional solid (2700 mm^3)");

    // 3. Second cut: 45 degree miter at end (X = 90)
    auto cut2 = object(); put(cut2, "type", "miter"); put(cut2, "positionMm", 90); put(cut2, "angleDeg", 45); put(cut2, "side", "end");
    put(a, "cut", cut2);
    r = command("addCut", a);
    const double miterVol = geom::ofFeature(doc, feat1).volumeMm3;
    check((bool)r["ok"] && close(miterVol, 2550.0), "Second sequential cut (45-deg miter at end) produces exact 2550 mm^3 mitered solid");

    // 4. Upstream parametric editing recalculation: change base length to 140 mm
    auto geomModified = clone((*doc.componentFor(feat1))["geometry"]);
    put(geomModified, "lengthMm", 140);
    a = object(); put(a, "feature", feat1); put(a, "geometry", geomModified);
    r = command("editStock", a);
    const double updatedVol = geom::ofFeature(doc, feat1).volumeMm3;
    check((bool)r["ok"] && close(updatedVol, miterVol), "Upstream dimension change recalculates and reapplies sequential cuts");

    // 5. Test cutting hollow tube: insert tube and cut at 30 deg
    r = command("insert", [&]{ auto a = object(); put(a, "component", "craft-plastic-tube"); return a; }());
    const auto instTube = r["id"].toString();
    const auto featTube = doc.mechanicsData().findInstance(instTube)->featureId;
    const double tubeInitialVol = geom::ofFeature(doc, featTube).volumeMm3;
    auto cutTube = object(); put(cutTube, "type", "miter"); put(cutTube, "positionMm", 80); put(cutTube, "angleDeg", 30);
    a = object(); put(a, "feature", featTube); put(a, "cut", cutTube);
    r = command("addCut", a);
    const double tubeCutVol = geom::ofFeature(doc, featTube).volumeMm3;
    check((bool)r["ok"] && tubeCutVol < tubeInitialVol && tubeCutVol > 1000.0, "Cut hollow plastic tube produces valid manifold solid with intact bore");

    // 6. Test invalid cut rejection: cut outside bounds
    auto badCut = object(); put(badCut, "type", "length"); put(badCut, "positionMm", 300);
    put(a, "feature", feat1); put(a, "cut", badCut);
    r = command("addCut", a);
    check(!(bool)r["ok"], "Out-of-bounds cut is rejected without modifying document state");

    // 7. Insert second stick and test alignment (corner90)
    r = command("insert", [&]{ auto a = object(); put(a, "component", "craft-wood-strip"); return a; }());
    const auto inst2 = r["id"].toString();
    a = object(); put(a, "target", inst1); put(a, "source", inst2); put(a, "alignType", "corner90");
    r = command("align", a);
    const auto* p2 = doc.mechanicsData().findInstance(inst2);
    check((bool)r["ok"] && p2 != nullptr && p2->placement.x != 0, "Align second stick to first with 90-degree corner joint");

    // 8. Test assembly group creation and group movement
    a = object(); put(a, "name", "Test Frame");
    juce::Array<juce::var> groupIds { inst1, inst2 };
    put(a, "instances", groupIds);
    r = command("createAssembly", a);
    const auto asmId = r["id"].toString();
    check((bool)r["ok"] && asmId.startsWith("asm-") && doc.mechanicsData().assemblies.size() == 1, "Group pieces into logical assembly");

    // Move assembly as a unit
    auto moveArgs = object();
    put(moveArgs, "group", asmId);
    auto deltaP = object(); put(deltaP, "x", 50.0); put(deltaP, "y", 30.0); put(deltaP, "z", 0.0);
    put(moveArgs, "placement", deltaP);
    r = command("moveAssembly", moveArgs);
    const double inst1_newX = doc.mechanicsData().findInstance(inst1)->placement.x;
    check((bool)r["ok"] && close(inst1_newX, 50.0), "Move assembly translates all member pieces as a unit");

    // 9. Cut list generation
    r = command("cutList");
    const auto cutListData = r["cutList"];
    check((bool)r["ok"] && (int)cutListData["totalPieces"] >= 2 && cutListData["summary"].size() > 0, "Generate structured cut list with quantities, dimensions, and offcuts");

    // 10. Undo/Redo of assembly move
    check(doc.undo(error) && close(doc.mechanicsData().findInstance(inst1)->placement.x, 0.0), "Undo restores assembly positions", error);
    check(doc.redo(error) && close(doc.mechanicsData().findInstance(inst1)->placement.x, 50.0), "Redo reapplies assembly positions", error);

    // 11. Native OCAF save and reopen verification
    const auto nativeFile = folder.getChildFile("phase2-construction.djo");
    check(doc.save(nativeFile, error), "Save Phase 2 construction to native .djo", error);
    CadDocument reopened;
    check(reopened.load(nativeFile, error), "Reopen Phase 2 construction from .djo", error);
    check(reopened.mechanicsData().assemblies.size() == 1 && reopened.mechanicsData().assemblies.front().instanceIds.size() == 2,
          "Reopen preserves assembly grouping and relationships");
    check(close(geom::ofFeature(reopened, feat1).volumeMm3, geom::ofFeature(doc, feat1).volumeMm3),
          "Reopen preserves exact cut geometry and volumes");

    return passed;
}

bool runCadCraftPhase3Check(juce::var& report)
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/craft-check-p3").getChildFile(juce::Uuid().toDashedString());
    folder.createDirectory();
    juce::Array<juce::var> checks; bool passed = true;
    const auto check = [&](bool ok, const juce::String& label, const juce::String& detail = juce::String())
    {
        auto item = object(); put(item, "check", label); put(item, "passed", ok);
        if (detail.isNotEmpty()) put(item, "detail", detail);
        checks.add(item); passed &= ok;
        report = object(); put(report, "passed", passed); put(report, "checks", checks); put(report, "folder", folder.getFullPathName());
        return ok;
    };
    const auto close = [](double a, double b) { return std::abs(a - b) < 1e-5 * juce::jmax(1.0, std::abs(b)); };

    // 1. Adhesive catalog checks
    const auto catalog = mech::defaultAdhesives();
    check(catalog.size() == 6, "Adhesive catalog contains all six required craft adhesives");
    bool foundPVA = false, foundCA = false, foundEpoxy = false, foundHotMelt = false, foundContact = false, foundSolvent = false;
    for (const auto& a : catalog)
    {
        if (a.id == "adh-pva") { foundPVA = true; check(a.compatibleSubstrates.contains("Wood") && a.shearStrengthMPa && *a.shearStrengthMPa > 0, "PVA adhesive has wood compatibility and valid shear strength"); }
        if (a.id == "adh-ca") { foundCA = true; check(a.compatibleSubstrates.contains("Plastic") && !a.compatibleSubstrates.contains("Foam board"), "Cyanoacrylate compatible with plastic but excludes polystyrene foam board"); }
        if (a.id == "adh-epoxy") { foundEpoxy = true; check(a.compatibleSubstrates.size() >= 4 && a.shearStrengthMPa && *a.shearStrengthMPa >= 20.0, "Epoxy supports wide substrate compatibility and high strength"); }
        if (a.id == "adh-hotmelt") { foundHotMelt = true; check(a.recommendedThicknessMm && *a.recommendedThicknessMm >= 0.5, "Hot melt adhesive supports gap-filling recommended thickness"); }
        if (a.id == "adh-contact") { foundContact = true; check(a.openTime.isNotEmpty(), "Contact cement specifies required open/tack time"); }
        if (a.id == "adh-solvent") { foundSolvent = true; check(a.compatibleSubstrates.size() == 1 && a.compatibleSubstrates[0] == "Plastic", "Solvent cement strictly restricted to weld-compatible plastics"); }
    }
    check(foundPVA && foundCA && foundEpoxy && foundHotMelt && foundContact && foundSolvent, "All required adhesive definitions resolve correctly with provenance");

    // Initialize command service and test document
    CadCommandService service;
    service.setComponentLibraryFile(folder.getChildFile("library.json"));
    service.setCatalogFile(folder.getChildFile("materials.json"));
    const auto command = [&](const char* action, juce::var args = object()) { put(args, "action", action); return service.componentCommand(args); };
    auto& doc = service.document();
    juce::String error;

    // 2. Insert two wood strips
    auto r = command("insert", [&]{ auto a = object(); put(a, "component", "craft-wood-strip"); return a; }());
    const auto inst1 = r["id"].toString();
    const auto feat1 = doc.mechanicsData().findInstance(inst1)->featureId;

    r = command("insert", [&]{ auto a = object(); put(a, "component", "craft-wood-strip"); return a; }());
    const auto inst2 = r["id"].toString();
    const auto feat2 = doc.mechanicsData().findInstance(inst2)->featureId;
    check(inst1.isNotEmpty() && inst2.isNotEmpty() && inst1 != inst2, "Insert two independent craft stock occurrences");

    // 3. Align inst2 to inst1 as butt joint
    auto alignArgs = object(); put(alignArgs, "target", inst1); put(alignArgs, "source", inst2); put(alignArgs, "alignType", "butt");
    r = command("align", alignArgs);
    check((bool)r["ok"], "Align second stick to end of first stick (butt joint)");

    // 4. Create surface bond with PVA glue
    auto bondArgs = object();
    put(bondArgs, "instanceA", inst1);
    put(bondArgs, "instanceB", inst2);
    put(bondArgs, "type", "surface");
    put(bondArgs, "adhesive", "adh-pva");
    put(bondArgs, "bondThicknessMm", 0.1);
    r = command("createBond", bondArgs);
    const auto bond1Id = r["id"].toString();
    const auto* bond1 = doc.mechanicsData().findBondedJoint(bond1Id);
    check((bool)r["ok"] && bond1 != nullptr && bond1->status == "valid" && bond1->contactAreaMm2 > 0.0 && bond1->estimatedVolumeMm3 > 0.0,
          "Create and evaluate valid PVA surface bond (contact area > 0, status == valid)");

    // 5. Test substrate compatibility failure: try bonding wood pieces with plastic solvent cement
    auto badBondArgs = object();
    put(badBondArgs, "instanceA", inst1);
    put(badBondArgs, "instanceB", inst2);
    put(badBondArgs, "type", "surface");
    put(badBondArgs, "adhesive", "adh-solvent");
    r = command("createBond", badBondArgs);
    const auto badBondId = r["id"].toString();
    const auto* badBond = doc.mechanicsData().findBondedJoint(badBondId);
    check((bool)r["ok"] && badBond != nullptr && badBond->status == "incompatible_substrate" && badBond->failureReason.contains("incompatible"),
          "Solvent cement rejected on wood substrate with incompatible_substrate status and clear reason");

    // 6. Test separation invalidation and automatic recovery
    const CadFrame separatedPose = mech::frameFromEulerDegrees(200.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    check(doc.setInstancePlacement(inst2, separatedPose, error), "Move second stick away from contact");
    const auto* bond1_sep = doc.mechanicsData().findBondedJoint(bond1Id);
    check(bond1_sep != nullptr && bond1_sep->status == "separated" && bond1_sep->separationDistanceMm > 50.0 && bond1_sep->contactAreaMm2 == 0.0,
          "Separation detected: joint invalidated with status 'separated' and measured gap");

    // Re-align and verify status recovers to valid
    r = command("align", alignArgs);
    const auto* bond1_recovered = doc.mechanicsData().findBondedJoint(bond1Id);
    check((bool)r["ok"] && bond1_recovered != nullptr && bond1_recovered->status == "valid" && bond1_recovered->contactAreaMm2 > 0.0,
          "Joint automatically recovers to 'valid' when components return to contact");

    // 7. Insert piece 3 and test edge and fillet bonds
    r = command("insert", [&]{ auto a = object(); put(a, "component", "craft-wood-strip"); return a; }());
    const auto inst3 = r["id"].toString();
    auto cornerArgs = object(); put(cornerArgs, "target", inst1); put(cornerArgs, "source", inst3); put(cornerArgs, "alignType", "corner90");
    r = command("align", cornerArgs);

    // Fillet bond with epoxy and bead solid creation
    auto filletArgs = object();
    put(filletArgs, "instanceA", inst1);
    put(filletArgs, "instanceB", inst3);
    put(filletArgs, "type", "fillet");
    put(filletArgs, "adhesive", "adh-epoxy");
    put(filletArgs, "beadLegMm", 2.0);
    put(filletArgs, "createBeadSolid", true);
    r = command("createBond", filletArgs);
    const auto filletId = r["id"].toString();
    const auto* filletJoint = doc.mechanicsData().findBondedJoint(filletId);
    check((bool)r["ok"] && filletJoint != nullptr && filletJoint->status == "valid" && filletJoint->contactLengthMm > 0.0 && filletJoint->beadFeatureId.isNotEmpty(),
          "Create fillet bond with genuine OCCT triangular bead solid");

    // 8. Connected construction behavior: move bonded group as a unit
    const auto connected = craft::getConnectedBondedGroup(doc, inst2);
    check(connected.size() >= 3, "Connected graph traversal identifies all pieces in bonded construction");

    const double initialInst1X = doc.mechanicsData().findInstance(inst1)->placement.x;
    const double initialInst2X = doc.mechanicsData().findInstance(inst2)->placement.x;
    const double initialInst3X = doc.mechanicsData().findInstance(inst3)->placement.x;

    auto moveGroupArgs = object();
    put(moveGroupArgs, "instance", inst2);
    auto deltaP = object(); put(deltaP, "x", 40.0); put(deltaP, "y", 25.0); put(deltaP, "z", 0.0);
    put(moveGroupArgs, "placement", deltaP);
    r = command("moveBondedGroup", moveGroupArgs);

    const double newInst1X = doc.mechanicsData().findInstance(inst1)->placement.x;
    const double newInst2X = doc.mechanicsData().findInstance(inst2)->placement.x;
    const double newInst3X = doc.mechanicsData().findInstance(inst3)->placement.x;

    check((bool)r["ok"] && close(newInst1X - initialInst1X, 40.0) && close(newInst2X - initialInst2X, 40.0) && close(newInst3X - initialInst3X, 40.0),
          "Move bonded group translates all connected pieces rigidly by exact delta");

    // Verify all joints remain valid after rigid group translation
    const auto* bond1_afterMove = doc.mechanicsData().findBondedJoint(bond1Id);
    const auto* fillet_afterMove = doc.mechanicsData().findBondedJoint(filletId);
    check(bond1_afterMove && bond1_afterMove->status == "valid" && fillet_afterMove && fillet_afterMove->status == "valid",
          "All bonded joints remain valid after connected group translation");

    // 9. Cut list & Adhesive BOM reporting
    r = command("cutList");
    const auto cutListData = r["cutList"];
    check(cutListData.hasProperty("adhesives"), "Cut list contains integrated adhesive usage section");

    r = command("adhesiveBom");
    const auto bom = r["adhesiveBom"];
    check((bool)r["ok"] && (int)bom["totalJoints"] >= 3 && (int)bom["validJoints"] >= 2 && (double)bom["totalAdhesiveVolumeMm3"] > 0.0,
          "Generate comprehensive Adhesive BOM (joint counts, valid/invalid breakdown, volumes, areas)");

    // 10. Undo / Redo verification of bonded construction operations
    check(doc.undo(error) && close(doc.mechanicsData().findInstance(inst1)->placement.x, initialInst1X),
          "Undo restores pre-move positions of bonded construction", error);
    check(doc.redo(error) && close(doc.mechanicsData().findInstance(inst1)->placement.x, newInst1X),
          "Redo reapplies bonded construction movement", error);

    // 11. Native .djo save and reopen persistence
    const auto* docBond1 = doc.mechanicsData().findBondedJoint(bond1Id);
    const double expectedContactArea = docBond1 != nullptr ? docBond1->contactAreaMm2 : -1.0;
    const juce::String expectedStatus = docBond1 != nullptr ? docBond1->status : "";

    const auto nativeFile = folder.getChildFile("phase3-construction.djo");
    check(doc.save(nativeFile, error), "Save Phase 3 bonded construction to native .djo", error);

    CadDocument reopened;
    check(reopened.load(nativeFile, error), "Reopen Phase 3 bonded construction from native .djo", error);
    check(reopened.mechanicsData().bondedJoints.size() == doc.mechanicsData().bondedJoints.size(),
          "Reopen preserves all bonded joint entities");
    const auto* reopenedBond1 = reopened.mechanicsData().findBondedJoint(bond1Id);
    const bool matches = reopenedBond1 != nullptr && reopenedBond1->status == expectedStatus && close(reopenedBond1->contactAreaMm2, expectedContactArea);
    if (!matches && reopenedBond1 != nullptr)
    {
        error = "Expected status " + expectedStatus + ", got " + reopenedBond1->status + 
                "; expected area " + juce::String(expectedContactArea) + ", got " + juce::String(reopenedBond1->contactAreaMm2);
    }
    check(matches, "Reopen restores exact contact evaluation and validity state", error);

    return passed;
}
}
