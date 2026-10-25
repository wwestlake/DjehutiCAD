#include "CadDocument.h"
#include "CadConstruction.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>

namespace djehuti::cad
{
namespace
{
constexpr double pi = juce::MathConstants<double>::pi;
juce::var object() { return juce::var(new juce::DynamicObject()); }
juce::var triple(double x, double y, double z) { return juce::Array<juce::var> { x, y, z }; }
bool numeric(const juce::var& v) { return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite((double) v); }
juce::var failure(const char* code, const juce::String& error)
{ auto r = object(); r.getDynamicObject()->setProperty("ok", false); r.getDynamicObject()->setProperty("code", code); r.getDynamicObject()->setProperty("error", error); return r; }
juce::var success() { auto r = object(); r.getDynamicObject()->setProperty("ok", true); return r; }
juce::var copyJson(const juce::var& v) { return juce::JSON::fromString(juce::JSON::toString(v, false, 17)); }
// Optional 3-vector field: absent leaves `present` false; present but malformed is an error.
bool readVec(const juce::var& d, const char* key, std::array<double, 3>& out, bool& present, bool nonzero, double limit, juce::String& error)
{
    present = false;
    if (!d.hasProperty(key)) return true;
    const auto v = d[key];
    if (!v.isArray() || v.size() != 3) { error = juce::String(key) + " must be three numbers."; return false; }
    double n = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (!numeric(v[i]) || std::abs((double) v[i]) > limit) { error = juce::String(key) + " must be three finite numbers within +/-" + juce::String(limit, 0) + "."; return false; }
        out[(size_t) i] = (double) v[i]; n += out[(size_t) i] * out[(size_t) i];
    }
    if (nonzero && n < 1.0e-18) { error = juce::String(key) + " must not be the zero vector."; return false; }
    present = true; return true;
}
gp_Dir zOf(const CadFrame& f) { return gp_Dir(0, 0, 1).Transformed(f.toTrsf()); }
gp_Pnt originOf(const CadFrame& f) { return gp_Pnt(f.x, f.y, f.z); }
gp_Trsf translation(const gp_Vec& v) { gp_Trsf t; t.SetTranslation(v); return t; }
bool findFace(const CadFeature& f, const juce::String& token, TopoDS_Face& face)
{
    for (const auto& entry : f.faceOrigins)
        if (entry.second.contains(token) && entry.first.ShapeType() == TopAbs_FACE) { face = TopoDS::Face(entry.first); return true; }
    return false;
}
}

namespace construction
{
juce::var toStorage(const CadConstructionRef& r)
{
    auto o = object(); auto* p = o.getDynamicObject();
    p->setProperty("id", r.id); p->setProperty("name", r.name); p->setProperty("kind", r.kind); p->setProperty("visible", r.visible);
    p->setProperty("association", r.association); p->setProperty("source", r.source); p->setProperty("face", r.face);
    p->setProperty("origin", triple(r.frame.x, r.frame.y, r.frame.z));
    p->setProperty("quaternion", juce::Array<juce::var> { r.frame.qw, r.frame.qx, r.frame.qy, r.frame.qz });
    return o;
}
bool fromStorage(const juce::var& v, CadConstructionRef& r, juce::String& error)
{
    if (!v.isObject()) { error = "A construction reference entry is invalid."; return false; }
    r = {};
    r.id = v["id"].toString(); r.name = v["name"].toString(); r.kind = v["kind"].toString(); r.visible = v.getProperty("visible", true);
    r.association = v.getProperty("association", "fixed").toString(); r.source = v["source"].toString(); r.face = v["face"].toString();
    if (r.id.isEmpty() || !isKind(r.kind) || !isAssociation(r.association)) { error = "A construction reference has an invalid identity, kind or association."; return false; }
    const auto o = v["origin"], q = v["quaternion"];
    if (!o.isArray() || o.size() != 3 || !q.isArray() || q.size() != 4) { error = "Construction reference " + r.id + " needs an origin and a quaternion."; return false; }
    for (int i = 0; i < 3; ++i) if (!numeric(o[i])) { error = "Construction reference " + r.id + " has a non-finite origin."; return false; }
    for (int i = 0; i < 4; ++i) if (!numeric(q[i])) { error = "Construction reference " + r.id + " has a non-finite rotation."; return false; }
    const double n = std::sqrt((double) q[0] * (double) q[0] + (double) q[1] * (double) q[1] + (double) q[2] * (double) q[2] + (double) q[3] * (double) q[3]);
    if (n < 1.0e-9) { error = "Construction reference " + r.id + " has a zero rotation."; return false; }
    r.frame.x = o[0]; r.frame.y = o[1]; r.frame.z = o[2];
    r.frame.qw = (double) q[0] / n; r.frame.qx = (double) q[1] / n; r.frame.qy = (double) q[2] / n; r.frame.qz = (double) q[3] / n;
    return true;
}
juce::var listToJson(const std::vector<CadConstructionRef>& list)
{ juce::Array<juce::var> out; for (const auto& r : list) out.add(toStorage(r)); return out; }
bool listFromJson(const juce::var& v, std::vector<CadConstructionRef>& list, juce::String& error)
{
    list.clear();
    if (v.isVoid()) return true;
    if (!v.isArray()) { error = "Construction references must be a list."; return false; }
    juce::StringArray ids;
    for (const auto& item : *v.getArray())
    {
        CadConstructionRef r; if (!fromStorage(item, r, error)) return false;
        if (ids.contains(r.id)) { error = "Duplicate construction reference id " + r.id + "."; return false; }
        ids.add(r.id); list.push_back(std::move(r));
    }
    return true;
}
juce::var frameToPresentation(const CadFrame& f)
{
    double rx = 0, ry = 0, rz = 0; mech::eulerDegreesFromFrame(f, rx, ry, rz);
    const auto d = zOf(f);
    auto o = object(); o.getDynamicObject()->setProperty("origin", triple(f.x, f.y, f.z));
    o.getDynamicObject()->setProperty("direction", triple(d.X(), d.Y(), d.Z()));
    o.getDynamicObject()->setProperty("rotationDegrees", triple(rx, ry, rz));
    return o;
}
}

const CadConstructionRef* CadDocument::findConstruction(const juce::String& id) const
{ for (const auto& r : constructionRefs) if (r.id == id) return &r; return nullptr; }

// The frame of a source occurrence or body (the space frame/reference associations are stored in), and the transform
// that maps its evaluated shape into world space (identity for a body whose shape is already placed).
bool CadDocument::constructionSource(const juce::String& id, gp_Trsf& frame, gp_Trsf& shapeTransform, const CadFeature*& feature, juce::String& why) const
{
    feature = nullptr; shapeTransform = gp_Trsf();
    if (const auto* instance = mechanics.findInstance(id))
    {
        feature = find(instance->featureId);
        if (feature == nullptr || feature->shape.IsNull()) { why = "The body of occurrence " + instance->name + " no longer exists."; return false; }
        if (!referenceWorld(id, frame, why)) return false;
        shapeTransform = instanceShapeTransform(feature->id, frame);
        return true;
    }
    feature = find(id);
    if (feature == nullptr) { why = "The source " + id + " no longer exists."; return false; }
    if (feature->type == "sketch") { why = feature->name + " is a sketch; attach to a body or occurrence."; return false; }
    frame = placement(*feature);
    return true;
}

CadResolvedRef CadDocument::resolveConstructionImpl(const CadConstructionRef& r, juce::StringArray visiting) const
{
    CadResolvedRef out;
    if (r.association == "fixed") { out.ok = true; out.world = r.frame; return out; }
    if (visiting.contains(r.id)) { out.problem = "Circular reference dependency."; return out; }
    visiting.add(r.id);
    if (r.association == "reference")
    {
        const auto* src = findConstruction(r.source);
        if (src == nullptr) { out.problem = "The source reference " + r.source + " no longer exists."; return out; }
        const auto s = resolveConstructionImpl(*src, visiting);
        if (!s.ok) { out.problem = "The source reference " + src->name + " is broken: " + s.problem; return out; }
        out.world = CadFrame::fromTrsf(s.world.toTrsf().Multiplied(r.frame.toTrsf())); out.ok = true; return out;
    }
    gp_Trsf frame, shapeT; const CadFeature* feature = nullptr; juce::String why;
    if (!constructionSource(r.source, frame, shapeT, feature, why)) { out.problem = why; return out; }
    if (r.association == "frame") { out.world = CadFrame::fromTrsf(frame.Multiplied(r.frame.toTrsf())); out.ok = true; return out; }
    TopoDS_Face face;
    if (!findFace(*feature, r.face, face)) { out.problem = "Face " + r.face + " of " + feature->name + " no longer exists."; return out; }
    BRepAdaptor_Surface surface(face, true);
    if (r.association == "cylinderAxis")
    {
        if (surface.GetType() != GeomAbs_Cylinder) { out.problem = "Face " + r.face + " of " + feature->name + " is no longer cylindrical."; return out; }
        const auto axis = surface.Cylinder().Axis(); double u0, u1, v0, v1; BRepTools::UVBounds(face, u0, u1, v0, v1);
        const auto centre = axis.Location().Translated(gp_Vec(axis.Direction()) * (0.5 * (v0 + v1))).Transformed(shapeT);
        const auto direction = axis.Direction().Transformed(shapeT);
        out.world = CadFrame::fromTrsf(mech::frameFromOriginAxis(centre.X(), centre.Y(), centre.Z(), direction.X(), direction.Y(), direction.Z())); out.ok = true; return out;
    }
    // faceCentre
    GProp_GProps props; BRepGProp::SurfaceProperties(face, props);
    const auto centre = props.CentreOfMass().Transformed(shapeT);
    if (surface.GetType() == GeomAbs_Plane)
    {
        auto normal = surface.Plane().Axis().Direction(); if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
        normal.Transform(shapeT);
        out.world = CadFrame::fromTrsf(mech::frameFromOriginAxis(centre.X(), centre.Y(), centre.Z(), normal.X(), normal.Y(), normal.Z()));
    }
    else
    {
        if (r.kind != "point") { out.problem = "Face " + r.face + " of " + feature->name + " is no longer planar, so it cannot define an orientation."; return out; }
        out.world = CadFrame::fromTrsf(translation(gp_Vec(centre.X(), centre.Y(), centre.Z())));
    }
    out.ok = true; return out;
}

CadResolvedRef CadDocument::resolveConstruction(const juce::String& id) const
{
    if (const auto* r = findConstruction(id)) return resolveConstructionImpl(*r, {});
    CadResolvedRef out; out.problem = "Unknown construction reference " + id + "."; return out;
}

juce::var CadDocument::constructionToJson(const CadConstructionRef& r) const
{
    const auto resolved = resolveConstructionImpl(r, {});
    auto o = object(); auto* p = o.getDynamicObject();
    p->setProperty("id", r.id); p->setProperty("name", r.name); p->setProperty("kind", r.kind); p->setProperty("visible", r.visible);
    auto assoc = object(); assoc.getDynamicObject()->setProperty("type", r.association);
    if (r.association != "fixed") assoc.getDynamicObject()->setProperty("source", r.source);
    if (r.association == "cylinderAxis" || r.association == "faceCentre") assoc.getDynamicObject()->setProperty("face", r.face);
    p->setProperty("association", assoc);
    p->setProperty("status", resolved.ok ? "ok" : "broken");
    if (!resolved.ok) p->setProperty("problem", resolved.problem);
    else p->setProperty("world", construction::frameToPresentation(resolved.world));
    if (r.association == "frame" || r.association == "reference") p->setProperty("relative", construction::frameToPresentation(r.frame));
    p->setProperty("units", "mm");
    return o;
}

// Builds a reference from a definition: kind, name, visible, association {type, source, face} and a world definition
// (origin, direction for axis/plane, rotationDegrees for cs). On update, absent fields keep the existing values.
bool CadDocument::buildConstruction(const juce::var& def, const CadConstructionRef* existing, CadConstructionRef& out, juce::String& error) const
{
    if (!def.isObject()) { error = "Reference parameters must be an object."; return false; }
    static const juce::StringArray known { "kind", "name", "visible", "association", "origin", "direction", "rotationDegrees" };
    for (const auto& prop : def.getDynamicObject()->getProperties()) if (!known.contains(prop.name.toString())) { error = "Unknown reference parameter: " + prop.name.toString(); return false; }
    out = existing != nullptr ? *existing : CadConstructionRef();
    if (def.hasProperty("kind")) out.kind = def["kind"].toString();
    if (!construction::isKind(out.kind)) { error = "Reference kind must be point, axis, cs or plane."; return false; }
    if (def.hasProperty("visible")) { if (!def["visible"].isBool()) { error = "visible must be true or false."; return false; } out.visible = def["visible"]; }
    if (def.hasProperty("name")) { if (!def["name"].isString()) { error = "name must be text."; return false; } out.name = def["name"].toString().trim(); if (out.name.length() > 80) { error = "A reference name is limited to 80 characters."; return false; } }

    // Association.
    bool associationChanged = existing == nullptr;
    juce::String type = out.association;
    if (def.hasProperty("association"))
    {
        const auto a = def["association"];
        if (!a.isObject()) { error = "association must be an object {type, source, face}."; return false; }
        for (const auto& prop : a.getDynamicObject()->getProperties()) if (!juce::StringArray { "type", "source", "face" }.contains(prop.name.toString())) { error = "Unknown association field: " + prop.name.toString(); return false; }
        type = a.getProperty("type", "fixed").toString();
        if (!construction::isAssociation(type)) { error = "Association type must be fixed, frame, cylinderAxis, faceCentre or reference."; return false; }
        out.association = type; out.source = a["source"].toString(); out.face = a["face"].toString(); associationChanged = true;
        if (type == "fixed") { out.source.clear(); out.face.clear(); }
    }
    if (type != "fixed" && out.source.isEmpty()) { error = "An associative reference needs a source id."; return false; }

    // Current world frame (to fill fields the caller did not give).
    CadFrame currentWorld;
    if (existing != nullptr) { const auto r = resolveConstructionImpl(*existing, {}); if (r.ok) currentWorld = r.world; }
    std::array<double, 3> origin { currentWorld.x, currentWorld.y, currentWorld.z }, direction { 0, 0, 1 }, rotation { 0, 0, 0 };
    { const auto z = zOf(currentWorld); direction = { z.X(), z.Y(), z.Z() }; mech::eulerDegreesFromFrame(currentWorld, rotation[0], rotation[1], rotation[2]); }
    bool hasOrigin = false, hasDirection = false, hasRotation = false;
    if (!readVec(def, "origin", origin, hasOrigin, false, 1.0e9, error) || !readVec(def, "direction", direction, hasDirection, true, 1.0e9, error) || !readVec(def, "rotationDegrees", rotation, hasRotation, false, 1.0e6, error)) return false;
    if (out.kind == "point" && (hasDirection || hasRotation)) { error = "A point has a position only; remove direction/rotationDegrees."; return false; }
    if ((out.kind == "axis" || out.kind == "plane") && hasRotation) { error = juce::String(out.kind == "axis" ? "An axis" : "A plane") + " is defined by an origin and a direction; use direction, not rotationDegrees."; return false; }
    if (out.kind == "cs" && hasDirection) { error = "A coordinate system is defined by an origin and rotationDegrees; use rotationDegrees, not direction."; return false; }
    const bool worldGiven = hasOrigin || hasDirection || hasRotation;

    const auto worldFrame = [&]() -> CadFrame
    {
        if (out.kind == "axis" || out.kind == "plane") return CadFrame::fromTrsf(mech::frameFromOriginAxis(origin[0], origin[1], origin[2], direction[0], direction[1], direction[2]));
        if (out.kind == "cs") return mech::frameFromEulerDegrees(origin[0], origin[1], origin[2], rotation[0], rotation[1], rotation[2]);
        return CadFrame::fromTrsf(translation(gp_Vec(origin[0], origin[1], origin[2])));
    };

    if (type == "fixed") { if (worldGiven || existing == nullptr || associationChanged) out.frame = worldFrame(); }
    else if (type == "frame" || type == "reference")
    {
        gp_Trsf sourceFrame;
        if (type == "reference")
        {
            if (out.source == out.id && out.id.isNotEmpty()) { error = "A reference cannot depend on itself."; return false; }
            const auto* src = findConstruction(out.source);
            if (src == nullptr) { error = "Unknown source reference " + out.source + "."; return false; }
            // Cycle: the source chain must not lead back to this reference.
            juce::StringArray chain; auto* walk = src;
            while (walk != nullptr && walk->association == "reference")
            {
                if (walk->id == out.id || chain.contains(walk->id)) { error = "This association would create a circular reference dependency."; return false; }
                chain.add(walk->id); walk = findConstruction(walk->source);
            }
            if (walk != nullptr && walk->id == out.id) { error = "This association would create a circular reference dependency."; return false; }
            const auto resolved = resolveConstructionImpl(*src, {});
            if (!resolved.ok) { error = "The source reference is broken: " + resolved.problem; return false; }
            sourceFrame = resolved.world.toTrsf();
        }
        else
        {
            gp_Trsf shapeT; const CadFeature* feature = nullptr; juce::String why;
            if (!constructionSource(out.source, sourceFrame, shapeT, feature, why)) { error = why; return false; }
            if (mechanics.findInstance(out.source) == nullptr)
                for (const auto& i : mechanics.instances) if (i.featureId == out.source) { error = feature->name + " is placed through its occurrences; attach to the occurrence " + i.name + " (" + i.id + ")."; return false; }
        }
        if (worldGiven) out.frame = CadFrame::fromTrsf(sourceFrame.Inverted().Multiplied(worldFrame().toTrsf()));
        else if (existing == nullptr) out.frame = CadFrame();   // follows the source's own frame exactly
        else if (associationChanged) out.frame = CadFrame::fromTrsf(sourceFrame.Inverted().Multiplied(currentWorld.toTrsf()));   // keep the reference where it is
    }
    else   // cylinderAxis / faceCentre: derived entirely from the face
    {
        if (type == "cylinderAxis" && out.kind == "plane") { error = "A cylinder gives a point, axis or coordinate system; a plane needs a planar face."; return false; }
        if (out.face.isEmpty()) { error = "This association needs a face token (see cad_list_faces)."; return false; }
        if (worldGiven) { error = "A reference derived from a face takes its position from the face; remove origin/direction/rotationDegrees."; return false; }
        out.frame = CadFrame();
        gp_Trsf frame, shapeT; const CadFeature* feature = nullptr; juce::String why;
        if (!constructionSource(out.source, frame, shapeT, feature, why)) { error = why; return false; }
        if (mechanics.findInstance(out.source) == nullptr)
            for (const auto& i : mechanics.instances) if (i.featureId == out.source) { error = feature->name + " is placed through its occurrences; attach to the occurrence " + i.name + " (" + i.id + ")."; return false; }
    }
    if (out.name.isEmpty())
    {
        int count = 1; for (const auto& r : constructionRefs) if (r.kind == out.kind) ++count;
        out.name = juce::String(out.kind == "point" ? "Point" : out.kind == "axis" ? "Axis" : out.kind == "plane" ? "Plane" : "Coordinate system") + " " + juce::String(count);
    }
    // The finished reference must resolve now (reports a wrong face type, a missing face or a non-planar normal).
    CadConstructionRef probe = out; if (probe.id.isEmpty()) probe.id = "(new)";
    const auto resolved = resolveConstructionImpl(probe, {});
    if (!resolved.ok) { error = resolved.problem; return false; }
    return true;
}

juce::var CadDocument::referenceCommand(const juce::var& args)
{
    if (!args.isObject()) return failure("invalid_arguments", "Arguments must be an object.");
    const auto action = args.getProperty("action", "list").toString(); const auto id = args["id"].toString(); juce::String error;
    if (action == "list")
    {
        auto reply = success(); juce::Array<juce::var> list; for (const auto& r : constructionRefs) list.add(constructionToJson(r));
        reply.getDynamicObject()->setProperty("references", list); return reply;
    }
    if (action == "snapPoints")
    {
        const auto body = args["body"].toString();
        if (mechanics.findInstance(body) == nullptr && (find(body) == nullptr || find(body)->shape.IsNull())) return failure("not_found", "Unknown body or occurrence: " + body);
        const auto points = snapPoints(body); juce::Array<juce::var> list;
        for (size_t i = 0; i < points.size() && i < 2000; ++i)
        { auto p = object(); p.getDynamicObject()->setProperty("type", points[i].type); p.getDynamicObject()->setProperty("point", triple(points[i].point[0], points[i].point[1], points[i].point[2])); p.getDynamicObject()->setProperty("label", points[i].label); if (points[i].face.isNotEmpty()) p.getDynamicObject()->setProperty("face", points[i].face); list.add(p); }
        auto reply = success(); reply.getDynamicObject()->setProperty("body", body); reply.getDynamicObject()->setProperty("count", (int) points.size()); reply.getDynamicObject()->setProperty("snapPoints", list); reply.getDynamicObject()->setProperty("units", "mm"); return reply;
    }
    if (action == "create")
    {
        CadConstructionRef built; if (!buildConstruction(args["parameters"], nullptr, built, error)) return failure("invalid_arguments", error);
        juce::String created;
        const auto ok = mutate([&](juce::String& e)
        {
            if ((int) constructionRefs.size() >= 5000) { e = "A document is limited to 5000 construction references."; return false; }
            built.id = newId("ref", nextNumber++); created = built.id; constructionRefs.push_back(built); return true;
        }, error);
        if (!ok) return failure("reference_failed", error);
        auto reply = success(); reply.getDynamicObject()->setProperty("id", created); reply.getDynamicObject()->setProperty("reference", constructionToJson(*findConstruction(created))); return reply;
    }
    const auto* existing = findConstruction(id);
    if (existing == nullptr) return failure("not_found", "Unknown construction reference: " + id);
    if (action == "get") { auto reply = success(); reply.getDynamicObject()->setProperty("id", id); reply.getDynamicObject()->setProperty("reference", constructionToJson(*existing)); return reply; }
    if (action == "update")
    {
        CadConstructionRef built; if (!buildConstruction(args["parameters"], existing, built, error)) return failure("invalid_arguments", error);
        const auto ok = mutate([&](juce::String&) { for (auto& r : constructionRefs) if (r.id == id) r = built; return true; }, error);
        if (!ok) return failure("reference_failed", error);
        auto reply = success(); reply.getDynamicObject()->setProperty("id", id); reply.getDynamicObject()->setProperty("reference", constructionToJson(*findConstruction(id))); return reply;
    }
    if (action == "delete")
    {
        // References that were derived from this one stay in the document and report the broken association.
        juce::StringArray dependents; for (const auto& r : constructionRefs) if (r.association == "reference" && r.source == id) dependents.add(r.name);
        const auto ok = mutate([&](juce::String&) { constructionRefs.erase(std::remove_if(constructionRefs.begin(), constructionRefs.end(), [&](const CadConstructionRef& r) { return r.id == id; }), constructionRefs.end()); return true; }, error);
        if (!ok) return failure("reference_failed", error);
        auto reply = success(); reply.getDynamicObject()->setProperty("id", id); reply.getDynamicObject()->setProperty("deleted", true);
        if (!dependents.isEmpty()) reply.getDynamicObject()->setProperty("warning", "Now broken: " + dependents.joinIntoString(", "));
        return reply;
    }
    return failure("invalid_arguments", "Action must be list, get, create, update, delete or snapPoints.");
}

std::vector<CadSnapPoint> CadDocument::snapPoints(const juce::String& bodyId) const
{
    std::vector<CadSnapPoint> out;
    gp_Trsf frame, shapeT; const CadFeature* feature = nullptr; juce::String why;
    if (!constructionSource(bodyId, frame, shapeT, feature, why) || feature == nullptr || feature->shape.IsNull()) return out;
    const auto world = [&](const gp_Pnt& p) { return p.Transformed(shapeT); };
    const auto add = [&](const gp_Pnt& p, const char* type, const juce::String& label, const juce::String& face = {})
    { CadSnapPoint s; const auto w = world(p); s.point = { w.X(), w.Y(), w.Z() }; s.type = type; s.label = label; s.face = face; out.push_back(std::move(s)); };
    TopTools_IndexedMapOfShape vertices, edges, faces;
    TopExp::MapShapes(feature->shape, TopAbs_VERTEX, vertices); TopExp::MapShapes(feature->shape, TopAbs_EDGE, edges); TopExp::MapShapes(feature->shape, TopAbs_FACE, faces);
    for (int i = 1; i <= vertices.Extent() && i <= 6000; ++i) add(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))), "vertex", "Vertex " + juce::String(i));
    for (int i = 1; i <= edges.Extent() && i <= 6000; ++i)
    {
        const auto edge = TopoDS::Edge(edges(i)); if (BRep_Tool::Degenerated(edge)) continue;
        BRepAdaptor_Curve curve(edge); const auto type = curve.GetType();
        // Midpoint and centre are offered only where they are exact: lines and circular arcs.
        if (type == GeomAbs_Line || type == GeomAbs_Circle) add(curve.Value(0.5 * (curve.FirstParameter() + curve.LastParameter())), "midpoint", "Edge " + juce::String(i) + " midpoint");
        if (type == GeomAbs_Circle) add(curve.Circle().Location(), "centre", "Edge " + juce::String(i) + " centre");
    }
    for (int i = 1; i <= faces.Extent() && i <= 3000; ++i)
    {
        const auto face = TopoDS::Face(faces(i)); BRepAdaptor_Surface surface(face, true); const auto type = surface.GetType();
        juce::String token; for (const auto& entry : feature->faceOrigins) if (entry.first.IsSame(face) && !entry.second.isEmpty()) { token = entry.second[0]; break; }
        if (type == GeomAbs_Plane) { GProp_GProps props; BRepGProp::SurfaceProperties(face, props); add(props.CentreOfMass(), "faceCentre", "Face " + juce::String(i) + " centre", token); }
        else if (type == GeomAbs_Cylinder)
        {
            const auto axis = surface.Cylinder().Axis(); double u0, u1, v0, v1; BRepTools::UVBounds(face, u0, u1, v0, v1);
            add(axis.Location().Translated(gp_Vec(axis.Direction()) * (0.5 * (v0 + v1))), "axisPoint", "Cylinder " + juce::String(i) + " axis midpoint", token);
        }
    }
    return out;
}

bool CadDocument::applyRigidDelta(const juce::String& id, const gp_Trsf& delta, juce::String& e)
{
    if (auto it = std::find_if(mechanics.instances.begin(), mechanics.instances.end(), [&](const CadMechanicalInstance& i) { return i.id == id; }); it != mechanics.instances.end())
    {
        if (!it->grounded && mechanics.instanceHasJoints(id)) { e = it->name + " is positioned by its joints; ground it or edit the joints to move it."; return false; }
        it->placement = CadFrame::fromTrsf(delta.Multiplied(it->placement.toTrsf()));
        juce::String ignored; reevaluateBondedJoints(ignored);
        ++mechanics.interfaceRevision; ++mechanics.profileRevision; return true;
    }
    for (auto& f : items)
        if (f.id == id)
        {
            const auto p = gp_Pnt(f.x, f.y, f.z).Transformed(delta);
            gp_Trsf turn = delta; turn.SetTranslationPart(gp_Vec());
            gp_Trsf a, b, c;
            a.SetRotation(gp::OX(), f.rx * pi / 180); b.SetRotation(gp::OY(), f.ry * pi / 180); c.SetRotation(gp::OZ(), f.rz * pi / 180);
            double rx = 0, ry = 0, rz = 0; mech::eulerDegreesFromFrame(CadFrame::fromTrsf(turn.Multiplied(c.Multiplied(b).Multiplied(a))), rx, ry, rz);
            f.x = p.X(); f.y = p.Y(); f.z = p.Z(); f.rx = rx; f.ry = ry; f.rz = rz; return true;
        }
    e = "Unknown body or occurrence: " + id; return false;
}

bool CadDocument::transformBody(const juce::String& id, const gp_Trsf& delta, juce::String& error)
{ return mutate([&](juce::String& e) { return applyRigidDelta(id, delta, e); }, error); }

juce::var CadDocument::transformCommand(const juce::var& args)
{
    if (!args.isObject()) return failure("invalid_arguments", "Arguments must be an object.");
    static const juce::StringArray known { "operation", "target", "copy", "displacement", "frame", "fromReference", "toReference", "axis", "angleDegrees", "source", "to", "flip", "plane", "center", "factor" };
    for (const auto& prop : args.getDynamicObject()->getProperties()) if (!known.contains(prop.name.toString())) return failure("invalid_arguments", "Unknown transform parameter: " + prop.name.toString());
    const auto op = args["operation"].toString(), target = args["target"].toString(); juce::String error;
    if (!juce::StringArray { "move", "rotate", "align", "mirror", "scale" }.contains(op)) return failure("invalid_arguments", "operation must be move, rotate, align, mirror or scale.");
    if (args.hasProperty("copy") && !args["copy"].isBool()) return failure("invalid_arguments", "copy must be true or false.");
    const bool copy = args.getProperty("copy", false);

    const auto* instance = mechanics.findInstance(target);
    const CadFeature* body = instance != nullptr ? find(instance->featureId) : find(target);
    if (body == nullptr || body->shape.IsNull() || body->type == "sketch") return failure("not_found", "Choose a body or an assembly occurrence to transform (unknown or empty: " + target + ").");
    if (instance == nullptr) for (const auto& i : mechanics.instances) if (i.featureId == target) return failure("invalid_arguments", body->name + " is placed through its occurrences; transform the occurrence " + i.name + " (" + i.id + ").");
    gp_Trsf targetFrame; { gp_Trsf shapeT; const CadFeature* f = nullptr; juce::String why; if (!constructionSource(target, targetFrame, shapeT, f, why)) return failure("not_found", why); }
    const auto targetOrigin = gp_Pnt(0, 0, 0).Transformed(targetFrame);

    const auto reference = [&](const char* key, std::initializer_list<const char*> kinds, CadResolvedRef& out, const CadConstructionRef*& ref) -> juce::String
    {
        const auto rid = args[key].toString();
        if (rid.isEmpty()) return juce::String(key) + " must name a construction reference.";
        ref = findConstruction(rid); if (ref == nullptr) return "Unknown construction reference " + rid + " (" + key + ").";
        bool allowed = false; for (const auto* k : kinds) allowed = allowed || ref->kind == k;
        if (!allowed) { juce::StringArray names; for (const auto* k : kinds) names.add(k); return juce::String(key) + " must be a reference of kind " + names.joinIntoString(", ") + "; " + ref->name + " is a " + ref->kind + "."; }
        out = resolveConstructionImpl(*ref, {}); if (!out.ok) return ref->name + " is broken: " + out.problem;
        return {};
    };

    gp_Trsf delta;
    if (op == "move")
    {
        if (args.hasProperty("displacement"))
        {
            std::array<double, 3> d {}; bool present = false; if (!readVec(args, "displacement", d, present, false, 1.0e9, error)) return failure("invalid_arguments", error);
            gp_Vec v(d[0], d[1], d[2]);
            const auto frameId = args["frame"].toString();
            if (frameId.isNotEmpty() && frameId != "world")
            {
                const auto* r = findConstruction(frameId); if (r == nullptr) return failure("not_found", "Unknown construction reference " + frameId + " (frame).");
                const auto resolved = resolveConstructionImpl(*r, {}); if (!resolved.ok) return failure("reference_broken", r->name + " is broken: " + resolved.problem);
                if (r->kind == "point") return failure("invalid_arguments", "A point has no orientation; choose an axis, plane or coordinate system as the displacement frame.");
                gp_Trsf rotation = resolved.world.toTrsf(); rotation.SetTranslationPart(gp_Vec()); v.Transform(rotation);
            }
            delta = translation(v);
        }
        else
        {
            CadResolvedRef to, from; const CadConstructionRef* toRef = nullptr; const CadConstructionRef* fromRef = nullptr;
            if (const auto why = reference("toReference", { "point", "axis", "cs", "plane" }, to, toRef); why.isNotEmpty()) return failure("invalid_arguments", why);
            gp_Pnt start = targetOrigin;
            if (args["fromReference"].toString().isNotEmpty()) { if (const auto why = reference("fromReference", { "point", "axis", "cs", "plane" }, from, fromRef); why.isNotEmpty()) return failure("invalid_arguments", why); start = originOf(from.world); }
            delta = translation(gp_Vec(start, originOf(to.world)));
        }
    }
    else if (op == "rotate")
    {
        CadResolvedRef axis; const CadConstructionRef* ref = nullptr;
        if (const auto why = reference("axis", { "axis", "cs", "plane" }, axis, ref); why.isNotEmpty()) return failure("invalid_arguments", why);
        if (!numeric(args["angleDegrees"]) || std::abs((double) args["angleDegrees"]) > 36000.0) return failure("invalid_arguments", "angleDegrees must be a finite angle within +/-36000.");
        delta.SetRotation(gp_Ax1(originOf(axis.world), zOf(axis.world)), (double) args["angleDegrees"] * pi / 180.0);
    }
    else if (op == "align")
    {
        CadResolvedRef src, dst; const CadConstructionRef* srcRef = nullptr; const CadConstructionRef* dstRef = nullptr;
        if (const auto why = reference("source", { "point", "axis", "cs", "plane" }, src, srcRef); why.isNotEmpty()) return failure("invalid_arguments", why);
        if (const auto why = reference("to", { "point", "axis", "cs", "plane" }, dst, dstRef); why.isNotEmpty()) return failure("invalid_arguments", why);
        if (args.hasProperty("flip") && !args["flip"].isBool()) return failure("invalid_arguments", "flip must be true or false.");
        const bool flip = args.getProperty("flip", false);
        juce::String kind = srcRef->kind;
        if (srcRef->kind != dstRef->kind)
        {
            if (srcRef->kind == "cs") kind = dstRef->kind; else if (dstRef->kind == "cs") kind = srcRef->kind;
            else return failure("invalid_arguments", "Align needs two references of the same kind (or a coordinate system with a point, axis or plane); " + srcRef->name + " is a " + srcRef->kind + " and " + dstRef->name + " is a " + dstRef->kind + ".");
        }
        const auto so = originOf(src.world), to = originOf(dst.world); auto sz = zOf(src.world), tz = zOf(dst.world); if (flip) tz.Reverse();
        const auto rotateOnto = [&](const gp_Dir& from, const gp_Dir& onto, const gp_Pnt& about) -> gp_Trsf
        {
            gp_Trsf r; const double dot = from.Dot(onto);
            if (dot > 1.0 - 1.0e-12) return r;
            if (dot < -1.0 + 1.0e-12) { gp_Dir perpendicular = std::abs(from.X()) < 0.9 ? gp_Dir(1, 0, 0) : gp_Dir(0, 1, 0); r.SetRotation(gp_Ax1(about, from.Crossed(perpendicular)), pi); return r; }
            r.SetRotation(gp_Ax1(about, from.Crossed(onto)), std::acos(juce::jlimit(-1.0, 1.0, dot))); return r;
        };
        if (kind == "point") delta = translation(gp_Vec(so, to));
        else if (kind == "cs")
        {
            gp_Trsf s = src.world.toTrsf(), t = dst.world.toTrsf();
            if (flip) { gp_Trsf turn; turn.SetRotation(gp_Ax1(gp::Origin(), gp::DX()), pi); t = t.Multiplied(turn); }
            delta = t.Multiplied(s.Inverted());
        }
        else
        {
            const auto turn = rotateOnto(sz, tz, so);   // rotate about the source origin so its direction matches the target's
            const auto movedOrigin = so;                // the rotation pivots on it, so it stays put
            if (kind == "axis")
            {
                // Concentric: slide the source origin onto the target axis by the perpendicular offset only.
                const gp_Vec toTarget(movedOrigin, to); const gp_Vec along = gp_Vec(tz) * toTarget.Dot(gp_Vec(tz)); delta = translation(toTarget - along).Multiplied(turn);
            }
            else   // plane: coplanar by moving along the target normal only
            {
                const gp_Vec toTarget(movedOrigin, to); delta = translation(gp_Vec(tz) * toTarget.Dot(gp_Vec(tz))).Multiplied(turn);
            }
        }
    }

    if (op == "mirror")
    {
        CadResolvedRef plane; const CadConstructionRef* ref = nullptr;
        if (const auto why = reference("plane", { "plane", "cs" }, plane, ref); why.isNotEmpty()) return failure("invalid_arguments", why);
        if (instance != nullptr) return failure("invalid_arguments", "Mirroring creates new mirrored geometry, which would turn the occurrence " + instance->name + " into an independent part. Mirror the body instead, or copy the occurrence and rotate it.");
        const auto n = zOf(plane.world); auto def = object(); auto* d = def.getDynamicObject();
        auto sources = juce::Array<juce::var>(); sources.add(target); d->setProperty("sources", sources); d->setProperty("operation", "bodies"); d->setProperty("target", ""); d->setProperty("includeSource", copy);
        auto p = object(); p.getDynamicObject()->setProperty("type", "custom"); p.getDynamicObject()->setProperty("origin", triple(plane.world.x, plane.world.y, plane.world.z)); p.getDynamicObject()->setProperty("vector", triple(n.X(), n.Y(), n.Z()));
        d->setProperty("plane", p);
        auto request = object(); request.getDynamicObject()->setProperty("action", "create"); request.getDynamicObject()->setProperty("parameters", def);
        auto reply = replicationCommand("mirror", request);
        if (!(bool) reply["ok"]) return reply;
        reply.getDynamicObject()->setProperty("operation", "mirror"); reply.getDynamicObject()->setProperty("copy", copy); reply.getDynamicObject()->setProperty("original", target); return reply;
    }
    if (op == "scale")
    {
        CadResolvedRef centre; const CadConstructionRef* ref = nullptr;
        if (const auto why = reference("center", { "point", "axis", "cs", "plane" }, centre, ref); why.isNotEmpty()) return failure("invalid_arguments", why);
        if (!numeric(args["factor"]) || (double) args["factor"] < 0.001 || (double) args["factor"] > 1000.0) return failure("invalid_arguments", "factor must be a finite scale from 0.001 to 1000.");
        if (instance != nullptr) return failure("invalid_arguments", "Scaling would change the definition shared by every occurrence of " + body->name + ". Scale the body before inserting it, or insert a component of the size you need.");
        if (body->type != "box" && body->type != "cylinder") return failure("invalid_arguments", "Scale supports box and cylinder bodies; " + body->name + " is a " + body->type + ".");
        const double k = (double) args["factor"]; const auto c = originOf(centre.world); juce::String newId_;
        const auto ok = mutate([&](juce::String& e)
        {
            CadFeature* f = nullptr;
            if (copy)
            {
                CadFeature dup = *body; dup.id = newId(body->type.toRawUTF8(), nextNumber++); dup.name = body->name + " copy"; dup.shape = {}; dup.faceOrigins.clear();
                items.push_back(dup); f = &items.back(); newId_ = f->id;
            }
            else for (auto& item : items) if (item.id == target) f = &item;
            if (f == nullptr) { e = "Unknown body: " + target; return false; }
            f->x = c.X() + k * (f->x - c.X()); f->y = c.Y() + k * (f->y - c.Y()); f->z = c.Z() + k * (f->z - c.Z());
            f->width *= k; f->depth *= k; f->height *= k; f->radius *= k;
            return true;
        }, error);
        if (!ok) return failure("transform_failed", error);
        auto reply = success(); reply.getDynamicObject()->setProperty("operation", "scale"); reply.getDynamicObject()->setProperty("copy", copy); reply.getDynamicObject()->setProperty("original", target);
        reply.getDynamicObject()->setProperty("id", copy ? newId_ : target); if (copy) reply.getDynamicObject()->setProperty("copyId", newId_); return reply;
    }

    // Rigid operations (move, rotate, align): the original is transformed, or - when requested - an unchanged copy is
    // made and the copy is transformed. An occurrence is copied as another occurrence of the same definition.
    juce::String copyId;
    const auto ok = mutate([&](juce::String& e)
    {
        juce::String moved = target;
        if (copy)
        {
            if (instance != nullptr)
            {
                CadMechanicalInstance dup = *instance; dup.id = newId("inst", nextNumber++); dup.name = instance->name + " copy"; dup.grounded = true;   // a copy keeps the pose it is placed at
                mechanics.instances.push_back(dup); moved = dup.id;
            }
            else
            {
                if (body->type == "component") { e = "Components are copied as occurrences; use an occurrence."; return false; }
                CadFeature dup = *body; dup.id = newId(body->type.toRawUTF8(), nextNumber++); dup.name = body->name + " copy"; dup.shape = {}; dup.faceOrigins.clear();
                if (dup.type == "gear" && dup.modeling.isObject()) { auto link = object(); link.getDynamicObject()->setProperty("mate", ""); link.getDynamicObject()->setProperty("role", ""); dup.modeling = copyJson(dup.modeling); dup.modeling.getDynamicObject()->setProperty("pair", link); }
                items.push_back(dup); moved = dup.id;
            }
            copyId = moved; ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        }
        return applyRigidDelta(moved, delta, e);
    }, error);
    if (!ok) return failure("transform_failed", error);
    auto reply = success(); auto* r = reply.getDynamicObject();
    r->setProperty("operation", op); r->setProperty("copy", copy); r->setProperty("original", target); r->setProperty("id", copy ? copyId : target);
    if (copy) r->setProperty("copyId", copyId);
    gp_Trsf rotationOnly = delta; rotationOnly.SetTranslationPart(gp_Vec());
    r->setProperty("translationMm", triple(delta.TranslationPart().X(), delta.TranslationPart().Y(), delta.TranslationPart().Z()));
    r->setProperty("rotationAngleDegrees", rotationOnly.GetRotation().GetRotationAngle() * 180.0 / pi);
    return reply;
}
}
