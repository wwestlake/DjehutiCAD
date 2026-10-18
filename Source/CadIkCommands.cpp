#include "CadIkCommands.h"

namespace djehuti::cad::ikcmd
{
namespace
{
const double kDegToRad = juce::MathConstants<double>::pi / 180.0;

juce::String reply(bool ok, const juce::String& error, std::function<void(juce::DynamicObject&)> fill = {})
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty("ok", ok);
    if (error.isNotEmpty()) o->setProperty("error", error);
    if (fill) fill(*o);
    return juce::JSON::toString(juce::var(o.release()), true);
}

bool vector3(const juce::var& value, double out[3])
{
    const auto* list = value.getArray();
    if (list == nullptr || list->size() != 3) return false;
    for (int k = 0; k < 3; ++k) { out[k] = (double) (*list)[k]; if (!std::isfinite(out[k])) return false; }
    return true;
}

// Origin + extrinsic XYZ degrees -> frame; keeps the existing part when a field is absent.
bool readFrame(const juce::var& v, const char* originKey, const char* rotationKey, CadFrame& frame, juce::String& error, const juce::String& label)
{
    double origin[3] { frame.x, frame.y, frame.z }, rotation[3] {};
    mech::eulerDegreesFromFrame(frame, rotation[0], rotation[1], rotation[2]);
    if (v.hasProperty(originKey) && !vector3(v.getProperty(originKey, {}), origin)) { error = label + ": " + originKey + " must be three finite numbers (mm)."; return false; }
    if (v.hasProperty(rotationKey) && !vector3(v.getProperty(rotationKey, {}), rotation)) { error = label + ": " + rotationKey + " must be three finite angles in degrees."; return false; }
    frame = mech::frameFromEulerDegrees(origin[0], origin[1], origin[2], rotation[0], rotation[1], rotation[2]);
    return true;
}

// Applies the JSON fields onto task (fields that are absent keep their current values).
bool applyTask(const juce::var& args, CadIkTask& task, CadMotionRuntime& runtime, juce::String& error)
{
    if (args.hasProperty("name")) task.name = args.getProperty("name", "").toString().trim();
    if (const auto* joints = args.getProperty("adjustableJoints", {}).getArray())
    {
        task.adjustableJoints.clear();
        for (const auto& j : *joints) task.adjustableJoints.add(j.toString());
    }
    if (args.hasProperty("positionToleranceMm")) task.positionToleranceMm = (double) args.getProperty("positionToleranceMm", 0.0);
    if (args.hasProperty("angleToleranceDeg")) task.angleToleranceRad = (double) args.getProperty("angleToleranceDeg", 0.0) * kDegToRad;
    if (const auto* list = args.getProperty("targets", {}).getArray())
    {
        std::vector<CadIkTarget> targets;
        for (const auto& v : *list)
        {
            CadIkTarget t;
            const auto id = v.getProperty("id", "").toString();
            for (const auto& existing : task.targets) if (id.isNotEmpty() && existing.id == id) t = existing;
            t.id = id;
            const auto label = "Target " + (id.isNotEmpty() ? id : juce::String(targets.size() + 1));
            if (v.hasProperty("name")) t.name = v.getProperty("name", "").toString().trim();
            if (v.hasProperty("instance")) t.instanceId = v.getProperty("instance", "").toString();
            if (v.hasProperty("referenceInstance")) { t.referenceInstance = v.getProperty("referenceInstance", "").toString(); if (t.referenceInstance == "world") t.referenceInstance.clear(); }
            if (v.hasProperty("mode")) t.mode = v.getProperty("mode", "pose").toString();
            if (v.hasProperty("enabled")) t.enabled = (bool) v.getProperty("enabled", true);
            if (t.name.isEmpty()) t.name = "Target " + juce::String(targets.size() + 1);
            if (!readFrame(v, "toolOrigin", "toolRotationDeg", t.toolFrame, error, label) || !readFrame(v, "desiredOrigin", "desiredRotationDeg", t.desired, error, label)) return false;
            if ((bool) v.getProperty("captureCurrent", false) && !runtime.captureTarget(t, error)) return false;
            targets.push_back(t);
        }
        task.targets = std::move(targets);
    }
    return true;
}

// Solve-only overrides on a copy: desired pose, mode or enabled per target id; nothing is saved.
bool applyOverrides(const juce::var& args, CadIkTask& task, juce::String& error)
{
    const auto* list = args.getProperty("targets", {}).getArray();
    if (list == nullptr) return true;
    for (const auto& v : *list)
    {
        const auto id = v.getProperty("id", "").toString();
        CadIkTarget* target = nullptr;
        for (auto& t : task.targets) if (t.id == id) target = &t;
        if (target == nullptr) { error = "The task has no target '" + id + "'."; return false; }
        if (v.hasProperty("mode")) target->mode = v.getProperty("mode", "pose").toString();
        if (v.hasProperty("enabled")) target->enabled = (bool) v.getProperty("enabled", true);
        if (!readFrame(v, "desiredOrigin", "desiredRotationDeg", target->desired, error, "Target " + id)) return false;
    }
    return true;
}

juce::var taskSummary(const CadIkTask& task)
{
    auto json = mech::ikTaskToJson(task);
    if (auto* o = json.getDynamicObject())
    {
        o->setProperty("angleToleranceDeg", task.angleToleranceRad / kDegToRad);
        if (auto* targets = o->getProperty("targets").getArray())
            for (int i = 0; i < targets->size() && i < (int) task.targets.size(); ++i)
                if (auto* to = (*targets)[i].getDynamicObject())
                {
                    double rx = 0, ry = 0, rz = 0;
                    mech::eulerDegreesFromFrame(task.targets[(size_t) i].desired, rx, ry, rz);
                    to->setProperty("desiredRotationDeg", juce::Array<juce::var> { rx, ry, rz });
                    mech::eulerDegreesFromFrame(task.targets[(size_t) i].toolFrame, rx, ry, rz);
                    to->setProperty("toolRotationDeg", juce::Array<juce::var> { rx, ry, rz });
                }
    }
    return json;
}
}

juce::String execute(const juce::String& command, const juce::var& args, CadCommandService& service, CadMotionRuntime& runtime)
{
    const auto& model = service.document().mechanicsData();
    const auto taskId = args.getProperty("task", "").toString();
    const auto* stored = taskId.isNotEmpty() ? model.findIkTask(taskId) : nullptr;
    if (taskId.isNotEmpty() && stored == nullptr && command != "create") return reply(false, "Unknown IK task: " + taskId);
    juce::String error, id;

    if (command == "create")
    {
        CadIkTask task;
        if (!applyTask(args, task, runtime, error) || !service.createIkTask(task, id, error)) return reply(false, error);
        const auto* created = service.document().mechanicsData().findIkTask(id);
        return reply(true, {}, [&](juce::DynamicObject& o) { o.setProperty("id", id); o.setProperty("task", taskSummary(*created)); });
    }
    if (command == "update")
    {
        if (stored == nullptr) return reply(false, "Name the IK task to update ('task').");
        auto task = *stored;
        if (!applyTask(args, task, runtime, error) || !service.updateIkTask(taskId, task, error)) return reply(false, error);
        return reply(true, {}, [&](juce::DynamicObject& o) { o.setProperty("task", taskSummary(*service.document().mechanicsData().findIkTask(taskId))); });
    }
    if (command == "remove")
    {
        if (stored == nullptr) return reply(false, "Name the IK task to remove ('task').");
        if (!service.removeIkTask(taskId, error)) return reply(false, error);
        return reply(true, {});
    }
    if (command == "inspect")
    {
        return reply(true, {}, [&](juce::DynamicObject& o)
        {
            if (stored != nullptr) { o.setProperty("task", taskSummary(*stored)); o.setProperty("status", runtime.ikStatus(stored)); return; }
            juce::Array<juce::var> list;
            for (const auto& t : model.ikTasks) list.add(taskSummary(t));
            o.setProperty("tasks", list);
        });
    }
    if (command == "status")
    {
        const auto* task = stored != nullptr ? stored : model.findIkTask(runtime.lastIkTaskId());
        return reply(true, {}, [&](juce::DynamicObject& o) { o.setProperty("status", runtime.ikStatus(task)); });
    }
    if (command == "resetPreview")
    {
        if (runtime.driver() == CadMotionRuntime::Driver::simulation) return reply(false, "DEWB owns the mechanism pose while the motion link is bound; the preview cannot be reset locally.");
        runtime.resetToReference();
        return reply(true, {});
    }
    if (command == "solve")
    {
        // A stored task (with optional, unsaved per-target overrides) or a complete ad hoc definition.
        CadIkTask task;
        if (stored != nullptr) { task = *stored; if (!applyOverrides(args, task, error)) return reply(false, error); }
        else if (!applyTask(args, task, runtime, error)) return reply(false, error);
        if (stored == nullptr)
            for (size_t i = 0; i < task.targets.size(); ++i) if (task.targets[i].id.isEmpty()) task.targets[i].id = "adhoc-" + juce::String((int) i + 1);
        const auto result = runtime.solveIk(task);
        return reply(result.valid, result.valid ? juce::String() : result.message, [&](juce::DynamicObject& o)
        {
            o.setProperty("published", result.valid);
            o.setProperty("result", result.toJson());
            o.setProperty("status", runtime.ikStatus(&task));
        });
    }
    return reply(false, "Unknown IK command '" + command + "'. Use create, update, remove, inspect, solve, status or resetPreview.");
}
}
