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
// Extension boundary (inverse kinematics): additional residual terms, for example a pose target on an
// instance frame, implement the internal Term interface in CadKinematics.cpp and are appended to the same
// system; they reuse the configuration evaluation, joint twists, scaling, verification and diagnostics.
namespace djehuti::cad::kin
{
struct Tolerances
{
    double positionMm = 1.0e-6;   // loop-closure position error
    double angleRad = 1.0e-9;     // loop-closure orientation error
    double coupling = 1.0e-9;     // coupling error in rad or m
};

enum class Status { solved, underconstrained, singular, conflicting, boundViolation, notConverged, invalidInput, cancelled };
juce::String statusName(Status status);

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
    juce::String summary() const;
    juce::var toJson() const;
};

SolveResult solve(const CadMechanics& model, const SolveRequest& request, const SolveOptions& options = {});
}
