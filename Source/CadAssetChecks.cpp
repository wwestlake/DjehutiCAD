#include "CadAssetBrowser.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
using namespace djehuti::cad;using namespace djehuti::cad::components;
namespace
{
juce::var args(const char* action){auto a=object();put(a,"action",action);return a;}
double volume(const CadDocument& d,const juce::String& id){const auto* f=d.find(id);if(!f||f->shape.IsNull())return -1;GProp_GProps p;BRepGProp::VolumeProperties(f->shape,p);return p.Mass();}
}
bool djehuti::cad::runCadAssetCheck(juce::var& report)
{
    juce::Array<juce::var> checks;int failed=0;juce::String error;
    const auto check=[&](bool ok,const juce::String& label){auto c=object();put(c,"ok",ok);put(c,"name",label);if(!ok){++failed;put(c,"detail",error);}checks.add(c);};
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/asset-check").getChildFile(juce::Uuid().toDashedString());folder.createDirectory();const auto file=folder.getChildFile("library.json");CadAssetLibrary library(file);CadCommandService a,b;
    auto r=library.command(args("list"),a.document());check((bool)r["ok"]&&r["assets"].size()==0,"Personal library starts empty independently of supplied components");
    auto q=args("createCategory");put(q,"name","Robotics");r=library.command(q,a.document());const auto category=r["id"].toString();check((bool)r["ok"],"Create persistent user category");
    check(!(bool)library.command(q,a.document())["ok"],"Duplicate category names are rejected");
    juce::String sketch,circle,part;a.createSketch("XY",0,sketch,error);a.addSketchCircle(sketch,0,0,5,circle,error);a.createExtrude(sketch,10,false,part,error);CadMaterial material;material.name="Test polymer";material.model="densityOnly";material.densityKgPerM3=1100;juce::String materialId;a.createMaterial(material,materialId,error);a.assignMaterial(part,materialId,error);const auto originalRevision=a.document().revision();
    q=args("save");put(q,"source",part);put(q,"name","Robot hub");put(q,"category",category);put(q,"description","Editable extruded circle");put(q,"materialLabel","Unspecified");r=library.command(q,a.document());const auto asset=r["id"].toString();
    check((bool)r["ok"]&&a.document().revision()==originalRevision,"Saving a designed parametric part leaves the source unchanged");
    check(!(bool)library.command(q,a.document())["ok"],"Duplicate asset names fail without implicit replacement");
    auto protect=args("deleteCategory");put(protect,"category",category);check(!(bool)library.command(protect,a.document())["ok"],"A nonempty category cannot silently delete its assets");
    a.newDocument();CadAssetLibrary restarted(file);q=args("list");r=restarted.command(q,b.document());check((bool)r["ok"]&&r["assets"].size()==1&&r["categories"].size()==2,"Assets and categories survive service restart after original document closes");
    q=args("insert");put(q,"asset",asset);auto p=object();put(p,"x",100);put(q,"placement",p);r=restarted.command(q,b.document());const auto instance=r["id"].toString(),feature=r["feature"].toString();
    check((bool)r["ok"]&&std::abs(volume(b.document(),feature)-250*juce::MathConstants<double>::pi)<1e-5,"Insert into a different document rebuilds valid geometry");
    check(b.document().materialsData().assignmentFor(feature)!=nullptr,"Imported geometry preserves its assigned material definition");
    check(b.document().find(feature)&&b.document().find(feature)->type=="extrude"&&b.document().sketchData().size()==1,"Insertion preserves editable feature and sketch history");
    check(std::abs(b.document().mechanicsData().findInstance(instance)->placement.x-100)<1e-8,"Existing occurrence placement uses millimetres");
    auto r2=restarted.command(q,b.document());check((bool)r2["ok"]&&r2["feature"]!=r["feature"],"Repeated insertion creates independent parametric copies");
    const auto importedSketch=b.document().find(feature)->sketchId;const auto importedCircle=b.document().findSketch(importedSketch)->entities.front().id;
    check(b.setSketchEntityDimension(importedSketch,importedCircle,6,error)&&std::abs(volume(b.document(),feature)-360*juce::MathConstants<double>::pi)<1e-5&&std::abs(volume(b.document(),r2["feature"].toString())-250*juce::MathConstants<double>::pi)<1e-5,"Editing an imported sketch rebuilds only its independent copy");
    check(b.undo(error)&&b.redo(error),"Imported feature edits use OCAF Undo/Redo");
    check(b.setInstancePlacement(instance,mech::frameFromEulerDegrees(120,5,2,0,0,20),error)&&b.undo(error)&&b.redo(error),"Placement uses ordinary undoable instance commands");
    for(const auto* ext:{"djo","djc"}){auto saved=folder.getChildFile(juce::String("receiving.")+ext);CadCommandService reopened;check(b.save(saved,error)&&reopened.load(saved,error)&&reopened.document().mechanicsData().instances.size()==2&&reopened.document().sketchData().size()==2,juce::String("Receiving document survives save/reopen: ")+ext);}
    const auto count=b.document().features().size();r=restarted.command(q,b.document());check((bool)r["ok"]&&b.undo(error)&&b.document().features().size()==count&&b.redo(error),"One insertion is a single undoable transaction");
    q=args("update");put(q,"asset",asset);auto d=object();put(d,"name","Hub renamed");put(d,"category","misc");put(q,"definition",d);check((bool)restarted.command(q,b.document())["ok"],"Rename and move asset metadata");
    q=args("deleteCategory");put(q,"category",category);check((bool)restarted.command(q,b.document())["ok"],"Moved assets permit deleting the now-empty category");
    q=args("exportSource");put(q,"asset",asset);const auto exported=folder.getChildFile("editable.djc");put(q,"path",exported.getFullPathName());CadCommandService editable;check((bool)restarted.command(q,b.document())["ok"]&&editable.load(exported,error)&&editable.document().find(part)->type=="extrude","Export an independently editable source document");
    q=args("delete");put(q,"asset",asset);check((bool)restarted.command(q,b.document())["ok"]&&b.document().features().size()==count+2,"Deleting library metadata cannot remove already inserted document copies");
    q=args("insert");put(q,"asset",asset);const auto revision=b.document().revision();check(!(bool)restarted.command(q,b.document())["ok"]&&b.document().revision()==revision,"Missing asset insertion fails without changing the receiving document");
    // Assemblies use their existing instance, joint and group representations.
    juce::String box,i1,i2;a.createBox(10,20,30,box,error);a.createInstance(box,"A",i1,error);a.createInstance(box,"B",i2,error);a.setInstancePlacement(i2,mech::frameFromEulerDegrees(40,0,0,0,0,0),error);
    q=args("save");put(q,"source","assembly");put(q,"name","Pair");put(q,"category","misc");r=restarted.command(q,a.document());const auto assembly=r["id"].toString();check((bool)r["ok"],"Save the current document assembly");
    CadCommandService assemblyDoc;q=args("insert");put(q,"asset",assembly);put(q,"placement",p);r=restarted.command(q,assemblyDoc.document());check((bool)r["ok"]&&r["instances"].size()==2&&assemblyDoc.document().mechanicsData().assemblies.size()==1,"Insert assembly with preserved occurrences and an ordinary assembly group");
    check(std::abs(assemblyDoc.document().mechanicsData().instances[1].placement.x-assemblyDoc.document().mechanicsData().instances[0].placement.x-40)<1e-8,"Assembly insertion preserves relative placement");
    q=args("get");put(q,"asset",assembly);auto corrupt=restarted.command(q,assemblyDoc.document())["asset"]["payload"];put(corrupt,"root","missing");put(corrupt,"type","Part");juce::var ignored;check(!assemblyDoc.document().insertAsset(corrupt,"Bad",{},ignored,error),"Malformed root data is controlled before starting a transaction");
    const auto invalid=folder.getChildFile("invalid.json");invalid.replaceWithText("broken");CadAssetLibrary bad(invalid);check(!(bool)bad.command(args("list"),b.document())["ok"]&&invalid.loadFileAsString()=="broken","Corrupt library data yields an error without overwriting the file");
    auto summary=object();put(summary,"ok",failed==0);put(summary,"passed",checks.size()-failed);put(summary,"failed",failed);put(summary,"checks",checks);report=summary;return failed==0;
}
bool runCadAssetUiCheck(juce::var& report)
{
    juce::Array<juce::var> checks;int failed=0;const auto check=[&](bool ok,const char* label){auto c=object();put(c,"ok",ok);put(c,"name",label);checks.add(c);if(!ok)++failed;};
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/asset-ui-check").getChildFile(juce::Uuid().toDashedString());folder.createDirectory();CadAssetLibrary library(folder.getChildFile("assets.json"));auto service=std::make_shared<CadCommandService>();juce::String feature,error;service->createBox(10,20,30,feature,error);CadAssetBrowser browser(library,service);browser.setSize(1200,680);
    const auto ui=[&](const char* op,const char* field="",const juce::String& value=juce::String()){auto a=object();put(a,"operation",op);put(a,"field",field);put(a,"value",value);return browser.ui(a);};
    auto r=ui("state");check((int)r["expandedCount"]==0&&r["rows"].size()==1,"Asset table initially shows collapsed categories and an empty-library message");
    browser.beginSave(feature);ui("set","name","GUI bracket");auto a=object();put(a,"operation","click");put(a,"button","save");r=browser.ui(a);check((bool)r["ok"]&&library.command(args("list"),service->document())["assets"].size()==1,"Save new asset GUI button uses shared library command");
    ui("collapseAll");check(ui("state")["rows"].size()==1,"Collapse All hides asset rows");ui("expandAll");r=ui("state");check(r["rows"].size()==2,"Expand All reveals categorized asset rows");
    const auto id=r["rows"][1]["id"].toString();a=object();put(a,"operation","select");put(a,"id",id);check((bool)browser.ui(a)["ok"],"Actual table selection fills the metadata editor");
    ui("set","search","bracket");check(ui("state")["rows"].size()==2,"Search finds matching assets and shows their category");ui("set","search","absent");check(ui("state")["rows"].size()==0,"Search supports an empty result without invented metadata");ui("set","search","");
    service->newDocument();a=object();put(a,"operation","click");put(a,"button","insert");r=browser.ui(a);check((bool)r["ok"]&&service->document().mechanicsData().instances.size()==1,"Insert copy GUI button reuses an asset in a new document");
    a=object();put(a,"operation","capture");put(a,"path",folder.getChildFile("asset-library.png").getFullPathName());check((bool)browser.ui(a)["ok"],"Browser component capture produced for visual inspection");
    CadAssetBrowser reopened(library,service);check((int)reopened.ui(object())["expandedCount"]==0,"A new Asset Library presentation resets all category expansion");
    auto summary=object();put(summary,"ok",failed==0);put(summary,"passed",checks.size()-failed);put(summary,"failed",failed);put(summary,"checks",checks);put(summary,"capture",folder.getChildFile("asset-library.png").getFullPathName());report=summary;return failed==0;
}
