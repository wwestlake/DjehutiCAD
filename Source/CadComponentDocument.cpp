#include "CadDocument.h"
#include <TDataStd_AsciiString.hxx>
#include <set>
#include <cmath>

namespace djehuti::cad
{
using namespace components;
const juce::var* CadDocument::componentFor(const juce::String& feature)const
{for(const auto& d:componentDefinitions)if(d.featureId==feature)return &d.record;return nullptr;}
juce::var CadDocument::componentData()const
{
    auto root=object();put(root,"schemaVersion",1);juce::Array<juce::var> list;
    for(const auto& d:componentDefinitions){auto v=object();put(v,"feature",d.featureId);put(v,"record",clone(d.record));list.add(v);}put(root,"definitions",list);return root;
}
bool CadDocument::readComponentData(const juce::var& root,juce::String& error)
{
    if(root.isVoid()){componentDefinitions.clear();return true;}
    if((int)root["schemaVersion"]!=1||!root["definitions"].isArray()){error="Unsupported document component definitions.";return false;}
    std::vector<CadComponentDefinition> next;std::set<juce::String> ids;
    for(const auto& item:*root["definitions"].getArray())
    {
        const auto feature=item["feature"].toString();
        if(feature.isEmpty()||!ids.insert(feature).second||!validate(item["record"],error)){if(error.isEmpty())error="Invalid document component identity.";return false;}
        next.push_back({feature,clone(item["record"])});
    }
    componentDefinitions=std::move(next);return true;
}
void CadDocument::writeComponentsToOCAF()
{
    if(componentDefinitions.empty()&&ocafDocument->Main().FindChild(componentContainerTag,false).IsNull())return;
    const auto json=juce::JSON::toString(componentData());TDataStd_AsciiString::Set(ocafDocument->Main().FindChild(componentContainerTag,true).FindChild(1,true),TCollection_AsciiString(json.toRawUTF8()));
}
bool CadDocument::restoreComponentsFromOCAF(juce::String& error)
{
    const auto label=ocafDocument->Main().FindChild(componentContainerTag,false);Handle(TDataStd_AsciiString) text;
    if(label.IsNull()||!label.FindChild(1,false).FindAttribute(TDataStd_AsciiString::GetID(),text)){componentDefinitions.clear();return true;}
    return readComponentData(juce::JSON::parse(juce::String::fromUTF8(text->Get().ToCString())),error);
}
bool CadDocument::insertComponent(const juce::var& record,const CadFrame& placement,juce::String& instanceId,juce::String& error)
{
    if(!validate(record,error))return false;
    for(double value:{placement.x,placement.y,placement.z,placement.qw,placement.qx,placement.qy,placement.qz})if(!std::isfinite(value)){error="Component placement must be finite.";return false;}
    const auto snapshot=clone(record);const auto stamp=digest(juce::JSON::toString(snapshot));
    return mutate([&](juce::String& e)
    {
        juce::String featureId;for(const auto& d:componentDefinitions)if(digest(juce::JSON::toString(d.record))==stamp&&find(d.featureId)){featureId=d.featureId;break;}
        if(featureId.isEmpty())
        {
            const auto geometry=components::shape(snapshot,e);if(geometry.IsNull())return false;
            CadFeature feature;feature.id=newId("component",nextNumber++);feature.type="component";feature.name=record["name"].toString();featureId=feature.id;items.push_back(feature);componentDefinitions.push_back({feature.id,snapshot});
            if(record.hasProperty("material"))
            {
                CadMaterial material;if(!matl::materialFromJson(record["material"],material,e)||!matl::validateMaterial(material,e))return false;
                const auto origin=material.id;juce::String materialId;
                auto expected=matl::materialToJson(material);expected.getDynamicObject()->removeProperty("id");expected.getDynamicObject()->removeProperty("catalogId");expected.getDynamicObject()->removeProperty("catalogRevision");
                for(const auto& existing:materials.materials)
                {auto actual=matl::materialToJson(existing);actual.getDynamicObject()->removeProperty("id");actual.getDynamicObject()->removeProperty("catalogId");actual.getDynamicObject()->removeProperty("catalogRevision");if(juce::JSON::toString(actual)==juce::JSON::toString(expected)){materialId=existing.id;break;}}
                if(materialId.isEmpty()){material.catalogId=origin;material.catalogRevision=material.revision;material.id="mat-"+juce::Uuid().toDashedString();materialId=material.id;materials.materials.push_back(material);}
                materials.assignments.push_back({featureId,materialId});
            }
        }
        CadMechanicalInstance instance;instance.id=newId("inst",nextNumber++);instance.name=record["name"].toString();instance.featureId=featureId;instance.placement=placement;mechanics.instances.push_back(instance);++mechanics.interfaceRevision;++mechanics.profileRevision;instanceId=instance.id;return true;
    },error);
}
}
