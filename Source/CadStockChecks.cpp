#include "CadDocument.h"
#include "CadMaterialsManager.h"
#include "DjehutiAgent.h"
#include <limits>

namespace djehuti::cad
{
// Focused integration checks run only with --cad-stock-check. User catalogs are never touched.
bool runCadStockCheck(juce::var& report)
{
    juce::Array<juce::var> checks; int failures=0;
    const auto record=[&](bool ok,const juce::String& name,const juce::String& detail=juce::String())
    { auto* r=new juce::DynamicObject();r->setProperty("check",name);r->setProperty("ok",ok);if(detail.isNotEmpty())r->setProperty("detail",detail);checks.add(r);if(!ok)++failures; };
    const auto parse=[](const juce::String& text){return juce::JSON::parse(text);};
    const auto text=[](const juce::var& value){return juce::JSON::toString(value,true);};
    const auto ok=[](const juce::var& value){return (bool)value["ok"];};
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/stock-check").getChildFile(juce::Uuid().toDashedString());folder.createDirectory();
    juce::StringArray registered; const auto toolCatalogue = DjehutiAgentPanel::toolCatalogue();
    for(const auto& tool:*toolCatalogue.getArray()) if(tool["parameters"].isObject()) registered.add(tool["name"].toString());
    record(registered.contains("cad_catalog")&&registered.contains("cad_stock")&&registered.contains("cad_material_duplicate"),"new tools have valid registered schemas");
    CadCommandService svc;svc.setCatalogFile(folder.getChildFile("user.json"));juce::String error,box;
    svc.createBox(20,30,5,box,error);
    auto catalog=svc.catalogCommand(parse("{}"));record(ok(catalog)&&catalog["catalog"]["materials"].size()>=5&&catalog["catalog"]["stocks"].size()>0,"bundled catalogs load without a user file",text(catalog));
    juce::var researched,aluminum;
    for(const auto& item:*catalog["catalog"]["materials"].getArray())
    {
        if(item["id"].toString()=="mat-research-mat-010")researched=item;
        if(item["id"].toString()=="mat-ref-6061-t6")aluminum=item;
    }
    record(researched.isObject()&&researched["model"]=="referenceOnly"&&researched["properties"]["density"].isVoid()&&(double)researched["properties"]["tensileStrength"]["value"]==50.0,
           "PETG research values remain selectable without invented density or elastic constants");
    record(aluminum.isObject()&&aluminum["model"]=="isotropic"&&aluminum["properties"]["youngsModulus"].isObject(),"engineering metal reference remains available");
    auto reference=svc.catalogCommand(parse(R"({"action":"useMaterial","material":"mat-research-mat-010","feature":")"+box+R"("})"));
    record(ok(reference)&&svc.document().materialsData().assignmentFor(box),"reference-only PETG imports and assigns through the shared catalog command",text(reference));
    for(const auto* extension:{"djo","djc"})
    {
        const auto path=folder.getChildFile(juce::String("petg-reference.")+extension);
        CadCommandService opened;juce::String reopenError;
        const bool saved=svc.save(path,reopenError),reopened=saved&&opened.load(path,reopenError);
        const auto* assignment=reopened?opened.document().materialsData().assignmentFor(box):nullptr;
        const auto* local=assignment?opened.document().materialsData().find(assignment->materialId):nullptr;
        record(reopened&&local&&local->model=="referenceOnly"&&local->tensileStrengthMPa==50.0,
               "PETG assignment and unknown values survive "+juce::String(extension)+" reopen",reopenError);
    }
    record(svc.undo(error)&&svc.document().materialsData().empty(),"PETG import and assignment undo together",error);
    auto r=svc.catalogCommand(parse(R"({"action":"createMaterial","definition":{"name":"Check alloy","family":"Aluminum","specification":"TEST ONLY","condition":"Check condition","source":"diagnostic fixture","densityKgPerM3":2700,"youngsModulusMPa":69000,"poissonsRatio":0.33}})"));
    const auto material=r["id"].toString();record(ok(r)&&material.startsWith("mat-"),"create user engineering material",text(r));
    r=svc.catalogCommand(parse(R"({"action":"updateMaterial","material":")"+material+R"(","definition":{"name":"Check alloy revised"}})"));
    catalog=svc.catalogCommand(parse("{}"));int revision=0;for(const auto& m:*catalog["catalog"]["materials"].getArray())if(m["id"].toString()==material)revision=(int)m["revision"];
    record(ok(r)&&revision==2,"editing retains identity and advances revision",text(r));
    r=svc.catalogCommand(parse(R"({"action":"updateMaterial","material":"mat-ref-a36","definition":{"name":"bad"}})"));record(!ok(r),"bundled entries are read-only");
    r=svc.catalogCommand(parse(R"({"action":"duplicateMaterial","material":"mat-ref-a36"})"));record(ok(r)&&r["id"].toString()!="mat-ref-a36","bundled material can be duplicated");
    r=svc.catalogCommand(parse(R"({"action":"createStock","definition":{"name":"Check substrate","material":")"+material+R"(","thicknessMm":1.5,"finishedThicknessMm":1.6,"sheetWidthMm":1000,"sheetLengthMm":2000,"gauge":"custom","gaugeStandard":"diagnostic only","insideRadiusMm":2,"kFactor":0.42,"qualification":"shopQualified","formingSource":"test fixture, not production data"}})"));
    const auto stockId=r["id"].toString();record(ok(r),"create qualified user stock linked to material",text(r));
    const auto beforeImport=svc.document().revision();
    r=svc.catalogCommand(parse(R"({"action":"useStock","stock":")"+stockId+R"("})"));const auto localStock=r["id"].toString();
    const auto* imported=svc.document().stocksData().find(localStock);const auto importedMaterial=imported?imported->materialId:juce::String();
    record(ok(r)&&imported&&imported->id!=stockId&&imported->catalogId==stockId&&svc.document().materialsData().find(importedMaterial)&&svc.document().revision()==beforeImport+1&&svc.document().stocksData().defaultStockId==localStock,"stock+material+default import is one remapped document transaction",text(r));
    if(!imported){auto* root=new juce::DynamicObject();root->setProperty("ok",false);root->setProperty("checks",checks);report=root;return false;}
    const auto materialSnapshot=text(matl::toJson(svc.document().materialsData())),stockSnapshot=text(stock::toJson(svc.document().stocksData()));
    record(svc.undo(error)&&svc.document().materialsData().empty()&&svc.document().stocksData().empty(),"undo removes both snapshots and restores empty default",error);
    record(svc.redo(error)&&text(matl::toJson(svc.document().materialsData()))==materialSnapshot&&text(stock::toJson(svc.document().stocksData()))==stockSnapshot,"redo restores exact snapshots and default",error);
    r=svc.catalogCommand(parse(R"({"action":"updateStock","stock":")"+stockId+R"(","definition":{"thicknessMm":1.55}})"));
    record(ok(r)&&text(stock::toJson(svc.document().stocksData()))==stockSnapshot,"catalog updates do not change imported document definitions");
    r=svc.materialCommand("cad_material_assign",parse(R"({"feature":")"+box+R"(","material":")"+importedMaterial+R"("})"));record(ok(r),"document material assignment through shared command service",text(r));
    r=svc.materialCommand("cad_material_delete",parse(R"({"material":")"+importedMaterial+R"(","clearAssignments":true})"));record(!ok(r),"stock prevents deleting material even with clearAssignments");
    r=svc.catalogCommand(parse(R"({"action":"deleteMaterial","material":")"+material+R"("})"));record(!ok(r),"catalog dependent deletion refused");
    r=svc.stockCommand(parse(R"({"action":"delete","stock":")"+localStock+R"("})"));record(!ok(r),"default-stock deletion refused");
    const auto stableStock=text(stock::toJson(svc.document().stocksData()));const auto stableRevision=svc.document().revision();
    for(const auto& bad:{juce::String(R"({"thicknessMm":0})"),juce::String(R"({"finishedThicknessMm":1})"),juce::String(R"({"kFactor":1.1})"),juce::String(R"({"sheetWidthMm":null})"),juce::String(R"({"gaugeStandard":""})"),juce::String(R"({"formingSource":""})"),juce::String(R"({"material":"missing"})"),juce::String(R"({"formingMethod":"bendDeduction"})"),juce::String(R"({"lengthUnit":"in"})"),juce::String(R"({"thicknessBasis":"finished"})")})
    {
        auto a=parse(R"({"action":"update","stock":")"+localStock+R"("})");a.getDynamicObject()->setProperty("definition",parse(bad));r=svc.stockCommand(a);
        record(!ok(r)&&svc.document().revision()==stableRevision&&text(stock::toJson(svc.document().stocksData()))==stableStock,"invalid edit rolls back: "+bad,text(r));
    }
    auto invalid=*svc.document().stocksData().find(localStock);invalid.kFactor=std::numeric_limits<double>::quiet_NaN();
    record(!svc.document().updateStock(localStock,invalid,error)&&svc.document().revision()==stableRevision,"nonfinite numeric values rejected",error);
    invalid=*svc.document().stocksData().find(localStock);invalid.thicknessMm=-1;invalid.materialId=material;
    CadMaterial badImport;badImport.id=material;badImport.name="Valid material, invalid stock";badImport.densityKgPerM3=2700;badImport.youngsModulusMPa=69000;badImport.poissonsRatio=.33;
    const auto beforeFailedImport=text(matl::toJson(svc.document().materialsData()));juce::String failedId;
    record(!svc.document().importStock(invalid,badImport,true,failedId,error)&&text(matl::toJson(svc.document().materialsData()))==beforeFailedImport&&text(stock::toJson(svc.document().stocksData()))==stableStock,"failed stock import leaves no orphan material",error);
    for(const auto* extension:{"djo","json"})
    {
        const auto file=folder.getChildFile(juce::String("roundtrip.")+extension);record(svc.save(file,error),"save "+juce::String(extension),error);
        CadCommandService loaded;loaded.setCatalogFile(folder.getChildFile("unavailable/catalog.json"));
        const bool loadedOk=loaded.load(file,error);
        record(loadedOk&&text(stock::toJson(loaded.document().stocksData()))==stableStock&&text(matl::toJson(loaded.document().materialsData()))==beforeFailedImport&&loaded.document().materialsData().assignmentFor(box),"offline roundtrip preserves references, metadata, assignment and default: "+juce::String(extension),error);
        if(loadedOk){r=loaded.stockCommand(parse(R"({"action":"setDefault"})"));record(ok(r)&&loaded.undo(error)&&loaded.document().stocksData().defaultStockId==localStock&&loaded.redo(error)&&loaded.document().stocksData().defaultStockId.isEmpty(),"history works after open: "+juce::String(extension),error);}
    }
    const auto oldFile=folder.getChildFile("legacy.json");auto old=parse(folder.getChildFile("roundtrip.json").loadFileAsString());old.getDynamicObject()->removeProperty("materials");old.getDynamicObject()->removeProperty("sheetStocks");old.getDynamicObject()->setProperty("formatVersion",6);oldFile.replaceWithText(text(old));
    CadCommandService legacy;record(legacy.load(oldFile,error)&&legacy.document().stocksData().empty()&&legacy.document().materialsData().empty()&&legacy.document().find(box),"older JSON without engineering containers loads",error);
    const auto legacyNative=folder.getChildFile("legacy.djo");legacy.save(legacyNative,error);record(legacy.load(legacyNative,error)&&legacy.document().stocksData().empty(),"native document without optional stock container loads",error);
    auto oldMaterials=parse(R"({"schemaVersion":1,"nextNumber":2,"materials":[{"id":"mat-1","name":"Old material","revision":1,"model":"isotropic","properties":{"density":{"value":7850,"unit":"kg/m^3"},"youngsModulus":{"value":200000,"unit":"MPa"},"poissonsRatio":{"value":0.3,"unit":"1"}}}],"assignments":[]})");CadMaterials oldLibrary;
    record(matl::fromJson(oldMaterials,oldLibrary,error)&&oldLibrary.materials.front().family.isEmpty(),"material schema 1 remains readable",error);
    r=svc.stockCommand(parse(R"({"action":"update","stock":")"+localStock+R"(","definition":{"thicknessMm":1.52}})"));record(ok(r)&&svc.document().stocksData().find(localStock)->revision==2&&svc.undo(error)&&svc.document().stocksData().find(localStock)->revision==1,"shared stock edits are versioned and undoable",error);
    const auto portable=folder.getChildFile("portable.json");r=svc.catalogCommand(parse(R"({"action":"exportFile","path":")"+portable.getFullPathName().replace("\\","/")+R"("})"));record(ok(r),"portable catalog export",text(r));
    const auto exportedText=portable.loadFileAsString();auto exportArgs=parse(R"({"action":"exportFile","path":")"+portable.getFullPathName().replace("\\","/")+R"("})");
    r=svc.catalogCommand(exportArgs);record(!ok(r)&&portable.loadFileAsString()==exportedText,"portable export requires consent before replacing a file");
    exportArgs.getDynamicObject()->setProperty("overwrite",true);r=svc.catalogCommand(exportArgs);record(ok(r),"portable export supports explicit overwrite",text(r));
    CadCommandService other;other.setCatalogFile(folder.getChildFile("other.json"));
    auto importArgs=parse(R"({"action":"importFile","path":")"+portable.getFullPathName().replace("\\","/")+R"("})");r=other.catalogCommand(importArgs);record(ok(r),"portable catalog import",text(r));
    other.catalogCommand(parse(R"({"action":"updateMaterial","material":")"+material+R"(","definition":{"name":"Independent edited entry"}})"));
    r=other.catalogCommand(importArgs);catalog=other.catalogCommand(parse("{}"));bool referencesValid=true;juce::StringArray ids;
    for(const auto& m:*catalog["catalog"]["materials"].getArray())ids.add(m["id"].toString());for(const auto& s:*catalog["catalog"]["stocks"].getArray())referencesValid&=ids.contains(s["material"].toString());
    record(ok(r)&&referencesValid,"conflicting portable IDs are remapped with their stock references",text(r));
    CadCommandService reopened;reopened.setCatalogFile(folder.getChildFile("user.json"));r=reopened.catalogCommand(parse("{}"));bool userSurvived=false;for(const auto& m:*r["catalog"]["materials"].getArray())userSurvived|=m["id"].toString()==material&&(int)m["revision"]==2;
    record(ok(r)&&userSurvived,"user catalog persists independently of CAD document");
    CadCommandService collision;collision.setCatalogFile(folder.getChildFile("user.json"));auto local=badImport;juce::String firstLocal;
    collision.createMaterial(local,firstLocal,error);
    r=collision.catalogCommand(parse(R"({"action":"useStock","stock":")"+stockId+R"("})"));const auto* collisionStock=collision.document().stocksData().find(r["id"].toString());
    record(ok(r)&&collisionStock&&collisionStock->materialId!=firstLocal&&collision.document().materialsData().find(firstLocal),"document import preserves existing local identities",text(r));
    const auto externalFile=folder.getChildFile("user.json");externalFile.replaceWithText(externalFile.loadFileAsString()+"\n");
    const auto externalText=externalFile.loadFileAsString(); // Compare actual bytes after JUCE's platform newline normalization.
    r=svc.catalogCommand(parse(R"({"action":"createMaterial","definition":{"name":"Should not save","densityKgPerM3":1,"youngsModulusMPa":1,"poissonsRatio":0.1}})"));
    record(!ok(r)&&externalFile.loadFileAsString()==externalText,"external catalog changes are not overwritten");
    svc.catalogCommand(parse(R"({"action":"reload"})"));
    auto* root=new juce::DynamicObject();root->setProperty("ok",failures==0);root->setProperty("failures",failures);root->setProperty("checks",checks);root->setProperty("folder",folder.getFullPathName());report=root;return failures==0;
}
}
