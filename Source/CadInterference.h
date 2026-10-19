#pragma once

#include <JuceHeader.h>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace djehuti::cad
{
class CadDocument;

// Static interference inspection of an assembly in one pose. Components are the physical parts the user sees:
// every assembly instance of a visible solid body (at its instance pose) plus every visible solid body that has no
// instances (at its authored placement). Features that only contribute to a finished body are never compared.
// Interference means a common volume above the threshold; shared faces, edges or points (contact) do not count.
struct CadInterferenceComponent
{
    juce::String id, name, featureId;
    bool instance = false;
    gp_Trsf placement;   // applied once to the body's evaluated shape (CadDocument::instanceShapeTransform for instances)
};

struct CadInterferencePair
{
    juce::String firstId, firstName, secondId, secondName;
    juce::String status;          // "interference" or "failed"
    double volumeMm3 = 0.0;       // common volume (interference)
    juce::String detail;          // containment note or the failure reason
    std::vector<std::array<float, 6>> regionEdges;   // world-mm segments outlining the common volume
};

struct CadInterferenceReport
{
    bool completed = false;       // every candidate pair was evaluated (false: see failures / error)
    juce::String error;           // the inspection could not run at all
    juce::String pose;            // "shown" (current runtime pose, preview included) or "reference" (saved placements)
    std::uint64_t documentRevision = 0, documentInstance = 0, poseSerial = 0;
    int components = 0, pairs = 0, candidatePairs = 0;
    double thresholdMm3 = 0.0;
    std::vector<CadInterferencePair> interferences, failures;
    juce::StringArray skipped;    // components without evaluable solid geometry
    bool interferenceFree() const { return completed && interferences.empty(); }
    juce::var toVar() const;
};

namespace interference
{
// instanceWorld: instance id -> world frame of the pose to inspect (CadMotionRuntime::shownInstanceWorld()); null
// inspects the saved reference placements. Never modifies the document. Message thread (reads evaluated shapes).
CadInterferenceReport check(const CadDocument& document, const std::map<juce::String, gp_Trsf>* instanceWorld, std::uint64_t poseSerial);
}
}
