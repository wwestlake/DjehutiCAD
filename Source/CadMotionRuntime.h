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

// A world-millimetre line drawn over the model (IK desired/achieved markers and the drag plane).
struct CadOverlayLine
{
    std::array<float, 3> a {}, b {};
    juce::uint32 argb = 0xffffffff;
};

// Screen-space handle for dragging an IK target's position within its drag plane (world millimetres).
struct CadDragHandle
{
    bool active = false;
    juce::String targetId;
    std::array<float, 3> point {}, normal {};
};

struct CadPoseSnapshot
{
    std::uint64_t serial = 0;
    std::vector<CadPoseInstance> instances;
    std::set<juce::String> instancedFeatures;   // bodies drawn only through their instances
    std::vector<CadAxisMarker> axes;
    float axisLength = 20.0f;
    std::vector<CadOverlayLine> overlay;
    CadDragHandle dragHandle;
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

    // Local inverse kinematics. Every entry point uses kin::solveTask with the current prescribed drivers and
    // the last valid configuration as the seed; only complete, verified poses are published, and a failed solve
    // keeps the last valid pose. Not available while DEWB owns the pose (simulation driver).
    kin::SolveResult solveIk(const CadIkTask& task);                       // synchronous shared automation result
    bool requestIkAsync(const CadIkTask& task, juce::String& error, double timeBudgetMs = 200.0); // one active + one replaceable pending request
    // The task whose markers are drawn (desired from the task, achieved from the displayed pose) and the target
    // whose drag plane ("XY", "XZ" or "YZ" of its reference frame) is shown; nullptr clears the markers.
    void setIkDisplay(const CadIkTask* task, const juce::String& dragTargetId = {}, const juce::String& dragPlane = "XY");
    bool captureTarget(CadIkTarget& target, juce::String& error) const;    // desired := current achieved pose (no motion)
    std::vector<kin::TargetReport> shownTargetReports() const;             // errors of the pose currently shown
    std::vector<kin::TargetReport> shownTargetReports(const CadIkTask& task) const;   // the given task's targets in the pose shown
    bool hasIkResult() const { return ikResultValid; }
    const kin::SolveResult& lastIkResult() const { return ikResult; }      // last solve: accepted pose or rejected candidate
    juce::String lastIkTaskId() const { return ikResultTask; }
    juce::var ikStatus(const CadIkTask* task) const;                       // structured result for automation
    std::uint64_t ikSerial() const { return ikResultSerial; }
    std::function<void()> onIkChanged;

private:
    class IkWorker;
    struct IkJob;
    void timerCallback() override;
    bool solveNow(const std::map<juce::String, double>& prescribed);
    void publish();
    void invalidateIk();
    void acceptIk(const CadIkTask& task, const kin::SolveResult& result);
    void deliverIk(const IkJob& job, const kin::SolveResult& result);
    std::map<juce::String, double> seedCoordinates() const;

    std::shared_ptr<CadCommandService> service;
    mutable std::mutex poseLock;
    std::shared_ptr<const CadPoseSnapshot> current;
    std::map<juce::String, double> inputs;          // driver coordinates last applied successfully
    kin::SolveResult valid, attempt;
    bool hasValid = false, hasAttempt = false;
    Driver currentDriver = Driver::preview;
    std::uint64_t shownRevision = 0, shownInstance = 0, serial = 0;
    std::map<juce::String, gp_Trsf> shownWorld;      // instance poses of the published snapshot

    // IK runtime state (never written to the document).
    std::unique_ptr<IkWorker> worker;
    std::shared_ptr<int> lifeToken = std::make_shared<int>(0);
    std::uint64_t ikGeneration = 0, ikResultSerial = 0;
    kin::SolveResult ikResult;
    bool ikResultValid = false;
    juce::String ikResultTask;
    std::unique_ptr<CadIkTask> displayTask;
    juce::String dragTargetId, dragPlane = "XY";
};
}
