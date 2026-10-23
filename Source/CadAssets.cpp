#include "CadAssets.h"
#include "CadDocument.h"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <set>
namespace djehuti::cad
{
using namespace components;
CadAssetLibrary::CadAssetLibrary(juce::File target):file(target)
{
    if(file==juce::File())
    {
        const auto args=juce::JUCEApplication::getCommandLineParameterArray();const int i=args.indexOf("--asset-library");
        file=i>=0&&i+1<args.size()&&juce::File::isAbsolutePath(args[i+1])?juce::File(args[i+1]):juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DjehutiCAD/asset-library.json");
    }
}
bool CadAssetLibrary::load(juce::String& error)
{
    if(data.isObject())return true;
    if(!file.existsAsFile()){data=object();put(data,"format","djehuti-asset-library");put(data,"schemaVersion",1);auto category=object();put(category,"id","misc");put(category,"name","Miscellaneous");put(data,"categories",juce::Array<juce::var>{category});put(data,"assets",juce::Array<juce::var>());disk.clear();return true;}
    if(file.getSize()>256*1024*1024){error="Asset library exceeds the 256 MB supported file size.";return false;}
    const auto text=file.loadFileAsString();auto parsed=juce::JSON::parse(text);
    if(parsed["format"]!="djehuti-asset-library"||(int)parsed["schemaVersion"]!=1||!parsed["categories"].isArray()||!parsed["assets"].isArray()){error="Invalid Asset Library data; the existing file was not changed.";return false;}
    std::set<juce::String> categories,assets;juce::StringArray names;
    for(const auto& c:*parsed["categories"].getArray()){const auto id=c["id"].toString(),name=c["name"].toString().trim();if(id.isEmpty()||name.isEmpty()||!categories.insert(id).second||names.contains(name,true)){error="Invalid or duplicate asset category.";return false;}names.add(name);}
    for(const auto& a:*parsed["assets"].getArray())if(a["id"].toString().isEmpty()||!assets.insert(a["id"].toString()).second||a["name"].toString().trim().isEmpty()||!categories.count(a["category"].toString())||!a["payload"].isObject()){error="Asset entry is invalid or references a missing category.";return false;}
    data=parsed;disk=text;return true;
}
bool CadAssetLibrary::store(const juce::var& next,juce::String& error)
{
    if(file.existsAsFile()&&file.loadFileAsString()!=disk){error="Asset Library changed on disk. Reload before editing.";return false;}
    if(!file.getParentDirectory().createDirectory()){error="Cannot create Asset Library directory.";return false;}
    juce::TemporaryFile temp(file);const auto text=juce::JSON::toString(next,false,17);
    if(text.getNumBytesAsUTF8()>256*1024*1024){error="Asset Library would exceed 256 MB; nothing was saved.";return false;}
    if(!temp.getFile().replaceWithText(text)||!temp.overwriteTargetFileWithTemporary()){error="Unable to save Asset Library atomically.";return false;}
    disk=text;data=next;return true;
}
juce::var CadAssetLibrary::command(const juce::var& args,CadDocument& document)
{
    auto reply=object();juce::String error;const auto action=args.getProperty("action","list").toString();
    if(action=="reload")data=juce::var();bool ok=load(error);
    const auto find=[&](const juce::String& id){for(int i=0;i<data["assets"].size();++i)if(data["assets"][i]["id"].toString()==id)return i;return -1;};
    const auto categoryExists=[&](const juce::String& id){for(const auto& c:*data["categories"].getArray())if(c["id"].toString()==id)return true;return false;};
    const auto duplicate=[&](const juce::var& asset,const juce::String& exclude){for(const auto& a:*data["assets"].getArray())if(a["id"].toString()!=exclude&&a["category"]==asset["category"]&&a["name"].toString().equalsIgnoreCase(asset["name"].toString()))return true;return false;};
    if(ok&&(action=="list"||action=="reload")){}
    else if(ok&&(action=="createCategory"||action=="renameCategory"||action=="deleteCategory"))
    {
        auto next=clone(data);auto* categories=next["categories"].getArray();const auto id=args["category"].toString(),name=args["name"].toString().trim();int index=-1;
        for(int i=0;i<categories->size();++i)if(categories->getReference(i)["id"].toString()==id)index=i;
        if(action!="createCategory"&&index<0){ok=false;error="Select an existing category.";}
        if(action!="deleteCategory")
        {
            if(name.isEmpty()||name.length()>120){ok=false;error="Category name must contain 1 to 120 characters.";}
            for(int i=0;i<categories->size();++i)if(i!=index&&categories->getReference(i)["name"].toString().equalsIgnoreCase(name)){ok=false;error="Category name already exists.";}
        }
        if(ok&&action=="deleteCategory")
        {for(const auto& a:*data["assets"].getArray())if(a["category"].toString()==id){ok=false;error="Only empty categories can be deleted. Move their assets first.";}if(ok)categories->remove(index);}
        else if(ok&&action=="renameCategory")put(categories->getReference(index),"name",name);
        else if(ok){auto c=object();put(c,"id",juce::Uuid().toDashedString());put(c,"name",name);categories->add(c);put(reply,"id",c["id"]);}
        if(ok)ok=store(next,error);
    }
    else if(ok&&action=="save")
    {
        auto source=args["source"].toString();if(const auto* instance=document.mechanicsData().findInstance(source))source=instance->featureId;
        auto payload=document.assetSnapshot(source,error);ok=payload.isObject();
        auto asset=object();const auto replace=args["replace"].toString();const int replacing=find(replace);
        if(ok&&!replace.isEmpty()&&replacing<0){ok=false;error="Replacement asset no longer exists.";}
        put(asset,"id",replace.isEmpty()?juce::Uuid().toDashedString():replace);put(asset,"name",args["name"].toString().trim());put(asset,"category",args["category"]);put(asset,"description",args["description"]);put(asset,"materialLabel",args["materialLabel"]);put(asset,"type",payload["type"]);put(asset,"payload",payload);put(asset,"modified",juce::Time::getCurrentTime().toISO8601(true));
        if(ok&&(asset["name"].toString().isEmpty()||asset["name"].toString().length()>200||!categoryExists(asset["category"].toString()))){ok=false;error="Enter an asset name (1-200 characters) and an existing category.";}
        if(ok&&duplicate(asset,replace)){ok=false;error="An asset with that name already exists in this category. Rename it or explicitly replace the selected entry.";}
        if(ok)
        {
            Bnd_Box bounds;
            if(source=="assembly")for(const auto& instance:document.mechanicsData().instances){if(const auto* f=document.find(instance.featureId))if(!f->shape.IsNull())BRepBndLib::Add(BRepBuilderAPI_Transform(f->shape,document.instanceShapeTransform(f->id,instance.placement.toTrsf()),true).Shape(),bounds);}
            else if(const auto* f=document.find(source))BRepBndLib::Add(f->shape,bounds);
            if(!bounds.IsVoid()){double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);put(asset,"dimensions",juce::Array<juce::var>{X-x,Y-y,Z-z});}
            if(source!="assembly")if(const auto* assignment=document.materialsData().assignmentFor(source))if(const auto* material=document.materialsData().find(assignment->materialId))put(asset,"materialLabel",material->name);
            auto next=clone(data);if(replacing>=0)next["assets"].getArray()->set(replacing,asset);else next["assets"].getArray()->add(asset);
            ok=store(next,error);if(ok)put(reply,"id",asset["id"]);
        }
    }
    else if(ok)
    {
        const int index=find(args["asset"].toString());if(index<0){ok=false;error="Select an existing asset.";}
        else
        {
            auto asset=clone(data["assets"][index]);
            if(action=="get")put(reply,"asset",asset);
            else if(action=="insert")
            {
                CadFrame placement;const auto p=args["placement"];
                for(const auto* key:{"x","y","z","rx","ry","rz"})if(p.hasProperty(key)&&(!(p[key].isInt()||p[key].isInt64()||p[key].isDouble())||!std::isfinite((double)p[key])||std::abs((double)p[key])>1e9)){ok=false;error="Placement must be finite mm/degrees within +/-1e9.";}
                if(ok){placement=mech::frameFromEulerDegrees((double)p["x"],(double)p["y"],(double)p["z"],(double)p["rx"],(double)p["ry"],(double)p["rz"]);juce::var inserted;ok=document.insertAsset(asset["payload"],asset["name"].toString(),placement,inserted,error);if(ok)for(const auto& property:inserted.getDynamicObject()->getProperties())put(reply,property.name,property.value);}
            }
            else if(action=="update"||action=="delete")
            {
                auto next=clone(data);
                if(action=="delete")next["assets"].getArray()->remove(index);
                else
                {
                    for(const auto* key:{"name","category","description","materialLabel"})if(args["definition"].hasProperty(key))put(asset,key,args["definition"][key]);
                    put(asset,"name",asset["name"].toString().trim());
                    if(asset["name"].toString().isEmpty()||!categoryExists(asset["category"].toString())||duplicate(asset,asset["id"].toString())){ok=false;error="Asset needs a unique name and an existing category.";}
                    put(asset,"modified",juce::Time::getCurrentTime().toISO8601(true));next["assets"].getArray()->set(index,asset);
                }
                if(ok)ok=store(next,error);
            }
            else if(action=="exportSource")
            {
                const auto path=args["path"].toString();if(!juce::File::isAbsolutePath(path)||!juce::File(path).hasFileExtension("djc")){ok=false;error="Choose an absolute .djc path for the editable asset copy.";}
                else if(juce::File(path).existsAsFile()){ok=false;error="Choose a new file; existing files are not overwritten.";}
                else if(!juce::File(path).replaceWithText(juce::JSON::toString(asset["payload"]["document"],false,17))){ok=false;error="Cannot export editable source copy.";}
            }
            else{ok=false;error="Unknown asset action.";}
        }
    }
    put(reply,"ok",ok);if(!ok)put(reply,"error",error);
    if(ok)
    {
        put(reply,"categories",clone(data["categories"]));juce::Array<juce::var> records;
        for(const auto& a:*data["assets"].getArray()){auto metadata=clone(a);metadata.getDynamicObject()->removeProperty("payload");records.add(metadata);}put(reply,"assets",records);put(reply,"path",file.getFullPathName());
    }
    return reply;
}
}
