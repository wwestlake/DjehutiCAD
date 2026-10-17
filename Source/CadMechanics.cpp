#include "CadMechanics.h"

#include <gp_Ax1.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <functional>
#include <set>

namespace djehuti::cad
{
gp_Trsf CadFrame::toTrsf() const
{
    gp_Trsf transform;
    transform.SetRotation(gp_Quaternion(qx, qy, qz, qw));
    transform.SetTranslationPart(gp_Vec(x, y, z));
    return transform;
}

CadFrame CadFrame::fromTrsf(const gp_Trsf& transform)
{
    CadFrame frame;
    const auto translation = transform.TranslationPart();
    frame.x = translation.X(); frame.y = translation.Y(); frame.z = translation.Z();
    auto q = transform.GetRotation();
    q.Normalize();
    if (q.W() < 0.0) q = gp_Quaternion(-q.X(), -q.Y(), -q.Z(), -q.W());
    frame.qw = q.W(); frame.qx = q.X(); frame.qy = q.Y(); frame.qz = q.Z();
    return frame;
}

const CadMechanicalInstance* CadMechanics::findInstance(const juce::String& id) const { for (const auto& i : instances) if (i.id == id) return &i; return nullptr; }
const CadJoint* CadMechanics::findJoint(const juce::String& id) const { for (const auto& j : joints) if (j.id == id) return &j; return nullptr; }
const CadJoint* CadMechanics::jointForChild(const juce::String& instanceId) const { for (const auto& j : joints) if (j.childInstance == instanceId) return &j; return nullptr; }
const CadSignalMapping* CadMechanics::mappingForJoint(const juce::String& jointId) const { for (const auto& m : mappings) if (m.jointId == jointId) return &m; return nullptr; }

namespace mech
{
juce::String coordinateQuantity(const juce::String& type) { return type == "revolute" ? "angle" : type == "prismatic" ? "position" : ""; }
juce::String coordinateUnit(const juce::String& type) { return type == "revolute" ? "rad" : type == "prismatic" ? "m" : ""; }
bool isRateQuantity(const juce::String& quantity) { return quantity == "angular_velocity" || quantity == "linear_velocity"; }

bool quantityDrivesJoint(const juce::String& quantity, const juce::String& type)
{
    if (type == "revolute") return quantity == "angle" || quantity == "angular_velocity";
    if (type == "prismatic") return quantity == "position" || quantity == "linear_velocity";
    return false;
}

juce::StringArray unitsFor(const juce::String& quantity)
{
    if (quantity == "angle") return { "rad", "deg", "rev" };
    if (quantity == "angular_velocity") return { "rad/s", "deg/s", "rpm", "rev/s" };
    if (quantity == "position") return { "m", "mm" };
    if (quantity == "linear_velocity") return { "m/s", "mm/s" };
    return {};
}

bool unitToSI(const juce::String& quantity, const juce::String& unit, double& factor)
{
    const auto pi = juce::MathConstants<double>::pi;
    static const std::map<juce::String, double> factors {
        { "angle|rad", 1.0 }, { "angle|deg", pi / 180.0 }, { "angle|rev", 2.0 * pi },
        { "angular_velocity|rad/s", 1.0 }, { "angular_velocity|deg/s", pi / 180.0 }, { "angular_velocity|rpm", 2.0 * pi / 60.0 }, { "angular_velocity|rev/s", 2.0 * pi },
        { "position|m", 1.0 }, { "position|mm", 1.0e-3 }, { "linear_velocity|m/s", 1.0 }, { "linear_velocity|mm/s", 1.0e-3 } };
    const auto found = factors.find(quantity + "|" + unit);
    if (found == factors.end()) return false;
    factor = found->second;
    return true;
}

gp_Trsf motion(const CadJoint& joint, double coordinate)
{
    gp_Trsf result;
    const auto delta = coordinate - joint.reference;
    if (joint.type == "revolute") result.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), delta);
    else if (joint.type == "prismatic") result.SetTranslation(gp_Vec(0.0, 0.0, delta * 1000.0));   // metres -> geometry millimetres
    return result;
}

gp_Trsf frameFromOriginAxis(double ox, double oy, double oz, double ax, double ay, double az)
{
    gp_Quaternion rotation;
    rotation.SetRotation(gp_Vec(0, 0, 1), gp_Vec(ax, ay, az).Normalized());
    gp_Trsf frame;
    frame.SetRotation(rotation);
    frame.SetTranslationPart(gp_Vec(ox, oy, oz));
    return frame;
}

bool validateJointGraph(const CadMechanics& model, juce::String& error)
{
    std::set<juce::String> children;
    for (const auto& joint : model.joints)
    {
        if (joint.type != "fixed" && joint.type != "revolute" && joint.type != "prismatic") { error = joint.name + ": unsupported joint type '" + joint.type + "'."; return false; }
        if (model.findInstance(joint.childInstance) == nullptr) { error = joint.name + ": child instance '" + joint.childInstance + "' does not exist."; return false; }
        if (joint.parentInstance.isNotEmpty() && model.findInstance(joint.parentInstance) == nullptr) { error = joint.name + ": parent instance '" + joint.parentInstance + "' does not exist."; return false; }
        if (joint.parentInstance == joint.childInstance) { error = joint.name + ": parent and child must be different instances."; return false; }
        if (!children.insert(joint.childInstance).second) { error = joint.name + ": instance '" + joint.childInstance + "' already has a parent joint."; return false; }
        if (joint.hasLimits && !(joint.lower <= joint.upper)) { error = joint.name + ": lower limit must not exceed upper limit."; return false; }
        if (!std::isfinite(joint.reference)) { error = joint.name + ": reference coordinate must be finite."; return false; }
    }
    for (const auto& joint : model.joints)
    {
        std::set<juce::String> visited { joint.childInstance };
        for (auto parent = joint.parentInstance; parent.isNotEmpty();)
        {
            if (!visited.insert(parent).second) { error = joint.name + ": joint hierarchy contains a cycle."; return false; }
            const auto* up = model.jointForChild(parent);
            parent = up != nullptr ? up->parentInstance : juce::String();
        }
    }
    return true;
}

bool evaluate(const CadMechanics& model, const std::map<juce::String, double>& coordinates, std::map<juce::String, gp_Trsf>& world, juce::String& error)
{
    if (!validateJointGraph(model, error)) return false;
    world.clear();
    std::function<gp_Trsf(const juce::String&)> resolve = [&](const juce::String& id) -> gp_Trsf
    {
        if (const auto found = world.find(id); found != world.end()) return found->second;
        const auto* instance = model.findInstance(id);
        gp_Trsf result;
        if (const auto* joint = model.jointForChild(id))
        {
            const auto q = coordinates.count(joint->id) != 0 ? coordinates.at(joint->id) : joint->reference;
            const auto parent = joint->parentInstance.isEmpty() ? gp_Trsf() : resolve(joint->parentInstance);
            result = parent.Multiplied(joint->parentToJoint.toTrsf()).Multiplied(motion(*joint, q)).Multiplied(joint->jointToChild.toTrsf());
        }
        else if (instance != nullptr) result = instance->placement.toTrsf();
        world[id] = result;
        return result;
    };
    for (const auto& instance : model.instances) resolve(instance.id);
    return true;
}

bool validateMapping(const CadMechanics& model, const CadSignalMapping& mapping, juce::String& error)
{
    const auto* joint = model.findJoint(mapping.jointId);
    if (joint == nullptr) { error = "Mapping refers to unknown joint '" + mapping.jointId + "'."; return false; }
    if (joint->type == "fixed") { error = joint->name + " is fixed and cannot be driven."; return false; }
    if (mapping.signalId.trim().isEmpty()) { error = joint->name + ": choose the DEWB signal that drives it."; return false; }
    if (!quantityDrivesJoint(mapping.sourceQuantity, joint->type)) { error = joint->name + ": a " + joint->type + " joint cannot be driven by '" + mapping.sourceQuantity + "'."; return false; }
    double factor = 0.0;
    if (!unitToSI(mapping.sourceQuantity, mapping.sourceUnit, factor)) { error = joint->name + ": unit '" + mapping.sourceUnit + "' is not valid for " + mapping.sourceQuantity + "."; return false; }
    if (!std::isfinite(mapping.scale) || mapping.scale == 0.0) { error = joint->name + ": scale must be finite and non-zero."; return false; }
    if (!std::isfinite(mapping.offset) || !std::isfinite(mapping.initialCoordinate)) { error = joint->name + ": offset and initial coordinate must be finite."; return false; }
    return true;
}

juce::var frameToJson(const CadFrame& frame, double lengthScale)
{
    auto object = std::make_unique<juce::DynamicObject>();
    juce::Array<juce::var> translation { frame.x * lengthScale, frame.y * lengthScale, frame.z * lengthScale };
    auto rotation = std::make_unique<juce::DynamicObject>();
    rotation->setProperty("w", frame.qw); rotation->setProperty("x", frame.qx); rotation->setProperty("y", frame.qy); rotation->setProperty("z", frame.qz);
    object->setProperty("translation", translation);
    object->setProperty("rotation", juce::var(rotation.release()));
    return juce::var(object.release());
}

namespace
{
bool frameFromJson(const juce::var& value, CadFrame& frame, juce::String& error)
{
    const auto* translation = value.getProperty("translation", {}).getArray();
    const auto rotation = value.getProperty("rotation", {});
    if (translation == nullptr || translation->size() != 3 || !rotation.isObject()) { error = "Frame needs translation [x, y, z] and rotation {w, x, y, z}."; return false; }
    frame.x = (*translation)[0]; frame.y = (*translation)[1]; frame.z = (*translation)[2];
    frame.qw = rotation.getProperty("w", 1.0); frame.qx = rotation.getProperty("x", 0.0); frame.qy = rotation.getProperty("y", 0.0); frame.qz = rotation.getProperty("z", 0.0);
    const auto norm = std::sqrt(frame.qw * frame.qw + frame.qx * frame.qx + frame.qy * frame.qy + frame.qz * frame.qz);
    for (const auto v : { frame.x, frame.y, frame.z, norm }) if (!std::isfinite(v)) { error = "Frame values must be finite."; return false; }
    if (std::abs(norm - 1.0) > 1.0e-6) { error = "Frame rotation must be a unit quaternion."; return false; }
    frame.qw /= norm; frame.qx /= norm; frame.qy /= norm; frame.qz /= norm;
    return true;
}
}

juce::var toJson(const CadMechanics& model)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("lengthUnit", "mm");
    root->setProperty("modelId", model.modelId);
    root->setProperty("interfaceRevision", model.interfaceRevision);
    root->setProperty("profileRevision", model.profileRevision);
    juce::Array<juce::var> instances, joints, mappings;
    for (const auto& i : model.instances)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("id", i.id); o->setProperty("name", i.name); o->setProperty("featureId", i.featureId); o->setProperty("placement", frameToJson(i.placement, 1.0));
        instances.add(juce::var(o.release()));
    }
    for (const auto& j : model.joints)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("id", j.id); o->setProperty("name", j.name); o->setProperty("type", j.type);
        o->setProperty("parentInstance", j.parentInstance); o->setProperty("childInstance", j.childInstance);
        o->setProperty("parentToJoint", frameToJson(j.parentToJoint, 1.0)); o->setProperty("jointToChild", frameToJson(j.jointToChild, 1.0));
        o->setProperty("reference", j.reference); o->setProperty("hasLimits", j.hasLimits); o->setProperty("lower", j.lower); o->setProperty("upper", j.upper);
        joints.add(juce::var(o.release()));
    }
    for (const auto& m : model.mappings)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("jointId", m.jointId); o->setProperty("signalId", m.signalId); o->setProperty("sourceQuantity", m.sourceQuantity); o->setProperty("sourceUnit", m.sourceUnit);
        o->setProperty("scale", m.scale); o->setProperty("offset", m.offset); o->setProperty("initialCoordinate", m.initialCoordinate);
        mappings.add(juce::var(o.release()));
    }
    root->setProperty("instances", instances); root->setProperty("joints", joints); root->setProperty("mappings", mappings);
    return juce::var(root.release());
}

bool fromJson(const juce::var& json, CadMechanics& model, juce::String& error)
{
    CadMechanics loaded;
    if (!json.isObject()) { error = "Mechanical data is invalid."; return false; }
    if (json.getProperty("lengthUnit", "mm").toString() != "mm") { error = "Mechanical data must use millimetres."; return false; }
    loaded.modelId = json.getProperty("modelId", "").toString();
    loaded.interfaceRevision = (int) json.getProperty("interfaceRevision", 0);
    loaded.profileRevision = (int) json.getProperty("profileRevision", 0);
    if (const auto* list = json.getProperty("instances", {}).getArray())
        for (const auto& v : *list)
        {
            CadMechanicalInstance i; i.id = v.getProperty("id", "").toString(); i.name = v.getProperty("name", "").toString(); i.featureId = v.getProperty("featureId", "").toString();
            if (i.id.isEmpty() || !frameFromJson(v.getProperty("placement", {}), i.placement, error)) { if (error.isEmpty()) error = "Instance entry is missing its id."; return false; }
            loaded.instances.push_back(i);
        }
    if (const auto* list = json.getProperty("joints", {}).getArray())
        for (const auto& v : *list)
        {
            CadJoint j; j.id = v.getProperty("id", "").toString(); j.name = v.getProperty("name", "").toString(); j.type = v.getProperty("type", "").toString();
            j.parentInstance = v.getProperty("parentInstance", "").toString(); j.childInstance = v.getProperty("childInstance", "").toString();
            j.reference = v.getProperty("reference", 0.0); j.hasLimits = (bool) v.getProperty("hasLimits", false); j.lower = v.getProperty("lower", 0.0); j.upper = v.getProperty("upper", 0.0);
            if (j.id.isEmpty() || !frameFromJson(v.getProperty("parentToJoint", {}), j.parentToJoint, error) || !frameFromJson(v.getProperty("jointToChild", {}), j.jointToChild, error)) { if (error.isEmpty()) error = "Joint entry is missing its id."; return false; }
            loaded.joints.push_back(j);
        }
    if (const auto* list = json.getProperty("mappings", {}).getArray())
        for (const auto& v : *list)
        {
            CadSignalMapping m; m.jointId = v.getProperty("jointId", "").toString(); m.signalId = v.getProperty("signalId", "").toString();
            m.sourceQuantity = v.getProperty("sourceQuantity", "").toString(); m.sourceUnit = v.getProperty("sourceUnit", "").toString();
            m.scale = v.getProperty("scale", 1.0); m.offset = v.getProperty("offset", 0.0); m.initialCoordinate = v.getProperty("initialCoordinate", 0.0);
            loaded.mappings.push_back(m);
        }
    if (!validateJointGraph(loaded, error)) return false;
    model = std::move(loaded);
    return true;
}
}
}
