#include "CadMotionRuntime.h"

namespace djehuti::cad
{
// A local IK solve snapshot: the model, task and inputs are copied, so the worker never reads live document
// data. The identity fields decide on the message thread whether the result may still be published.
struct CadMotionRuntime::IkJob
{
    CadMechanics model;
    CadIkTask task;
    std::map<juce::String, double> prescribed, seed;
    int seedRank = -1;
    double timeBudgetMs = 200.0;
    std::uint64_t generation = 0, documentInstance = 0, revision = 0;
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
};

// One worker thread for interactive IK: at most one active job and one pending request that newer requests
// replace. It sleeps while there is no work, so nothing is solved while idle.
class CadMotionRuntime::IkWorker final : public juce::Thread
{
public:
    std::function<void(const IkJob&, const kin::SolveResult&)> deliver;

    IkWorker() : juce::Thread("DjehutiCAD IK") { startThread(); }
    ~IkWorker() override
    {
        signalThreadShouldExit();
        cancelAll();
        wake.signal();
        stopThread(4000);
    }
    void submit(std::unique_ptr<IkJob> job)
    {
        { const std::lock_guard<std::mutex> lock(mutex); pending = std::move(job); }   // supersedes an older pending request
        wake.signal();
    }
    void cancelAll()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        pending.reset();
        if (activeCancel != nullptr) activeCancel->store(true);
    }

private:
    void run() override
    {
        while (!threadShouldExit())
        {
            std::unique_ptr<IkJob> job;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                job = std::move(pending);
                if (job != nullptr) activeCancel = job->cancel;
            }
            if (job == nullptr) { wake.wait(-1); continue; }
            kin::SolveOptions options;
            options.timeBudgetMs = job->timeBudgetMs;
            options.maxSubsteps = job->timeBudgetMs > 200.0 ? 128 : 64;
            options.cancel = job->cancel.get();
            const auto result = kin::solveTask(job->model, job->task, job->prescribed, job->seed, job->seedRank, options);
            { const std::lock_guard<std::mutex> lock(mutex); activeCancel.reset(); }
            if (!threadShouldExit() && !job->cancel->load() && deliver) deliver(*job, result);
        }
    }

    std::mutex mutex;
    std::unique_ptr<IkJob> pending;
    std::shared_ptr<std::atomic<bool>> activeCancel;
    juce::WaitableEvent wake;
};

CadMotionRuntime::CadMotionRuntime(std::shared_ptr<CadCommandService> s) : service(std::move(s))
{
    shownRevision = service->document().revision();
    shownInstance = service->document().documentInstance();
    solveNow({});
    startTimerHz(30);
}

CadMotionRuntime::~CadMotionRuntime()
{
    stopTimer();
    lifeToken.reset();
    worker.reset();   // joins the IK thread; queued deliveries see the expired life token
}

std::shared_ptr<const CadPoseSnapshot> CadMotionRuntime::pose() const
{
    const std::lock_guard<std::mutex> lock(poseLock);
    return current;
}

void CadMotionRuntime::setDriver(Driver next)
{
    // DEWB taking control makes every pending local IK request obsolete; it never runs against DEWB inputs.
    if (next == Driver::simulation && currentDriver != Driver::simulation) invalidateIk();
    currentDriver = next;
}

bool CadMotionRuntime::setPreviewCoordinate(const juce::String& jointId, double value, juce::String& error)
{
    if (currentDriver == Driver::simulation) { error = "DEWB drives the joints while the motion link is bound; disconnect or stop listening to preview."; return false; }
    const auto* joint = service->document().mechanicsData().findJoint(jointId);
    if (joint == nullptr) { error = "Unknown joint: " + jointId; return false; }
    if (joint->type == "fixed") { error = joint->name + " is fixed and has no coordinate."; return false; }
    if (joint->role != "driver") { error = joint->name + " is a " + joint->role + " coordinate; preview one of the mechanism's drivers instead."; return false; }
    if (!std::isfinite(value)) { error = "Preview value must be finite."; return false; }
    invalidateIk();
    auto next = inputs;
    next[jointId] = value;
    if (!solveNow(next)) { error = attempt.message + (attempt.problems.isEmpty() ? juce::String() : " " + attempt.problems.joinIntoString("; ")) + " The view keeps the last valid pose."; return false; }
    return true;
}

void CadMotionRuntime::resetToReference()
{
    invalidateIk();
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
        // New or Open replaced the document: ids may repeat across files, so nothing carries over
        // (driver values, poses, IK requests, results or markers).
        shownInstance = document.documentInstance();
        shownRevision = document.revision();
        displayTask.reset(); dragTargetId.clear();
        ikResult = {}; ikResultValid = false; ikResultTask.clear(); ++ikResultSerial;
        resetToReference();
        if (onIkChanged) onIkChanged();
        return;
    }
    if (document.revision() == shownRevision) return;
    invalidateIk();
    shownRevision = document.revision();
    solveNow(inputs);
}

bool CadMotionRuntime::solveNow(const std::map<juce::String, double>& prescribed)
{
    const auto& model = service->document().mechanicsData();
    kin::SolveRequest request;
    for (const auto& [id, value] : prescribed)
        if (const auto* joint = model.findJoint(id); joint != nullptr && joint->type != "fixed" && joint->role == "driver") request.prescribed[id] = value;
    request.seed = seedCoordinates();
    if (hasValid) request.seedRank = valid.rank;
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

std::map<juce::String, double> CadMotionRuntime::seedCoordinates() const
{
    std::map<juce::String, double> seed;
    if (!hasValid) return seed;
    const auto& model = service->document().mechanicsData();
    for (const auto& [id, value] : valid.coordinates) if (model.findJoint(id) != nullptr) seed[id] = value;
    return seed;
}

void CadMotionRuntime::invalidateIk()
{
    ++ikGeneration;
    if (worker != nullptr) worker->cancelAll();
}

kin::SolveResult CadMotionRuntime::solveIk(const CadIkTask& task)
{
    timerCallback();   // use the current document and seed even before the next timer tick
    if (currentDriver == Driver::simulation)
    {
        kin::SolveResult refused; refused.inverse = true; refused.status = kin::Status::invalidInput;
        refused.message = "DEWB owns the mechanism pose while the motion link is bound; its driver coordinates stay prescribed, so local IK is not available until the link is disconnected.";
        return refused;
    }
    invalidateIk();   // a direct solve supersedes any drag request in flight
    kin::SolveOptions options;
    options.timeBudgetMs = 2000.0;
    options.maxSubsteps = 128;
    const auto result = kin::solveTask(service->document().mechanicsData(), task, inputs, seedCoordinates(), hasValid ? valid.rank : -1, options);
    acceptIk(task, result);
    displayTask = std::make_unique<CadIkTask>(task);
    publish();
    if (onIkChanged) onIkChanged();
    return result;
}

bool CadMotionRuntime::requestIkAsync(const CadIkTask& task, juce::String& error, double timeBudgetMs)
{
    timerCallback();
    if (currentDriver == Driver::simulation) { error = "DEWB owns the mechanism pose while the motion link is bound; local IK is not available."; return false; }
    // Supersede both active work and any result already queued for message-thread delivery.
    invalidateIk();
    if (worker == nullptr)
    {
        worker = std::make_unique<IkWorker>();
        std::weak_ptr<int> life = lifeToken;
        worker->deliver = [this, life](const IkJob& job, const kin::SolveResult& result)
        {
            auto copy = std::make_shared<IkJob>(job);
            juce::MessageManager::callAsync([this, life, copy, result] { if (!life.expired()) deliverIk(*copy, result); });
        };
    }
    const auto& document = service->document();
    auto job = std::make_unique<IkJob>();
    job->model = document.mechanicsData();
    job->task = task;
    job->timeBudgetMs = juce::jlimit(1.0, 2000.0, timeBudgetMs);
    job->prescribed = inputs;
    job->seed = seedCoordinates();
    job->seedRank = hasValid ? valid.rank : -1;
    job->generation = ikGeneration;
    job->documentInstance = document.documentInstance();
    job->revision = document.revision();
    worker->submit(std::move(job));
    return true;
}

// Message thread. A worker result is published only if nothing it depended on has changed since it was queued.
void CadMotionRuntime::deliverIk(const IkJob& job, const kin::SolveResult& result)
{
    const auto& document = service->document();
    const bool current = job.generation == ikGeneration && job.documentInstance == document.documentInstance() && job.revision == document.revision()
                      && currentDriver == Driver::preview && displayTask != nullptr && displayTask->id == job.task.id;
    if (!current) return;
    acceptIk(job.task, result);
    publish();
    if (onIkChanged) onIkChanged();
}

void CadMotionRuntime::acceptIk(const CadIkTask& task, const kin::SolveResult& result)
{
    ikResult = result; ikResultValid = true; ikResultTask = task.id; ++ikResultSerial;
    attempt = result; hasAttempt = true;
    if (!result.valid) return;   // rejected candidate: the last valid pose stays; its errors are diagnostics only
    valid = result; hasValid = true;
    // The adjusted drivers become the prescribed inputs from now on, so later solves keep this configuration.
    for (const auto& id : task.adjustableJoints)
        if (const auto found = result.coordinates.find(id); found != result.coordinates.end()) inputs[id] = found->second;
}

void CadMotionRuntime::setIkDisplay(const CadIkTask* task, const juce::String& targetId, const juce::String& plane)
{
    const bool changed = (task == nullptr) != (displayTask == nullptr)
                      || (task != nullptr && displayTask != nullptr
                          && juce::JSON::toString(mech::ikTaskToJson(*task)) != juce::JSON::toString(mech::ikTaskToJson(*displayTask)))
                      || dragTargetId != (task != nullptr ? targetId : juce::String()) || dragPlane != plane;
    if (changed) invalidateIk();
    displayTask = task != nullptr ? std::make_unique<CadIkTask>(*task) : nullptr;
    dragTargetId = task != nullptr ? targetId : juce::String();
    dragPlane = plane;
    publish();
}

bool CadMotionRuntime::captureTarget(CadIkTarget& target, juce::String& error) const
{
    if (service->document().mechanicsData().findInstance(target.instanceId) == nullptr) { error = "Choose the instance that carries the endpoint before capturing its pose."; return false; }
    if (target.referenceInstance.isNotEmpty() && shownWorld.count(target.referenceInstance) == 0) { error = "The reference instance does not exist."; return false; }
    target.desired = kin::achievedInReference(target, shownWorld);
    return true;
}

std::vector<kin::TargetReport> CadMotionRuntime::shownTargetReports() const
{
    return displayTask != nullptr ? shownTargetReports(*displayTask) : std::vector<kin::TargetReport> {};
}

std::vector<kin::TargetReport> CadMotionRuntime::shownTargetReports(const CadIkTask& task) const
{
    std::vector<kin::TargetReport> reports;
    for (const auto& t : task.targets)
        if (t.enabled && shownWorld.count(t.instanceId) != 0 && (t.referenceInstance.isEmpty() || shownWorld.count(t.referenceInstance) != 0))
            reports.push_back(kin::evaluateTarget(t, shownWorld, task.positionToleranceMm, task.angleToleranceRad));
    return reports;
}

juce::var CadMotionRuntime::ikStatus(const CadIkTask* task) const
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty("authority", currentDriver == Driver::simulation ? "simulation" : "preview");
    if (task != nullptr) o->setProperty("task", mech::ikTaskToJson(*task));
    if (ikResultValid && (task == nullptr || task->id == ikResultTask))
    {
        o->setProperty("lastSolve", ikResult.toJson());
        o->setProperty("lastSolvePublished", ikResult.valid);
    }
    // Errors of the pose actually shown, which differ from a rejected candidate's errors.
    juce::Array<juce::var> shown;
    if (task != nullptr)
        for (const auto& t : task->targets)
            if (t.enabled && shownWorld.count(t.instanceId) != 0) shown.add(kin::evaluateTarget(t, shownWorld, task->positionToleranceMm, task->angleToleranceRad).toJson());
    o->setProperty("shownPoseTargets", shown);
    o->setProperty("showingLastValidPose", showingLastValidPose());
    return juce::var(o.release());
}

namespace
{
std::array<float, 3> point3(const gp_XYZ& p) { return { (float) p.X(), (float) p.Y(), (float) p.Z() }; }
void addLine(std::vector<CadOverlayLine>& lines, const gp_XYZ& a, const gp_XYZ& b, juce::uint32 argb) { lines.push_back({ point3(a), point3(b), argb }); }
// A frame triad: X, Y, Z axes of length size, tinted toward the marker colour.
void addTriad(std::vector<CadOverlayLine>& lines, const gp_Trsf& frame, double size, juce::Colour tint)
{
    const auto o = frame.TranslationPart();
    const juce::Colour axes[3] { juce::Colour(0xffff6b6b), juce::Colour(0xff6bff8f), juce::Colour(0xff6ba6ff) };
    for (int k = 0; k < 3; ++k)
    {
        const gp_Dir d = gp_Dir(k == 0 ? 1 : 0, k == 1 ? 1 : 0, k == 2 ? 1 : 0).Transformed(frame);
        addLine(lines, o, o + d.XYZ() * size, axes[k].interpolatedWith(tint, 0.45f).getARGB());
    }
}
}

// Poses come from the last valid solve; when an attempt fails the previous valid pose stays on screen.
void CadMotionRuntime::publish()
{
    const auto& document = service->document();
    const auto& model = document.mechanicsData();
    auto next = std::make_shared<CadPoseSnapshot>();
    next->serial = ++serial;
    next->documentInstance = document.documentInstance(); next->documentRevision = document.revision();
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

    // IK markers, computed from the same instance poses that are drawn: desired (green triad), achieved
    // (magenta triad), the gap between them, and the drag plane of the selected target.
    if (displayTask != nullptr)
    {
        const double size = next->axisLength * 0.6;
        for (const auto& t : displayTask->targets)
        {
            if (!t.enabled || world.count(t.instanceId) == 0 || (t.referenceInstance.isNotEmpty() && world.count(t.referenceInstance) == 0)) continue;
            const auto report = kin::evaluateTarget(t, world, displayTask->positionToleranceMm, displayTask->angleToleranceRad);
            addTriad(next->overlay, report.desiredWorld, size, juce::Colour(0xff7dff9a));
            addTriad(next->overlay, report.achievedWorld, size * 0.75, juce::Colour(0xffff7ae0));
            const auto d = report.desiredWorld.TranslationPart(), a = report.achievedWorld.TranslationPart();
            if ((d - a).Modulus() > 1.0e-6) addLine(next->overlay, a, d, report.reached ? 0xff7dff9a : 0xffffd36b);
            if (t.id == dragTargetId && t.mode != "orientation")
            {
                // The drag plane: a plane of the target's reference frame through the desired point.
                const auto reference = t.referenceInstance.isEmpty() ? gp_Trsf() : world[t.referenceInstance];
                const int normalAxis = dragPlane == "YZ" ? 0 : dragPlane == "XZ" ? 1 : 2;
                const gp_Dir u = gp_Dir(normalAxis == 0 ? 0 : 1, normalAxis == 0 ? 1 : 0, 0).Transformed(reference);
                const gp_Dir v = gp_Dir(0, normalAxis == 2 ? 1 : 0, normalAxis == 2 ? 0 : 1).Transformed(reference);
                const gp_Dir normal = gp_Dir(normalAxis == 0 ? 1 : 0, normalAxis == 1 ? 1 : 0, normalAxis == 2 ? 1 : 0).Transformed(reference);
                const double half = size * 2.0;
                const juce::uint32 planeColour = 0xff5fc8ff;
                const gp_XYZ corners[4] { d + (u.XYZ() + v.XYZ()) * half, d + (u.XYZ() - v.XYZ()) * half, d - (u.XYZ() + v.XYZ()) * half, d - (u.XYZ() - v.XYZ()) * half };
                for (int k = 0; k < 4; ++k) addLine(next->overlay, corners[k], corners[(k + 1) % 4], planeColour);
                addLine(next->overlay, d - u.XYZ() * half, d + u.XYZ() * half, 0xff3c7fa3);
                addLine(next->overlay, d - v.XYZ() * half, d + v.XYZ() * half, 0xff3c7fa3);
                // The handle itself: a yellow diamond in the plane at the desired point.
                const double h = size * 0.35;
                const gp_XYZ diamond[4] { d + u.XYZ() * h, d + v.XYZ() * h, d - u.XYZ() * h, d - v.XYZ() * h };
                for (int k = 0; k < 4; ++k) addLine(next->overlay, diamond[k], diamond[(k + 1) % 4], 0xffffe066);
                next->dragHandle.active = true;
                next->dragHandle.targetId = t.id;
                next->dragHandle.point = point3(d);
                next->dragHandle.normal = point3(normal.XYZ());
            }
        }
    }
    shownWorld = world;
    {
        const std::lock_guard<std::mutex> lock(poseLock);
        current = std::move(next);
    }
    if (onPoseChanged) onPoseChanged();
}
}
