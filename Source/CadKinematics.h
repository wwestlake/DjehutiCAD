#pragma once

#include "CadMechanics.h"

#include <atomic>
#include <map>
#include <memory>

// Forward mechanism solver. Prescribed (driver) and locked coordinates are inputs; dependent coordinates are
// solved so that every joint and coupling constraint holds. The authored mechanical graph is converted into
// a spanning forest rooted at the world and grounded instances; joints outside the forest become 6-D loop
// closure equations, couplings become scalar equations. Solving uses damped Gauss-Newton steps with OCCT's
// math_SVD and rank diagnostics with math_Jacobi; the result is independently verified before it is valid.
//
// Inverse kinematics uses the same system: endpoint targets are additional residual terms (Term interface in
// CadKinematics.cpp) and the drivers a request names as adjustable become unknowns for that solve only. The
// joint, loop-closure and coupling equations, scaling, continuation, verification and diagnostics are shared.
namespace djehuti::cad::kin
{
struct Tolerances
{
    double positionMm = 1.0e-6;   // loop-closure position error
    double angleRad = 1.0e-9;     // loop-closure orientation error
    double coupling = 1.0e-9;     // coupling error in rad or m
};

// targetNotReached / limitRestricted: the mechanism equations hold but an IK target is outside its tolerance
// (limitRestricted: a hard joint bound stopped the search). Both are invalid: the pose is not published.
enum class Status { solved, underconstrained, singular, conflicting, boundViolation, notConverged, invalidInput, cancelled, targetNotReached, limitRestricted };
juce::String statusName(Status status);

// One IK target evaluated in a configuration: desired and achieved tool frame in the target's reference
// frame, both world frames (for markers), and the errors (mm, rad; the angle is the rotation-vector magnitude).
struct TargetReport
{
    juce::String id, name, mode;
    bool enabled = true, reached = false;
    CadFrame desired, achieved;
    gp_Trsf desiredWorld, achievedWorld;
    double positionErrorMm = 0.0, angleErrorRad = 0.0;
    juce::var toJson() const;
};
TargetReport evaluateTarget(const CadIkTarget& target, const std::map<juce::String, gp_Trsf>& world, double positionToleranceMm, double angleToleranceRad);
// Tool frame pose in the target's reference frame for the given instance poses (used by Capture Current Pose).
CadFrame achievedInReference(const CadIkTarget& target, const std::map<juce::String, gp_Trsf>& world);

struct SolveOptions
{
    int maxIterations = 60;            // per continuation step
    int maxSubsteps = 64;              // continuation steps for large driver changes
    double maxAngleStepRad = 0.2;      // continuation step for revolute drivers
    double maxLinearStepFraction = 0.05;  // continuation step for prismatic drivers, fraction of the mechanism size
    double timeBudgetMs = 250.0;
    const std::atomic<bool>* cancel = nullptr;
    Tolerances tolerances;
};

struct SolveRequest
{
    std::map<juce::String, double> prescribed;   // driver coordinates for this solve (rad or m)
    std::map<juce::String, double> seed;         // last valid coordinates (continuation start)
    int seedRank = -1;                           // constraint rank of the last valid solve, for singularity detection
    // Inverse kinematics: drivers released for this solve only (never in prescribed), and the targets.
    // Unlisted drivers keep their prescribed or seed value; dependent, locked and fixed joints keep their roles.
    juce::StringArray adjustable;
    std::vector<CadIkTarget> targets;
    double targetPositionToleranceMm = 0.01, targetAngleToleranceRad = 1.0e-4;
    bool inverse() const { return !adjustable.isEmpty() || !targets.empty(); }
};

struct SolveResult
{
    Status status = Status::invalidInput;
    bool valid = false;                              // true: coordinates and poses satisfy every check
    std::map<juce::String, double> coordinates;      // every joint, rad or m (fixed joints at reference)
    std::map<juce::String, gp_Trsf> world;           // every instance, world millimetres
    double positionResidualMm = 0.0, angleResidualRad = 0.0, couplingResidual = 0.0;
    int iterations = 0, substeps = 0, unknowns = 0, equations = 0, rank = 0;
    int freeCoordinates = 0, redundantEquations = 0, rigidBodyFreedom = 0;
    juce::StringArray freeJoints, floatingInstances, problems, warnings;
    juce::String message;
    // IK only. For a valid result these describe the published pose; for a rejected candidate they are
    // diagnostics of that candidate (the nearest configuration that satisfies the mechanism, when one was found).
    std::vector<TargetReport> targets;
    bool inverse = false, candidateConsistent = false;   // candidateConsistent: the candidate satisfies the mechanism
    juce::StringArray limitedJoints;                     // joints at a hard bound when the search stopped
    juce::String summary() const;
    juce::var toJson() const;
};

SolveResult solve(const CadMechanics& model, const SolveRequest& request, const SolveOptions& options = {});

// Local IK solve for an authored (or edited, unsaved) task: validates the task, checks that every adjustable
// joint is an eligible driver, releases those drivers for this solve, and keeps every other prescribed input.
SolveResult solveTask(const CadMechanics& model, const CadIkTask& task, const std::map<juce::String, double>& prescribed,
                      const std::map<juce::String, double>& seed, int seedRank, const SolveOptions& options = {});
}
