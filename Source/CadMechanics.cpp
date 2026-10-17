#include "CadMechanics.h"

#include <gp_Ax1.hxx>
#include <gp_EulerSequence.hxx>
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
const CadCoupling* CadMechanics::findCoupling(const juce::String& id) const { for (const auto& c : couplings) if (c.id == id) return &c; return nullptr; }
const CadSignalMapping* CadMechanics::mappingForJoint(const juce::String& jointId) const { for (const auto& m : mappings) if (m.jointId == jointId) return &m; return nullptr; }
bool CadMechanics::instanceHasJoints(const juce::String& id) const { for (const auto& j : joints) if (j.parentInstance == id || j.childInstance == id) return true; return false; }

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

// Orientation entry for the UI: extrinsic rotations about world X, then Y, then Z, in degrees.
CadFrame frameFromEulerDegrees(double x, double y, double z, double rx, double ry, double rz)
{
    const auto toRad = juce::MathConstants<double>::pi / 180.0;
    gp_Quaternion q;
    q.SetEulerAngles(gp_Extrinsic_XYZ, rx * toRad, ry * toRad, rz * toRad);
    gp_Trsf t; t.SetRotation(q); t.SetTranslationPart(gp_Vec(x, y, z));
    return CadFrame::fromTrsf(t);
}

void eulerDegreesFromFrame(const CadFrame& frame, double& rx, double& ry, double& rz)
{
    const auto toDeg = 180.0 / juce::MathConstants<double>::pi;
    gp_Quaternion(frame.qx, frame.qy, frame.qz, frame.qw).GetEulerAngles(gp_Extrinsic_XYZ, rx, ry, rz);
    rx *= toDeg; ry *= toDeg; rz *= toDeg;
    for (auto* v : { &rx, &ry, &rz }) if (std::abs(*v) < 1.0e-9) *v = 0.0;
}

double couplingFactor(const CadCoupling& c)
{
    if (c.type == "rackPinion") return c.pitchRadius;
    if (c.type == "screw") return c.lead / (2.0 * juce::MathConstants<double>::pi);
    return c.ratio;
}

bool validateCoupling(const CadMechanics& model, const CadCoupling& c, juce::String& error)
{
    const auto label = c.name.isNotEmpty() ? c.name : juce::String("Coupling");
    const auto* a = model.findJoint(c.jointA);
    const auto* b = model.findJoint(c.jointB);
    if (a == nullptr || b == nullptr) { error = label + ": choose two existing joints."; return false; }
    if (c.jointA == c.jointB) { error = label + ": a coupling needs two different joints."; return false; }
    const bool ok = (c.type == "gear" && a->type == "revolute" && b->type == "revolute")
                 || (c.type == "linear" && a->type == "prismatic" && b->type == "prismatic")
                 || ((c.type == "rackPinion" || c.type == "screw") && a->type == "revolute" && b->type == "prismatic");
    if (!ok)
    {
        error = label + ": " + (c.type == "gear" ? "a gear coupling joins two revolute joints."
                              : c.type == "linear" ? "a linear coupling joins two prismatic joints."
                              : c.type == "rackPinion" || c.type == "screw" ? "the first joint must be revolute (pinion/screw) and the second prismatic."
                              : "unknown coupling type '" + c.type + "'.");
        return false;
    }
    const auto k = couplingFactor(c);
    if (!std::isfinite(k) || k == 0.0 || !std::isfinite(c.offset)) { error = label + ": the ratio, pitch radius or lead must be finite and non-zero, and the offset finite."; return false; }
    return true;
}

// Checks references and numbers. Closed loops and several joints on one instance are valid mechanisms.
bool validateModel(const CadMechanics& model, juce::String& error)
{
    std::set<juce::String> ids;
    for (const auto& instance : model.instances) if (!ids.insert(instance.id).second) { error = "Duplicate instance id " + instance.id + "."; return false; }
    for (const auto& joint : model.joints)
    {
        if (!ids.insert(joint.id).second) { error = "Duplicate id " + joint.id + "."; return false; }
        if (joint.type != "fixed" && joint.type != "revolute" && joint.type != "prismatic") { error = joint.name + ": unsupported joint type '" + joint.type + "'."; return false; }
        if (model.findInstance(joint.childInstance) == nullptr) { error = joint.name + ": child instance '" + joint.childInstance + "' does not exist."; return false; }
        if (joint.parentInstance.isNotEmpty() && model.findInstance(joint.parentInstance) == nullptr) { error = joint.name + ": parent instance '" + joint.parentInstance + "' does not exist."; return false; }
        if (joint.parentInstance == joint.childInstance) { error = joint.name + ": parent and child must be different instances."; return false; }
        if (joint.hasLimits && !(joint.lower <= joint.upper)) { error = joint.name + ": lower limit must not exceed upper limit."; return false; }
        if (!std::isfinite(joint.reference) || !std::isfinite(joint.lockValue) || !std::isfinite(joint.lower) || !std::isfinite(joint.upper)) { error = joint.name + ": coordinates and limits must be finite."; return false; }
        if (joint.role != "driver" && joint.role != "dependent" && joint.role != "locked") { error = joint.name + ": role must be driver, dependent or locked."; return false; }
        if (joint.enforceLimits && !joint.hasLimits) { error = joint.name + ": hard bounds need limits."; return false; }
    }
    for (const auto& c : model.couplings)
    {
        if (!ids.insert(c.id).second) { error = "Duplicate id " + c.id + "."; return false; }
        if (!validateCoupling(model, c, error)) return false;
    }
    return true;
}

bool validateMapping(const CadMechanics& model, const CadSignalMapping& mapping, juce::String& error)
{
    const auto* joint = model.findJoint(mapping.jointId);
    if (joint == nullptr) { error = "Mapping refers to unknown joint '" + mapping.jointId + "'."; return false; }
    if (joint->type == "fixed") { error = joint->name + " is fixed and cannot be driven."; return false; }
    if (joint->role != "driver") { error = joint->name + " is a " + joint->role + " coordinate; only driver coordinates can be mapped to DEWB signals."; return false; }
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

// Schema 1 positioned joint children through a single-parent tree. Used only to migrate those documents.
std::map<juce::String, gp_Trsf> legacyReferencePlacements(const CadMechanics& model)
{
    std::map<juce::String, gp_Trsf> world;
    std::set<juce::String> active;
    std::function<gp_Trsf(const juce::String&)> resolve = [&](const juce::String& id) -> gp_Trsf
    {
        if (const auto found = world.find(id); found != world.end()) return found->second;
        gp_Trsf result;
        const CadJoint* parentJoint = nullptr;
        for (const auto& j : model.joints) if (j.childInstance == id) { parentJoint = &j; break; }
        if (parentJoint != nullptr && active.insert(id).second)
        {
            const auto parent = parentJoint->parentInstance.isEmpty() ? gp_Trsf() : resolve(parentJoint->parentInstance);
            result = parent.Multiplied(parentJoint->parentToJoint.toTrsf()).Multiplied(parentJoint->jointToChild.toTrsf());
        }
        else if (const auto* instance = model.findInstance(id)) result = instance->placement.toTrsf();
        world[id] = result;
        return result;
    };
    for (const auto& instance : model.instances) resolve(instance.id);
    return world;
}
}

juce::var toJson(const CadMechanics& model)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("schemaVersion", CadMechanics::kSchemaVersion);
    root->setProperty("lengthUnit", "mm");
    root->setProperty("modelId", model.modelId);
    root->setProperty("interfaceRevision", model.interfaceRevision);
    root->setProperty("profileRevision", model.profileRevision);
    juce::Array<juce::var> instances, joints, couplings, mappings;
    for (const auto& i : model.instances)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("id", i.id); o->setProperty("name", i.name); o->setProperty("featureId", i.featureId); o->setProperty("placement", frameToJson(i.placement, 1.0));
        o->setProperty("grounded", i.grounded);
        instances.add(juce::var(o.release()));
    }
    for (const auto& j : model.joints)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("id", j.id); o->setProperty("name", j.name); o->setProperty("type", j.type);
        o->setProperty("parentInstance", j.parentInstance); o->setProperty("childInstance", j.childInstance);
        o->setProperty("parentToJoint", frameToJson(j.parentToJoint, 1.0)); o->setProperty("jointToChild", frameToJson(j.jointToChild, 1.0));
        o->setProperty("reference", j.reference); o->setProperty("hasLimits", j.hasLimits); o->setProperty("lower", j.lower); o->setProperty("upper", j.upper);
        o->setProperty("role", j.role); o->setProperty("lockValue", j.lockValue); o->setProperty("enforceLimits", j.enforceLimits);
        joints.add(juce::var(o.release()));
    }
    for (const auto& c : model.couplings)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("id", c.id); o->setProperty("name", c.name); o->setProperty("type", c.type); o->setProperty("jointA", c.jointA); o->setProperty("jointB", c.jointB);
        o->setProperty("ratio", c.ratio); o->setProperty("pitchRadius", c.pitchRadius); o->setProperty("lead", c.lead); o->setProperty("offset", c.offset);
        couplings.add(juce::var(o.release()));
    }
    for (const auto& m : model.mappings)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("jointId", m.jointId); o->setProperty("signalId", m.signalId); o->setProperty("sourceQuantity", m.sourceQuantity); o->setProperty("sourceUnit", m.sourceUnit);
        o->setProperty("scale", m.scale); o->setProperty("offset", m.offset); o->setProperty("initialCoordinate", m.initialCoordinate);
        mappings.add(juce::var(o.release()));
    }
    root->setProperty("instances", instances); root->setProperty("joints", joints); root->setProperty("couplings", couplings); root->setProperty("mappings", mappings);
    return juce::var(root.release());
}

// Migration policy for schema 1 (independent-joint documents): each instance that was a tree root becomes
// grounded at its placement, each joint child's placement becomes its former chain-derived reference pose,
// and every movable joint becomes a driver. The resulting mechanism behaves exactly as before.
bool fromJson(const juce::var& json, CadMechanics& model, juce::String& error)
{
    CadMechanics loaded;
    if (!json.isObject()) { error = "Mechanical data is invalid."; return false; }
    if (json.getProperty("lengthUnit", "mm").toString() != "mm") { error = "Mechanical data must use millimetres."; return false; }
    const int schema = (int) json.getProperty("schemaVersion", 1);
    if (schema > CadMechanics::kSchemaVersion) { error = "Mechanical data uses schema " + juce::String(schema) + "; this DjehutiCAD reads up to schema " + juce::String(CadMechanics::kSchemaVersion) + "."; return false; }
    loaded.modelId = json.getProperty("modelId", "").toString();
    loaded.interfaceRevision = (int) json.getProperty("interfaceRevision", 0);
    loaded.profileRevision = (int) json.getProperty("profileRevision", 0);
    if (const auto* list = json.getProperty("instances", {}).getArray())
        for (const auto& v : *list)
        {
            CadMechanicalInstance i; i.id = v.getProperty("id", "").toString(); i.name = v.getProperty("name", "").toString(); i.featureId = v.getProperty("featureId", "").toString();
            i.grounded = (bool) v.getProperty("grounded", false);
            if (i.id.isEmpty() || !frameFromJson(v.getProperty("placement", {}), i.placement, error)) { if (error.isEmpty()) error = "Instance entry is missing its id."; return false; }
            loaded.instances.push_back(i);
        }
    if (const auto* list = json.getProperty("joints", {}).getArray())
        for (const auto& v : *list)
        {
            CadJoint j; j.id = v.getProperty("id", "").toString(); j.name = v.getProperty("name", "").toString(); j.type = v.getProperty("type", "").toString();
            j.parentInstance = v.getProperty("parentInstance", "").toString(); j.childInstance = v.getProperty("childInstance", "").toString();
            j.reference = v.getProperty("reference", 0.0); j.hasLimits = (bool) v.getProperty("hasLimits", false); j.lower = v.getProperty("lower", 0.0); j.upper = v.getProperty("upper", 0.0);
            j.role = v.getProperty("role", "driver").toString(); j.lockValue = v.getProperty("lockValue", 0.0); j.enforceLimits = (bool) v.getProperty("enforceLimits", false);
            if (j.id.isEmpty() || !frameFromJson(v.getProperty("parentToJoint", {}), j.parentToJoint, error) || !frameFromJson(v.getProperty("jointToChild", {}), j.jointToChild, error)) { if (error.isEmpty()) error = "Joint entry is missing its id."; return false; }
            loaded.joints.push_back(j);
        }
    if (const auto* list = json.getProperty("couplings", {}).getArray())
        for (const auto& v : *list)
        {
            CadCoupling c; c.id = v.getProperty("id", "").toString(); c.name = v.getProperty("name", "").toString(); c.type = v.getProperty("type", "").toString();
            c.jointA = v.getProperty("jointA", "").toString(); c.jointB = v.getProperty("jointB", "").toString();
            c.ratio = v.getProperty("ratio", 1.0); c.pitchRadius = v.getProperty("pitchRadius", 0.01); c.lead = v.getProperty("lead", 0.001); c.offset = v.getProperty("offset", 0.0);
            if (c.id.isEmpty()) { error = "Coupling entry is missing its id."; return false; }
            loaded.couplings.push_back(c);
        }
    if (const auto* list = json.getProperty("mappings", {}).getArray())
        for (const auto& v : *list)
        {
            CadSignalMapping m; m.jointId = v.getProperty("jointId", "").toString(); m.signalId = v.getProperty("signalId", "").toString();
            m.sourceQuantity = v.getProperty("sourceQuantity", "").toString(); m.sourceUnit = v.getProperty("sourceUnit", "").toString();
            m.scale = v.getProperty("scale", 1.0); m.offset = v.getProperty("offset", 0.0); m.initialCoordinate = v.getProperty("initialCoordinate", 0.0);
            loaded.mappings.push_back(m);
        }
    if (schema < 2)
    {
        const auto placements = legacyReferencePlacements(loaded);
        std::set<juce::String> children;
        for (const auto& j : loaded.joints) children.insert(j.childInstance);
        for (auto& i : loaded.instances)
        {
            if (const auto found = placements.find(i.id); found != placements.end()) i.placement = CadFrame::fromTrsf(found->second);
            i.grounded = children.count(i.id) == 0;
        }
        for (auto& j : loaded.joints) j.role = "driver";
    }
    if (!validateModel(loaded, error)) return false;
    model = std::move(loaded);
    return true;
}
}
}
