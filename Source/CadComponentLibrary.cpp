#include "CadComponents.h"
#include "CadDocument.h"
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
    if(!file.existsAsFile()&&!save(next,error))return false;entries=std::move(next);loaded=true;return true;
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
    else if(ok&&action=="importFile")
    {
        const auto path=args["path"].toString();juce::Array<juce::var> records;auto parsing=object(),importing=object();
        if(!juce::File::isAbsolutePath(path)){ok=false;error="Choose an absolute workbook or portable library path.";}
        else if(juce::File(path).hasFileExtension("xlsx"))ok=readWorkbook(juce::File(path),records,parsing,error);
        else ok=decode(juce::JSON::parse(juce::File(path)),records,error);
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
            if(const auto* source=document.componentFor(feature)){auto inherited=clone(*source);put(item,"properties",inherited["properties"]);for(const auto* key:{"manufacturer","model","partNumber","sourceUrl","massKg"})if(inherited.hasProperty(key))put(item,key,inherited[key]);}
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
        else if(action=="insert")
        {
            CadFrame placement;const auto p=args["placement"];
            if(!p.isVoid())
            {
                if(!p.isObject()){ok=false;error="Placement must contain numeric mm/degree coordinates.";}
                for(const auto* key:{"x","y","z","rx","ry","rz"})if(p.hasProperty(key)&&(!(p[key].isDouble()||p[key].isInt()||p[key].isInt64())||!std::isfinite((double)p[key])||std::abs((double)p[key])>1e9)){ok=false;error="Placement coordinates must be finite and within +/- 1e9 (mm/degrees).";}
                if(ok)placement=mech::frameFromEulerDegrees((double)p["x"],(double)p["y"],(double)p["z"],(double)p["rx"],(double)p["ry"],(double)p["rz"]);
            }
            if(ok)ok=document.insertComponent(*found,placement,identity,error);if(ok)put(response,"definitions",document.componentData());
        }
        else if(action=="update"||action=="attachModel")
        {
            auto item=clone(*found);if(action=="attachModel")
            {const auto path=args["path"].toString();ok=juce::File::isAbsolutePath(path)&&attachModel(item,juce::File(path),error);if(!ok&&error.isEmpty())error="Choose an absolute local CAD path.";}
            else for(const auto* key:{"name","category","description"})if(args["definition"].hasProperty(key))put(item,key,args["definition"][key]);
            if(ok){put(item,"revision",(int)(*found)["revision"]+1);put(item,"edited",true);ok=validate(item,error);}
            if(ok){auto next=entries;for(auto& record:next)if(record["id"]==item["id"])record=item;ok=save(next,error);if(ok){entries=std::move(next);identity=item["id"].toString();}}
        }
        else {ok=false;error="Unknown component action: "+action;}
    }
    put(response,"ok",ok);if(!ok)put(response,"error",error);if(identity.isNotEmpty())put(response,"id",identity);return response;
}
juce::var CadCommandService::componentCommand(const juce::var& args){return componentLibrary.command(args,model);}
}
