#pragma once

#include <JuceHeader.h>

#include <array>
#include <optional>
#include <vector>

// Structural analysis cases owned by the CAD document. A case describes a problem (which parts, which loads and
// supports, gravity, settings); it never contains a solution, and nothing here solves anything.
//
// Units (CadEngineeringUnits.h): magnitudes are stored in SI (force N, moment N*m, acceleration m/s^2). Application
// points are model positions and stay in millimetres, like all other document geometry. Input may use any supported
// unit; stored and returned values always carry their unit explicitly.
//
// References use persistent identities only: part = document feature id, instance = assembly instance id, face =
// persistent face token on that part (e.g. "extrude-17/end"). They are checked when a case is created or edited
// (invalid references are refused) and re-checked whenever the case is listed; when a later geometry or assembly
// change breaks a reference the case is reported invalid with the reason, and nothing is re-attached to a
// different part or face.
namespace djehuti::cad
{
class CadDocument;

// What a load, support or case scope refers to: exactly one of feature / instance, optionally narrowed to a face.
// A face of an instance is the face of the instance's body, carried to the instance's pose.
struct CadAnalysisTarget
{
    juce::String feature, instance, face;
    juce::String part() const { return feature.isNotEmpty() ? feature : instance; }
};

// A concentrated force (N) or moment (N*m) along a unit direction expressed in frame ("world" or an assembly
// instance id, whose frame moves with that instance). point (mm, same frame) is the application point; when absent
// the load acts on the target as a whole (distribution is a solver decision).
struct CadAnalysisLoad
{
    juce::String id, name, type = "force";   // "force" | "moment"
    CadAnalysisTarget target;
    juce::String frame = "world";
    std::array<double, 3> direction { 0.0, 0.0, -1.0 };   // unit vector
    double magnitude = 0.0;                                // N or N*m, > 0
    std::optional<std::array<double, 3>> pointMm;
};

// A restraint: the listed degrees of freedom of the target are held at zero, expressed in frame.
// Degrees of freedom: "ux", "uy", "uz" (translations), "rx", "ry", "rz" (rotations).
struct CadAnalysisSupport
{
    juce::String id, name;
    CadAnalysisTarget target;
    juce::String frame = "world";
    juce::StringArray fixed;
};

struct CadAnalysisCase
{
    juce::String id, name, description;
    int revision = 1;
    int nextItem = 1;                                    // numbering for load-N / support-N inside this case
    std::vector<CadAnalysisTarget> parts;                // the structure analysed (parts or instances, no faces)
    std::vector<CadAnalysisLoad> loads;
    std::vector<CadAnalysisSupport> supports;
    std::optional<std::array<double, 3>> gravity;        // acceleration, m/s^2, world frame; absent = no self-weight
    juce::String analysisType = "linearStatic";
    juce::var options;                                   // free-form settings object kept for future analyses
};

struct CadAnalysisCases
{
    static constexpr int kSchemaVersion = 1;
    int nextNumber = 1;
    std::vector<CadAnalysisCase> cases;
    const CadAnalysisCase* find(const juce::String& id) const;
    bool empty() const { return cases.empty(); }
};

namespace analysis
{
juce::StringArray degreesOfFreedom();          // ux, uy, uz, rx, ry, rz
juce::StringArray analysisTypes();             // "linearStatic"
// Reads a case from JSON in any supported units and normalizes it to the stored convention. Load and support
// ids that are missing are assigned from nextItem. Checks the structure (not the document references).
bool caseFromJson(const juce::var& json, CadAnalysisCase& analysisCase, juce::String& error);
juce::var caseToJson(const CadAnalysisCase& analysisCase);
bool validateCase(const CadAnalysisCase& analysisCase, juce::String& error);
juce::var toJson(const CadAnalysisCases& cases);
bool fromJson(const juce::var& json, CadAnalysisCases& cases, juce::String& error);
// Every reference of the case checked against the document as it is now: missing parts, instances, faces (by
// persistent token) and frames. Empty when the case is valid. Never resolves a reference to anything else.
juce::StringArray referenceProblems(const CadDocument& document, const CadAnalysisCase& analysisCase);
}
}
