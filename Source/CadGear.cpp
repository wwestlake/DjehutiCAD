#include "CadGear.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Standard_Failure.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Trsf.hxx>

#include <cmath>

namespace djehuti::cad::gear
{
namespace
{
constexpr double pi = juce::MathConstants<double>::pi;
juce::var object() { return juce::var(new juce::DynamicObject()); }
juce::var copyJson(const juce::var& v) { return juce::JSON::fromString(juce::JSON::toString(v, false, 17)); }
bool numeric(const juce::var& v) { return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite((double) v); }
juce::String mm(double v) { return juce::String(v, 4) + " mm"; }
juce::var pairLinkDefault() { auto p = object(); p.getDynamicObject()->setProperty("mate", ""); p.getDynamicObject()->setProperty("role", ""); return p; }

// Field ranges only; geometric feasibility is checked by spur().
bool validateFields(const juce::var& p, juce::String& error)
{
    const auto fail = [&](const juce::String& e) { error = e; return false; };
    if (!p.isObject()) return fail("Gear parameters must be an object.");
    const auto known = defaults();
    for (const auto& v : p.getDynamicObject()->getProperties())
        if (!known.hasProperty(v.name)) return fail("Unknown gear parameter: " + v.name.toString());
    const auto family = p.getProperty("family", "spur").toString();
    if (family == "bevel" || family == "worm")
        return fail(juce::String(family == "bevel" ? "Bevel" : "Worm") + " gear geometry is not implemented yet. Only family \"spur\" (external involute spur gears) is available.");
    if (family != "spur") return fail("Unknown gear family \"" + family + "\". Supported: spur.");
    const auto range = [&](const char* key, double lo, double hi, bool loOpen, const juce::String& message)
    {
        const auto v = p[key];
        if (!numeric(v)) return fail(juce::String(key) + " must be a finite number.");
        const double x = (double) v;
        if ((loOpen ? x <= lo : x < lo) || x > hi) return fail(message);
        return true;
    };
    if (!range("teeth", 5, 400, false, "Tooth count must be a whole number from 5 to 400.")) return false;
    if ((double) p["teeth"] != std::floor((double) p["teeth"])) return fail("Tooth count must be a whole number from 5 to 400.");
    if (!range("module", 0.05, 100, false, "Module must be from 0.05 to 100 mm.")) return false;
    if (!range("pressureAngleDegrees", 10, 35, false, "Pressure angle must be from 10 to 35 degrees.")) return false;
    if (!range("faceWidth", 0, 100000, true, "Face width must be greater than 0 mm.")) return false;
    if (!range("boreDiameter", 0, 1.0e6, false, "Bore diameter must be 0 (solid) or a positive diameter in mm.")) return false;
    if (!range("backlash", 0, 1.0e6, false, "Backlash allowance must be 0 or a positive circular thinning in mm.")) return false;
    if (!range("profileShift", -1.5, 1.5, false, "Profile shift coefficient must be from -1.5 to 1.5.")) return false;
    if (!range("addendumCoefficient", 0, 2, true, "Addendum coefficient must be greater than 0 and at most 2.")) return false;
    if (!range("dedendumCoefficient", (double) p["addendumCoefficient"], 3, false, "Dedendum coefficient must be at least the addendum coefficient (non-negative tip clearance) and at most 3.")) return false;
    const double clearance = (double) p["dedendumCoefficient"] - (double) p["addendumCoefficient"];
    if (!range("rootFilletCoefficient", 0, clearance + 1.0e-12, false, "Root fillet coefficient must be from 0 to the tip clearance coefficient (dedendum - addendum = " + juce::String(clearance, 3) + ") so the fillet stays below the mating tip."))
        return false;
    if (p.hasProperty("pair"))
    {
        const auto link = p["pair"];
        if (!link.isObject()) return fail("pair must be an object {mate, role}.");
        for (const auto& v : link.getDynamicObject()->getProperties())
            if (v.name.toString() != "mate" && v.name.toString() != "role") return fail("Unknown pair field: " + v.name.toString());
        if (!link["mate"].isString() && !link["mate"].isVoid()) return fail("pair.mate must be a gear feature id.");
        if (!juce::StringArray { "", "A", "B" }.contains(link["role"].toString())) return fail("pair.role must be A or B.");
    }
    return true;
}
}

double involute(double angle) { return std::tan(angle) - angle; }

double Spur::halfAngle(double radius) const
{
    if (radius <= baseRadius) return baseHalfAngle;
    return baseHalfAngle - involute(std::acos(baseRadius / radius));
}

juce::var defaults()
{
    auto p = object(); auto* o = p.getDynamicObject();
    o->setProperty("family", "spur");
    o->setProperty("teeth", 20);
    o->setProperty("module", 2.0);
    o->setProperty("pressureAngleDegrees", 20.0);
    o->setProperty("faceWidth", 10.0);
    o->setProperty("boreDiameter", 8.0);
    o->setProperty("backlash", 0.0);
    o->setProperty("profileShift", 0.0);
    o->setProperty("addendumCoefficient", 1.0);
    o->setProperty("dedendumCoefficient", 1.25);
    o->setProperty("rootFilletCoefficient", 0.2);
    o->setProperty("pair", pairLinkDefault());
    return p;
}

bool spur(const juce::var& p, Spur& s, juce::String& error)
{
    s = {};
    if (!validateFields(p, error)) return false;
    const auto fail = [&](const juce::String& e) { error = e; return false; };
    s.teeth = (int) (double) p["teeth"];
    s.module = (double) p["module"];
    s.pressureAngle = (double) p["pressureAngleDegrees"] * pi / 180.0;
    s.faceWidth = (double) p["faceWidth"];
    s.boreDiameter = (double) p["boreDiameter"];
    s.backlash = (double) p["backlash"];
    s.profileShift = (double) p["profileShift"];
    s.addendumCoefficient = (double) p["addendumCoefficient"];
    s.dedendumCoefficient = (double) p["dedendumCoefficient"];
    s.rootFilletCoefficient = (double) p["rootFilletCoefficient"];
    const double m = s.module, z = s.teeth, a = s.pressureAngle, shift = s.profileShift * m;
    s.pitchRadius = m * z / 2.0;
    s.baseRadius = s.pitchRadius * std::cos(a);
    s.tipRadius = s.pitchRadius + s.addendumCoefficient * m + shift;
    s.rootRadius = s.pitchRadius - s.dedendumCoefficient * m + shift;
    if (s.rootRadius <= 0.0)
        return fail("The root circle has no material (root diameter " + mm(2 * s.rootRadius) + "). Use more teeth, a smaller dedendum or a positive profile shift.");
    s.toothThickness = m * (pi / 2.0 + 2.0 * s.profileShift * std::tan(a)) - s.backlash;
    if (s.toothThickness <= 0.02 * m)
        return fail("The backlash allowance (" + mm(s.backlash) + ") consumes the tooth thickness. Reduce the backlash.");
    s.baseHalfAngle = s.toothThickness / (2.0 * s.pitchRadius) + involute(a);
    s.involuteStartRadius = std::max(s.baseRadius, s.rootRadius);
    if (s.tipRadius <= s.involuteStartRadius + 1.0e-6 * m)
        return fail("The tip circle lies inside the base/root circle, so the teeth have no involute flank. Increase the addendum or profile shift.");
    s.tipLand = 2.0 * s.tipRadius * s.halfAngle(s.tipRadius);
    if (s.tipLand <= 0.02 * m)
        return fail("The teeth become pointed (tip thickness " + mm(s.tipLand) + "). Reduce the addendum, profile shift or backlash, or use more teeth.");
    const double pitchAngle = 2.0 * pi / z;
    s.rootGap = s.rootRadius * (pitchAngle - 2.0 * s.halfAngle(s.rootRadius));
    if (s.rootGap <= 0.02 * m)
        return fail("Adjacent teeth close up at the root circle (space " + mm(s.rootGap) + "). Reduce the profile shift or dedendum, or use more teeth.");

    double fillet = s.rootFilletCoefficient * m;
    if (fillet > 0.0)
    {
        const double rf = s.rootRadius;
        if (rf < s.baseRadius)
        {
            // Below the base circle the flank is radial: keep the fillet's tangent point on that radial segment.
            const double reach = 0.9 * (s.baseRadius - rf);
            const double tangent = std::sqrt(rf * rf + 2.0 * rf * fillet) - rf;
            if (tangent > reach)
            {
                fillet = ((rf + reach) * (rf + reach) - rf * rf) / (2.0 * rf);
                s.warnings.add("Root fillet reduced to " + mm(fillet) + " to stay on the radial flank below the base circle.");
            }
        }
        if (2.2 * fillet > s.rootGap)
        {
            fillet = s.rootGap / 2.2;
            s.warnings.add("Root fillet reduced to " + mm(fillet) + " to fit the tooth space.");
        }
        if (fillet < 1.0e-4 * m) fillet = 0.0;
    }
    s.rootFillet = fillet;

    if (s.boreDiameter > 0.0)
    {
        const double rim = s.rootRadius - s.boreDiameter / 2.0;
        if (rim < 0.25 * m)
            return fail("The bore leaves a rim of " + mm(rim) + " below the tooth roots. Use a bore of at most " + mm(2.0 * (s.rootRadius - 0.25 * m)) + ".");
        if (rim < m) s.warnings.add("Thin rim below the tooth roots (" + mm(rim) + ", less than one module).");
    }
    const double sinA = std::sin(a);
    s.undercutTeeth = 2.0 * (s.addendumCoefficient - s.profileShift) / (sinA * sinA);
    if (z < s.undercutTeeth - 1.0e-9)
        s.warnings.add("Fewer teeth than the undercut limit (" + juce::String(s.undercutTeeth, 1) + ") for this pressure angle and shift. A generating cutter would undercut the root; this profile is not undercut, so a mating gear's tip may interfere. A positive profile shift avoids this.");
    if (s.tipLand < 0.2 * m) s.warnings.add("Narrow tip land (" + mm(s.tipLand) + ").");
    return true;
}

bool validate(const juce::var& parameters, juce::String& error)
{
    Spur ignored;
    return spur(parameters, ignored, error);
}

juce::var describe(const Spur& s)
{
    auto d = object(); auto* o = d.getDynamicObject();
    o->setProperty("family", "spur");
    o->setProperty("teeth", s.teeth);
    o->setProperty("module", s.module);
    o->setProperty("pressureAngleDegrees", s.pressureAngle * 180.0 / pi);
    o->setProperty("pitchDiameter", 2.0 * s.pitchRadius);
    o->setProperty("baseDiameter", 2.0 * s.baseRadius);
    o->setProperty("tipDiameter", 2.0 * s.tipRadius);
    o->setProperty("rootDiameter", 2.0 * s.rootRadius);
    o->setProperty("involuteStartDiameter", 2.0 * s.involuteStartRadius);
    o->setProperty("circularPitch", pi * s.module);
    o->setProperty("basePitch", pi * s.module * std::cos(s.pressureAngle));
    o->setProperty("toothThickness", s.toothThickness);
    o->setProperty("tipLand", s.tipLand);
    o->setProperty("rootSpace", s.rootGap);
    o->setProperty("rootFilletRadius", s.rootFillet);
    o->setProperty("undercutLimitTeeth", s.undercutTeeth);
    o->setProperty("undercutRisk", s.teeth < s.undercutTeeth - 1.0e-9);
    o->setProperty("units", "mm");
    juce::Array<juce::var> warnings; for (const auto& w : s.warnings) warnings.add(w);
    o->setProperty("warnings", warnings);
    return d;
}

TopoDS_Shape build(const juce::var& p, const juce::String& owner, FaceNames& names, juce::String& error)
{
    Spur s;
    if (!spur(p, s, error)) return {};
    names.clear();
    try
    {
        const int z = s.teeth;
        const double pitch = 2.0 * pi / z, rb = s.baseRadius, ra = s.tipRadius, rf = s.rootRadius;
        const bool radial = rf < rb - 1.0e-9 * s.module;
        const gp_Ax2 axes(gp::Origin(), gp::DZ(), gp::DX());
        const double rho = s.rootFillet;

        // Right flank of tooth 0 (tooth centred on +X; its tooth space lies at negative angles). With roll
        // parameter t the involute is r = rb sqrt(1 + t^2), angle = -baseHalfAngle + t - atan(t).
        const auto polar = [](double r, double angle) { return gp_Pnt(r * std::cos(angle), r * std::sin(angle), 0.0); };
        const auto involutePoint = [&](double t) { return polar(rb * std::sqrt(1.0 + t * t), -s.baseHalfAngle + t - std::atan(t)); };
        // Unit normal of the flank pointing into the tooth space: (t e_r - e_theta) / sqrt(1 + t^2).
        const auto spaceNormal = [&](double t)
        {
            const double angle = -s.baseHalfAngle + t - std::atan(t), k = 1.0 / std::sqrt(1.0 + t * t);
            const gp_Vec er(std::cos(angle), std::sin(angle), 0.0), et(-std::sin(angle), std::cos(angle), 0.0);
            return (er * t - et) * k;
        };
        const double tTip = std::sqrt((ra / rb) * (ra / rb) - 1.0);
        const double tRoot = radial ? 0.0 : std::sqrt(std::max(0.0, (rf / rb) * (rf / rb) - 1.0));

        // Root fillet: a circle of radius rho tangent to the root circle (from outside) and to the flank. Below the
        // base circle the flank is the radial line at -baseHalfAngle; otherwise the fillet meets the involute itself.
        gp_Pnt rootPoint, filletTop, filletCentre;
        double tStart = radial ? 0.0 : tRoot;
        if (rho <= 0.0)
            rootPoint = filletTop = radial ? polar(rf, -s.baseHalfAngle) : involutePoint(tRoot);
        else if (radial)
        {
            const double beta = -s.baseHalfAngle, delta = std::asin(rho / (rf + rho));
            filletCentre = polar(rf + rho, beta - delta);
            rootPoint = polar(rf, beta - delta);
            filletTop = polar(std::sqrt((rf + rho) * (rf + rho) - rho * rho), beta);
        }
        else
        {
            const auto miss = [&](double t) { return gp_Vec(gp::Origin(), involutePoint(t)).Added(spaceNormal(t) * rho).Magnitude() - (rf + rho); };
            if (miss(tTip) <= 0.0) { error = "The root fillet (" + mm(rho) + ") does not fit below the tip. Reduce rootFilletCoefficient."; return {}; }
            double lo = tRoot, hi = tTip;
            for (int i = 0; i < 200 && hi - lo > 1.0e-15; ++i) { const double mid = 0.5 * (lo + hi); (miss(mid) < 0.0 ? lo : hi) = mid; }
            tStart = 0.5 * (lo + hi);
            filletTop = involutePoint(tStart);
            filletCentre = filletTop.Translated(spaceNormal(tStart) * rho);
            const gp_Vec towardCentre(gp::Origin(), filletCentre);
            rootPoint = gp::Origin().Translated(towardCentre.Normalized() * rf);
        }
        if (std::atan2(rootPoint.Y(), rootPoint.X()) <= -pitch / 2.0 + 1.0e-9)
        { error = "The root fillets of adjacent teeth overlap in the tooth space. Reduce rootFilletCoefficient or the profile shift."; return {}; }

        // Involute flank from its lower end (base circle, or the fillet tangent point) to the tip. Uniform roll-angle
        // sampling is densest near the base circle, where the involute curvature is highest.
        constexpr int samples = 25;
        Handle(TColgp_HArray1OfPnt) points = new TColgp_HArray1OfPnt(1, samples);
        for (int k = 0; k < samples; ++k) points->SetValue(k + 1, involutePoint(tStart + (tTip - tStart) * k / (samples - 1)));
        GeomAPI_Interpolate interpolation(points, false, 1.0e-9);
        interpolation.Perform();
        if (!interpolation.IsDone()) { error = "Unable to construct the involute flank curve."; return {}; }
        const Handle(Geom_BSplineCurve) right = interpolation.Curve();
        gp_Trsf mirror; mirror.SetMirror(gp_Ax2(gp::Origin(), gp::DY()));
        const Handle(Geom_Curve) left = Handle(Geom_Curve)::DownCast(right->Transformed(mirror))->Reversed();   // tip -> lower end
        Handle(Geom_Curve) rightFillet, leftFillet;
        if (rho > 0.0)
        {
            const gp_Vec a(filletCentre, rootPoint), b(filletCentre, filletTop);
            const gp_Pnt middle = filletCentre.Translated((a + b).Normalized() * rho);
            rightFillet = GC_MakeArcOfCircle(rootPoint, middle, filletTop).Value();
            leftFillet = Handle(Geom_Curve)::DownCast(rightFillet->Transformed(mirror))->Reversed();   // top -> root
        }
        const gp_Pnt flankLow = points->Value(1), tip = points->Value(samples);

        struct Tooth { TopoDS_Vertex rightRoot, rightTop, rightLow, rightTip, leftTip, leftLow, leftTop, leftRoot; Handle(Geom_Curve) right, left, rightFillet, leftFillet; };
        std::vector<Tooth> teeth((size_t) z);
        for (int i = 0; i < z; ++i)
        {
            gp_Trsf rotation; rotation.SetRotation(gp::OZ(), i * pitch);
            auto& t = teeth[(size_t) i];
            const auto vertex = [&](const gp_Pnt& point) { return BRepBuilderAPI_MakeVertex(point.Transformed(rotation)).Vertex(); };
            t.rightTip = vertex(tip); t.leftTip = vertex(tip.Transformed(mirror));
            t.rightLow = vertex(flankLow); t.leftLow = vertex(flankLow.Transformed(mirror));
            t.rightTop = radial ? vertex(filletTop) : t.rightLow; t.leftTop = radial ? vertex(filletTop.Transformed(mirror)) : t.leftLow;
            t.rightRoot = rho > 0.0 ? vertex(rootPoint) : t.rightTop; t.leftRoot = rho > 0.0 ? vertex(rootPoint.Transformed(mirror)) : t.leftTop;
            t.right = Handle(Geom_Curve)::DownCast(right->Transformed(rotation));
            t.left = Handle(Geom_Curve)::DownCast(left->Transformed(rotation));
            if (rho > 0.0)
            {
                t.rightFillet = Handle(Geom_Curve)::DownCast(rightFillet->Transformed(rotation));
                t.leftFillet = Handle(Geom_Curve)::DownCast(leftFillet->Transformed(rotation));
            }
        }

        // One closed counter-clockwise wire: fillet, radial flank, involute, tip arc, involute, radial flank,
        // fillet and the root arc to the next tooth.
        BRepBuilderAPI_MakeWire wire;
        const auto add = [&](BRepBuilderAPI_MakeEdge&& edge)
        {
            if (!edge.IsDone()) return false;
            wire.Add(edge.Edge());
            return wire.IsDone();
        };
        for (int i = 0; i < z; ++i)
        {
            const auto& t = teeth[(size_t) i];
            const auto& next = teeth[(size_t) ((i + 1) % z)];
            bool ok = true;
            if (rho > 0.0) ok = ok && add(BRepBuilderAPI_MakeEdge(t.rightFillet, t.rightRoot, t.rightTop));
            if (radial) ok = ok && add(BRepBuilderAPI_MakeEdge(t.rightTop, t.rightLow));
            ok = ok && add(BRepBuilderAPI_MakeEdge(t.right, t.rightLow, t.rightTip));
            ok = ok && add(BRepBuilderAPI_MakeEdge(gp_Circ(axes, ra), t.rightTip, t.leftTip));
            ok = ok && add(BRepBuilderAPI_MakeEdge(t.left, t.leftTip, t.leftLow));
            if (radial) ok = ok && add(BRepBuilderAPI_MakeEdge(t.leftLow, t.leftTop));
            if (rho > 0.0) ok = ok && add(BRepBuilderAPI_MakeEdge(t.leftFillet, t.leftTop, t.leftRoot));
            ok = ok && add(BRepBuilderAPI_MakeEdge(gp_Circ(axes, rf), t.leftRoot, next.rightRoot));
            if (!ok) { error = "Unable to connect the profile of tooth " + juce::String(i + 1) + "."; return {}; }
        }
        BRepBuilderAPI_MakeFace makeFace(wire.Wire(), true);
        if (!makeFace.IsDone()) { error = "Unable to form the planar gear profile."; return {}; }
        TopoDS_Face face = makeFace.Face();
        if (s.boreDiameter > 0.0)
        {
            auto bore = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(axes, s.boreDiameter / 2.0)).Edge()).Wire();
            bore.Reverse();
            BRepBuilderAPI_MakeFace withBore(face);
            withBore.Add(bore);
            if (!withBore.IsDone()) { error = "Unable to add the bore."; return {}; }
            face = withBore.Face();
        }
        if (!BRepCheck_Analyzer(face, true).IsValid()) { error = "The gear profile is not a valid planar face."; return {}; }

        BRepPrimAPI_MakePrism prism(face, gp_Vec(0.0, 0.0, s.faceWidth));
        prism.Build();
        if (!prism.IsDone()) { error = "Unable to extrude the gear profile."; return {}; }
        TopoDS_Shape shape = prism.Shape();
        names.push_back({ prism.FirstShape(), { owner + "/start" } });
        names.push_back({ prism.LastShape(), { owner + "/end" } });
        const double tolerance = 1.0e-7 * std::max(1.0, ra);
        for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next())
        {
            const auto edge = TopoDS::Edge(ex.Current());
            BRepAdaptor_Curve curve(edge);
            const gp_Pnt mid = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0);
            double angle = std::atan2(mid.Y(), mid.X()); if (angle < 0) angle += 2.0 * pi;
            const int nearest = (int) std::lround(angle / pitch) % z;
            double offset = angle - nearest * pitch; if (offset > pi) offset -= 2.0 * pi; if (offset < -pi) offset += 2.0 * pi;
            const auto side = juce::String(offset > 0 ? ":left" : ":right");
            juce::String role;
            if (curve.GetType() == GeomAbs_Circle)
            {
                const auto circle = curve.Circle();
                const bool centred = circle.Location().Distance(gp::Origin()) < tolerance;
                if (centred && s.boreDiameter > 0 && std::abs(circle.Radius() - s.boreDiameter / 2.0) < tolerance) role = "bore";
                else if (centred && std::abs(circle.Radius() - ra) < tolerance) role = "tip:" + juce::String(nearest);
                else if (centred && std::abs(circle.Radius() - rf) < tolerance) role = "root:" + juce::String(((int) std::floor(angle / pitch)) % z);
                else role = "fillet:" + juce::String(nearest) + side;
            }
            else if (curve.GetType() == GeomAbs_Line) role = "radial:" + juce::String(nearest) + side;
            else role = "flank:" + juce::String(nearest) + side;
            for (TopTools_ListIteratorOfListOfShape it(prism.Generated(edge)); it.More(); it.Next())
                if (it.Value().ShapeType() == TopAbs_FACE) names.push_back({ it.Value(), { owner + "/" + role } });
        }
        GProp_GProps props; BRepGProp::VolumeProperties(shape, props);
        if (!BRepCheck_Analyzer(shape, true).IsValid() || !std::isfinite(props.Mass()) || std::abs(props.Mass()) <= 1.0e-12)
        { error = "OCCT produced an invalid gear solid."; return {}; }
        if (props.Mass() < 0) shape.Reverse();
        return shape;
    }
    catch (const Standard_Failure& failure)
    {
        error = juce::String("Gear construction failed: ") + failure.GetMessageString();
        return {};
    }
}

bool mesh(const Spur& a, const Spur& b, Mesh& r, juce::StringArray& problems, juce::StringArray& warnings)
{
    r = {};
    if (std::abs(a.module - b.module) > 1.0e-12 * std::max(1.0, a.module))
        problems.add("Modules differ (" + mm(a.module) + " and " + mm(b.module) + "); the teeth cannot mesh.");
    if (std::abs(a.pressureAngle - b.pressureAngle) > 1.0e-12)
        problems.add("Pressure angles differ (" + juce::String(a.pressureAngle * 180 / pi, 3) + " and " + juce::String(b.pressureAngle * 180 / pi, 3) + " degrees); the teeth cannot mesh.");
    if (!problems.isEmpty()) return false;
    const double m = a.module, alpha = a.pressureAngle, z1 = a.teeth, z2 = b.teeth;
    const double target = involute(alpha) + 2.0 * std::tan(alpha) * (a.profileShift + b.profileShift) / (z1 + z2);
    if (target <= 1.0e-12) { problems.add("The combined profile shift is too negative for a working pressure angle."); return false; }
    double aw = alpha;
    for (int i = 0; i < 100; ++i)
    {
        const double f = involute(aw) - target, slope = std::tan(aw) * std::tan(aw);
        const double next = juce::jlimit(1.0e-6, pi / 2.0 - 1.0e-6, aw - f / std::max(slope, 1.0e-12));
        if (std::abs(next - aw) < 1.0e-15) { aw = next; break; }
        aw = next;
    }
    if (std::abs(involute(aw) - target) > 1.0e-12) { problems.add("Unable to solve the working pressure angle for these profile shifts."); return false; }
    r.workingPressureAngle = aw;
    r.centreDistance = m * (z1 + z2) / 2.0 * std::cos(alpha) / std::cos(aw);
    r.ratio = -z1 / z2;
    const double lineOfAction = r.centreDistance * std::sin(aw);
    const double tipA = std::sqrt(a.tipRadius * a.tipRadius - a.baseRadius * a.baseRadius);
    const double tipB = std::sqrt(b.tipRadius * b.tipRadius - b.baseRadius * b.baseRadius);
    r.contactRatio = (tipA + tipB - lineOfAction) / (pi * m * std::cos(alpha));
    r.tipClearanceA = r.centreDistance - a.tipRadius - b.rootRadius;
    r.tipClearanceB = r.centreDistance - b.tipRadius - a.rootRadius;
    if (r.tipClearanceA <= 0.0) problems.add("Gear A's tips reach gear B's root circle (clearance " + mm(r.tipClearanceA) + "). Reduce the addendum or profile shift.");
    if (r.tipClearanceB <= 0.0) problems.add("Gear B's tips reach gear A's root circle (clearance " + mm(r.tipClearanceB) + "). Reduce the addendum or profile shift.");
    if (tipB > lineOfAction + 1.0e-9 * m) problems.add("Involute interference: gear B's tips would contact gear A below its base circle. Use a positive profile shift on gear A, more teeth on A, or a smaller addendum.");
    if (tipA > lineOfAction + 1.0e-9 * m) problems.add("Involute interference: gear A's tips would contact gear B below its base circle. Use a positive profile shift on gear B, more teeth on B, or a smaller addendum.");
    if (r.contactRatio < 1.0) problems.add("Contact ratio " + juce::String(r.contactRatio, 3) + " is below 1: the teeth would lose contact between engagements.");
    else if (r.contactRatio < 1.2) warnings.add("Low contact ratio (" + juce::String(r.contactRatio, 3) + ").");
    const double minimumClearance = std::min(r.tipClearanceA, r.tipClearanceB);
    if (minimumClearance > 0.0 && minimumClearance < 0.1 * m) warnings.add("Small tip clearance (" + mm(minimumClearance) + ").");
    return problems.isEmpty();
}

juce::var pairDefaults()
{
    auto p = object(); auto* o = p.getDynamicObject();
    o->setProperty("module", 2.0);
    o->setProperty("pressureAngleDegrees", 20.0);
    o->setProperty("faceWidth", 10.0);
    o->setProperty("backlash", 0.1);
    o->setProperty("addendumCoefficient", 1.0);
    o->setProperty("dedendumCoefficient", 1.25);
    o->setProperty("rootFilletCoefficient", 0.2);
    const auto gearDefaults = [](int teeth, double bore)
    {
        auto g = object(); auto* go = g.getDynamicObject();
        go->setProperty("teeth", teeth); go->setProperty("profileShift", 0.0); go->setProperty("boreDiameter", bore); go->setProperty("name", "");
        return g;
    };
    o->setProperty("gearA", gearDefaults(20, 8.0));
    o->setProperty("gearB", gearDefaults(40, 10.0));
    o->setProperty("origin", juce::Array<juce::var> { 0.0, 0.0, 0.0 });
    o->setProperty("lineOfCentersDegrees", 0.0);
    o->setProperty("mechanism", "none");
    return p;
}

double meshRotationDegrees(int teethB, double lineDegrees)
{
    double r = lineDegrees + 180.0 - 180.0 / teethB;
    r = std::fmod(r, 360.0); if (r > 180.0) r -= 360.0; if (r <= -180.0) r += 360.0;
    return r;
}

bool planPair(const juce::var& request, PairPlan& plan, juce::String& error)
{
    plan = {};
    const auto fail = [&](const juce::String& e) { error = e; return false; };
    if (!request.isObject()) return fail("Gear pair parameters must be an object.");
    auto p = pairDefaults();
    for (const auto& v : request.getDynamicObject()->getProperties())
    {
        const auto key = v.name.toString();
        if (!p.hasProperty(v.name)) return fail("Unknown gear pair parameter: " + key);
        if (key == "gearA" || key == "gearB")
        {
            if (!v.value.isObject()) return fail(key + " must be an object {teeth, profileShift, boreDiameter, name}.");
            for (const auto& g : v.value.getDynamicObject()->getProperties())
            {
                if (!p[v.name].hasProperty(g.name)) return fail("Unknown " + key + " field: " + g.name.toString());
                p[v.name].getDynamicObject()->setProperty(g.name, copyJson(g.value));
            }
        }
        else p.getDynamicObject()->setProperty(v.name, copyJson(v.value));
    }
    plan.mechanism = p["mechanism"].toString();
    if (!juce::StringArray { "none", "create", "keep" }.contains(plan.mechanism)) return fail("mechanism must be none, create or keep.");
    const auto origin = p["origin"];
    if (!origin.isArray() || origin.size() != 3 || !numeric(origin[0]) || !numeric(origin[1]) || !numeric(origin[2])) return fail("origin must be three finite millimetre coordinates.");
    if (!numeric(p["lineOfCentersDegrees"])) return fail("lineOfCentersDegrees must be a finite angle.");
    for (const auto* key : { "gearA", "gearB" }) if (!p[key]["name"].isString()) return fail(juce::String(key) + ".name must be a string.");

    const auto gearDefinition = [&](const char* key, const char* role)
    {
        auto g = defaults(); auto* o = g.getDynamicObject();
        for (const auto* shared : { "module", "pressureAngleDegrees", "faceWidth", "addendumCoefficient", "dedendumCoefficient", "rootFilletCoefficient" }) o->setProperty(shared, copyJson(p[shared]));
        o->setProperty("backlash", (double) p["backlash"] / 2.0);   // validated before the definitions are built
        for (const auto* own : { "teeth", "profileShift", "boreDiameter" }) o->setProperty(own, copyJson(p[key][own]));
        auto link = pairLinkDefault(); link.getDynamicObject()->setProperty("role", role); o->setProperty("pair", link);
        return g;
    };
    if (!numeric(p["backlash"]) || (double) p["backlash"] < 0) return fail("Backlash must be 0 or a positive total circular backlash in mm.");
    plan.gearA = gearDefinition("gearA", "A");
    plan.gearB = gearDefinition("gearB", "B");
    if (!spur(plan.gearA, plan.a, error)) return fail("Gear A: " + error);
    if (!spur(plan.gearB, plan.b, error)) return fail("Gear B: " + error);
    juce::StringArray problems;
    if (!mesh(plan.a, plan.b, plan.values, problems, plan.warnings)) return fail(problems.joinIntoString(" "));
    for (const auto& w : plan.a.warnings) plan.warnings.add("Gear A: " + w);
    for (const auto& w : plan.b.warnings) plan.warnings.add("Gear B: " + w);

    const double line = (double) p["lineOfCentersDegrees"], lineRad = line * pi / 180.0;
    for (int i = 0; i < 3; ++i) plan.centreA[(size_t) i] = (double) origin[i];
    plan.centreB = plan.centreA;
    plan.centreB[0] += plan.values.centreDistance * std::cos(lineRad);
    plan.centreB[1] += plan.values.centreDistance * std::sin(lineRad);
    plan.rotationA = line;
    plan.rotationB = meshRotationDegrees(plan.b.teeth, line);
    plan.nameA = p["gearA"]["name"].toString().trim();
    plan.nameB = p["gearB"]["name"].toString().trim();
    return true;
}

juce::var PairPlan::toJson() const
{
    auto j = object(); auto* o = j.getDynamicObject();
    o->setProperty("centreDistance", values.centreDistance);
    o->setProperty("workingPressureAngleDegrees", values.workingPressureAngle * 180.0 / pi);
    o->setProperty("ratio", values.ratio);
    o->setProperty("ratioDescription", "gear B turns " + juce::String(-values.ratio, 6) + " revolutions per gear A revolution, in the opposite direction");
    o->setProperty("contactRatio", values.contactRatio);
    o->setProperty("tipClearanceA", values.tipClearanceA);
    o->setProperty("tipClearanceB", values.tipClearanceB);
    o->setProperty("centreA", juce::Array<juce::var> { centreA[0], centreA[1], centreA[2] });
    o->setProperty("centreB", juce::Array<juce::var> { centreB[0], centreB[1], centreB[2] });
    o->setProperty("rotationADegrees", rotationA);
    o->setProperty("rotationBDegrees", rotationB);
    o->setProperty("gearA", describe(a));
    o->setProperty("gearB", describe(b));
    o->setProperty("mechanism", mechanism);
    juce::Array<juce::var> w; for (const auto& s : warnings) w.add(s);
    o->setProperty("warnings", w);
    o->setProperty("units", "mm");
    return j;
}
}
