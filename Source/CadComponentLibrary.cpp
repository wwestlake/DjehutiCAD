#include "CadComponents.h"
#include "EngineeringSpecification.h"
#include "CadDocument.h"
#include "CadCraftStock.h"
#include <EngineeringData.h>
#include <set>
#include <cmath>

namespace djehuti::cad
{
using namespace components;
namespace
{
juce::var encode(const juce::Array<juce::var>& records){auto root=object();put(root,"format","djehuti-component-library");put(root,"schemaVersion",1);put(root,"records",records);return root;}
bool decode(const juce::var& root,juce::Array<juce::var>& records,juce::String& error)
{
    if(root["format"].toString()!="djehuti-component-library"||(int)root["schemaVersion"]!=1||!root["records"].isArray()){error="Unsupported component library file.";return false;}
    std::set<juce::String> ids;for(const auto& item:*root["records"].getArray()){if(!validate(item,error))return false;if(!ids.insert(item["id"].toString()).second){error="Duplicate component identity in library file.";return false;}records.add(clone(item));}return true;
}
}
juce::File CadComponentLibrary::defaultFile()
{
    const auto args=juce::JUCEApplication::getCommandLineParameterArray();const int index=args.indexOf("--component-library");
    if(index>=0&&index+1<args.size()&&juce::File::isAbsolutePath(args[index+1]))return juce::File(args[index+1]);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DjehutiCAD/component-library.json");
}
void CadComponentLibrary::setFile(const juce::File& target){file=target;entries.clear();diskText.clear();loaded=false;}
const juce::var* CadComponentLibrary::find(const juce::String& id)const{for(const auto& item:entries)if(item["id"].toString()==id)return &item;return nullptr;}
bool CadComponentLibrary::ensureLoaded(juce::String& error)
{
    if(loaded)return true;if(file==juce::File())file=defaultFile();juce::Array<juce::var> next;
    diskText=file.existsAsFile()?file.loadFileAsString():juce::String();
    const auto json=file.existsAsFile()?diskText:juce::String::fromUTF8(EngineeringData::componentlibraryseed_json,EngineeringData::componentlibraryseed_jsonSize);
    if(!decode(juce::JSON::parse(json),next,error))return false;
    // Add newly shipped definitions to an existing catalog, without replacing user edits or document snapshots.
    juce::Array<juce::var> bundled;
    if(!decode(juce::JSON::parse(juce::String::fromUTF8(EngineeringData::componentlibraryseed_json,EngineeringData::componentlibraryseed_jsonSize)),bundled,error))return false;
    for(const auto& stock:craft::templates())bundled.add(stock);
    bool extended=false;
    for(const auto& stock:bundled)
    {
        bool present=false;for(const auto& entry:next)if(entry["id"]==stock["id"]){present=true;break;}
        if(!present){if(!validate(stock,error))return false;next.add(stock);extended=true;}
    }
    if((!file.existsAsFile()||extended)&&!save(next,error))return false;entries=std::move(next);loaded=true;return true;
}
bool CadComponentLibrary::save(const juce::Array<juce::var>& next,juce::String& error)
{
    if(file.loadFileAsString()!=diskText){error="Component library changed outside this session. Reload before editing.";return false;}
    if(!file.getParentDirectory().createDirectory().wasOk()){error="Cannot create component library directory.";return false;}
    const auto content=juce::JSON::toString(encode(next));juce::TemporaryFile temp(file);
    if(!temp.getFile().replaceWithText(content)||!temp.overwriteTargetFileWithTemporary()){error="Cannot save component library.";return false;}diskText=content;return true;
}
bool CadComponentLibrary::exportFile(const juce::File& target,juce::String& error)const
{
    if(target.existsAsFile()){error="Library export destination already exists; choose a new file.";return false;}
    if(!target.replaceWithText(juce::JSON::toString(encode(entries)))){error="Cannot export component library.";return false;}return true;
}
bool CadComponentLibrary::importRecords(const juce::Array<juce::var>& incoming,bool replace,juce::var& report,juce::String& error)
{
    if(!ensureLoaded(error))return false;auto next=entries;juce::Array<juce::var> conflicts,invalid;int added=0,unchanged=0,replaced=0;
    for(const auto& raw:incoming)
    {
        juce::String reason;if(!validate(raw,reason)){auto v=object();put(v,"id",raw["id"]);put(v,"reason",reason);invalid.add(v);continue;}
        auto item=clone(raw);item.getDynamicObject()->removeProperty("sourceHash");item.getDynamicObject()->removeProperty("edited");
        const auto hash=digest(juce::JSON::toString(item));int found=-1;for(int i=0;i<next.size();++i)if(next[i]["id"]==item["id"]){found=i;break;}
        if(found>=0)
        {
            const auto& old=next[found];if(old["sourceHash"].toString()==hash){++unchanged;continue;}
            if(!replace){auto conflict=object();put(conflict,"id",old["id"]);put(conflict,"name",old["name"]);put(conflict,"userEdited",old["edited"]);conflicts.add(conflict);continue;}
            put(item,"revision",(int)old["revision"]+1);put(item,"sourceHash",hash);put(item,"edited",false);next.set(found,item);++replaced;
        }
        else {put(item,"sourceHash",hash);put(item,"edited",false);next.add(item);++added;}
    }
    if((added||replaced)&&!save(next,error))return false;entries=std::move(next);
    report=object();put(report,"imported",added);put(report,"unchanged",unchanged);put(report,"replaced",replaced);put(report,"conflicts",conflicts);put(report,"invalid",invalid);return true;
}
juce::var CadComponentLibrary::command(const juce::var& args,CadDocument& document)
{
    auto response=object();juce::String error,identity;const auto action=args.getProperty("action","list").toString();
    if(action=="reload")loaded=false;bool ok=ensureLoaded(error);
    if(ok&&(action=="list"||action=="reload")){put(response,"records",clone(entries));put(response,"path",file.getFullPathName());}
    else if(ok&&action=="document")put(response,"definitions",document.componentData());
    else if(ok&&action=="editStock")ok=document.updateCraftStock(args["feature"].toString(),args["geometry"],error);
    else if(ok&&action=="addCut")ok=document.addCraftCut(args["feature"].toString(),args["cut"],error);
    else if(ok&&action=="clearCuts")ok=document.clearCraftCuts(args["feature"].toString(),error);
    else if(ok&&action=="align")ok=document.alignInstances(args["target"].toString(),args["source"].toString(),args["alignType"].toString(),args["options"],error);
    else if(ok&&action=="createAssembly")
    {
        std::vector<juce::String> ids;
        if(const auto* array=args.getProperty("instances",{}).getArray())
            for(const auto& id:*array) ids.push_back(id.toString());
        ok=document.createAssemblyGroup(args.getProperty("name","").toString(),ids,identity,error);
    }
    else if(ok&&action=="moveAssembly")
    {
        const auto p=args["placement"];
        const auto delta=mech::frameFromEulerDegrees((double)p.getProperty("x",0.0),(double)p.getProperty("y",0.0),(double)p.getProperty("z",0.0),
                                                     (double)p.getProperty("rx",0.0),(double)p.getProperty("ry",0.0),(double)p.getProperty("rz",0.0));
        ok=document.moveAssemblyGroup(args["group"].toString(),delta,error);
    }
    else if(ok&&action=="cutList")put(response,"cutList",document.generateCutList());
    else if(ok&&action=="createBond")
    {
        CadBondedJoint joint;
        joint.instanceA = args["instanceA"].toString();
        joint.instanceB = args["instanceB"].toString();
        joint.type = args.getProperty("type", "surface").toString();
        joint.adhesiveId = args.getProperty("adhesive", "adh-pva").toString();
        joint.bondThicknessMm = (double)args.getProperty("bondThicknessMm", 0.1);
        joint.beadLegMm = (double)args.getProperty("beadLegMm", 2.0);
        joint.createBeadSolid = (bool)args.getProperty("createBeadSolid", false);
        joint.surfaceA = args.getProperty("surfaceA", "auto").toString();
        joint.surfaceB = args.getProperty("surfaceB", "auto").toString();
        ok = document.createBondedJoint(joint, identity, error);
        if (ok)
        {
            if (const auto* created = document.mechanicsData().findBondedJoint(identity))
                put(response, "joint", mech::bondedJointToJson(*created));
        }
    }
    else if(ok&&action=="removeBond") ok = document.removeBondedJoint(args["id"].toString(), error);
    else if(ok&&action=="reevaluateBonds") ok = document.reevaluateBondedJoints(error);
    else if(ok&&action=="moveBondedGroup")
    {
        const auto p = args["placement"];
        const auto delta = mech::frameFromEulerDegrees((double)p.getProperty("x",0.0),(double)p.getProperty("y",0.0),(double)p.getProperty("z",0.0),
                                                       (double)p.getProperty("rx",0.0),(double)p.getProperty("ry",0.0),(double)p.getProperty("rz",0.0));
        ok = document.moveBondedGroup(args["instance"].toString(), delta, error);
    }
    else if(ok&&action=="adhesiveBom") put(response, "adhesiveBom", document.adhesiveBom());
    else if(ok&&action=="adhesives")
    {
        juce::Array<juce::var> catalog;
        for (const auto& a : mech::defaultAdhesives()) catalog.add(mech::adhesiveToJson(a));
        put(response, "adhesives", catalog);
    }
    else if(ok&&action=="importFile")
    {
        const auto path=args["path"].toString();juce::Array<juce::var> records;auto parsing=object(),importing=object();
        if(!juce::File::isAbsolutePath(path)){ok=false;error="Choose an absolute workbook or portable library path.";}
        else if(juce::File(path).hasFileExtension("xlsx"))ok=readWorkbook(juce::File(path),records,parsing,error);
        else ok=decode(juce::JSON::parse(juce::File(path)),records,error);
        if(ok)for(auto& record:records)if(!engineering::specification(record).isVoid())put(record,"engineeringAssetRoot",juce::File(path).getParentDirectory().getFullPathName());
        if(ok)ok=importRecords(records,(bool)args["replaceConflicts"],importing,error);
        put(response,"parsing",parsing);put(response,"import",importing);
    }
    else if(ok&&action=="publishPart")
    {
        const auto feature=args["feature"].toString();const auto* part=document.find(feature);
        if(!part||part->shape.IsNull()||!TopExp_Explorer(part->shape,TopAbs_SOLID).More()){ok=false;error="Select a solid part to publish. Assembly publishing is not supported.";}
        else
        {
            auto item=object();put(item,"id","custom-"+juce::Uuid().toDashedString());put(item,"revision",1);put(item,"origin","custom");put(item,"name",args.getProperty("name",part->name));put(item,"category",args.getProperty("category","Custom/Parts"));put(item,"description",args["description"]);put(item,"properties",juce::Array<juce::var>());
            if(const auto* source=document.componentFor(feature)){auto inherited=clone(*source);put(item,"properties",inherited["properties"]);for(const auto* key:{"manufacturer","model","partNumber","sourceUrl","massKg","engineering","engineeringAssetRoot"})if(inherited.hasProperty(key))put(item,key,inherited[key]);}
            auto provenance=object();put(provenance,"documentModelId",document.mechanicsData().modelId);put(provenance,"feature",feature);put(provenance,"revision",(juce::int64)document.revision());put(provenance,"file",document.file().getFileName());put(item,"provenance",provenance);
            auto geometry=object();put(geometry,"kind","brep");put(geometry,"level","custom");put(geometry,"label","User-designed solid; exact evaluated BRep snapshot");
            if(const auto* inherited=document.componentFor(feature))
            {const auto level=(*inherited)["geometry"]["level"].toString();if(level=="envelope"||level=="simplified"){put(geometry,"level",level);put(geometry,"label","Frozen source "+level+"; exact BRep snapshot retains its original approximation");}}
            const auto placement=mech::frameFromEulerDegrees(part->x,part->y,part->z,part->rx,part->ry,part->rz).toTrsf();put(geometry,"data",encodeShape(BRepBuilderAPI_Transform(part->shape,placement.Inverted(),true).Shape()));put(item,"geometry",geometry);
            if(const auto* assignment=document.materialsData().assignmentFor(feature))if(const auto* material=document.materialsData().find(assignment->materialId))put(item,"material",matl::materialToJson(*material));
            juce::var report;ok=validate(item,error)&&importRecords({item},false,report,error);if(ok)identity=item["id"].toString();put(response,"import",report);
        }
    }
    else if(ok)
    {
        const auto* found=find(args["component"].toString());if(!found){ok=false;error="Select a library component.";}
        else if(action=="engineering")
        {
            auto interpreted=engineering::interpret(*found);put(response,"engineering",interpreted);ok=(bool)interpreted["ok"];
            if(!ok)error=interpreted["error"].toString();
            const auto root=args.getProperty("path",(*found)["engineeringAssetRoot"]).toString();
            if(ok&&(bool)args["verifyAssets"])
            {
                ok=juce::File::isAbsolutePath(root)&&engineering::verifyAssets(*found,juce::File(root),error);
                if(!ok&&error.isEmpty())error="Supply the absolute directory containing the portable library and original assets.";
                put(response,"assetsVerified",ok);
            }
            put(response,"assetRoot",root);
        }
        else if(action=="insert")
        {
            CadFrame placement;const auto p=args["placement"];
            if(!p.isVoid())
            {
                if(!p.isObject()){ok=false;error="Placement must contain numeric mm/degree coordinates.";}
                for(const auto* key:{"x","y","z","rx","ry","rz"})if(p.hasProperty(key)&&(!(p[key].isDouble()||p[key].isInt()||p[key].isInt64())||!std::isfinite((double)p[key])||std::abs((double)p[key])>1e9)){ok=false;error="Placement coordinates must be finite and within +/- 1e9 (mm/degrees).";}
                if(ok)placement=mech::frameFromEulerDegrees((double)p["x"],(double)p["y"],(double)p["z"],(double)p["rx"],(double)p["ry"],(double)p["rz"]);
            }
            auto record=clone(*found);
            if(craft::isStock(record))
            {
                put(record,"templateId",record["id"]);put(record,"templateRevision",record["revision"]);put(record,"templateGeometry",clone(record["geometry"]));put(record,"authoredRevision",1);
                if(args.hasProperty("geometry"))put(record,"geometry",clone(args["geometry"]));
                if(args.hasProperty("material"))put(record,"material",clone(args["material"]));
                if(!craft::isStock(record)||record["geometry"]["profile"]!=(*found)["geometry"]["profile"]){ok=false;error="Stock insertion must preserve its template profile.";}
            }
            if(ok)ok=document.insertComponent(record,placement,identity,error);if(ok)put(response,"definitions",document.componentData());
        }
        else if(action=="update"||action=="attachModel"||action=="updateStock"||action=="duplicateStock")
        {
            auto item=clone(*found);if(action=="attachModel")
            {const auto path=args["path"].toString();ok=juce::File::isAbsolutePath(path)&&attachModel(item,juce::File(path),error);if(!ok&&error.isEmpty())error="Choose an absolute local CAD path.";}
            else if(action=="updateStock"||action=="duplicateStock")
            {
                if(!craft::isStock(item)){ok=false;error="Select a craft stock template.";}
                else
                {
                    if(args.hasProperty("geometry"))put(item,"geometry",clone(args["geometry"]));
                    if(!craft::isStock(item)||item["geometry"]["profile"]!=(*found)["geometry"]["profile"]){ok=false;error="A template edit must preserve its stock profile.";}
                    if(args.hasProperty("material"))
                    {CadMaterial m;ok=ok&&matl::materialFromJson(args["material"],m,error)&&matl::validateMaterial(m,error);if(ok)put(item,"material",clone(args["material"]));}
                    if(action=="duplicateStock"){put(item,"id","craft-user-"+juce::Uuid().toDashedString());put(item,"name",item["name"].toString()+" copy");}
                }
            }
            else for(const auto* key:{"name","category","description"})if(args["definition"].hasProperty(key))put(item,key,args["definition"][key]);
            if(ok){put(item,"revision",(int)(*found)["revision"]+1);put(item,"edited",true);ok=validate(item,error);}
            if(ok){auto next=entries;if(action=="duplicateStock")next.add(item);else for(auto& record:next)if(record["id"]==item["id"])record=item;ok=save(next,error);if(ok){entries=std::move(next);identity=item["id"].toString();}}
        }
        else {ok=false;error="Unknown component action: "+action;}
    }
    put(response,"ok",ok);if(!ok)put(response,"error",error);if(identity.isNotEmpty())put(response,"id",identity);return response;
}
juce::var CadCommandService::componentCommand(const juce::var& args)
{
    auto request=clone(args);juce::String error;
    if(args["action"].toString()=="insert"&&componentLibrary.ensureLoaded(error))
        if(const auto* entry=componentLibrary.find(args["component"].toString());entry&&craft::isStock(*entry)&&!args.hasProperty("material"))
        {
            if(entry->hasProperty("material"))put(request,"material",clone((*entry)["material"]));
            else if(engineeringCatalog.ensureLoaded(error))
            {
                if(const auto* material=engineeringCatalog.materialsData().find((*entry)["materialCatalogId"].toString()))put(request,"material",matl::materialToJson(*material));
                else error="Craft template references an unavailable engineering material.";
            }
            if(error.isNotEmpty()){auto r=object();put(r,"ok",false);put(r,"error",error);return r;}
        }
    return componentLibrary.command(request,model);
}
}
