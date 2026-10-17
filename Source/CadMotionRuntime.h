#pragma once

#include <JuceHeader.h>
#include "CadDocument.h"

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

struct CadPoseSnapshot
{
    std::uint64_t serial = 0;
    std::vector<CadPoseInstance> instances;
    std::set<juce::String> instancedFeatures;   // bodies drawn only through their instances
};

// The one runtime pose shared by every CAD view. Joint coordinates here are temporary runtime state
// (local preview values or the latest validated simulation state); nothing is written to the document,
// so poses never touch OCAF history, undo or the dirty flag. Poses are always evaluated from the authored
// reference placements and the current coordinates, never accumulated onto a previous pose.
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
    void applySimulationCoordinates(const std::map<juce::String, double>& values);
    double coordinate(const juce::String& jointId) const;
    juce::String evaluationError() const { return lastError; }
    std::function<void()> onPoseChanged;

private:
    void timerCallback() override;
    void rebuild();

    std::shared_ptr<CadCommandService> service;
    mutable std::mutex poseLock;
    std::shared_ptr<const CadPoseSnapshot> current;
    std::map<juce::String, double> coordinates;
    Driver currentDriver = Driver::preview;
    std::uint64_t shownRevision = 0, serial = 0;
    juce::String lastError;
};
}
