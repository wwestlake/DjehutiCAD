#pragma once

#include <JuceHeader.h>
#include "CadDocument.h"
#include "CadAnalysisCases.h"
#include "CadMotionRuntime.h"
#include <vector>
#include <array>
#include <string>

namespace djehuti::cad::structural
{

struct SectionProperties
{
    juce::String profile; // "rectangular", "rod", "tube", "roundedStick"
    double widthM = 0.0;
    double thicknessM = 0.0;
    double outerDiameterM = 0.0;
    double innerDiameterM = 0.0;
    double areaM2 = 0.0;
    double iyM4 = 0.0; // second moment of area about local y-axis (m^4)
    double izM4 = 0.0; // second moment of area about local z-axis (m^4)
    double jM4 = 0.0;  // torsional constant (m^4)
    double syM3 = 0.0; // section modulus about y-axis (m^3)
    double szM3 = 0.0; // section modulus about z-axis (m^3)
    double wtM3 = 0.0; // torsional section modulus (m^3)
};

SectionProperties computeSection(const juce::String& profile, double widthMm, double thicknessMm, double outerDiameterMm, double innerDiameterMm);

struct Node
{
    int id = 0;
    std::array<double, 3> positionM {}; // coordinates in meters (world)
    std::array<bool, 6> fixedDOFs { false, false, false, false, false, false }; // ux, uy, uz, rx, ry, rz
    std::array<double, 6> appliedLoad { 0, 0, 0, 0, 0, 0 }; // Fx, Fy, Fz (N), Mx, My, Mz (N*m)
    juce::StringArray instanceIds; // connected instances
};

struct Member
{
    int id = 0;
    juce::String instanceId;
    int node1 = 0;
    int node2 = 0;
    double lengthM = 0.0;
    std::array<double, 3> dirX {}; // local x-axis unit vector in world space
    std::array<double, 3> dirY {}; // local y-axis unit vector in world space
    std::array<double, 3> dirZ {}; // local z-axis unit vector in world space
    SectionProperties section;
    double youngsModulusPa = 0.0;
    double shearModulusPa = 0.0;
    double densityKgPerM3 = 0.0;
    double tensileStrengthPa = 0.0;
    double compressiveStrengthPa = 0.0;
    double shearStrengthPa = 0.0;
    bool hasStrengthLimits = false;
};

struct Station
{
    double xM = 0.0; // position along element (0 to L)
    std::array<double, 3> worldPosM {};
    std::array<double, 3> displacementM {};
    double normalStressPa = 0.0;
};

struct MemberResult
{
    int memberId = 0;
    juce::String instanceId;
    // Local internal forces at start (Node 1)
    double axialForceN = 0.0; // tension positive
    double shearForceYM = 0.0;
    double shearForceZM = 0.0;
    double torsionNm = 0.0;
    double bendingMomentYNm = 0.0;
    double bendingMomentZNm = 0.0;
    // Peak values across member
    double maxAxialForceN = 0.0;
    double maxShearForceN = 0.0;
    double maxBendingMomentNm = 0.0;
    double maxTorsionNm = 0.0;
    double maxNormalStressPa = 0.0;
    double maxShearStressPa = 0.0;
    double utilizationRatio = 0.0; // maxNormalStress / allowable
    std::vector<Station> stations;
};

struct NodeResult
{
    int nodeId = 0;
    std::array<double, 3> displacementM {}; // ux, uy, uz (m)
    std::array<double, 3> rotationRad {};   // rx, ry, rz (rad)
    std::array<double, 3> reactionForceN {}; // Rx, Ry, Rz (N)
    std::array<double, 3> reactionMomentNm {}; // Mx, My, Mz (N*m)
    double displacementMagnitudeM = 0.0;
};

struct StructuralResult
{
    bool solved = false;
    juce::String status; // "completed", "singular", "unsupported", "error"
    juce::String diagnostic;
    double maxDeflectionM = 0.0;
    double maxNormalStressPa = 0.0;
    double maxShearStressPa = 0.0;
    double maxUtilizationRatio = 0.0;
    double totalAppliedForceN[3] { 0, 0, 0 };
    double totalReactionForceN[3] { 0, 0, 0 };
    std::vector<NodeResult> nodeResults;
    std::vector<MemberResult> memberResults;
    std::vector<CadOverlayLine> overlays;
    juce::var toVar() const;
};

struct StructuralOptions
{
    double visualDeformationScale = 20.0;
    bool showUndeformed = true;
    bool showDeformed = true;
    bool showLoadsAndSupports = true;
    bool colorByStress = true;
};

// Extracts the 3D frame model from the document and analysis case specification.
bool extractModel(const CadDocument& doc, const CadAnalysisCase& analysisCase,
                  std::vector<Node>& nodes, std::vector<Member>& members,
                  juce::String& diagnostic, juce::StringArray* warnings = nullptr);

// Solves the small-displacement linear-elastic 3D frame static equilibrium problem.
StructuralResult solve(const std::vector<Node>& nodes, const std::vector<Member>& members,
                       const StructuralOptions& options = {});

// High-level entry point: extracts and solves the given analysis case from document.
StructuralResult evaluateCase(const CadDocument& doc, const CadAnalysisCase& analysisCase,
                              const StructuralOptions& options = {});

// Utility function to generate overlays for a solved result.
std::vector<CadOverlayLine> generateOverlays(const std::vector<Node>& nodes,
                                             const std::vector<Member>& members,
                                             const StructuralResult& result,
                                             const StructuralOptions& options);

} // namespace djehuti::cad::structural
