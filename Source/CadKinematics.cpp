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
    }
    return "unknown";
}

juce::String SolveResult::summary() const
{
    juce::String text = statusName(status);
    if (valid) text << " | closure " << juce::String(positionResidualMm, 9) << " mm, " << juce::String(angleResidualRad, 12) << " rad";
    if (freeCoordinates > 0) text << " | " << freeCoordinates << " free coordinate(s)";
    if (rigidBodyFreedom > 0) text << " | " << rigidBodyFreedom << " rigid-body DOF unconstrained";
    if (redundantEquations > 0) text << " | " << redundantEquations << " redundant equation(s)";
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
    return juce::var(o.release());
}

namespace
{
const juce::String kWorld = "world";

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
        std::deque<juce::String> grounded { kWorld };
        visited.insert(kWorld); treeEdge[kWorld] = -1; fixedRoots.insert(kWorld); rootPose[kWorld] = gp_Trsf();
        for (const auto& instance : model.instances)
            if (instance.grounded && visited.insert(instance.id).second)
            {
                treeEdge[instance.id] = -1; fixedRoots.insert(instance.id); rootPose[instance.id] = instance.placement.toTrsf(); grounded.push_back(instance.id);
            }
        grow(grounded);
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

struct Context
{
    const Graph& graph;
    const Config& config;
    const std::map<juce::String, double>& values;
    const std::map<juce::String, int>& column;   // unknown joint id -> column
    double length;                              // characteristic length, mm
};

// A block of residual equations. Implementations fill rows [row, row + size()) of r and J.
struct Term
{
    virtual ~Term() = default;
    virtual int size() const = 0;
    virtual void evaluate(const Context& ctx, math_Vector& r, math_Matrix& J, int row) const = 0;
    virtual double physicalError(const Context& ctx, const math_Vector& r, int row, double& positionMm, double& angleRad, double& coupling) const = 0;
    virtual juce::String describe(double positionMm, double angleRad, double coupling) const = 0;
};

// world(parent) * parentToJoint * motion(q) * jointToChild must coincide with world(child).
struct ClosureTerm final : Term
{
    int edge;
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
        auto add = [&](int index, double sign, const gp_Pnt& p)
        {
            const auto it = ctx.column.find(ctx.graph.edges[(size_t) index].joint->id);
            if (it == ctx.column.end()) return;
            gp_Vec v, w; twist(ctx.graph, ctx.config, index, p, v, w);
            const auto col = it->second;
            J(row, col) += sign * v.X() / ctx.length; J(row + 1, col) += sign * v.Y() / ctx.length; J(row + 2, col) += sign * v.Z() / ctx.length;
            J(row + 3, col) += sign * w.X(); J(row + 4, col) += sign * w.Y(); J(row + 5, col) += sign * w.Z();
        };
        for (const auto& [index, sign] : ctx.config.path.at(e.parent)) add(index, sign, p1);
        add(edge, 1.0, p1);
        for (const auto& [index, sign] : ctx.config.path.at(e.child)) add(index, -sign, p2);
    }
    double physicalError(const Context& ctx, const math_Vector& r, int row, double& positionMm, double& angleRad, double&) const override
    {
        positionMm = std::sqrt(r(row) * r(row) + r(row + 1) * r(row + 1) + r(row + 2) * r(row + 2)) * ctx.length;
        angleRad = std::sqrt(r(row + 3) * r(row + 3) + r(row + 4) * r(row + 4) + r(row + 5) * r(row + 5));
        return 0.0;
    }
    juce::String describe(double positionMm, double angleRad, double) const override { return "loop closure at joint " + name + ": " + juce::String(positionMm, 6) + " mm, " + juce::String(angleRad, 8) + " rad"; }
    juce::String name;
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
    double physicalError(const Context& ctx, const math_Vector& r, int row, double&, double&, double& c) const override { c = std::abs(r(row)) * scale(ctx); return c; }
    juce::String describe(double, double, double c) const override { return "coupling " + coupling->name + ": residual " + juce::String(c, 10) + (b->type == "prismatic" ? " m" : " rad"); }
};

struct Errors { double positionMm = 0.0, angleRad = 0.0, coupling = 0.0; };

class System
{
public:
    System(const CadMechanics& m, const Graph& g, double len) : model(m), graph(g), length(len)
    {
        for (const auto index : graph.closures)
        {
            auto term = std::make_unique<ClosureTerm>(index);
            term->name = graph.edges[(size_t) index].joint->name;
            terms.push_back(std::move(term));
        }
        for (const auto& c : model.couplings) terms.push_back(std::make_unique<CouplingTerm>(&c, model.findJoint(c.jointA), model.findJoint(c.jointB)));
        for (const auto& t : terms) rows += t->size();
    }
    int rowCount() const { return rows; }

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
        for (const auto& t : terms)
        {
            double p = 0.0, a = 0.0, c = 0.0;
            t->physicalError(ctx, r, row, p, a, c);
            worst.positionMm = juce::jmax(worst.positionMm, p); worst.angleRad = juce::jmax(worst.angleRad, a); worst.coupling = juce::jmax(worst.coupling, c);
            if (problems != nullptr && (p > tol.positionMm || a > tol.angleRad || c > tol.coupling)) problems->add(t->describe(p, a, c));
            row += t->size();
        }
        return worst;
    }

private:
    const CadMechanics& model;
    const Graph& graph;
    double length;
    std::vector<std::unique_ptr<Term>> terms;
    int rows = 0;
};

bool within(const Errors& e, const Tolerances& t) { return e.positionMm <= t.positionMm && e.angleRad <= t.angleRad && e.coupling <= t.coupling; }

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
}

SolveResult solve(const CadMechanics& model, const SolveRequest& request, const SolveOptions& options)
{
    SolveResult result;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto& tol = options.tolerances;
    juce::String error;
    if (!mech::validateModel(model, error)) { result.status = Status::invalidInput; result.message = error; return result; }

    Graph graph; graph.build(model);
    const auto length = characteristicLength(model);
    System system(model, graph, length);
    for (const auto& root : graph.floatingRoots)
    {
        result.rigidBodyFreedom += 6;
        if (const auto* i = model.findInstance(root)) result.floatingInstances.add(i->name);
    }

    // Coordinate classification and inputs.
    std::map<juce::String, double> values, start, target;
    std::map<juce::String, int> column;
    std::vector<const CadJoint*> unknownJoints;
    for (const auto& [id, value] : request.prescribed)
    {
        const auto* joint = model.findJoint(id);
        if (joint == nullptr) { result.status = Status::invalidInput; result.message = "Prescribed coordinate for unknown joint '" + id + "'."; return result; }
        if (joint->type == "fixed" || joint->role != "driver") { result.status = Status::invalidInput; result.message = joint->name + " is " + (joint->type == "fixed" ? juce::String("fixed") : "a " + joint->role + " coordinate") + "; only driver coordinates can be prescribed."; return result; }
        if (!std::isfinite(value)) { result.status = Status::invalidInput; result.message = joint->name + ": prescribed value must be finite."; return result; }
        if (joint->enforceLimits && (value < joint->lower || value > joint->upper)) { result.status = Status::invalidInput; result.message = joint->name + ": prescribed value " + juce::String(value, 6) + " is outside its hard bounds [" + juce::String(joint->lower, 6) + ", " + juce::String(joint->upper, 6) + "]."; return result; }
    }
    for (const auto& joint : model.joints)
    {
        const auto seed = request.seed.count(joint.id) != 0 && std::isfinite(request.seed.at(joint.id)) ? request.seed.at(joint.id) : joint.reference;
        if (joint.type == "fixed") { values[joint.id] = joint.reference; continue; }
        if (joint.role == "locked") { values[joint.id] = joint.lockValue; continue; }
        if (joint.role == "driver")
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
    steps = juce::jlimit(1, juce::jmax(1, options.maxSubsteps), steps);
    if (n == 0) steps = 1;

    math_Vector r(1, juce::jmax(1, m), 0.0);
    math_Matrix J(1, juce::jmax(1, m), 1, juce::jmax(1, n), 0.0);
    Config config;
    bool converged = false, stagnated = false;
    for (int s = 1; s <= steps; ++s)
    {
        for (const auto& [id, goal] : target) values[id] = s == steps ? goal : start[id] + (goal - start[id]) * (double) s / (double) steps;
        result.substeps = s;
        double lambda = 1.0e-6;
        system.evaluate(values, column, r, J, config);
        converged = m == 0 || within(system.errors(values, column, r, config, nullptr, tol), tol);
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
            // Damped least-squares step: [J; sqrt(lambda) D] dx = [-r; 0], solved with OCCT's SVD.
            math_Matrix A(1, m + n, 1, n, 0.0);
            math_Vector b(1, m + n, 0.0), dx(1, n, 0.0);
            for (int i = 1; i <= m; ++i) { b(i) = -r(i); for (int j = 1; j <= n; ++j) A(i, j) = J(i, j); }
            for (int j = 1; j <= n; ++j)
            {
                double d = 0.0; for (int i = 1; i <= m; ++i) d += J(i, j) * J(i, j);
                A(m + j, j) = std::sqrt(lambda * juce::jmax(d, 1.0e-12));
            }
            math_SVD svd(A);
            if (!svd.IsDone()) { stagnated = true; break; }
            svd.Solve(b, dx, 1.0e-12);
            // Limit the step so one iteration cannot jump to another assembly branch.
            double factor = 1.0;
            for (int j = 1; j <= n; ++j)
            {
                const auto limit = unknownJoints[(size_t) j - 1]->type == "revolute" ? 0.5 : 0.25 * length / 1000.0;
                if (std::abs(dx(j)) > limit) factor = juce::jmin(factor, limit / std::abs(dx(j)));
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
                converged = within(system.errors(values, column, r, config, nullptr, tol), tol);
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
    if (n > 0 && m > 0)
    {
        math_Matrix N(1, n, 1, n, 0.0);
        for (int a = 1; a <= n; ++a) for (int b2 = 1; b2 <= n; ++b2) { double s2 = 0.0; for (int i = 1; i <= m; ++i) s2 += J(i, a) * J(i, b2); N(a, b2) = s2; }
        math_Jacobi jacobi(N);
        if (jacobi.IsDone())
        {
            double sigmaMax = 0.0;
            for (int i = 1; i <= n; ++i) sigmaMax = juce::jmax(sigmaMax, std::sqrt(juce::jmax(0.0, jacobi.Value(i))));
            const auto threshold = juce::jmax(1.0e-8 * sigmaMax, 1.0e-10);
            for (int i = 1; i <= n; ++i)
            {
                if (std::sqrt(juce::jmax(0.0, jacobi.Value(i))) > threshold) { ++result.rank; continue; }
                math_Vector v(1, n); jacobi.Vector(i, v);
                for (int j = 1; j <= n; ++j) if (std::abs(v(j)) > 0.3 && !result.freeJoints.contains(unknownJoints[(size_t) j - 1]->name)) result.freeJoints.add(unknownJoints[(size_t) j - 1]->name);
            }
        }
    }
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
        if (joint->enforceLimits && (values[joint->id] < joint->lower - tol.coupling || values[joint->id] > joint->upper + tol.coupling))
            boundProblems.add(joint->name + " would be at " + juce::String(values[joint->id], 6) + ", outside its hard bounds [" + juce::String(joint->lower, 6) + ", " + juce::String(joint->upper, 6) + "]");

    result.coordinates = values;
    for (const auto& instance : model.instances) result.world[instance.id] = config.world.count(instance.id) != 0 ? config.world[instance.id] : instance.placement.toTrsf();
    if (result.redundantEquations > 0 && within(errs, tol)) result.warnings.add(juce::String(result.redundantEquations) + " redundant constraint equation(s) are consistent (typical for planar mechanisms modelled in 3-D).");
    if (result.rigidBodyFreedom > 0) result.warnings.add("Not connected to ground: " + result.floatingInstances.joinIntoString(", ") + " (shown at the authored placement).");

    if (!finite) { result.status = Status::notConverged; result.message = "The solve produced non-finite or inconsistent values."; return result; }
    if (!within(errs, tol))
    {
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
    result.valid = true;
    if (request.seedRank > result.rank && n > 0) { result.status = Status::singular; result.message = "Singular configuration: the constraint rank dropped from " + juce::String(request.seedRank) + " to " + juce::String(result.rank) + "; motion through this pose is ambiguous."; }
    else if (result.freeCoordinates > 0) { result.status = Status::underconstrained; result.message = "Solved with remaining freedom: " + result.freeJoints.joinIntoString(", ") + " keep their last values."; }
    else { result.status = Status::solved; result.message = "Solved."; }
    return result;
}
}
