#pragma once

#include <JuceHeader.h>
#include <gp_Trsf.hxx>

#include <map>
#include <vector>

// Authored mechanical relationships: assembly instances, joints (the physical connections of the mechanical
// graph), coordinate couplings, coordinate roles and the signal connection profile. These are document data
// stored in OCAF; solved coordinates and poses are derived at runtime and never written back.
//
// The mechanical graph may contain closed loops (four-bar, slider-crank). The kinematics solver
// (CadKinematics.h) builds an internal spanning forest plus loop-closure equations; the authored graph here
// stays intact and authoritative.
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
// in the assembly's reference configuration (all joints at their reference coordinates). A grounded instance
// is fixed to the world at its placement.
struct CadMechanicalInstance
{
    juce::String id, name, featureId;
    CadFrame placement;
    bool grounded = false;
};

// A joint is a physical connection between a parent instance (or the world) and a child instance:
//   world(child) = world(parent) * parentToJoint * motion(q - reference) * jointToChild
// parentToJoint maps joint-frame coordinates into the parent instance frame (world when parent is empty).
// jointToChild maps child-instance-frame coordinates into the joint frame at the reference coordinate.
// motion() rotates about (revolute) or translates along (prismatic) the joint frame's +Z axis.
// The coordinate role says how a solve treats q: driver (prescribed input), dependent (solved from the
// constraints) or locked (held at lockValue). Limits are advisory unless enforceLimits makes them hard bounds.
struct CadJoint
{
    juce::String id, name, type;      // "fixed" | "revolute" | "prismatic"
    juce::String parentInstance;      // empty = assembly/world frame
    juce::String childInstance;
    CadFrame parentToJoint, jointToChild;
    double reference = 0.0;           // radians (revolute) or metres (prismatic)
    bool hasLimits = false;
    double lower = 0.0, upper = 0.0;  // same units as reference
    juce::String role = "driver";     // "driver" | "dependent" | "locked" (ignored for fixed joints)
    double lockValue = 0.0;           // radians or metres, used when role == "locked"
    bool enforceLimits = false;       // true: limits are hard kinematic bounds for the solver
};

// A user-authored coordinate coupling between two joints (never inferred from touching geometry):
//   (qB - referenceB) = k * (qA - referenceA) + offset
// gear: A and B revolute, k = ratio (signed), offset = phase in rad
// linear: A and B prismatic, k = ratio (signed), offset in m
// rackPinion: A revolute, B prismatic, k = pitchRadius in m per rad (signed), offset in m
// screw: A revolute, B prismatic, k = lead / (2 pi) with lead in m per revolution (signed), offset in m
struct CadCoupling
{
    juce::String id, name, type, jointA, jointB;
    double ratio = 1.0, pitchRadius = 0.01, lead = 0.001, offset = 0.0;
};

// Connection-profile entry: which DEWB signal drives a joint and how it converts to the joint coordinate.
// coordinate = offset + scale * toSI(value) for state quantities (angle, position). For rate quantities
// (angular_velocity, linear_velocity) DEWB integrates scale * toSI(rate) from initialCoordinate in simulation time.
struct CadSignalMapping
{
    juce::String jointId, signalId, sourceQuantity, sourceUnit;
    double scale = 1.0, offset = 0.0, initialCoordinate = 0.0;
};

// Inverse-kinematics endpoint target. The tool frame is an engineering frame attached to the instance
// (millimetres, in the instance frame), not the body's centre or mesh origin. desired is the tool frame's
// required pose expressed in the reference frame: the world (referenceInstance empty) or another instance,
// evaluated in the same candidate configuration as the endpoint.
struct CadIkTarget
{
    juce::String id, name, instanceId, referenceInstance;
    CadFrame toolFrame, desired;
    juce::String mode = "pose";       // "position" | "orientation" | "pose"
    bool enabled = true;
};

// An authored IK task: the driver joints a local solve may adjust, the targets, and their tolerances.
// Solving it never changes saved joint roles; the selected drivers are released only for that solve.
struct CadIkTask
{
    juce::String id, name;
    juce::StringArray adjustableJoints;
    std::vector<CadIkTarget> targets;
    double positionToleranceMm = 0.01;
    double angleToleranceRad = 1.0e-4;
};

// Craft Designer Phase 3: Adhesives and bonded joints
struct CadAdhesive
{
    juce::String id;
    juce::String name;
    juce::String category;
    int revision = 1;
    juce::StringArray compatibleSubstrates;
    std::optional<double> shearStrengthMPa;
    std::optional<double> recommendedThicknessMm;
    juce::String openTime;
    juce::String cureTime;
    juce::String notes;
    juce::String provenance;
};

struct CadBondedJoint
{
    juce::String id, name;
    juce::String instanceA, instanceB;
    juce::String surfaceA = "auto", surfaceB = "auto";
    juce::String adhesiveId;
    int adhesiveRevision = 1;
    juce::String type = "surface"; // "surface" | "edge" | "fillet"
    double bondThicknessMm = 0.1;
    double beadLegMm = 2.0;
    bool createBeadSolid = false;
    juce::String beadFeatureId;
    double contactAreaMm2 = 0.0;
    double contactLengthMm = 0.0;
    double estimatedVolumeMm3 = 0.0;
    juce::String status = "valid"; // "valid" | "separated" | "incompatible_substrate" | "invalid_reference"
    juce::String failureReason;
    double separationDistanceMm = 0.0;
};

struct CadAssemblyGroup
{
    juce::String id, name;
    std::vector<juce::String> instanceIds;
};

struct CadMechanics
{
    static constexpr int kSchemaVersion = 4;
    juce::String modelId;
    int interfaceRevision = 0;   // advances when anything DEWB validates against changes (not on renames)
    int profileRevision = 0;     // advances when the interface or any mapping changes
    std::vector<CadMechanicalInstance> instances;
    std::vector<CadJoint> joints;
    std::vector<CadCoupling> couplings;
    std::vector<CadSignalMapping> mappings;
    std::vector<CadIkTask> ikTasks;
    std::vector<CadAssemblyGroup> assemblies;
    std::vector<CadAdhesive> adhesives;
    std::vector<CadBondedJoint> bondedJoints;

    const CadMechanicalInstance* findInstance(const juce::String& id) const;
    const CadJoint* findJoint(const juce::String& id) const;
    const CadCoupling* findCoupling(const juce::String& id) const;
    const CadSignalMapping* mappingForJoint(const juce::String& jointId) const;
    const CadIkTask* findIkTask(const juce::String& id) const;
    const CadAssemblyGroup* findAssembly(const juce::String& id) const;
    const CadAdhesive* findAdhesive(const juce::String& id) const;
    const CadBondedJoint* findBondedJoint(const juce::String& id) const;
    bool instanceHasJoints(const juce::String& instanceId) const;
    bool empty() const { return instances.empty() && joints.empty() && couplings.empty() && mappings.empty() && ikTasks.empty() && assemblies.empty() && adhesives.empty() && bondedJoints.empty(); }
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
CadFrame frameFromEulerDegrees(double x, double y, double z, double rx, double ry, double rz);   // extrinsic XYZ
void eulerDegreesFromFrame(const CadFrame& frame, double& rx, double& ry, double& rz);
double couplingFactor(const CadCoupling& coupling);
bool validateModel(const CadMechanics& model, juce::String& error);
bool validateCoupling(const CadMechanics& model, const CadCoupling& coupling, juce::String& error);
bool validateMapping(const CadMechanics& model, const CadSignalMapping& mapping, juce::String& error);
// Structure of an IK task (references, modes, finite frames, tolerances). Joint eligibility (driver role)
// is checked when a solve is requested, so roles stay editable while tasks exist.
// Unsaved previews have no document id; persisted definitions always require one.
bool validateIkTask(const CadMechanics& model, const CadIkTask& task, juce::String& error, bool requireId = true);
juce::var ikTaskToJson(const CadIkTask& task);
bool ikTaskFromJson(const juce::var& json, CadIkTask& task, juce::String& error);
juce::var toJson(const CadMechanics& model);
bool fromJson(const juce::var& json, CadMechanics& model, juce::String& error);
juce::var frameToJson(const CadFrame& frame, double lengthScale);
std::vector<CadAdhesive> defaultAdhesives();
juce::var adhesiveToJson(const CadAdhesive& adhesive);
bool adhesiveFromJson(const juce::var& json, CadAdhesive& adhesive, juce::String& error);
juce::var bondedJointToJson(const CadBondedJoint& joint);
bool bondedJointFromJson(const juce::var& json, CadBondedJoint& joint, juce::String& error);
}
}
