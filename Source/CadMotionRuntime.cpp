#include "CadMotionRuntime.h"

namespace djehuti::cad
{
CadMotionRuntime::CadMotionRuntime(std::shared_ptr<CadCommandService> s) : service(std::move(s))
{
    rebuild();
    startTimerHz(30);
}

CadMotionRuntime::~CadMotionRuntime() { stopTimer(); }

std::shared_ptr<const CadPoseSnapshot> CadMotionRuntime::pose() const
{
    const std::lock_guard<std::mutex> lock(poseLock);
    return current;
}

void CadMotionRuntime::setDriver(Driver next) { currentDriver = next; }

bool CadMotionRuntime::setPreviewCoordinate(const juce::String& jointId, double value, juce::String& error)
{
    if (currentDriver == Driver::simulation) { error = "DEWB drives the joints while the motion link is bound; disconnect or stop listening to preview."; return false; }
    const auto* joint = service->document().mechanicsData().findJoint(jointId);
    if (joint == nullptr) { error = "Unknown joint: " + jointId; return false; }
    if (joint->type == "fixed") { error = joint->name + " is fixed and has no coordinate."; return false; }
    if (!std::isfinite(value)) { error = "Preview value must be finite."; return false; }
    coordinates[jointId] = value;
    rebuild();
    return true;
}

void CadMotionRuntime::resetToReference() { coordinates.clear(); rebuild(); }

void CadMotionRuntime::applySimulationCoordinates(const std::map<juce::String, double>& values)
{
    for (const auto& [joint, value] : values) coordinates[joint] = value;
    rebuild();
}

double CadMotionRuntime::coordinate(const juce::String& jointId) const
{
    if (const auto found = coordinates.find(jointId); found != coordinates.end()) return found->second;
    const auto* joint = service->document().mechanicsData().findJoint(jointId);
    return joint != nullptr ? joint->reference : 0.0;
}

void CadMotionRuntime::timerCallback()
{
    if (service->document().revision() != shownRevision) rebuild();
}

void CadMotionRuntime::rebuild()
{
    const auto& document = service->document();
    const auto& model = document.mechanicsData();
    shownRevision = document.revision();
    for (auto it = coordinates.begin(); it != coordinates.end();)
        it = model.findJoint(it->first) == nullptr ? coordinates.erase(it) : std::next(it);

    auto next = std::make_shared<CadPoseSnapshot>();
    next->serial = ++serial;
    std::map<juce::String, gp_Trsf> world;
    juce::String error;
    if (!mech::evaluate(model, coordinates, world, error)) lastError = error;
    else lastError.clear();
    for (const auto& instance : model.instances)
    {
        const auto* feature = document.find(instance.featureId);
        if (feature == nullptr) continue;
        next->instancedFeatures.insert(instance.featureId);
        const auto found = world.find(instance.id);
        if (found == world.end()) continue;
        // The tessellated mesh already contains the body's authored placement; the instance frame starts at that
        // placement point, so only the motion relative to it is applied (no double placement).
        gp_Trsf anchor; anchor.SetTranslation(gp_Vec(feature->x, feature->y, feature->z));
        const auto transform = found->second.Multiplied(anchor.Inverted());
        CadPoseInstance posed;
        posed.instanceId = instance.id; posed.featureId = instance.featureId;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 3; ++row)
                posed.matrix[(size_t) (column * 4 + row)] = (float) transform.Value(row + 1, column + 1);
        posed.matrix[3] = posed.matrix[7] = posed.matrix[11] = 0.0f; posed.matrix[15] = 1.0f;
        next->instances.push_back(posed);
    }
    {
        const std::lock_guard<std::mutex> lock(poseLock);
        current = std::move(next);
    }
    if (onPoseChanged) onPoseChanged();
}
}
