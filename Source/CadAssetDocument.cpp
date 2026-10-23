#include "CadDocument.h"
#include "CadComponents.h"
#include <map>
#include <set>

namespace djehuti::cad
{
using namespace components;
namespace
{
void collectStrings(const juce::var& v,juce::StringArray& strings)
{
    if(v.isString())strings.addIfNotAlreadyThere(v.toString());
    else if(auto* a=v.getArray())for(const auto& x:*a)collectStrings(x,strings);
    else if(auto* o=v.getDynamicObject())for(const auto& p:o->getProperties())collectStrings(p.value,strings);
}
void mergeArrays(juce::var& into,const juce::var& from)
{
    if(auto* a=from.getArray()){if(!into.isArray())into=juce::Array<juce::var>();for(const auto& x:*a)into.getArray()->add(clone(x));}
    else if(auto* o=from.getDynamicObject())
    {if(!into.isObject())into=object();for(const auto& p:o->getProperties()){auto value=into[p.name];if(p.value.isArray()||p.value.isObject())mergeArrays(value,p.value);else if(value.isVoid())value=clone(p.value);into.getDynamicObject()->setProperty(p.name,value);}}
}
juce::var remap(const juce::var& v,const std::map<juce::String,juce::String>& ids)
{
    if(v.isString())
    {
        const auto text=v.toString();if(const auto found=ids.find(text);found!=ids.end())return found->second;
        const int slash=text.indexOfChar('/'),colon=text.indexOfChar(':');const int cut=slash<0?colon:colon<0?slash:juce::jmin(slash,colon);
        if(cut>0)if(const auto found=ids.find(text.substring(0,cut));found!=ids.end())return found->second+text.substring(cut);return v;
    }
    if(auto* a=v.getArray()){juce::Array<juce::var> out;for(const auto& x:*a)out.add(remap(x,ids));return out;}
    if(auto* o=v.getDynamicObject()){auto out=object();for(const auto& p:o->getProperties()){const auto key=p.name.toString();const auto it=ids.find(key);put(out,it==ids.end()?key:it->second,remap(p.value,ids));}return out;}
    return v;
}
}
juce::var CadDocument::assetSnapshot(const juce::String& source,juce::String& error) const
{
    const bool assembly=source=="assembly";
    const auto* feature=find(source);
    if(!assembly&&(!feature||feature->shape.IsNull()||!TopExp_Explorer(feature->shape,TopAbs_SOLID).More())){error="Select a solid part, an occurrence, or the entire document assembly.";return {};}
    if(assembly&&mechanics.instances.empty()){error="The current document has no assembly occurrences.";return {};}
    auto document=juce::JSON::parse(serialize());
    document.getDynamicObject()->removeProperty("analysisCases");document.getDynamicObject()->removeProperty("projectKit");
    if(!assembly)
    {
        std::set<juce::String> keep{source};bool changed=true;
        while(changed)
        {
            changed=false;juce::StringArray references;
            for(const auto& f:*document["features"].getArray())if(keep.count(f["id"].toString()))
                for(const auto* key:{"firstOperand","secondOperand","sketchId","targetFeatureId","revolve","modeling","replication"})collectStrings(f[key],references);
            for(const auto& sketch:*document["sketches"].getArray())if(keep.count(sketch["id"].toString()))collectStrings(sketch["supportFeature"],references);
            for(const auto& ref:references)if(find(ref)&&keep.insert(ref).second)changed=true;
        }
        const auto filter=[&](juce::var& array,const char* key){if(auto* a=array.getArray())for(int i=a->size();--i>=0;)if(!keep.count(a->getReference(i)[key].toString()))a->remove(i);};
        auto features=document["features"];filter(features,"id");for(auto& f:*features.getArray())put(f,"visible",f["id"].toString()==source);put(document,"features",features);
        auto sketches=document["sketches"];filter(sketches,"id");put(document,"sketches",sketches);
        auto refs=document["references"];filter(refs,"ownerFeatureId");put(document,"references",refs);
        auto definitions=document["components"]["definitions"];filter(definitions,"feature");auto components=clone(document["components"]);put(components,"definitions",definitions);put(document,"components",components);
        auto materialData=clone(document["materials"]);auto assignments=materialData["assignments"];filter(assignments,"feature");if(materialData.isObject()){put(materialData,"assignments",assignments);put(document,"materials",materialData);}
        CadMechanics empty;empty.modelId=juce::Uuid().toDashedString();put(document,"mechanics",mech::toJson(empty));
    }
    CadDocument verified;if(!verified.deserialize(juce::JSON::toString(document),error))return {};
    auto payload=object();put(payload,"document",document);put(payload,"root",assembly?juce::String():source);put(payload,"type",assembly?"Assembly":"Part");return payload;
}
bool CadDocument::insertAsset(const juce::var& payload,const juce::String& name,const CadFrame& placement,juce::var& result,juce::String& error)
{
    if(payload["type"]!="Part"&&payload["type"]!="Assembly"){error="Unsupported asset type.";return false;}
    CadDocument source;if(!payload["document"].isObject()||!source.deserialize(juce::JSON::toString(payload["document"]),error)){if(error.isEmpty())error="Asset contains no valid engineering document.";return false;}
    const auto root=payload["root"].toString();
    if(payload["type"]=="Part"&&(!source.find(root)||source.find(root)->shape.IsNull())){error="Asset root part is missing.";return false;}
    for(double n:{placement.x,placement.y,placement.z,placement.qw,placement.qx,placement.qy,placement.qz})if(!std::isfinite(n)){error="Asset placement must be finite.";return false;}
    // Remap authored identities only; external catalog identities remain frozen.
    auto incoming=clone(payload["document"]);std::map<juce::String,juce::String> ids;int number=nextNumber;
    const auto add=[&](const juce::String& id){if(id.isNotEmpty()&&!ids.count(id))ids[id]=id.upToFirstOccurrenceOf("-",false,false)+"-"+juce::Uuid().toDashedString().substring(0,8)+"-"+juce::String(number++);};
    for(const auto& f:source.items)add(f.id);
    for(const auto& sketch:source.sketches){for(const auto& entity:sketch.entities){add(entity.id);add(entity.groupId);}for(const auto& constraint:sketch.constraints)add(constraint.id);}
    for(const auto* key:{"instances","joints","couplings","mappings","ikTasks","assemblies","adhesives","bondedJoints"})if(auto* array=incoming["mechanics"][key].getArray())for(const auto& item:*array)add(item["id"].toString());
    for(const auto& material:source.materials.materials)add(material.id);
    if(auto* array=incoming["sheetStocks"]["stocks"].getArray())for(const auto& item:*array)add(item["id"].toString());
    incoming=remap(incoming,ids);auto merged=juce::JSON::parse(serialize());
    for(const auto* key:{"features","sketches","references","materials","sheetStocks","components","mechanics"})if(incoming.hasProperty(key)){auto v=merged[key];mergeArrays(v,incoming[key]);put(merged,key,v);}
    put(merged,"nextNumber",number);
    return mutate([&](juce::String& e)
    {
        if(!deserialize(juce::JSON::toString(merged),e))return false;
        result=object();juce::Array<juce::var> inserted;
        if(payload["type"]=="Part")
        {
            CadMechanicalInstance instance;instance.id="inst-"+juce::Uuid().toDashedString();instance.name=name;instance.featureId=ids.at(root);instance.placement=placement;
            mechanics.instances.push_back(instance);inserted.add(instance.id);put(result,"id",instance.id);put(result,"feature",instance.featureId);
        }
        else
        {
            CadAssemblyGroup group;group.id="asm-"+juce::Uuid().toDashedString();group.name=name;
            for(const auto& original:source.mechanics.instances)for(auto& instance:mechanics.instances)if(instance.id==ids.at(original.id))
            {instance.placement=CadFrame::fromTrsf(placement.toTrsf()*instance.placement.toTrsf());group.instanceIds.push_back(instance.id);inserted.add(instance.id);}
            if(group.instanceIds.empty()){e="Assembly asset contains no occurrences.";return false;}
            mechanics.assemblies.push_back(group);put(result,"group",group.id);put(result,"id",group.instanceIds.front());
        }
        ++mechanics.interfaceRevision;++mechanics.profileRevision;put(result,"instances",inserted);return true;
    },error);
}
}
