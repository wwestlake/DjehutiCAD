#include "CadDocument.h"
#include "CadCraftStock.h"
#include <TDataStd_AsciiString.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <set>
#include <cmath>
#include <algorithm>

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
    auto snapshot=clone(record);
    if(snapshot["geometry"]["kind"].toString()=="brep"&&snapshot["geometry"]["data"].toString().isEmpty())
    {
        if(snapshot["geometry"]["sourcePath"].toString().isNotEmpty())
        {
            const auto asset=components::resolveAssetFile(snapshot,snapshot["geometry"]["sourcePath"].toString());
            if(!asset.existsAsFile()||!components::attachModel(snapshot,asset,error)){if(error.isEmpty())error="Missing associated manufacturer CAD source.";return false;}
        }
        else if(snapshot["geometry"]["drawingReconstruction"].toString().isNotEmpty())
        {
            const auto built=components::shape(snapshot,error);if(built.IsNull())return false;
            auto geom=snapshot["geometry"];put(geom,"data",components::encodeShape(built));
        }
    }
    const auto stamp=digest(juce::JSON::toString(snapshot));
    return mutate([&](juce::String& e)
    {
        juce::String featureId;
        // Each craft insertion is an independent piece. createInstance() remains the explicit linked-copy path.
        if(!craft::isStock(record))for(const auto& d:componentDefinitions)if(digest(juce::JSON::toString(d.record))==stamp&&find(d.featureId)){featureId=d.featureId;break;}
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
bool CadDocument::updateCraftStock(const juce::String& feature, const juce::var& geometry, juce::String& error)
{
    const auto* existing = componentFor(feature);
    if (!existing || !craft::isStock(*existing)) { error = "Select a parametric craft stock piece."; return false; }
    if (!craft::validateGeometry(geometry, error)) return false;
    if (geometry["profile"] != (*existing)["geometry"]["profile"]) { error = "Stock editing preserves its profile; insert a different template to change profiles."; return false; }
    const auto authored = clone(geometry);
    return mutate([&](juce::String& e)
    {
        for (auto& definition : componentDefinitions) if (definition.featureId == feature)
        {
            put(definition.record, "geometry", authored);
            put(definition.record, "authoredRevision", (int)definition.record.getProperty("authoredRevision", 1) + 1);
            break;
        }
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        reevaluateBondedJoints(e);
        return true;
    }, error);
}
bool CadDocument::addCraftCut(const juce::String& feature, const juce::var& cutSpec, juce::String& error)
{
    const auto* existing = componentFor(feature);
    if (!existing || !craft::isStock(*existing)) { error = "Select a parametric craft stock piece."; return false; }
    auto nextGeometry = clone((*existing)["geometry"]);
    juce::Array<juce::var> cuts;
    if (const auto* existingCuts = nextGeometry.getProperty("cuts", {}).getArray())
        cuts = *existingCuts;
    cuts.add(clone(cutSpec));
    put(nextGeometry, "cuts", cuts);
    return updateCraftStock(feature, nextGeometry, error);
}
bool CadDocument::clearCraftCuts(const juce::String& feature, juce::String& error)
{
    const auto* existing = componentFor(feature);
    if (!existing || !craft::isStock(*existing)) { error = "Select a parametric craft stock piece."; return false; }
    auto nextGeometry = clone((*existing)["geometry"]);
    nextGeometry.getDynamicObject()->removeProperty("cuts");
    return updateCraftStock(feature, nextGeometry, error);
}
bool CadDocument::alignInstances(const juce::String& targetInstance, const juce::String& sourceInstance, const juce::String& alignType, const juce::var& options, juce::String& error)
{
    const auto* instA = mechanics.findInstance(targetInstance);
    if (!instA) { error = "Target instance not found: " + targetInstance; return false; }
    const auto* instB = mechanics.findInstance(sourceInstance);
    if (!instB) { error = "Source instance not found: " + sourceInstance; return false; }

    const auto* featA = find(instA->featureId);
    const auto* featB = find(instB->featureId);
    if (!featA || featA->shape.IsNull()) { error = "Target instance geometry not evaluated."; return false; }
    if (!featB || featB->shape.IsNull()) { error = "Source instance geometry not evaluated."; return false; }

    Bnd_Box bA, bB;
    BRepBndLib::Add(featA->shape, bA);
    BRepBndLib::Add(featB->shape, bB);
    Standard_Real ax1, ay1, az1, ax2, ay2, az2;
    Standard_Real bx1, by1, bz1, bx2, by2, bz2;
    bA.Get(ax1, ay1, az1, ax2, ay2, az2);
    bB.Get(bx1, by1, bz1, bx2, by2, bz2);

    const double dx = options.getProperty("dx", 0.0);
    const double dy = options.getProperty("dy", 0.0);
    const double dz = options.getProperty("dz", 0.0);

    const gp_Trsf TA = instA->placement.toTrsf();
    gp_Trsf TB_new;

    if (alignType == "butt")
    {
        gp_Trsf local;
        local.SetTranslation(gp_Vec(ax2 - bx1 + dx, ay1 - by1 + dy, az1 - bz1 + dz));
        TB_new = TA * local;
    }
    else if (alignType == "corner90")
    {
        gp_Trsf rot;
        rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), juce::MathConstants<double>::halfPi);
        gp_Trsf tr;
        tr.SetTranslation(gp_Vec(ax2 + dx, ay1 + dy, az1 + dz));
        TB_new = TA * tr * rot;
    }
    else if (alignType == "corner90_neg")
    {
        gp_Trsf rot;
        rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), -juce::MathConstants<double>::halfPi);
        gp_Trsf tr;
        tr.SetTranslation(gp_Vec(ax2 + dx, ay1 + dy, az1 + dz));
        TB_new = TA * tr * rot;
    }
    else if (alignType == "stack")
    {
        gp_Trsf local;
        local.SetTranslation(gp_Vec(ax1 - bx1 + dx, ay1 - by1 + dy, az2 - bz1 + dz));
        TB_new = TA * local;
    }
    else if (alignType == "flush")
    {
        gp_Trsf local;
        local.SetTranslation(gp_Vec(dx, ay1 - by1 + dy, az1 - bz1 + dz));
        TB_new = TA * local;
    }
    else if (alignType == "relative")
    {
        const double rx = options.getProperty("rx", 0.0);
        const double ry = options.getProperty("ry", 0.0);
        const double rz = options.getProperty("rz", 0.0);
        const CadFrame localFrame = mech::frameFromEulerDegrees(dx, dy, dz, rx, ry, rz);
        TB_new = TA * localFrame.toTrsf();
    }
    else
    {
        error = "Unknown alignment type: " + alignType;
        return false;
    }

    const CadFrame newPlacement = CadFrame::fromTrsf(TB_new);
    return setInstancePlacement(sourceInstance, newPlacement, error);
}
bool CadDocument::createAssemblyGroup(const juce::String& name, const std::vector<juce::String>& instanceIds, juce::String& groupId, juce::String& error)
{
    if (instanceIds.empty()) { error = "Select at least one piece to form an assembly."; return false; }
    return mutate([&](juce::String& e)
    {
        CadAssemblyGroup group;
        group.id = newId("asm", nextNumber++);
        group.name = name.isNotEmpty() ? name : ("Assembly " + group.id);
        for (const auto& id : instanceIds)
        {
            if (mechanics.findInstance(id) == nullptr) { e = "Unknown instance: " + id; return false; }
            group.instanceIds.push_back(id);
        }
        mechanics.assemblies.push_back(group);
        groupId = group.id;
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        return true;
    }, error);
}
bool CadDocument::addToAssemblyGroup(const juce::String& groupId, const std::vector<juce::String>& instanceIds, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& g : mechanics.assemblies)
        {
            if (g.id == groupId)
            {
                for (const auto& id : instanceIds)
                {
                    if (mechanics.findInstance(id) == nullptr) { e = "Unknown instance: " + id; return false; }
                    if (std::find(g.instanceIds.begin(), g.instanceIds.end(), id) == g.instanceIds.end())
                        g.instanceIds.push_back(id);
                }
                ++mechanics.interfaceRevision; ++mechanics.profileRevision;
                return true;
            }
        }
        e = "Assembly group not found: " + groupId;
        return false;
    }, error);
}
bool CadDocument::removeFromAssemblyGroup(const juce::String& groupId, const std::vector<juce::String>& instanceIds, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& g : mechanics.assemblies)
        {
            if (g.id == groupId)
            {
                for (const auto& id : instanceIds)
                {
                    g.instanceIds.erase(std::remove(g.instanceIds.begin(), g.instanceIds.end(), id), g.instanceIds.end());
                }
                ++mechanics.interfaceRevision; ++mechanics.profileRevision;
                return true;
            }
        }
        e = "Assembly group not found: " + groupId;
        return false;
    }, error);
}
bool CadDocument::moveAssemblyGroup(const juce::String& groupId, const CadFrame& deltaPlacement, juce::String& error)
{
    const auto* group = mechanics.findAssembly(groupId);
    if (!group) { error = "Assembly group not found: " + groupId; return false; }
    const gp_Trsf delta = deltaPlacement.toTrsf();
    return mutate([&](juce::String& e)
    {
        const auto* g = mechanics.findAssembly(groupId);
        if (!g) return false;
        for (const auto& id : g->instanceIds)
        {
            for (auto& inst : mechanics.instances)
            {
                if (inst.id == id)
                {
                    const gp_Trsf cur = inst.placement.toTrsf();
                    const gp_Trsf moved = delta * cur;
                    inst.placement = CadFrame::fromTrsf(moved);
                    break;
                }
            }
        }
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        reevaluateBondedJoints(e);
        return true;
    }, error);
}
juce::var CadDocument::generateCutList() const
{
    return craft::cutList(*this);
}
bool CadDocument::createBondedJoint(const CadBondedJoint& spec, juce::String& jointId, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        CadBondedJoint joint = spec;
        joint.id = newId("bond", nextNumber++);
        if (joint.name.isEmpty()) joint.name = "Bond " + joint.id;
        craft::evaluateBondedJoint(*this, joint);
        if (joint.createBeadSolid && joint.type == "fillet" && joint.status == "valid")
        {
            juce::String solidErr;
            const TopoDS_Shape bead = craft::createFilletBeadSolid(*this, joint, solidErr);
            if (!bead.IsNull())
            {
                joint.beadFeatureId = "bead-" + joint.id;
#if 0
                feat.id = newId("feat", nextNumber++);
                feat.name = joint.name + " Bead";
                feat.shape = bead;
                feat.type = "filletBead";
                items.push_back(feat);
#endif
            }
        }
        mechanics.bondedJoints.push_back(joint);
        jointId = joint.id;
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        return true;
    }, error);
}
bool CadDocument::updateBondedJoint(const juce::String& jointId, const CadBondedJoint& spec, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& j : mechanics.bondedJoints)
        {
            if (j.id == jointId)
            {
                j.name = spec.name;
                j.adhesiveId = spec.adhesiveId;
                j.type = spec.type;
                j.bondThicknessMm = spec.bondThicknessMm;
                j.beadLegMm = spec.beadLegMm;
                j.surfaceA = spec.surfaceA;
                j.surfaceB = spec.surfaceB;
                craft::evaluateBondedJoint(*this, j);
                ++mechanics.interfaceRevision; ++mechanics.profileRevision;
                return true;
            }
        }
        e = "Bonded joint not found: " + jointId;
        return false;
    }, error);
}
bool CadDocument::removeBondedJoint(const juce::String& jointId, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto it = mechanics.bondedJoints.begin(); it != mechanics.bondedJoints.end(); ++it)
        {
            if (it->id == jointId)
            {
                if (false && it->beadFeatureId.isNotEmpty())
                {
                    items.erase(std::remove_if(items.begin(), items.end(), [&](const CadFeature& f) { return f.id == it->beadFeatureId; }), items.end());
                }
                mechanics.bondedJoints.erase(it);
                ++mechanics.interfaceRevision; ++mechanics.profileRevision;
                return true;
            }
        }
        e = "Bonded joint not found: " + jointId;
        return false;
    }, error);
}
bool CadDocument::reevaluateBondedJoints(juce::String& error)
{
    for (auto& j : mechanics.bondedJoints)
    {
        craft::evaluateBondedJoint(*this, j);
    }
    ++mechanics.interfaceRevision; ++mechanics.profileRevision;
    return true;
}
bool CadDocument::moveBondedGroup(const juce::String& instanceId, const CadFrame& deltaPlacement, juce::String& error)
{
    const auto connectedIds = craft::getConnectedBondedGroup(*this, instanceId);
    if (connectedIds.empty()) { error = "Instance not found: " + instanceId; return false; }
    const gp_Trsf delta = deltaPlacement.toTrsf();
    return mutate([&](juce::String& e)
    {
        for (const auto& id : connectedIds)
        {
            for (auto& inst : mechanics.instances)
            {
                if (inst.id == id)
                {
                    const gp_Trsf cur = inst.placement.toTrsf();
                    const gp_Trsf moved = delta * cur;
                    inst.placement = CadFrame::fromTrsf(moved);
                    break;
                }
            }
        }
        reevaluateBondedJoints(e);
        return true;
    }, error);
}
juce::var CadDocument::adhesiveBom() const
{
    return craft::adhesiveBom(*this);
}
}
