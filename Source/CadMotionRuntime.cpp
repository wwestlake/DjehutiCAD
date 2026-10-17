#include "CadMotionRuntime.h"

namespace djehuti::cad
{
CadMotionRuntime::CadMotionRuntime(std::shared_ptr<CadCommandService> s) : service(std::move(s))
{
    shownRevision = service->document().revision();
    shownInstance = service->document().documentInstance();
    solveNow({});
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
    if (joint->role != "driver") { error = joint->name + " is a " + joint->role + " coordinate; preview one of the mechanism's drivers instead."; return false; }
    if (!std::isfinite(value)) { error = "Preview value must be finite."; return false; }
    auto next = inputs;
    next[jointId] = value;
    if (!solveNow(next)) { error = attempt.message + (attempt.problems.isEmpty() ? juce::String() : " " + attempt.problems.joinIntoString("; ")) + " The view keeps the last valid pose."; return false; }
    return true;
}

void CadMotionRuntime::resetToReference()
{
    inputs.clear();
    hasValid = false;      // seed from the authored reference configuration again
    solveNow({});
}

kin::SolveResult CadMotionRuntime::applySimulationCoordinates(const std::map<juce::String, double>& values)
{
    auto next = inputs;
    for (const auto& [joint, value] : values) next[joint] = value;
    solveNow(next);
    return attempt;
}

double CadMotionRuntime::coordinate(const juce::String& jointId) const
{
    if (hasValid) if (const auto found = valid.coordinates.find(jointId); found != valid.coordinates.end()) return found->second;
    const auto* joint = service->document().mechanicsData().findJoint(jointId);
    return joint != nullptr ? (joint->role == "locked" ? joint->lockValue : joint->reference) : 0.0;
}

juce::String CadMotionRuntime::statusJson() const
{
    auto json = attempt.toJson();
    if (auto* object = json.getDynamicObject()) object->setProperty("showingLastValidPose", showingLastValidPose());
    return juce::JSON::toString(json, true);
}

// Solve only when the authored model changes; idle frames cost nothing.
void CadMotionRuntime::timerCallback()
{
    const auto& document = service->document();
    if (document.documentInstance() != shownInstance)
    {
        // New or Open replaced the document: ids may repeat across files, so nothing carries over.
        shownInstance = document.documentInstance();
        shownRevision = document.revision();
        resetToReference();
        return;
    }
    if (document.revision() == shownRevision) return;
    shownRevision = document.revision();
    solveNow(inputs);
}

bool CadMotionRuntime::solveNow(const std::map<juce::String, double>& prescribed)
{
    const auto& model = service->document().mechanicsData();
    kin::SolveRequest request;
    for (const auto& [id, value] : prescribed)
        if (const auto* joint = model.findJoint(id); joint != nullptr && joint->type != "fixed" && joint->role == "driver") request.prescribed[id] = value;
    if (hasValid)
    {
        for (const auto& [id, value] : valid.coordinates) if (model.findJoint(id) != nullptr) request.seed[id] = value;
        request.seedRank = valid.rank;
    }
    attempt = kin::solve(model, request);
    hasAttempt = true;
    if (attempt.valid)
    {
        valid = attempt;
        hasValid = true;
        inputs = request.prescribed;
    }
    publish();
    return attempt.valid;
}

// Poses come from the last valid solve; when an attempt fails the previous valid pose stays on screen.
void CadMotionRuntime::publish()
{
    const auto& document = service->document();
    const auto& model = document.mechanicsData();
    auto next = std::make_shared<CadPoseSnapshot>();
    next->serial = ++serial;
    std::map<juce::String, gp_Trsf> world;
    for (const auto& instance : model.instances)
    {
        const auto found = hasValid ? valid.world.find(instance.id) : valid.world.end();
        world[instance.id] = hasValid && found != valid.world.end() ? found->second : instance.placement.toTrsf();
    }
    for (const auto& instance : model.instances)
    {
        const auto* feature = document.find(instance.featureId);
        if (feature == nullptr) continue;
        next->instancedFeatures.insert(instance.featureId);
        // The tessellated mesh already contains the body's authored placement; the instance frame starts at that
        // placement point, so only the motion relative to it is applied (no double placement).
        gp_Trsf anchor; anchor.SetTranslation(gp_Vec(feature->x, feature->y, feature->z));
        const auto transform = world[instance.id].Multiplied(anchor.Inverted());
        CadPoseInstance posed;
        posed.instanceId = instance.id; posed.featureId = instance.featureId;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 3; ++row)
                posed.matrix[(size_t) (column * 4 + row)] = (float) transform.Value(row + 1, column + 1);
        posed.matrix[3] = posed.matrix[7] = posed.matrix[11] = 0.0f; posed.matrix[15] = 1.0f;
        next->instances.push_back(posed);
    }
    double extent = 0.0;
    for (const auto& joint : model.joints)
    {
        if (joint.type == "fixed") continue;
        const auto parent = joint.parentInstance.isEmpty() ? gp_Trsf() : world[joint.parentInstance];
        const auto frame = parent.Multiplied(joint.parentToJoint.toTrsf()).Multiplied(mech::motion(joint, coordinate(joint.id)));
        const gp_Dir axis = gp_Dir(0, 0, 1).Transformed(frame);
        const auto origin = frame.TranslationPart();
        CadAxisMarker marker;
        marker.jointId = joint.id; marker.linear = joint.type == "prismatic";
        marker.origin = { (float) origin.X(), (float) origin.Y(), (float) origin.Z() };
        marker.direction = { (float) axis.X(), (float) axis.Y(), (float) axis.Z() };
        next->axes.push_back(marker);
        extent = juce::jmax(extent, origin.Modulus());
    }
    next->axisLength = (float) juce::jlimit(5.0, 200.0, extent * 0.25 + 10.0);
    {
        const std::lock_guard<std::mutex> lock(poseLock);
        current = std::move(next);
    }
    if (onPoseChanged) onPoseChanged();
}
}
