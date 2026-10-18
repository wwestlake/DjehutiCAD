#include "CadKinematics.h"

#include <gp_Ax1.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>
#include <math_Jacobi.hxx>
#include <math_Matrix.hxx>
#include <math_SVD.hxx>
#include <math_Vector.hxx>

#include <cmath>
#include <deque>
#include <set>

namespace djehuti::cad::kin
{
juce::String statusName(Status status)
{
    switch (status)
    {
        case Status::solved: return "solved";
        case Status::underconstrained: return "underconstrained";
        case Status::singular: return "singular";
        case Status::conflicting: return "conflicting";
        case Status::boundViolation: return "boundViolation";
        case Status::notConverged: return "notConverged";
        case Status::invalidInput: return "invalidInput";
        case Status::cancelled: return "cancelled";
        case Status::targetNotReached: return "targetNotReached";
        case Status::limitRestricted: return "limitRestricted";
    }
    return "unknown";
}

namespace
{
const juce::String kWorld = "world";
const double kRadToDeg = 180.0 / juce::MathConstants<double>::pi;

// Rotation vector (axis * angle, angle in [0, pi]) of a relative transform; the quaternion sign is normalised
// so q and -q give the same, shortest rotation.
gp_Vec rotationVector(const gp_Trsf& relative)
{
    auto q = relative.GetRotation();
    q.Normalize();
    if (q.W() < 0.0) q = gp_Quaternion(-q.X(), -q.Y(), -q.Z(), -q.W());
    const gp_Vec im(q.X(), q.Y(), q.Z());
    const auto s = im.Magnitude();
    if (s < 1.0e-15) return gp_Vec(0, 0, 0);
    const auto angle = 2.0 * std::atan2(s, q.W());
    return im * (angle / s);
}

gp_Trsf frameOf(const std::map<juce::String, gp_Trsf>& world, const juce::String& id)
{
    if (id.isEmpty()) return gp_Trsf();
    const auto found = world.find(id);
    return found != world.end() ? found->second : gp_Trsf();
}

juce::var originJson(const gp_Trsf& t) { const auto p = t.TranslationPart(); return juce::Array<juce::var> { p.X(), p.Y(), p.Z() }; }
}

juce::var TargetReport::toJson() const
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty("id", id); o->setProperty("name", name); o->setProperty("mode", mode); o->setProperty("enabled", enabled); o->setProperty("reached", reached);
    o->setProperty("positionErrorMm", positionErrorMm); o->setProperty("angleErrorRad", angleErrorRad); o->setProperty("angleErrorDeg", angleErrorRad * kRadToDeg);
    o->setProperty("desired", mech::frameToJson(desired, 1.0)); o->setProperty("achieved", mech::frameToJson(achieved, 1.0));
    o->setProperty("desiredWorldOriginMm", originJson(desiredWorld)); o->setProperty("achievedWorldOriginMm", originJson(achievedWorld));
    return juce::var(o.release());
}

CadFrame achievedInReference(const CadIkTarget& target, const std::map<juce::String, gp_Trsf>& world)
{
    const auto tool = frameOf(world, target.instanceId).Multiplied(target.toolFrame.toTrsf());
    return CadFrame::fromTrsf(frameOf(world, target.referenceInstance).Inverted().Multiplied(tool));
}

TargetReport evaluateTarget(const CadIkTarget& target, const std::map<juce::String, gp_Trsf>& world, double positionToleranceMm, double angleToleranceRad)
{
    TargetReport r;
    r.id = target.id; r.name = target.name.isNotEmpty() ? target.name : target.id; r.mode = target.mode; r.enabled = target.enabled;
    const auto reference = frameOf(world, target.referenceInstance);
    r.achievedWorld = frameOf(world, target.instanceId).Multiplied(target.toolFrame.toTrsf());
    r.desiredWorld = reference.Multiplied(target.desired.toTrsf());
    r.achieved = CadFrame::fromTrsf(reference.Inverted().Multiplied(r.achievedWorld));
    r.desired = target.desired;
    r.positionErrorMm = gp_Pnt(r.achievedWorld.TranslationPart()).Distance(gp_Pnt(r.desiredWorld.TranslationPart()));
    r.angleErrorRad = rotationVector(r.achievedWorld.Multiplied(r.desiredWorld.Inverted())).Magnitude();
    const bool positionOk = target.mode == "orientation" || r.positionErrorMm <= positionToleranceMm;
    const bool angleOk = target.mode == "position" || r.angleErrorRad <= angleToleranceRad;
    r.reached = positionOk && angleOk && std::isfinite(r.positionErrorMm) && std::isfinite(r.angleErrorRad);
    return r;
}

juce::String SolveResult::summary() const
{
    juce::String text = statusName(status);
    if (valid) text << " | closure " << juce::String(positionResidualMm, 9) << " mm, " << juce::String(angleResidualRad, 12) << " rad";
    if (freeCoordinates > 0) text << " | " << freeCoordinates << " free coordinate(s)";
    if (rigidBodyFreedom > 0) text << " | " << rigidBodyFreedom << " rigid-body DOF unconstrained";
    if (redundantEquations > 0) text << " | " << redundantEquations << " redundant equation(s)";
    if (inverse && !targets.empty())
    {
        int reached = 0;
        for (const auto& t : targets) if (t.reached) ++reached;
        text << " | targets reached " << reached << "/" << (int) targets.size();
    }
    return text;
}

juce::var SolveResult::toJson() const
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty("status", statusName(status)); o->setProperty("valid", valid); o->setProperty("message", message);
    o->setProperty("positionResidualMm", positionResidualMm); o->setProperty("angleResidualRad", angleResidualRad); o->setProperty("couplingResidual", couplingResidual);
    o->setProperty("iterations", iterations); o->setProperty("substeps", substeps); o->setProperty("unknowns", unknowns); o->setProperty("equations", equations);
    o->setProperty("rank", rank); o->setProperty("freeCoordinates", freeCoordinates); o->setProperty("redundantEquations", redundantEquations); o->setProperty("rigidBodyFreedom", rigidBodyFreedom);
    juce::Array<juce::var> free, floating, problemList, warningList; auto coords = std::make_unique<juce::DynamicObject>();
    for (const auto& j : freeJoints) free.add(j);
    for (const auto& i : floatingInstances) floating.add(i);
    for (const auto& p : problems) problemList.add(p);
    for (const auto& w : warnings) warningList.add(w);
    for (const auto& [id, value] : coordinates) coords->setProperty(id, value);
    o->setProperty("freeJoints", free); o->setProperty("floatingInstances", floating); o->setProperty("problems", problemList); o->setProperty("warnings", warningList);
    o->setProperty("coordinates", juce::var(coords.release()));
    if (inverse)
    {
        juce::Array<juce::var> targetList, limited;
        for (const auto& t : targets) targetList.add(t.toJson());
        for (const auto& j : limitedJoints) limited.add(j);
        o->setProperty("inverse", true); o->setProperty("candidateConsistent", candidateConsistent);
        o->setProperty("targets", targetList); o->setProperty("limitedJoints", limited);
    }
    return juce::var(o.release());
}

namespace
{
// Internal spanning forest over the authored mechanical graph. Tree edges position bodies; every other joint
// becomes a closure equation. The authored joints are referenced, never modified.
struct Graph
{
    struct Edge { const CadJoint* joint = nullptr; juce::String parent, child; };
    std::vector<Edge> edges;
    std::map<juce::String, std::vector<int>> adjacency;
    std::map<juce::String, int> treeEdge;      // node -> edge index (-1 for roots)
    std::map<juce::String, bool> forward;      // node is the child of its tree edge
    std::vector<juce::String> order;           // roots first, then breadth-first
    std::vector<int> closures;
    std::set<juce::String> fixedRoots;         // world and grounded instances
    std::set<juce::String> grounded;           // every node connected to the world or a grounded instance
    std::vector<juce::String> floatingRoots;
    std::map<juce::String, gp_Trsf> rootPose;

    void build(const CadMechanics& model)
    {
        for (const auto& joint : model.joints)
        {
            Edge e { &joint, joint.parentInstance.isEmpty() ? kWorld : joint.parentInstance, joint.childInstance };
            adjacency[e.parent].push_back((int) edges.size());
            adjacency[e.child].push_back((int) edges.size());
            edges.push_back(e);
        }
        std::vector<bool> used(edges.size(), false);
        std::set<juce::String> visited;
        auto grow = [&](std::deque<juce::String> queue)
        {
            while (!queue.empty())
            {
                const auto node = queue.front(); queue.pop_front();
                order.push_back(node);
                for (const auto index : adjacency[node])
                {
                    if (used[(size_t) index]) continue;
                    used[(size_t) index] = true;
                    const auto& e = edges[(size_t) index];
                    const auto other = e.parent == node ? e.child : e.parent;
                    if (visited.insert(other).second) { treeEdge[other] = index; forward[other] = e.child == other; queue.push_back(other); }
                    else closures.push_back(index);
                }
            }
        };
        std::deque<juce::String> groundedQueue { kWorld };
        visited.insert(kWorld); treeEdge[kWorld] = -1; fixedRoots.insert(kWorld); rootPose[kWorld] = gp_Trsf();
        for (const auto& instance : model.instances)
            if (instance.grounded && visited.insert(instance.id).second)
            {
                treeEdge[instance.id] = -1; fixedRoots.insert(instance.id); rootPose[instance.id] = instance.placement.toTrsf(); groundedQueue.push_back(instance.id);
            }
        grow(groundedQueue);
        grounded = visited;
        for (const auto& instance : model.instances)
            if (visited.insert(instance.id).second)
            {
                treeEdge[instance.id] = -1; floatingRoots.push_back(instance.id); rootPose[instance.id] = instance.placement.toTrsf();
                grow({ instance.id });
            }
        for (size_t i = 0; i < edges.size(); ++i) if (!used[i]) closures.push_back((int) i);   // edges not reached (cannot happen) stay checked
    }
};

struct Config
{
    std::map<juce::String, gp_Trsf> world;
    std::map<juce::String, std::vector<std::pair<int, int>>> path;   // node -> (edge, sign) from its root
    std::map<int, gp_Trsf> jointFrame;                                // edge -> joint frame in world after motion
};

Config evaluateConfig(const Graph& graph, const std::map<juce::String, double>& q)
{
    Config c;
    auto coordinate = [&](const CadJoint& j) { const auto it = q.find(j.id); return it != q.end() ? it->second : j.reference; };
    for (const auto& node : graph.order)
    {
        const auto tree = graph.treeEdge.at(node);
        if (tree < 0) { c.world[node] = graph.rootPose.at(node); c.path[node] = {}; continue; }
        const auto& e = graph.edges[(size_t) tree];
        const auto local = e.joint->parentToJoint.toTrsf().Multiplied(mech::motion(*e.joint, coordinate(*e.joint)));
        if (graph.forward.at(node))
        {
            const auto frame = c.world[e.parent].Multiplied(local);
            c.jointFrame[tree] = frame;
            c.world[node] = frame.Multiplied(e.joint->jointToChild.toTrsf());
            c.path[node] = c.path[e.parent]; c.path[node].push_back({ tree, +1 });
        }
        else
        {
            c.world[node] = c.world[e.child].Multiplied(local.Multiplied(e.joint->jointToChild.toTrsf()).Inverted());
            c.jointFrame[tree] = c.world[node].Multiplied(local);
            c.path[node] = c.path[e.child]; c.path[node].push_back({ tree, -1 });
        }
    }
    for (const auto index : graph.closures)
    {
        const auto& e = graph.edges[(size_t) index];
        c.jointFrame[index] = c.world[e.parent].Multiplied(e.joint->parentToJoint.toTrsf()).Multiplied(mech::motion(*e.joint, coordinate(*e.joint)));
    }
    return c;
}

// Instantaneous motion of a point p (mm) per unit joint coordinate: angular velocity w and linear velocity v.
void twist(const Graph& graph, const Config& c, int edge, const gp_Pnt& p, gp_Vec& v, gp_Vec& w)
{
    const auto& joint = *graph.edges[(size_t) edge].joint;
    const auto& frame = c.jointFrame.at(edge);
    const gp_Vec axis(gp_Dir(0, 0, 1).Transformed(frame));
    if (joint.type == "revolute") { w = axis; v = axis.Crossed(gp_Vec(gp_Pnt(frame.TranslationPart()), p)); }   // w x (p - origin)
    else if (joint.type == "prismatic") { w = gp_Vec(0, 0, 0); v = axis * 1000.0; }
    else { w = gp_Vec(0, 0, 0); v = gp_Vec(0, 0, 0); }
}

struct Context
{
    const Graph& graph;
    const Config& config;
    const std::map<juce::String, double>& values;
    const std::map<juce::String, int>& column;   // unknown joint id -> column
    double length;                              // characteristic length, mm
};

// Physical errors: mechanism terms in mm / rad / coupling units; targets as the worst error-to-tolerance ratio.
struct Errors { double positionMm = 0.0, angleRad = 0.0, coupling = 0.0, targetRatio = 0.0; };

// A block of residual equations. Implementations fill rows [row, row + size()) of r and J, and fold their
// physical error into Errors (adding a problem line for mechanism terms that exceed their tolerance).
struct Term
{
    virtual ~Term() = default;
    virtual int size() const = 0;
    virtual void evaluate(const Context& ctx, math_Vector& r, math_Matrix& J, int row) const = 0;
    virtual void physicalError(const Context& ctx, const math_Vector& r, int row, Errors& errors, juce::StringArray* problems, const Tolerances& tol) const = 0;
};

// Adds the motion of point p carried by the chain from a root to `node` (sign +1) or subtracts it (-1).
// Position rows (if any) start at posRow, rotation rows at rotRow; -1 skips that block.
void addChain(const Context& ctx, const juce::String& node, double sign, const gp_Pnt& p, math_Matrix& J, int posRow, int rotRow, double scale)
{
    if (node.isEmpty()) return;
    for (const auto& [index, edgeSign] : ctx.config.path.at(node))
    {
        const auto it = ctx.column.find(ctx.graph.edges[(size_t) index].joint->id);
        if (it == ctx.column.end()) continue;
        gp_Vec v, w; twist(ctx.graph, ctx.config, index, p, v, w);
        const double s = sign * edgeSign * scale;
        const int col = it->second;
        if (posRow >= 0) { J(posRow, col) += s * v.X() / ctx.length; J(posRow + 1, col) += s * v.Y() / ctx.length; J(posRow + 2, col) += s * v.Z() / ctx.length; }
        if (rotRow >= 0) { J(rotRow, col) += s * w.X(); J(rotRow + 1, col) += s * w.Y(); J(rotRow + 2, col) += s * w.Z(); }
    }
}

// world(parent) * parentToJoint * motion(q) * jointToChild must coincide with world(child).
struct ClosureTerm final : Term
{
    int edge;
    juce::String name;
    explicit ClosureTerm(int e) : edge(e) {}
    int size() const override { return 6; }
    void evaluate(const Context& ctx, math_Vector& r, math_Matrix& J, int row) const override
    {
        const auto& e = ctx.graph.edges[(size_t) edge];
        const auto t1 = ctx.config.jointFrame.at(edge).Multiplied(e.joint->jointToChild.toTrsf());
        const auto& t2 = ctx.config.world.at(e.child);
        const gp_Pnt p1(t1.TranslationPart()), p2(t2.TranslationPart());
        const auto rot = rotationVector(t1.Multiplied(t2.Inverted()));
        const gp_Vec dp(p2, p1);
        r(row) = dp.X() / ctx.length; r(row + 1) = dp.Y() / ctx.length; r(row + 2) = dp.Z() / ctx.length;
        r(row + 3) = rot.X(); r(row + 4) = rot.Y(); r(row + 5) = rot.Z();
        addChain(ctx, e.parent, 1.0, p1, J, row, row + 3, 1.0);
        if (const auto it = ctx.column.find(e.joint->id); it != ctx.column.end())
        {
            gp_Vec v, w; twist(ctx.graph, ctx.config, edge, p1, v, w);
            const auto col = it->second;
            J(row, col) += v.X() / ctx.length; J(row + 1, col) += v.Y() / ctx.length; J(row + 2, col) += v.Z() / ctx.length;
            J(row + 3, col) += w.X(); J(row + 4, col) += w.Y(); J(row + 5, col) += w.Z();
        }
        addChain(ctx, e.child, -1.0, p2, J, row, row + 3, 1.0);
    }
    void physicalError(const Context& ctx, const math_Vector& r, int row, Errors& errors, juce::StringArray* problems, const Tolerances& tol) const override
    {
        const auto p = std::sqrt(r(row) * r(row) + r(row + 1) * r(row + 1) + r(row + 2) * r(row + 2)) * ctx.length;
        const auto a = std::sqrt(r(row + 3) * r(row + 3) + r(row + 4) * r(row + 4) + r(row + 5) * r(row + 5));
        errors.positionMm = juce::jmax(errors.positionMm, p); errors.angleRad = juce::jmax(errors.angleRad, a);
        if (problems != nullptr && (p > tol.positionMm || a > tol.angleRad)) problems->add("loop closure at joint " + name + ": " + juce::String(p, 6) + " mm, " + juce::String(a, 8) + " rad");
    }
};

// (qB - refB) - k (qA - refA) - offset = 0, scaled by the characteristic length when B is prismatic.
struct CouplingTerm final : Term
{
    const CadCoupling* coupling; const CadJoint* a; const CadJoint* b;
    CouplingTerm(const CadCoupling* c, const CadJoint* ja, const CadJoint* jb) : coupling(c), a(ja), b(jb) {}
    int size() const override { return 1; }
    double scale(const Context& ctx) const { return b->type == "prismatic" ? ctx.length / 1000.0 : 1.0; }
    void evaluate(const Context& ctx, math_Vector& r, math_Matrix& J, int row) const override
    {
        const auto k = mech::couplingFactor(*coupling), s = scale(ctx);
        r(row) = ((ctx.values.at(b->id) - b->reference) - k * (ctx.values.at(a->id) - a->reference) - coupling->offset) / s;
        if (const auto it = ctx.column.find(b->id); it != ctx.column.end()) J(row, it->second) += 1.0 / s;
        if (const auto it = ctx.column.find(a->id); it != ctx.column.end()) J(row, it->second) -= k / s;
    }
    void physicalError(const Context& ctx, const math_Vector& r, int row, Errors& errors, juce::StringArray* problems, const Tolerances& tol) const override
    {
        const auto c = std::abs(r(row)) * scale(ctx);
        errors.coupling = juce::jmax(errors.coupling, c);
        if (problems != nullptr && c > tol.coupling) problems->add("coupling " + coupling->name + ": residual " + juce::String(c, 10) + (b->type == "prismatic" ? " m" : " rad"));
    }
};

// IK endpoint target: world(instance) * tool must coincide with world(reference) * goal in the rows the mode
// selects. Both frames come from the same candidate configuration, so a moving reference is handled exactly
// like the two sides of a loop closure. Target rows are weighted below the mechanism rows so the search
// favours keeping the machine together over reaching an unreachable target.
struct TargetTerm final : Term
{
    static constexpr double kWeight = 0.1;
    const CadIkTarget* target;
    gp_Trsf tool, goal;   // goal: desired tool pose in the reference frame for the current continuation step
    double positionTol, angleTol;
    bool position, rotation;
    TargetTerm(const CadIkTarget* t, double pTol, double aTol)
        : target(t), tool(t->toolFrame.toTrsf()), goal(t->desired.toTrsf()), positionTol(pTol), angleTol(aTol), position(t->mode != "orientation"), rotation(t->mode != "position") {}
    int size() const override { return (position ? 3 : 0) + (rotation ? 3 : 0); }
    void frames(const Context& ctx, gp_Trsf& endpoint, gp_Trsf& desired) const
    {
        endpoint = ctx.config.world.at(target->instanceId).Multiplied(tool);
        desired = (target->referenceInstance.isEmpty() ? gp_Trsf() : ctx.config.world.at(target->referenceInstance)).Multiplied(goal);
    }
    void evaluate(const Context& ctx, math_Vector& r, math_Matrix& J, int row) const override
    {
        gp_Trsf endpoint, desired; frames(ctx, endpoint, desired);
        const gp_Pnt p1(endpoint.TranslationPart()), p2(desired.TranslationPart());
        const int posRow = position ? row : -1, rotRow = rotation ? row + (position ? 3 : 0) : -1;
        if (position) { const gp_Vec dp(p2, p1); r(posRow) = kWeight * dp.X() / ctx.length; r(posRow + 1) = kWeight * dp.Y() / ctx.length; r(posRow + 2) = kWeight * dp.Z() / ctx.length; }
        if (rotation) { const auto rot = rotationVector(endpoint.Multiplied(desired.Inverted())); r(rotRow) = kWeight * rot.X(); r(rotRow + 1) = kWeight * rot.Y(); r(rotRow + 2) = kWeight * rot.Z(); }
        addChain(ctx, target->instanceId, 1.0, p1, J, posRow, rotRow, kWeight);
        addChain(ctx, target->referenceInstance, -1.0, p2, J, posRow, rotRow, kWeight);
    }
    void physicalError(const Context& ctx, const math_Vector& r, int row, Errors& errors, juce::StringArray*, const Tolerances&) const override
    {
        double ratio = 0.0;
        if (position) ratio = std::sqrt(r(row) * r(row) + r(row + 1) * r(row + 1) + r(row + 2) * r(row + 2)) * ctx.length / kWeight / positionTol;
        if (rotation) { const int k = row + (position ? 3 : 0); ratio = juce::jmax(ratio, std::sqrt(r(k) * r(k) + r(k + 1) * r(k + 1) + r(k + 2) * r(k + 2)) / kWeight / angleTol); }
        errors.targetRatio = juce::jmax(errors.targetRatio, ratio);
    }
};

class System
{
public:
    System(const CadMechanics& model, const Graph& g, double len, const SolveRequest& request) : graph(g), length(len)
    {
        for (const auto index : graph.closures)
        {
            auto term = std::make_unique<ClosureTerm>(index);
            term->name = graph.edges[(size_t) index].joint->name;
            terms.push_back(std::move(term));
        }
        for (const auto& c : model.couplings) terms.push_back(std::make_unique<CouplingTerm>(&c, model.findJoint(c.jointA), model.findJoint(c.jointB)));
        for (const auto& t : request.targets)
            if (t.enabled)
            {
                auto term = std::make_unique<TargetTerm>(&t, request.targetPositionToleranceMm, request.targetAngleToleranceRad);
                targetTerms.push_back(term.get());
                terms.push_back(std::move(term));
            }
        rows = sizeOfTerms();
    }
    int rowCount() const { return rows; }
    const std::vector<TargetTerm*>& targets() const { return targetTerms; }
    // First residual row (1-based) of each target term.
    int targetRow(const TargetTerm* t) const { int row = 1; for (const auto& term : terms) { if (term.get() == t) return row; row += term->size(); } return -1; }

    void evaluate(const std::map<juce::String, double>& values, const std::map<juce::String, int>& column, math_Vector& r, math_Matrix& J, Config& config) const
    {
        config = evaluateConfig(graph, values);
        r.Init(0.0); J.Init(0.0);
        const Context ctx { graph, config, values, column, length };
        int row = r.Lower();
        for (const auto& t : terms) { t->evaluate(ctx, r, J, row); row += t->size(); }
    }

    Errors errors(const std::map<juce::String, double>& values, const std::map<juce::String, int>& column, const math_Vector& r, const Config& config, juce::StringArray* problems, const Tolerances& tol) const
    {
        Errors worst;
        const Context ctx { graph, config, values, column, length };
        int row = r.Lower();
        for (const auto& t : terms) { t->physicalError(ctx, r, row, worst, problems, tol); row += t->size(); }
        return worst;
    }

private:
    int sizeOfTerms() const { int n = 0; for (const auto& t : terms) n += t->size(); return n; }
    const Graph& graph;
    double length;
    std::vector<std::unique_ptr<Term>> terms;
    std::vector<TargetTerm*> targetTerms;
    int rows = 0;
};

bool within(const Errors& e, const Tolerances& t) { return e.positionMm <= t.positionMm && e.angleRad <= t.angleRad && e.coupling <= t.coupling; }
bool done(const Errors& e, const Tolerances& t) { return within(e, t) && e.targetRatio <= 1.0; }

double characteristicLength(const CadMechanics& model)
{
    bool any = false; gp_XYZ lo, hi;
    auto include = [&](const gp_XYZ& p) { if (!any) { lo = hi = p; any = true; return; } lo.SetCoord(juce::jmin(lo.X(), p.X()), juce::jmin(lo.Y(), p.Y()), juce::jmin(lo.Z(), p.Z())); hi.SetCoord(juce::jmax(hi.X(), p.X()), juce::jmax(hi.Y(), p.Y()), juce::jmax(hi.Z(), p.Z())); };
    std::map<juce::String, gp_Trsf> placement;
    for (const auto& i : model.instances) { placement[i.id] = i.placement.toTrsf(); include(placement[i.id].TranslationPart()); }
    for (const auto& j : model.joints)
    {
        const auto parent = j.parentInstance.isEmpty() ? gp_Trsf() : placement[j.parentInstance];
        include(parent.Multiplied(j.parentToJoint.toTrsf()).TranslationPart());
    }
    return any ? juce::jmax(1.0, (hi - lo).Modulus()) : 1.0;
}

// Rank of J (via the normal matrix) and the unknowns spanning its null space.
int rankOf(const math_Matrix& J, int m, int n, const std::vector<const CadJoint*>& unknownJoints, juce::StringArray* freeJoints)
{
    if (n <= 0 || m <= 0) return 0;
    math_Matrix N(1, n, 1, n, 0.0);
    for (int a = 1; a <= n; ++a) for (int b = 1; b <= n; ++b) { double s = 0.0; for (int i = 1; i <= m; ++i) s += J(i, a) * J(i, b); N(a, b) = s; }
    math_Jacobi jacobi(N);
    if (!jacobi.IsDone()) return 0;
    double sigmaMax = 0.0;
    for (int i = 1; i <= n; ++i) sigmaMax = juce::jmax(sigmaMax, std::sqrt(juce::jmax(0.0, jacobi.Value(i))));
    const auto threshold = juce::jmax(1.0e-8 * sigmaMax, 1.0e-10);
    int rank = 0;
    for (int i = 1; i <= n; ++i)
    {
        if (std::sqrt(juce::jmax(0.0, jacobi.Value(i))) > threshold) { ++rank; continue; }
        if (freeJoints == nullptr) continue;
        math_Vector v(1, n); jacobi.Vector(i, v);
        for (int j = 1; j <= n; ++j) if (std::abs(v(j)) > 0.3 && !freeJoints->contains(unknownJoints[(size_t) j - 1]->name)) freeJoints->add(unknownJoints[(size_t) j - 1]->name);
    }
    return rank;
}

gp_Quaternion slerp(gp_Quaternion a, gp_Quaternion b, double t)
{
    a.Normalize(); b.Normalize();
    double d = a.X() * b.X() + a.Y() * b.Y() + a.Z() * b.Z() + a.W() * b.W();
    if (d < 0.0) { b = gp_Quaternion(-b.X(), -b.Y(), -b.Z(), -b.W()); d = -d; }
    double wa = 1.0 - t, wb = t;
    if (d < 0.9995) { const auto theta = std::acos(juce::jlimit(-1.0, 1.0, d)), s = std::sin(theta); wa = std::sin((1.0 - t) * theta) / s; wb = std::sin(t * theta) / s; }
    gp_Quaternion q(wa * a.X() + wb * b.X(), wa * a.Y() + wb * b.Y(), wa * a.Z() + wb * b.Z(), wa * a.W() + wb * b.W());
    q.Normalize();
    return q;
}

gp_Trsf interpolate(const gp_Trsf& from, const gp_Trsf& to, double t)
{
    const auto a = from.TranslationPart(), b = to.TranslationPart();
    gp_Trsf result;
    result.SetRotation(slerp(from.GetRotation(), to.GetRotation(), t));
    result.SetTranslationPart(gp_Vec(a + (b - a) * t));
    return result;
}

SolveResult invalidInput(const juce::String& message, bool inverse)
{
    SolveResult r; r.status = Status::invalidInput; r.message = message; r.inverse = inverse;
    return r;
}

juce::String describeMisses(const std::vector<TargetReport>& reports)
{
    juce::StringArray lines;
    for (const auto& t : reports)
        if (!t.reached)
            lines.add(t.name + ": " + (t.mode == "orientation" ? juce::String() : juce::String(t.positionErrorMm, 4) + " mm") + (t.mode == "pose" ? ", " : "")
                      + (t.mode == "position" ? juce::String() : juce::String(t.angleErrorRad * kRadToDeg, 4) + " deg") + " from its goal");
    return lines.joinIntoString("; ");
}
}

SolveResult solve(const CadMechanics& model, const SolveRequest& request, const SolveOptions& options)
{
    const bool ik = request.inverse();
    SolveResult result;
    result.inverse = ik;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto& tol = options.tolerances;
    juce::String error;
    if (!mech::validateModel(model, error)) return invalidInput(error, ik);

    // IK inputs: released drivers and enabled targets.
    std::set<juce::String> adjustable;
    for (const auto& id : request.adjustable)
    {
        const auto* joint = model.findJoint(id);
        if (joint == nullptr) return invalidInput("Adjustable joint '" + id + "' does not exist.", ik);
        if (joint->type == "fixed") return invalidInput(joint->name + " is fixed and has no coordinate to adjust.", ik);
        if (joint->role != "driver") return invalidInput(joint->name + " is a " + joint->role + " coordinate; IK adjusts driver joints only (dependent joints already follow the mechanism, locked joints stay locked).", ik);
        if (request.prescribed.count(id) != 0) return invalidInput(joint->name + " is prescribed for this solve and cannot also be adjusted.", ik);
        if (!adjustable.insert(id).second) return invalidInput(joint->name + " is listed twice as adjustable.", ik);
    }
    if (ik)
    {
        int enabled = 0;
        for (const auto& t : request.targets)
        {
            if (!t.enabled) continue;
            ++enabled;
            if (model.findInstance(t.instanceId) == nullptr) return invalidInput("Target " + t.name + ": its instance does not exist.", ik);
            if (t.referenceInstance.isNotEmpty() && model.findInstance(t.referenceInstance) == nullptr) return invalidInput("Target " + t.name + ": its reference instance does not exist.", ik);
        }
        if (enabled == 0) return invalidInput("Enable at least one target.", ik);
        if (adjustable.empty()) return invalidInput("Select at least one driver joint for the IK solve to adjust.", ik);
        if (!(request.targetPositionToleranceMm > 0.0) || !(request.targetAngleToleranceRad > 0.0) || !std::isfinite(request.targetPositionToleranceMm) || !std::isfinite(request.targetAngleToleranceRad))
            return invalidInput("Target tolerances must be finite and greater than zero.", ik);
    }
    for (const auto& [id, value] : request.prescribed)
    {
        const auto* joint = model.findJoint(id);
        if (joint == nullptr) return invalidInput("Prescribed coordinate for unknown joint '" + id + "'.", ik);
        if (joint->type == "fixed" || joint->role != "driver") return invalidInput(joint->name + " is " + (joint->type == "fixed" ? juce::String("fixed") : "a " + joint->role + " coordinate") + "; only driver coordinates can be prescribed.", ik);
        if (!std::isfinite(value)) return invalidInput(joint->name + ": prescribed value must be finite.", ik);
        if (joint->enforceLimits && (value < joint->lower || value > joint->upper)) return invalidInput(joint->name + ": prescribed value " + juce::String(value, 6) + " is outside its hard bounds [" + juce::String(joint->lower, 6) + ", " + juce::String(joint->upper, 6) + "].", ik);
    }

    Graph graph; graph.build(model);
    const auto length = characteristicLength(model);
    System system(model, graph, length, request);
    for (const auto& root : graph.floatingRoots)
    {
        result.rigidBodyFreedom += 6;
        if (const auto* i = model.findInstance(root)) result.floatingInstances.add(i->name);
    }
    // A target on an ungrounded part would only move relative to an arbitrary authored placement.
    for (const auto& t : request.targets)
        for (const auto& id : { t.instanceId, t.referenceInstance })
            if (t.enabled && id.isNotEmpty() && graph.grounded.count(id) == 0)
                return invalidInput(model.findInstance(id)->name + " is not connected to ground through joints, so target " + t.name + " cannot define its motion. Ground an instance or joint the chain to the world.", ik);

    // Coordinate classification and inputs.
    std::map<juce::String, double> values, start, target;
    std::map<juce::String, int> column;
    std::vector<const CadJoint*> unknownJoints;
    for (const auto& joint : model.joints)
    {
        const auto seed = request.seed.count(joint.id) != 0 && std::isfinite(request.seed.at(joint.id)) ? request.seed.at(joint.id) : joint.reference;
        if (joint.type == "fixed") { values[joint.id] = joint.reference; continue; }
        if (joint.role == "locked") { values[joint.id] = joint.lockValue; continue; }
        if (joint.role == "driver" && adjustable.count(joint.id) == 0)
        {
            start[joint.id] = seed;
            target[joint.id] = request.prescribed.count(joint.id) != 0 ? request.prescribed.at(joint.id) : seed;
            values[joint.id] = seed;
            continue;
        }
        column[joint.id] = (int) unknownJoints.size() + 1;
        unknownJoints.push_back(&joint);
        values[joint.id] = seed;
    }
    const int n = (int) unknownJoints.size(), m = system.rowCount();
    result.unknowns = n; result.equations = m;

    // Bounded continuation: large driver changes are split so the solution follows the current assembly branch.
    int steps = 1;
    for (const auto& [id, goal] : target)
    {
        const auto* joint = model.findJoint(id);
        const auto delta = std::abs(goal - start[id]);
        const auto step = joint->type == "revolute" ? options.maxAngleStepRad : options.maxLinearStepFraction * length / 1000.0;
        if (step > 0.0) steps = juce::jmax(steps, (int) std::ceil(delta / step));
    }
    // Targets are approached the same way: from the endpoint's current pose to the goal, in steps.
    std::vector<gp_Trsf> goalStart, goalEnd;
    if (ik)
    {
        const auto c0 = evaluateConfig(graph, values);
        for (const auto* term : system.targets())
        {
            const auto reference = term->target->referenceInstance.isEmpty() ? gp_Trsf() : c0.world.at(term->target->referenceInstance);
            const auto current = reference.Inverted().Multiplied(c0.world.at(term->target->instanceId).Multiplied(term->tool));
            const auto goal = term->target->desired.toTrsf();
            goalStart.push_back(current); goalEnd.push_back(goal);
            const auto distance = gp_Pnt(current.TranslationPart()).Distance(gp_Pnt(goal.TranslationPart()));
            const auto angle = rotationVector(current.Multiplied(goal.Inverted())).Magnitude();
            if (term->position && options.maxLinearStepFraction > 0.0) steps = juce::jmax(steps, (int) std::ceil(distance / (options.maxLinearStepFraction * length)));
            if (term->rotation && options.maxAngleStepRad > 0.0) steps = juce::jmax(steps, (int) std::ceil(angle / options.maxAngleStepRad));
        }
    }
    steps = juce::jlimit(1, juce::jmax(1, options.maxSubsteps), steps);
    if (n == 0) steps = 1;

    math_Vector r(1, juce::jmax(1, m), 0.0);
    math_Matrix J(1, juce::jmax(1, m), 1, juce::jmax(1, n), 0.0);
    Config config;
    bool converged = false, stagnated = false;
    int startRank = -1;
    const double boundEps = 1.0e-12;
    for (int s = 1; s <= steps; ++s)
    {
        for (const auto& [id, goal] : target) values[id] = s == steps ? goal : start[id] + (goal - start[id]) * (double) s / (double) steps;
        for (size_t i = 0; i < system.targets().size(); ++i) system.targets()[i]->goal = s == steps ? goalEnd[i] : interpolate(goalStart[i], goalEnd[i], (double) s / (double) steps);
        result.substeps = s;
        double lambda = 1.0e-6;
        system.evaluate(values, column, r, J, config);
        if (ik && s == 1)
        {
            startRank = rankOf(J, m, n, unknownJoints, nullptr);
            // A target whose rows do not depend on any unknown cannot be moved by this solve.
            for (const auto* term : system.targets())
            {
                const auto first = system.targetRow(term);
                bool moves = false;
                for (int i = first; i < first + term->size() && !moves; ++i) for (int j = 1; j <= n && !moves; ++j) moves = std::abs(J(i, j)) > 1.0e-12;
                if (!moves) return invalidInput("None of the adjustable joints moves target " + term->target->name + " (directly or through the mechanism) from the current configuration.", ik);
            }
        }
        converged = m == 0 || done(system.errors(values, column, r, config, nullptr, tol), tol);
        stagnated = false;
        for (int iteration = 0; !converged && n > 0 && iteration < options.maxIterations; ++iteration)
        {
            ++result.iterations;
            if ((options.cancel != nullptr && options.cancel->load()) || juce::Time::getMillisecondCounterHiRes() - started > options.timeBudgetMs)
            {
                result.status = (options.cancel != nullptr && options.cancel->load()) ? Status::cancelled : Status::notConverged;
                result.message = result.status == Status::cancelled ? "The solve was cancelled." : "The solve exceeded its " + juce::String(options.timeBudgetMs, 0) + " ms budget.";
                return result;
            }
            // Damped least-squares step: [J; sqrt(lambda) D] dx = [-r; 0], solved with OCCT's SVD. During an IK
            // search a coordinate at a hard bound whose step points outward is held there (active set).
            std::vector<bool> pinned((size_t) n + 1, false);
            math_Vector dx(1, n, 0.0);
            bool solvedStep = false;
            for (int pass = 0; pass <= n; ++pass)
            {
                math_Matrix A(1, m + n, 1, n, 0.0);
                math_Vector b(1, m + n, 0.0);
                for (int i = 1; i <= m; ++i) { b(i) = -r(i); for (int j = 1; j <= n; ++j) A(i, j) = pinned[(size_t) j] ? 0.0 : J(i, j); }
                for (int j = 1; j <= n; ++j)
                {
                    double d = 0.0; for (int i = 1; i <= m; ++i) d += J(i, j) * J(i, j);
                    A(m + j, j) = std::sqrt(lambda * juce::jmax(d, 1.0e-12));
                }
                math_SVD svd(A);
                if (!svd.IsDone()) break;
                svd.Solve(b, dx, 1.0e-12);
                solvedStep = true;
                if (!ik) break;
                bool changed = false;
                for (int j = 1; j <= n; ++j)
                {
                    const auto* joint = unknownJoints[(size_t) j - 1];
                    if (!joint->enforceLimits || pinned[(size_t) j]) continue;
                    const auto v = values[joint->id];
                    if ((v <= joint->lower + boundEps && dx(j) < 0.0) || (v >= joint->upper - boundEps && dx(j) > 0.0)) { pinned[(size_t) j] = true; changed = true; }
                }
                if (!changed) break;
            }
            if (!solvedStep) { stagnated = true; break; }
            for (int j = 1; j <= n; ++j) if (pinned[(size_t) j]) dx(j) = 0.0;
            // Limit the step so one iteration cannot jump to another assembly branch (or, in IK, across a bound).
            double factor = 1.0;
            for (int j = 1; j <= n; ++j)
            {
                const auto* joint = unknownJoints[(size_t) j - 1];
                const auto limit = joint->type == "revolute" ? 0.5 : 0.25 * length / 1000.0;
                if (std::abs(dx(j)) > limit) factor = juce::jmin(factor, limit / std::abs(dx(j)));
                if (ik && joint->enforceLimits && dx(j) != 0.0)
                {
                    const auto room = dx(j) > 0.0 ? joint->upper - values[joint->id] : values[joint->id] - joint->lower;
                    if (factor * std::abs(dx(j)) > room) factor = juce::jmax(0.0, room) / std::abs(dx(j));
                }
            }
            auto trial = values;
            double stepNorm = 0.0;
            for (int j = 1; j <= n; ++j) { trial[unknownJoints[(size_t) j - 1]->id] += factor * dx(j); stepNorm += dx(j) * dx(j); }
            math_Vector rt(1, juce::jmax(1, m), 0.0); math_Matrix Jt(1, juce::jmax(1, m), 1, juce::jmax(1, n), 0.0); Config ct;
            system.evaluate(trial, column, rt, Jt, ct);
            if (rt.Norm() < r.Norm())
            {
                values = trial; r = rt; J = Jt; config = ct;
                lambda = juce::jmax(lambda * 0.3, 1.0e-12);
                converged = done(system.errors(values, column, r, config, nullptr, tol), tol);
            }
            else
            {
                lambda *= 10.0;
                if (lambda > 1.0e10 || std::sqrt(stepNorm) * factor < 1.0e-15) { stagnated = true; break; }
            }
        }
        if (!converged && !stagnated && n > 0 && m > 0)
        {
            // A residual minimum (J^T r ~ 0 while r stays large) means the inputs cannot all hold here.
            double g2 = 0.0, jf2 = 0.0;
            for (int j = 1; j <= n; ++j)
            {
                double g = 0.0;
                for (int i = 1; i <= m; ++i) { g += J(i, j) * r(i); jf2 += J(i, j) * J(i, j); }
                g2 += g * g;
            }
            stagnated = std::sqrt(g2) <= 1.0e-6 * std::sqrt(jf2) * r.Norm();
        }
        if (!converged) break;
    }

    // Rank and remaining-freedom diagnostics at the final configuration.
    result.rank = rankOf(J, m, n, unknownJoints, &result.freeJoints);
    result.freeCoordinates = n - result.rank;
    result.redundantEquations = juce::jmax(0, m - result.rank);

    // Independent verification in physical units, regardless of what the iteration reported.
    system.evaluate(values, column, r, J, config);
    const auto errs = system.errors(values, column, r, config, &result.problems, tol);
    result.positionResidualMm = errs.positionMm; result.angleResidualRad = errs.angleRad; result.couplingResidual = errs.coupling;
    bool finite = true;
    for (const auto& [id, value] : values) finite = finite && std::isfinite(value);
    for (const auto& [id, t] : config.world) for (int row = 1; row <= 3; ++row) for (int col = 1; col <= 4; ++col) finite = finite && std::isfinite(t.Value(row, col));
    for (const auto& [id, goal] : target) if (values[id] != goal) { result.problems.add("driver " + model.findJoint(id)->name + " was not held at its prescribed value"); finite = false; }
    juce::StringArray boundProblems;
    for (const auto* joint : unknownJoints)
    {
        if (!joint->enforceLimits) continue;
        const auto v = values[joint->id];
        if (v < joint->lower - tol.coupling || v > joint->upper + tol.coupling)
            boundProblems.add(joint->name + " would be at " + juce::String(v, 6) + ", outside its hard bounds [" + juce::String(joint->lower, 6) + ", " + juce::String(joint->upper, 6) + "]");
        else if (ik && (v <= joint->lower + 1.0e-9 || v >= joint->upper - 1.0e-9)) result.limitedJoints.add(joint->name);
    }

    result.coordinates = values;
    for (const auto& instance : model.instances) result.world[instance.id] = config.world.count(instance.id) != 0 ? config.world[instance.id] : instance.placement.toTrsf();
    if (result.redundantEquations > 0 && within(errs, tol)) result.warnings.add(juce::String(result.redundantEquations) + " redundant constraint equation(s) are consistent (typical for planar mechanisms modelled in 3-D).");
    if (result.rigidBodyFreedom > 0) result.warnings.add("Not connected to ground: " + result.floatingInstances.joinIntoString(", ") + " (shown at the authored placement).");
    bool reached = true;
    for (const auto& t : request.targets)
        if (t.enabled) { result.targets.push_back(evaluateTarget(t, result.world, request.targetPositionToleranceMm, request.targetAngleToleranceRad)); reached = reached && result.targets.back().reached; }

    if (!finite) { result.status = Status::notConverged; result.message = "The solve produced non-finite or inconsistent values."; return result; }
    if (!within(errs, tol))
    {
        if (ik)
        {
            // The search ended between the machine and an unreachable target. Hold the adjustable joints where it
            // stopped and solve the mechanism alone: that gives a consistent candidate whose target errors are honest.
            SolveRequest forward;
            forward.prescribed = request.prescribed;
            for (const auto& id : adjustable) forward.prescribed[id] = values[id];
            forward.seed = values;
            auto remaining = options;
            remaining.timeBudgetMs = juce::jmax(50.0, options.timeBudgetMs - (juce::Time::getMillisecondCounterHiRes() - started));
            const auto projected = solve(model, forward, remaining);
            SolveResult out = result;
            out.valid = false; out.problems.clear(); out.targets.clear();
            if (!projected.valid)
            {
                out.status = projected.status == Status::cancelled ? Status::cancelled : Status::notConverged;
                out.problems = projected.problems;
                out.message = "The IK search did not find a configuration that satisfies the mechanism (" + statusName(projected.status) + ": " + projected.message + "). This is a numerical finding near the last valid pose.";
                return out;
            }
            out.candidateConsistent = true;
            out.coordinates = projected.coordinates; out.world = projected.world;
            out.positionResidualMm = projected.positionResidualMm; out.angleResidualRad = projected.angleResidualRad; out.couplingResidual = projected.couplingResidual;
            bool all = true;
            for (const auto& t : request.targets)
                if (t.enabled) { out.targets.push_back(evaluateTarget(t, out.world, request.targetPositionToleranceMm, request.targetAngleToleranceRad)); all = all && out.targets.back().reached; }
            if (all) { out.valid = true; out.status = projected.status == Status::singular ? Status::singular : Status::solved; out.message = "Targets reached."; return out; }
            out.status = out.limitedJoints.isEmpty() ? Status::targetNotReached : Status::limitRestricted;
            out.message = "Target not reached (" + describeMisses(out.targets) + "). " + (out.limitedJoints.isEmpty() ? juce::String() : "Stopped at the hard bounds of " + out.limitedJoints.joinIntoString(", ") + ". ")
                        + "The local search found no closer configuration that keeps the mechanism together; this does not prove the target is unreachable.";
            return out;
        }
        if (n == 0 || stagnated)
        {
            result.status = Status::conflicting;
            result.message = n == 0 ? "The drivers and locks over-determine the mechanism: the constraints cannot hold with these inputs."
                                    : "The constraints and inputs could not be satisfied from the current configuration (the search reached a residual minimum). The inputs may conflict or be out of reach; this is a numerical finding near the last valid pose.";
        }
        else { result.status = Status::notConverged; result.message = "The solve did not converge within " + juce::String(options.maxIterations) + " iterations per step."; }
        return result;
    }
    if (!boundProblems.isEmpty()) { result.status = Status::boundViolation; result.problems.addArray(boundProblems); result.message = "A dependent coordinate would leave its hard bounds."; return result; }
    if (ik && !reached)
    {
        result.candidateConsistent = true;
        result.status = result.limitedJoints.isEmpty() ? Status::targetNotReached : Status::limitRestricted;
        result.message = "Target not reached (" + describeMisses(result.targets) + "). " + (result.limitedJoints.isEmpty() ? juce::String() : "Stopped at the hard bounds of " + result.limitedJoints.joinIntoString(", ") + ". ")
                       + (stagnated ? "The search reached a residual minimum near the last valid pose; " : "The search did not converge; ") + "this does not prove the target is unreachable.";
        return result;
    }
    result.valid = true;
    const auto previousRank = ik ? startRank : request.seedRank;
    if (previousRank > result.rank && n > 0) { result.status = Status::singular; result.message = "Singular configuration: the constraint rank dropped from " + juce::String(previousRank) + " to " + juce::String(result.rank) + "; motion through this pose is ambiguous."; }
    else if (result.freeCoordinates > 0)
    {
        result.status = Status::underconstrained;
        result.message = ik ? "Targets reached. They leave freedom in " + result.freeJoints.joinIntoString(", ") + "; the solution nearest the previous pose was kept."
                            : "Solved with remaining freedom: " + result.freeJoints.joinIntoString(", ") + " keep their last values.";
    }
    else { result.status = Status::solved; result.message = ik ? "Targets reached." : "Solved."; }
    return result;
}

SolveResult solveTask(const CadMechanics& model, const CadIkTask& task, const std::map<juce::String, double>& prescribed,
                      const std::map<juce::String, double>& seed, int seedRank, const SolveOptions& options)
{
    juce::String error;
    if (!mech::validateIkTask(model, task, error)) return invalidInput(error, true);
    SolveRequest request;
    request.seed = seed; request.seedRank = seedRank;
    request.adjustable = task.adjustableJoints;
    request.targets = task.targets;
    request.targetPositionToleranceMm = task.positionToleranceMm; request.targetAngleToleranceRad = task.angleToleranceRad;
    // Released drivers drop their prescribed value; every other driver keeps it.
    for (const auto& [id, value] : prescribed) if (!task.adjustableJoints.contains(id)) request.prescribed[id] = value;
    return solve(model, request, options);
}
}
