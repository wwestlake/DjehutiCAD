#pragma once

#include <JuceHeader.h>
#include "CadDocument.h"
#include "CadKinematics.h"

#include <array>
#include <map>
#include <mutex>
#include <set>

namespace djehuti::cad
{
// matrix (column-major, millimetres) maps the instance's authored mesh vertices to their current world pose.
struct CadPoseInstance
{
    juce::String instanceId, featureId;
    std::array<float, 16> matrix {};
};

// A joint axis indication in world millimetres (origin and unit direction of the joint +Z axis).
struct CadAxisMarker
{
    juce::String jointId;
    std::array<float, 3> origin {}, direction {};
    bool linear = false;
};

struct CadPoseSnapshot
{
    std::uint64_t serial = 0;
    std::vector<CadPoseInstance> instances;
    std::set<juce::String> instancedFeatures;   // bodies drawn only through their instances
    std::vector<CadAxisMarker> axes;
    float axisLength = 20.0f;
};

// The one runtime pose shared by every CAD view. Driver coordinates come from local preview or from the
// DEWB motion link; the mechanism solver (CadKinematics) computes every dependent coordinate, and poses are
// always evaluated from the authored model and the solved coordinates. A failed solve keeps the last valid
// pose and reports why. Nothing here touches the document, OCAF history, undo or the dirty flag.
class CadMotionRuntime final : private juce::Timer
{
public:
    enum class Driver { preview, simulation };

    explicit CadMotionRuntime(std::shared_ptr<CadCommandService> service);
    ~CadMotionRuntime() override;

    std::shared_ptr<const CadPoseSnapshot> pose() const;
    Driver driver() const { return currentDriver; }
    void setDriver(Driver next);
    bool setPreviewCoordinate(const juce::String& jointId, double value, juce::String& error);
    void resetToReference();
    // Mapped DEWB coordinates are prescribed driver inputs; returns the solve result (valid == pose published).
    kin::SolveResult applySimulationCoordinates(const std::map<juce::String, double>& values);
    double coordinate(const juce::String& jointId) const;     // last valid solved value
    const kin::SolveResult& lastAttempt() const { return attempt; }
    bool showingLastValidPose() const { return hasAttempt && !attempt.valid; }
    juce::String evaluationError() const { return showingLastValidPose() ? attempt.message : juce::String(); }
    juce::String statusJson() const;
    std::function<void()> onPoseChanged;

private:
    void timerCallback() override;
    bool solveNow(const std::map<juce::String, double>& prescribed);
    void publish();

    std::shared_ptr<CadCommandService> service;
    mutable std::mutex poseLock;
    std::shared_ptr<const CadPoseSnapshot> current;
    std::map<juce::String, double> inputs;          // driver coordinates last applied successfully
    kin::SolveResult valid, attempt;
    bool hasValid = false, hasAttempt = false;
    Driver currentDriver = Driver::preview;
    std::uint64_t shownRevision = 0, serial = 0;
};
}
