#include "CadDocument.h"
#include "CadGear.h"

#include <atomic>
#include <cmath>

// Document commands for parametric gears. Geometry lives in CadGear.cpp; this file connects it to the OCAF
// document through the shared modeling command (single gears) and one mutate() transaction (pairs), and checks
// that a recorded pair still meshes: geometry, placement, phase, joints and the existing gear coupling.
namespace djehuti::cad
{
namespace
{
constexpr double pi = juce::MathConstants<double>::pi;
juce::var object() { return juce::var(new juce::DynamicObject()); }
juce::var copyJson(const juce::var& v) { return juce::JSON::fromString(juce::JSON::toString(v, false, 17)); }
juce::var strings(const juce::StringArray& values) { juce::Array<juce::var> out; for (const auto& v : values) out.add(v); return out; }
juce::String mm(double v) { return juce::String(v, 4) + " mm"; }
juce::var failure(const char* code, const juce::String& error)
{
    auto r = object(); r.getDynamicObject()->setProperty("ok", false); r.getDynamicObject()->setProperty("code", code); r.getDynamicObject()->setProperty("error", error); return r;
}
juce::String mateOf(const CadFeature& f) { return f.modeling["pair"]["mate"].toString(); }
bool isGear(const CadFeature* f) { return f != nullptr && gear::isGearType(f->type); }
// Overlays request fields on a pair definition; gearA/gearB objects merge field by field.
void overlay(juce::var& base, const juce::var& changes)
{
    if (!changes.isObject()) return;
    for (const auto& v : changes.getDynamicObject()->getProperties())
    {
        const auto key = v.name.toString();
        if ((key == "gearA" || key == "gearB") && v.value.isObject() && base[v.name].isObject())
        { for (const auto& g : v.value.getDynamicObject()->getProperties()) base[v.name].getDynamicObject()->setProperty(g.name, copyJson(g.value)); }
        else base.getDynamicObject()->setProperty(v.name, copyJson(v.value));
    }
}
}

bool CadDocument::gearShaft(const CadFeature& f, const juce::String& instanceId, gp_Pnt& origin, gp_Dir& axis) const
{
    origin = gp_Pnt(f.x, f.y, f.z);
    axis = gp::DZ().Transformed(placement(f));
    if (instanceId.isEmpty()) return true;
    gp_Trsf world; juce::String ignored;
    if (!referenceWorld(instanceId, world, ignored)) return false;
    const auto shape = instanceShapeTransform(f.id, world);
    origin.Transform(shape); axis.Transform(shape);
    return true;
}

juce::var CadDocument::gearPairStatus(const juce::String& id) const
{
    const auto* f = find(id);
    if (!isGear(f)) return failure("not_found", "Unknown gear feature: " + id);
    auto reply = object(); auto* out = reply.getDynamicObject();
    out->setProperty("ok", true); out->setProperty("id", id);
    const auto mate = mateOf(*f);
    out->setProperty("paired", mate.isNotEmpty());
    if (mate.isEmpty()) return reply;
    juce::StringArray problems, warnings;
    auto expected = object(), actual = object();
    const auto finish = [&]
    {
        out->setProperty("compatible", problems.isEmpty());
        out->setProperty("problems", strings(problems));
        out->setProperty("warnings", strings(warnings));
        out->setProperty("expected", expected); out->setProperty("actual", actual);
        return reply;
    };
    const auto* g = find(mate);
    if (!isGear(g)) { problems.add("The paired gear " + mate + " no longer exists. Unpair this gear or create a new pair."); return finish(); }
    if (mateOf(*g) != id) problems.add(g->name + " is no longer paired with " + f->name + ".");
    const bool selfIsB = f->modeling["pair"]["role"].toString() == "B";
    const auto& A = selfIsB ? *g : *f;
    const auto& B = selfIsB ? *f : *g;
    out->setProperty("gearA", A.id); out->setProperty("gearB", B.id);
    gear::Spur a, b; juce::String error;
    if (!gear::spur(A.modeling, a, error)) { problems.add(A.name + ": " + error); return finish(); }
    if (!gear::spur(B.modeling, b, error)) { problems.add(B.name + ": " + error); return finish(); }
    gear::Mesh values;
    gear::mesh(a, b, values, problems, warnings);
    if (values.centreDistance <= 0.0) return finish();
    expected.getDynamicObject()->setProperty("centreDistance", values.centreDistance);
    expected.getDynamicObject()->setProperty("ratio", values.ratio);
    expected.getDynamicObject()->setProperty("contactRatio", values.contactRatio);
    expected.getDynamicObject()->setProperty("workingPressureAngleDegrees", values.workingPressureAngle * 180.0 / pi);

    gp_Pnt oa, ob; gp_Dir xa, xb;
    gearShaft(A, {}, oa, xa); gearShaft(B, {}, ob, xb);
    const double dot = xa.Dot(xb);
    if (std::abs(dot) < 1.0 - 1.0e-9)
        problems.add("The gear axes are not parallel (" + juce::String(std::acos(juce::jlimit(-1.0, 1.0, std::abs(dot))) * 180.0 / pi, 4) + " degrees apart); spur gears need parallel shafts.");
    else
    {
        const gp_Vec d(oa, ob);
        const double along = d.Dot(gp_Vec(xa));
        const gp_Vec perpendicular = d - gp_Vec(xa) * along;
        const double distance = perpendicular.Magnitude();
        actual.getDynamicObject()->setProperty("centreDistance", distance);
        if (std::abs(distance - values.centreDistance) > 1.0e-6 * std::max(1.0, values.centreDistance))
            problems.add("The centre distance is " + mm(distance) + "; these gears mesh at " + mm(values.centreDistance) + ".");
        const double sense = dot > 0 ? 1.0 : -1.0;
        const double bLow = std::min(along, along + sense * b.faceWidth), bHigh = std::max(along, along + sense * b.faceWidth);
        const double overlap = std::min(a.faceWidth, bHigh) - std::max(0.0, bLow);
        actual.getDynamicObject()->setProperty("axialOverlap", overlap);
        if (overlap <= 1.0e-9) problems.add("The face widths do not overlap along the shafts, so the teeth cannot engage.");
        if (distance > 1.0e-9)
        {
            // Mesh condition: frac(phiA zA / 2pi + phiB zB / 2pi) = 1/2, with phi the local angle of the line of
            // centres (toward the partner) in each gear's own frame. It is invariant under coupled rotation.
            const gp_Dir toward(perpendicular);
            const auto localAngle = [&](const CadFeature& feature, const gp_Dir& direction)
            { const auto local = direction.Transformed(placement(feature).Inverted()); return std::atan2(local.Y(), local.X()); };
            double fraction = localAngle(A, toward) * a.teeth / (2.0 * pi) + localAngle(B, toward.Reversed()) * b.teeth / (2.0 * pi) - 0.5;
            fraction -= std::round(fraction);
            const double phaseError = std::abs(fraction) * pi * a.module;
            actual.getDynamicObject()->setProperty("phaseErrorAtPitch", phaseError);
            if (phaseError > 1.0e-6 * std::max(1.0, a.module))
            {
                const double turn = fraction * 360.0 / b.teeth * (xb.Z() >= 0 ? 1.0 : -1.0);
                problems.add("The tooth phase is off by " + mm(phaseError) + " at the pitch circle, so the teeth collide. Rotate " + B.name + " by " + juce::String(turn, 6) + " degrees about its axis, or regenerate the pair with cad_gear_pair update.");
            }
        }
    }

    // Mechanism: revolute joints carrying instances of either gear, and existing gear couplings between them.
    std::vector<const CadJoint*> jointsA, jointsB;
    for (const auto& joint : mechanics.joints)
    {
        if (joint.type != "revolute") continue;
        const auto* instance = mechanics.findInstance(joint.childInstance);
        if (instance == nullptr) continue;
        if (instance->featureId == A.id) jointsA.push_back(&joint);
        if (instance->featureId == B.id) jointsB.push_back(&joint);
    }
    juce::Array<juce::var> jointIds, couplings;
    const auto checkJoint = [&](const CadJoint* joint, const CadFeature& feature)
    {
        jointIds.add(joint->id);
        gp_Pnt gearOrigin; gp_Dir gearAxis;
        if (!gearShaft(feature, joint->childInstance, gearOrigin, gearAxis)) return;
        gp_Trsf parent; juce::String ignored;
        if (joint->parentInstance.isNotEmpty() && !referenceWorld(joint->parentInstance, parent, ignored)) return;
        const auto frame = parent.Multiplied(joint->parentToJoint.toTrsf());
        const auto jointOrigin = gp::Origin().Transformed(frame);
        const auto jointAxis = gp::DZ().Transformed(frame);
        const gp_Vec offset(jointOrigin, gearOrigin);
        const double miss = (offset - gp_Vec(jointAxis) * offset.Dot(gp_Vec(jointAxis))).Magnitude();
        if (std::abs(jointAxis.Dot(gearAxis)) < 1.0 - 1.0e-9 || miss > 1.0e-6 * std::max(1.0, values.centreDistance))
            problems.add("Joint " + joint->name + " does not turn about the axis of " + feature.name + ".");
    };
    for (const auto* joint : jointsA) checkJoint(joint, A);
    for (const auto* joint : jointsB) checkJoint(joint, B);
    const auto contains = [](const std::vector<const CadJoint*>& list, const juce::String& jointId)
    { for (const auto* j : list) if (j->id == jointId) return true; return false; };
    for (const auto& c : mechanics.couplings)
    {
        if (c.type != "gear") continue;
        const bool forward = contains(jointsA, c.jointA) && contains(jointsB, c.jointB);
        const bool backward = contains(jointsB, c.jointA) && contains(jointsA, c.jointB);
        if (!forward && !backward) continue;
        const double want = forward ? -(double) a.teeth / b.teeth : -(double) b.teeth / a.teeth;
        auto entry = object();
        entry.getDynamicObject()->setProperty("id", c.id); entry.getDynamicObject()->setProperty("ratio", c.ratio);
        entry.getDynamicObject()->setProperty("expectedRatio", want); entry.getDynamicObject()->setProperty("offset", c.offset);
        couplings.add(entry);
        if (std::abs(c.ratio - want) > 1.0e-9 * std::abs(want))
            problems.add("Coupling " + c.name + " has ratio " + juce::String(c.ratio, 9) + "; the tooth counts require " + juce::String(want, 9) + ".");
        // A phase offset is harmless only when it is a whole number of the driven gear's tooth pitches.
        const int drivenTeeth = forward ? b.teeth : a.teeth;
        const double pitches = c.offset * drivenTeeth / (2.0 * pi);
        if (std::abs(pitches - std::round(pitches)) * pi * a.module > 1.0e-6 * std::max(1.0, a.module))
            problems.add("Coupling " + c.name + " has a phase offset of " + juce::String(c.offset * 180.0 / pi, 6) + " degrees, which turns the driven gear out of engagement.");
    }
    if (couplings.isEmpty() && !jointsA.empty() && !jointsB.empty())
        warnings.add("Both gears have revolute joints but no gear coupling joins them. Use cad_gear_pair couple to add the correct ratio.");
    auto mechanism = object();
    mechanism.getDynamicObject()->setProperty("joints", jointIds);
    mechanism.getDynamicObject()->setProperty("couplings", couplings);
    out->setProperty("mechanism", mechanism);
    return finish();
}

juce::var CadDocument::gearCommand(const juce::var& args)
{
    if (!args.isObject()) return failure("invalid_arguments", "Arguments must be an object.");
    const auto action = args.getProperty("action", "get").toString();
    const auto id = args["id"].toString();
    if (action == "check") return gearPairStatus(id);
    if (action == "describe")
    {
        auto p = gear::defaults();
        if (args["parameters"].isObject()) for (const auto& v : args["parameters"].getDynamicObject()->getProperties()) p.getDynamicObject()->setProperty(v.name, copyJson(v.value));
        gear::Spur s; juce::String error;
        if (!gear::spur(p, s, error)) return failure("invalid_arguments", error);
        auto r = object(); r.getDynamicObject()->setProperty("ok", true); r.getDynamicObject()->setProperty("parameters", p); r.getDynamicObject()->setProperty("derived", gear::describe(s));
        return r;
    }
    auto reply = modelingCommand("gear", args);
    if (!(bool) reply["ok"]) return reply;
    if (reply["parameters"].isObject())
    {
        gear::Spur s; juce::String ignored;
        if (gear::spur(reply["parameters"], s, ignored)) reply.getDynamicObject()->setProperty("derived", gear::describe(s));
    }
    const auto resultId = reply["id"].toString();
    if (action != "delete" && action != "preview" && resultId.isNotEmpty())
        if (const auto* f = find(resultId); isGear(f) && mateOf(*f).isNotEmpty())
        {
            const auto status = gearPairStatus(resultId);
            reply.getDynamicObject()->setProperty("pairing", status);
            if (!(bool) status["compatible"])
            {
                juce::StringArray problems; for (const auto& p : *status["problems"].getArray()) problems.add(p.toString());
                reply.getDynamicObject()->setProperty("warning", f->name + " is paired, and the pair no longer meshes: " + problems.joinIntoString(" ") + " Use cad_gear_pair update to regenerate a compatible pair, or cad_gear_pair unpair.");
            }
        }
    return reply;
}

juce::var CadDocument::gearPairRequest(const CadFeature& A, const CadFeature& B) const
{
    auto r = gear::pairDefaults(); auto* o = r.getDynamicObject();
    for (const auto* key : { "module", "pressureAngleDegrees", "faceWidth", "addendumCoefficient", "dedendumCoefficient", "rootFilletCoefficient" }) o->setProperty(key, copyJson(A.modeling[key]));
    o->setProperty("backlash", (double) A.modeling["backlash"] + (double) B.modeling["backlash"]);
    using Entry = std::pair<const char*, const CadFeature*>;
    for (const auto& [key, feature] : { Entry { "gearA", &A }, Entry { "gearB", &B } })
        for (const auto* field : { "teeth", "profileShift", "boreDiameter" }) r[key].getDynamicObject()->setProperty(field, copyJson(feature->modeling[field]));
    o->setProperty("origin", juce::Array<juce::var> { A.x, A.y, A.z });
    o->setProperty("lineOfCentersDegrees", A.rz);
    o->setProperty("mechanism", "keep");
    return r;
}

juce::var CadDocument::gearPairParameters(const juce::String& gearId) const
{
    const auto* f = find(gearId);
    if (!isGear(f)) return {};
    const auto* g = find(mateOf(*f));
    if (!isGear(g) || mateOf(*g) != gearId) return {};
    const bool selfIsB = f->modeling["pair"]["role"].toString() == "B";
    return gearPairRequest(selfIsB ? *g : *f, selfIsB ? *f : *g);
}

bool CadDocument::applyGearPair(const gear::PairPlan& plan, const juce::String& existingA, const juce::String& existingB, juce::var& reply, juce::String& error)
{
    juce::String idA = existingA, idB = existingB;
    const bool update = existingA.isNotEmpty();
    juce::StringArray createdInstances, createdJoints, createdCouplings;
    const auto ok = mutate([&](juce::String& e)
    {
        if (!update)
        {
            for (const auto* role : { "A", "B" })
            {
                CadFeature f; f.id = newId("gear", nextNumber++); f.type = "gear";
                const bool isA = juce::String(role) == "A";
                const auto& name = isA ? plan.nameA : plan.nameB;
                f.name = name.isNotEmpty() ? name : "Gear " + juce::String(role) + " (" + juce::String(isA ? plan.a.teeth : plan.b.teeth) + "T)";
                items.push_back(f);
                (isA ? idA : idB) = f.id;
            }
        }
        std::map<juce::String, gp_Vec> moved;
        for (auto& f : items)
        {
            if (f.id != idA && f.id != idB) continue;
            const bool isA = f.id == idA;
            auto definition = copyJson(isA ? plan.gearA : plan.gearB);
            auto link = object(); link.getDynamicObject()->setProperty("mate", isA ? idB : idA); link.getDynamicObject()->setProperty("role", isA ? "A" : "B");
            definition.getDynamicObject()->setProperty("pair", link);
            const auto& centre = isA ? plan.centreA : plan.centreB;
            moved[f.id] = gp_Vec(centre[0] - f.x, centre[1] - f.y, centre[2] - f.z);
            f.modeling = definition;
            f.x = centre[0]; f.y = centre[1]; f.z = centre[2]; f.rx = 0.0; f.ry = 0.0; f.rz = isA ? plan.rotationA : plan.rotationB;
            if (update && (isA ? plan.nameA : plan.nameB).isNotEmpty()) f.name = isA ? plan.nameA : plan.nameB;
            f.visible = true;
        }
        const auto* A = find(idA); const auto* B = find(idB);
        if (A == nullptr || B == nullptr) { e = "The pair's gears no longer exist."; return false; }

        // Instances follow their gear's new placement; shaft joints are re-derived from the gear axes.
        for (auto& instance : mechanics.instances)
            if (const auto it = moved.find(instance.featureId); it != moved.end() && it->second.Magnitude() > 0.0)
            { gp_Trsf shift; shift.SetTranslation(it->second); instance.placement = CadFrame::fromTrsf(instance.placement.toTrsf().Multiplied(shift)); }
        const auto shaftRequest = [&](const CadFeature& feature, const juce::String& instance, const CadJoint* existing, CadJointRequest& request)
        {
            gp_Pnt origin; gp_Dir axis;
            if (!gearShaft(feature, instance, origin, axis)) return false;
            request = {};
            request.type = "revolute"; request.childInstance = instance;
            request.name = existing != nullptr ? existing->name : feature.name + " shaft";
            request.parentInstance = existing != nullptr ? existing->parentInstance : juce::String();
            request.originX = origin.X(); request.originY = origin.Y(); request.originZ = origin.Z();
            request.axisX = axis.X(); request.axisY = axis.Y(); request.axisZ = axis.Z();
            if (existing != nullptr) { request.reference = existing->reference; request.hasLimits = existing->hasLimits; request.lower = existing->lower; request.upper = existing->upper; }
            return true;
        };
        const auto featureOfJoint = [&](const CadJoint& joint) -> const CadFeature*
        {
            if (joint.type != "revolute") return nullptr;
            const auto* instance = mechanics.findInstance(joint.childInstance);
            if (instance == nullptr) return nullptr;
            return instance->featureId == idA ? A : instance->featureId == idB ? B : nullptr;
        };
        if (update)
            for (auto& joint : mechanics.joints)
                if (const auto* feature = featureOfJoint(joint))
                {
                    CadJointRequest request; auto updated = joint;
                    if (!shaftRequest(*feature, joint.childInstance, &joint, request) || !jointFramesFromRequest(request, updated, e)) { if (e.isEmpty()) e = "Unable to realign " + joint.name + "."; return false; }
                    joint = updated;
                }

        if (plan.mechanism == "create")
        {
            for (const auto* feature : { A, B })
            {
                bool hasInstance = false;
                for (const auto& instance : mechanics.instances) if (instance.featureId == feature->id) hasInstance = true;
                if (hasInstance) continue;
                CadMechanicalInstance instance;
                instance.id = newId("inst", nextNumber++); instance.name = feature->name + " instance"; instance.featureId = feature->id;
                instance.placement.x = feature->x; instance.placement.y = feature->y; instance.placement.z = feature->z;
                mechanics.instances.push_back(instance); createdInstances.add(instance.id);
            }
            for (const auto* feature : { A, B })
            {
                bool hasJoint = false;
                for (const auto& joint : mechanics.joints) if (featureOfJoint(joint) == feature) hasJoint = true;
                if (hasJoint) continue;
                juce::String instanceId;
                for (const auto& instance : mechanics.instances) if (instance.featureId == feature->id) { instanceId = instance.id; break; }
                CadJoint joint; joint.id = newId("joint", nextNumber++);
                CadJointRequest request;
                if (!shaftRequest(*feature, instanceId, nullptr, request) || !jointFramesFromRequest(request, joint, e)) { if (e.isEmpty()) e = "Unable to create the shaft joint of " + feature->name + "."; return false; }
                joint.role = feature == A ? "driver" : "dependent";
                mechanics.joints.push_back(joint); createdJoints.add(joint.id);
            }
        }
        // Existing gear couplings between the two shafts take the tooth-count ratio and the engaged phase.
        juce::String jointA, jointB;
        for (const auto& joint : mechanics.joints) { const auto* feature = featureOfJoint(joint); if (feature == A && jointA.isEmpty()) jointA = joint.id; if (feature == B && jointB.isEmpty()) jointB = joint.id; }
        bool coupled = false;
        for (auto& c : mechanics.couplings)
        {
            if (c.type != "gear") continue;
            const auto* fa = mechanics.findJoint(c.jointA); const auto* fb = mechanics.findJoint(c.jointB);
            if (fa == nullptr || fb == nullptr) continue;
            const auto* ga = featureOfJoint(*fa); const auto* gb = featureOfJoint(*fb);
            if (ga == nullptr || gb == nullptr || ga == gb) continue;
            c.ratio = ga == A ? plan.values.ratio : 1.0 / plan.values.ratio;
            c.offset = 0.0; coupled = true;
        }
        if (plan.mechanism == "create" && !coupled && jointA.isNotEmpty() && jointB.isNotEmpty())
        {
            CadCoupling c; c.id = newId("coupling", nextNumber++); c.type = "gear"; c.jointA = jointA; c.jointB = jointB;
            c.ratio = plan.values.ratio; c.offset = 0.0; c.name = "Gear mesh " + juce::String(plan.a.teeth) + ":" + juce::String(plan.b.teeth);
            if (!mech::validateCoupling(mechanics, c, e)) return false;
            mechanics.couplings.push_back(c); createdCouplings.add(c.id);
        }
        if (!mech::validateModel(mechanics, e)) return false;
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        return true;
    }, error);
    if (!ok) return false;
    reply = object(); auto* out = reply.getDynamicObject();
    out->setProperty("ok", true);
    out->setProperty("gearA", idA); out->setProperty("gearB", idB);
    out->setProperty("plan", plan.toJson());
    out->setProperty("createdInstances", strings(createdInstances));
    out->setProperty("createdJoints", strings(createdJoints));
    out->setProperty("createdCouplings", strings(createdCouplings));
    out->setProperty("pairing", gearPairStatus(idA));
    return true;
}

std::shared_ptr<const CadRenderSnapshot> CadDocument::gearPairPreview(const juce::var& args, juce::var& result, juce::String& error) const
{
    CadDocument draft; draft.geometryCache = geometryCache;
    if (!draft.deserialize(serialize(), error)) return {};
    auto request = copyJson(args);
    request.getDynamicObject()->setProperty("action", args["id"].toString().isEmpty() ? "create" : "update");
    result = draft.gearPairCommand(request);
    if (!(bool) result["ok"]) { error = result["error"].toString(); return {}; }
    result.getDynamicObject()->setProperty("preview", true);
    result.getDynamicObject()->setProperty("committed", false);
    static std::atomic<std::uint64_t> serial { 0 };
    auto view = std::make_shared<CadRenderSnapshot>(*draft.snapshot());
    view->documentInstance = instanceId;
    view->revision = (std::uint64_t(1) << 63) | (std::uint64_t(1) << 59) | ++serial;
    return view;
}

juce::var CadDocument::gearPairCommand(const juce::var& args)
{
    if (!args.isObject()) return failure("invalid_arguments", "Arguments must be an object.");
    const auto action = args.getProperty("action", "plan").toString();
    const auto id = args["id"].toString();
    juce::String error;
    if (action == "check") return gearPairStatus(id);
    if (action == "get")
    {
        const auto parameters = gearPairParameters(id);
        if (!parameters.isObject()) return failure("not_found", "Not a paired gear: " + id);
        const auto status = gearPairStatus(id);
        auto r = object(); auto* o = r.getDynamicObject();
        o->setProperty("ok", true); o->setProperty("gearA", status["gearA"]); o->setProperty("gearB", status["gearB"]);
        o->setProperty("parameters", parameters); o->setProperty("pairing", status);
        return r;
    }
    if (action == "preview")
    {
        juce::var details;
        if (gearPairPreview(args, details, error) == nullptr) return failure("gear_pair_failed", error);
        return details;
    }
    const auto pairOf = [&](const CadFeature*& A, const CadFeature*& B, juce::String& e)
    {
        const auto* f = find(id);
        if (!isGear(f)) { e = "Unknown gear feature: " + id; return false; }
        const auto* g = find(mateOf(*f));
        if (!isGear(g) || mateOf(*g) != id) { e = f->name + " is not part of a gear pair."; return false; }
        const bool selfIsB = f->modeling["pair"]["role"].toString() == "B";
        A = selfIsB ? g : f; B = selfIsB ? f : g;
        return true;
    };
    if (action == "plan" || action == "create" || action == "update")
    {
        if (args.hasProperty("parameters") && !args["parameters"].isObject()) return failure("invalid_arguments", "parameters must be an object.");
        juce::var request = juce::var(new juce::DynamicObject());
        const CadFeature* A = nullptr; const CadFeature* B = nullptr;
        if (action == "update")
        {
            if (!pairOf(A, B, error)) return failure("not_found", error);
            request = gearPairRequest(*A, *B);
            overlay(request, args["parameters"]);
        }
        else overlay(request, args["parameters"]);
        gear::PairPlan plan;
        if (!gear::planPair(request, plan, error)) return failure("invalid_arguments", error);
        if (action == "plan")
        {
            auto r = object(); r.getDynamicObject()->setProperty("ok", true); r.getDynamicObject()->setProperty("plan", plan.toJson()); return r;
        }
        if (action == "create" && plan.mechanism == "keep") plan.mechanism = "none";
        juce::var reply;
        if (!applyGearPair(plan, A != nullptr ? A->id : juce::String(), B != nullptr ? B->id : juce::String(), reply, error))
            return failure("gear_pair_failed", error.isEmpty() ? juce::String("OCCT could not construct the gear pair.") : error);
        return reply;
    }
    if (action == "unpair")
    {
        const auto* f = find(id);
        if (!isGear(f)) return failure("not_found", "Unknown gear feature: " + id);
        const auto mate = mateOf(*f);
        const auto ok = mutate([&](juce::String&)
        {
            for (auto& item : items)
                if ((item.id == id || (item.id == mate && mateOf(item) == id)) && gear::isGearType(item.type))
                { auto definition = copyJson(item.modeling); auto link = object(); link.getDynamicObject()->setProperty("mate", ""); link.getDynamicObject()->setProperty("role", ""); definition.getDynamicObject()->setProperty("pair", link); item.modeling = definition; }
            return true;
        }, error);
        if (!ok) return failure("gear_pair_failed", error);
        auto r = object(); r.getDynamicObject()->setProperty("ok", true); r.getDynamicObject()->setProperty("id", id); r.getDynamicObject()->setProperty("unpaired", true); return r;
    }
    if (action == "couple")
    {
        const CadFeature* A = nullptr; const CadFeature* B = nullptr;
        if (!pairOf(A, B, error)) return failure("not_found", error);
        const auto jointA = args["jointA"].toString(), jointB = args["jointB"].toString();
        const auto idA = A->id, idB = B->id;
        juce::String couplingId;
        const auto ok = mutate([&](juce::String& e)
        {
            const auto* ja = mechanics.findJoint(jointA); const auto* jb = mechanics.findJoint(jointB);
            if (ja == nullptr || jb == nullptr || ja->type != "revolute" || jb->type != "revolute") { e = "Choose two existing revolute joints (jointA drives jointB)."; return false; }
            const auto* ia = mechanics.findInstance(ja->childInstance); const auto* ib = mechanics.findInstance(jb->childInstance);
            if (ia == nullptr || ib == nullptr || !((ia->featureId == idA && ib->featureId == idB) || (ia->featureId == idB && ib->featureId == idA)))
            { e = "jointA and jointB must carry instances of the two paired gears."; return false; }
            gear::Spur sa, sb; juce::String ignored;
            if (!gear::spur(find(ia->featureId)->modeling, sa, ignored) || !gear::spur(find(ib->featureId)->modeling, sb, ignored)) { e = "A paired gear definition is invalid."; return false; }
            const double ratio = -(double) sa.teeth / sb.teeth;
            for (auto& c : mechanics.couplings)
                if (c.type == "gear" && ((c.jointA == jointA && c.jointB == jointB) || (c.jointA == jointB && c.jointB == jointA)))
                { c.ratio = c.jointA == jointA ? ratio : 1.0 / ratio; c.offset = 0.0; couplingId = c.id; }
            if (couplingId.isEmpty())
            {
                CadCoupling c; c.id = newId("coupling", nextNumber++); c.type = "gear"; c.jointA = jointA; c.jointB = jointB; c.ratio = ratio; c.offset = 0.0;
                c.name = "Gear mesh " + juce::String(sa.teeth) + ":" + juce::String(sb.teeth);
                if (!mech::validateCoupling(mechanics, c, e)) return false;
                mechanics.couplings.push_back(c); couplingId = c.id;
            }
            if (!mech::validateModel(mechanics, e)) return false;
            const auto status = gearPairStatus(idA);
            if (!(bool) status["compatible"])
            { juce::StringArray problems; for (const auto& p : *status["problems"].getArray()) problems.add(p.toString()); e = "The gears do not mesh as coupled: " + problems.joinIntoString(" "); return false; }
            ++mechanics.interfaceRevision; ++mechanics.profileRevision;
            return true;
        }, error);
        if (!ok) return failure("gear_pair_failed", error);
        auto r = object(); r.getDynamicObject()->setProperty("ok", true); r.getDynamicObject()->setProperty("coupling", couplingId); r.getDynamicObject()->setProperty("pairing", gearPairStatus(idA)); return r;
    }
    return failure("invalid_arguments", "Action must be plan, get, preview, create, update, check, couple or unpair.");
}
}
