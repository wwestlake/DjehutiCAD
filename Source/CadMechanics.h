#pragma once

#include <JuceHeader.h>
#include <gp_Trsf.hxx>

#include <map>
#include <vector>

// Authored mechanical relationships (assembly instances, joints and the signal connection profile).
// These are document data stored in OCAF; runtime poses are derived from them and never written back.
namespace djehuti::cad
{
// A rigid frame: translation in millimetres plus a unit Hamilton quaternion (w, x, y, z). The frame maps
// coordinates expressed in itself into the frame it is stored relative to: p_outer = R(q) * p_inner + t.
struct CadFrame
{
    double x = 0.0, y = 0.0, z = 0.0;
    double qw = 1.0, qx = 0.0, qy = 0.0, qz = 0.0;
    gp_Trsf toTrsf() const;
    static CadFrame fromTrsf(const gp_Trsf& transform);
};

// An assembly occurrence of a solid feature. Its identity is independent of the feature's modeling history,
// so two instances of the same body move independently. placement is the instance frame in world millimetres
// for root instances; for a joint child the world frame is derived from the joint chain instead.
struct CadMechanicalInstance
{
    juce::String id, name, featureId;
    CadFrame placement;
};

// world(child) = world(parent) * parentToJoint * motion(q - reference) * jointToChild
// parentToJoint maps joint-frame coordinates into the parent instance frame (world when parent is empty).
// jointToChild maps child-instance-frame coordinates into the joint frame at the reference coordinate.
// motion() rotates about (revolute) or translates along (prismatic) the joint frame's +Z axis.
struct CadJoint
{
    juce::String id, name, type;      // "fixed" | "revolute" | "prismatic"
    juce::String parentInstance;      // empty = assembly/world frame
    juce::String childInstance;
    CadFrame parentToJoint, jointToChild;
    double reference = 0.0;           // radians (revolute) or metres (prismatic)
    bool hasLimits = false;
    double lower = 0.0, upper = 0.0;  // same units as reference
};

// Connection-profile entry: which DEWB signal drives a joint and how it converts to the joint coordinate.
// coordinate = offset + scale * toSI(value) for state quantities (angle, position). For rate quantities
// (angular_velocity, linear_velocity) DEWB integrates scale * toSI(rate) from initialCoordinate in simulation time.
struct CadSignalMapping
{
    juce::String jointId, signalId, sourceQuantity, sourceUnit;
    double scale = 1.0, offset = 0.0, initialCoordinate = 0.0;
};

struct CadMechanics
{
    juce::String modelId;
    int interfaceRevision = 0;   // advances when anything DEWB validates against changes (not on renames)
    int profileRevision = 0;     // advances when the interface or any mapping changes
    std::vector<CadMechanicalInstance> instances;
    std::vector<CadJoint> joints;
    std::vector<CadSignalMapping> mappings;

    const CadMechanicalInstance* findInstance(const juce::String& id) const;
    const CadJoint* findJoint(const juce::String& id) const;
    const CadJoint* jointForChild(const juce::String& instanceId) const;
    const CadSignalMapping* mappingForJoint(const juce::String& jointId) const;
    bool empty() const { return instances.empty() && joints.empty() && mappings.empty(); }
};

namespace mech
{
juce::String coordinateQuantity(const juce::String& jointType);   // "angle", "position" or "" (fixed)
juce::String coordinateUnit(const juce::String& jointType);       // "rad", "m" or ""
bool isRateQuantity(const juce::String& quantity);
bool quantityDrivesJoint(const juce::String& quantity, const juce::String& jointType);
juce::StringArray unitsFor(const juce::String& quantity);
bool unitToSI(const juce::String& quantity, const juce::String& unit, double& factor);
gp_Trsf motion(const CadJoint& joint, double coordinate);
gp_Trsf frameFromOriginAxis(double ox, double oy, double oz, double ax, double ay, double az);
bool evaluate(const CadMechanics& model, const std::map<juce::String, double>& coordinates, std::map<juce::String, gp_Trsf>& world, juce::String& error);
bool validateJointGraph(const CadMechanics& model, juce::String& error);
bool validateMapping(const CadMechanics& model, const CadSignalMapping& mapping, juce::String& error);
juce::var toJson(const CadMechanics& model);
bool fromJson(const juce::var& json, CadMechanics& model, juce::String& error);
juce::var frameToJson(const CadFrame& frame, double lengthScale);
}
}
