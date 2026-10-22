#include "CadDocument.h"

#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax3.hxx>
#include <algorithm>
#include <cmath>

namespace djehuti::cad
{
namespace
{
bool numeric(const juce::var& v) { return v.isDouble() || v.isInt() || v.isInt64(); }
double area(const TopoDS_Shape& s) { GProp_GProps p; BRepGProp::SurfaceProperties(s, p); return p.Mass(); }
void nameFace(CadFaceOrigins& names, const TopoDS_Shape& s, const juce::String& token)
{
    if (s.IsNull()) return;
    for (auto& entry : names) if (entry.first.IsSame(s)) { entry.second.addIfNotAlreadyThere(token); return; }
    names.push_back({s, {token}});
}
bool validParameters(const CadRevolveParameters& p, juce::String& error)
{
    if (!juce::StringArray{"sketchU", "sketchV", "worldX", "worldY", "worldZ", "custom"}.contains(p.axisType))
        error = "Revolve axis type must be sketchU, sketchV, worldX, worldY, worldZ or custom.";
    else if (!std::isfinite(p.angleDegrees) || p.angleDegrees <= 0 || p.angleDegrees > 360)
        error = "Revolve angle must be finite, greater than 0 and no greater than 360 degrees.";
    else
    {
        for (const auto v : p.origin) if (!std::isfinite(v)) { error = "Revolve axis origin must contain finite numbers in millimetres."; return false; }
        for (const auto v : p.direction) if (!std::isfinite(v)) { error = "Revolve axis direction must contain finite numbers."; return false; }
        if (p.axisType == "custom" && std::hypot(p.direction[0], p.direction[1], p.direction[2]) < 1e-12)
            error = "Revolve axis direction must be nonzero.";
        else return true;
    }
    return false;
}
}

juce::var revolveParametersToJson(const CadRevolveParameters& p)
{
    auto* axis = new juce::DynamicObject(); axis->setProperty("type", p.axisType);
    juce::Array<juce::var> origin, direction;
    for (auto v : p.origin) origin.add(v);
    for (auto v : p.direction) direction.add(v);
    axis->setProperty("origin", origin); axis->setProperty("direction", direction);
    auto* o = new juce::DynamicObject(); o->setProperty("axis", axis);
    o->setProperty("angleDegrees", p.angleDegrees); o->setProperty("reverse", p.reverse);
    return o;
}

bool readRevolveParameters(const juce::var& value, CadRevolveParameters& p, juce::String& error)
{
    if (!value.isObject()) { error = "Revolve parameters must be an object."; return false; }
    auto copy = p;
    for (const auto& property : value.getDynamicObject()->getProperties())
        if (!juce::StringArray{"sketch", "axis", "angleDegrees", "reverse"}.contains(property.name.toString()))
        { error = "Unknown Revolve parameter: " + property.name.toString(); return false; }
    if (value.hasProperty("angleDegrees"))
    {
        if (!numeric(value["angleDegrees"])) { error = "angleDegrees must be a number."; return false; }
        copy.angleDegrees = (double)value["angleDegrees"];
    }
    if (value.hasProperty("reverse"))
    {
        if (!value["reverse"].isBool()) { error = "reverse must be a boolean."; return false; }
        copy.reverse = (bool)value["reverse"];
    }
    if (value.hasProperty("axis"))
    {
        const auto axis = value["axis"];
        if (!axis.isObject()) { error = "axis must be an object."; return false; }
        for (const auto& property : axis.getDynamicObject()->getProperties())
            if (!juce::StringArray{"type", "origin", "direction"}.contains(property.name.toString()))
            { error = "Unknown Revolve axis parameter: " + property.name.toString(); return false; }
        if (axis.hasProperty("type"))
        {
            if (!axis["type"].isString()) { error = "axis.type must be a string."; return false; }
            copy.axisType = axis["type"].toString();
        }
        for (const auto* key : {"origin", "direction"}) if (axis.hasProperty(key))
        {
            const auto* a = axis[key].getArray();
            if (a == nullptr || a->size() != 3) { error = juce::String("axis.") + key + " must contain exactly three numbers."; return false; }
            auto& target = juce::String(key) == "origin" ? copy.origin : copy.direction;
            for (int i = 0; i < 3; ++i)
            {
                if (!numeric((*a)[i])) { error = juce::String("axis.") + key + " must contain numbers."; return false; }
                target[(size_t)i] = (double)(*a)[i];
            }
        }
    }
    if (!validParameters(copy, error)) return false;
    p = copy; return true;
}

bool CadDocument::createRevolve(const juce::String& sketchId, const CadRevolveParameters& p, juce::String& id, juce::String& error)
{
    id.clear(); error.clear();
    if (!validParameters(p, error)) return false;
    juce::String created;
    const bool ok = mutate([&](juce::String& e)
    {
        const auto* sketch = findSketch(sketchId);
        if (sketch == nullptr) { e = "Revolve requires an existing closed sketch profile."; return false; }
        if (!validateSketch(*sketch, e)) return false;
        CadFeature f; f.id = newId("revolve", nextNumber++); f.name = "Revolve " + juce::String(nextNumber - 1);
        f.type = "revolve"; f.sketchId = sketchId; f.firstOperand = sketchId; f.revolve = p;
        created = f.id; items.push_back(f);
        for (auto& source : items) if (source.id == sketchId) source.visible = false;
        return true;
    }, error);
    if (ok) id = created;
    return ok;
}

bool CadDocument::updateRevolve(const juce::String& id, const juce::String& sketchId, const CadRevolveParameters& p, juce::String& error)
{
    error.clear();
    if (!validParameters(p, error)) return false;
    return mutate([&](juce::String& e)
    {
        auto feature = std::find_if(items.begin(), items.end(), [&](const auto& f) { return f.id == id && f.type == "revolve"; });
        if (feature == items.end()) { e = "Unknown Revolve feature: " + id; return false; }
        const auto* sketch = findSketch(sketchId);
        if (sketch == nullptr) { e = "Revolve requires an existing closed sketch profile."; return false; }
        if (!validateSketch(*sketch, e)) return false;
        feature->sketchId = sketchId; feature->firstOperand = sketchId; feature->revolve = p;
        for (auto& source : items) if (source.id == sketchId) source.visible = false;
        return true;
    }, error);
}

TopoDS_Shape CadDocument::revolveSketch(const CadFeature& feature, const CadSketch& sketch, const CadSketchFrame& frame, CadFaceOrigins& origins, juce::String& error)
{
    const auto& p = feature.revolve;
    if (!validParameters(p, error)) return {};
    gp_Pnt origin; gp_Dir direction;
    if (p.axisType == "sketchU" || p.axisType == "sketchV")
    { origin = frame.origin; direction = p.axisType == "sketchU" ? frame.u : frame.v; }
    else if (p.axisType == "custom")
    { origin = gp_Pnt(p.origin[0], p.origin[1], p.origin[2]); direction = gp_Dir(p.direction[0], p.direction[1], p.direction[2]); }
    else direction = p.axisType == "worldX" ? gp_Dir(1,0,0) : p.axisType == "worldY" ? gp_Dir(0,1,0) : gp_Dir(0,0,1);
    if (std::abs(gp_Vec(direction).Dot(gp_Vec(frame.normal))) > 1e-9
        || std::abs(gp_Vec(frame.origin, origin).Dot(gp_Vec(frame.normal))) > 1e-7)
    { error = "Revolve axis must lie in the sketch plane. Choose sketch U/V or a coplanar custom axis."; return {}; }
    const gp_Ax1 axis(origin, p.reverse ? direction.Reversed() : direction);
    std::vector<TopoDS_Face> faces;
    std::vector<std::pair<TopoDS_Shape, juce::String>> edgeEntities;
    if (!sketchProfiles(sketch, frame, faces, error, &edgeEntities) || faces.empty())
    { if (error.isEmpty()) error = "Revolve needs at least one closed profile."; return {}; }

    // Classify nested closed loops as material/hole/island. Reject ambiguous overlapping or touching loops.
    std::vector<double> areas; std::vector<int> parent(faces.size(), -1), depth(faces.size(), 0);
    for (const auto& face : faces)
    {
        BRepAlgoAPI_Check check(face);
        const auto a = area(face);
        if (!check.IsValid() || !std::isfinite(a) || a <= 1e-12)
        { error = "Revolve profile is invalid or self-intersecting. Close the loops and remove crossing or duplicate edges."; return {}; }
        areas.push_back(a);
    }
    for (size_t i = 0; i < faces.size(); ++i) for (size_t j = i + 1; j < faces.size(); ++j)
    {
        BRepExtrema_DistShapeShape distance(BRepTools::OuterWire(faces[i]), BRepTools::OuterWire(faces[j]));
        if (!distance.IsDone() || distance.Value() <= 1e-7)
        { error = "Revolve profile loops touch or intersect. Use separate, closed boundaries."; return {}; }
        BRepAlgoAPI_Common common(faces[i], faces[j]); common.Build();
        if (!common.IsDone() || common.HasErrors()) { error = "Unable to classify Revolve profile boundaries."; return {}; }
        const double overlap = area(common.Shape()), smaller = std::min(areas[i], areas[j]);
        if (overlap <= 1e-10) continue;
        if (std::abs(overlap - smaller) > std::max(1e-9, smaller * 1e-8))
        { error = "Revolve profile loops overlap without containment."; return {}; }
        const auto child = areas[i] < areas[j] ? i : j, enclosing = child == i ? j : i;
        if (parent[child] < 0 || areas[enclosing] < areas[(size_t)parent[child]]) parent[child] = (int)enclosing;
    }
    for (size_t i = 0; i < faces.size(); ++i) for (int ancestor = parent[i]; ancestor >= 0; ancestor = parent[(size_t)ancestor]) ++depth[i];

    gp_Trsf toAxis; toAxis.SetTransformation(gp_Ax3(origin, frame.normal, direction.Crossed(frame.normal)));
    Bnd_Box radialBounds;
    for (const auto& face : faces)
    {
        const auto local = BRepBuilderAPI_Transform(face, toAxis, true).Shape();
        BRepBndLib::AddOptimal(local, radialBounds, false, false);
    }
    double xmin, ymin, zmin, xmax, ymax, zmax; radialBounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    if (xmin < -1e-7 && xmax > 1e-7)
    { error = "Revolve profile crosses the axis. Keep all profile material on one side; an edge may lie on the axis."; return {}; }

    TopoDS_Shape result; CadTopologyEvolution history; history.featureId = feature.id;
    const bool full = p.angleDegrees == 360.0;
    for (size_t i = 0; i < faces.size(); ++i)
    {
        if (depth[i] % 2 != 0) continue;
        BRepBuilderAPI_MakeFace region(BRep_Tool::Surface(faces[i]), BRepTools::OuterWire(faces[i]), true);
        for (size_t j = 0; j < faces.size(); ++j) if (parent[j] == (int)i)
        {
            auto hole = BRepTools::OuterWire(faces[j]);
            if (hole.Orientation() == BRepTools::OuterWire(faces[i]).Orientation()) hole.Reverse();
            region.Add(hole);
        }
        if (!region.IsDone() || !BRepCheck_Analyzer(region.Face()).IsValid())
        { error = "Unable to build a valid Revolve region with its holes."; return {}; }
        BRepPrimAPI_MakeRevol operation(region.Face(), axis, p.angleDegrees * juce::MathConstants<double>::pi / 180.0, true);
        operation.Build();
        if (!operation.IsDone() || operation.Shape().IsNull()) { error = "OCCT could not revolve the profile. Check its axis and closed boundaries."; return {}; }
        const auto solid = operation.Shape();
        juce::String loopKey;
        for (TopExp_Explorer ex(faces[i], TopAbs_EDGE); ex.More() && loopKey.isEmpty(); ex.Next())
            for (const auto& entity : edgeEntities) if (entity.first.IsSame(ex.Current())) { loopKey = entity.second; break; }
        if (!full) { nameFace(origins, operation.FirstShape(), feature.id + "/start:" + loopKey); nameFace(origins, operation.LastShape(), feature.id + "/end:" + loopKey); }
        for (TopExp_Explorer ex(region.Face(), TopAbs_EDGE); ex.More(); ex.Next())
        {
            for (TopTools_ListIteratorOfListOfShape it(operation.Generated(ex.Current())); it.More(); it.Next())
            {
                history.generated.push_back({ex.Current(), it.Value()});
                for (const auto& entity : edgeEntities) if (entity.first.IsSame(ex.Current())) nameFace(origins, it.Value(), feature.id + "/side:" + entity.second);
            }
        }
        if (result.IsNull()) result = solid;
        else
        {
            BRepAlgoAPI_Fuse fuse(result, solid); fuse.Build();
            if (!fuse.IsDone() || fuse.HasErrors()) { error = "Revolved profile regions could not be combined."; return {}; }
            CadFaceOrigins mapped;
            for (const auto& entry : origins)
            {
                if (fuse.IsDeleted(entry.first)) continue;
                const auto& changed = fuse.Modified(entry.first);
                if (changed.IsEmpty()) mapped.push_back(entry);
                else for (TopTools_ListIteratorOfListOfShape it(changed); it.More(); it.Next()) mapped.push_back({it.Value(), entry.second});
            }
            origins = std::move(mapped); result = fuse.Shape();
        }
    }
    if (result.IsNull() || !BRepCheck_Analyzer(result, true).IsValid())
    { error = "Revolve produced invalid geometry. Check profile intersections and proximity to the axis."; return {}; }
    int solids = 0;
    for (TopExp_Explorer ex(result, TopAbs_SOLID); ex.More(); ex.Next())
    {
        GProp_GProps props; BRepGProp::VolumeProperties(ex.Current(), props);
        if (!std::isfinite(props.Mass()) || props.Mass() <= 1e-12) { error = "Revolve produced a zero-volume or inverted solid."; return {}; }
        ++solids;
    }
    if (solids == 0) { error = "Revolve did not produce a solid. Check that the profile has area and the axis lies in its plane."; return {}; }
    operationHistory.push_back(std::move(history));
    return result;
}

juce::var CadCommandService::revolveCommand(const juce::var& args)
{
    auto* reply = new juce::DynamicObject(); juce::var result(reply);
    const auto fail = [&](const juce::String& code, const juce::String& error)
    { reply->setProperty("ok", false); reply->setProperty("code", code); reply->setProperty("error", error); return result; };
    if (!args.isObject()) return fail("invalid_arguments", "Revolve arguments must be an object.");
    for (const auto& property : args.getDynamicObject()->getProperties())
        if (!juce::StringArray{"action", "id", "parameters"}.contains(property.name.toString()))
            return fail("invalid_arguments", "Unknown Revolve argument: " + property.name.toString());
    if ((args.hasProperty("action") && !args["action"].isString()) || (args.hasProperty("id") && !args["id"].isString()))
        return fail("invalid_arguments", "action and id must be strings.");
    const auto action = args.getProperty("action", "get").toString();
    if (!juce::StringArray{"create", "get", "update", "delete"}.contains(action))
        return fail("invalid_arguments", "Revolve action must be create, get, update or delete.");
    auto id = args.getProperty("id", "").toString();
    const auto* feature = model.find(id);
    if (action != "create" && (feature == nullptr || feature->type != "revolve")) return fail("not_found", "Unknown Revolve feature: " + id);
    if (action == "create" || action == "update")
    {
        const auto params = args.getProperty("parameters", juce::var());
        auto p = action == "update" ? feature->revolve : CadRevolveParameters();
        juce::String error;
        if (!readRevolveParameters(params, p, error)) return fail("invalid_arguments", error);
        if (params.hasProperty("sketch") && !params["sketch"].isString()) return fail("invalid_arguments", "parameters.sketch must be a sketch identifier.");
        const auto sketch = params.getProperty("sketch", feature != nullptr && action == "update" ? feature->sketchId : juce::String()).toString();
        if (sketch.isEmpty()) return fail("invalid_arguments", "parameters.sketch is required for creation.");
        const bool ok = action == "create" ? createRevolve(sketch, p, id, error) : updateRevolve(id, sketch, p, error);
        if (!ok) return fail("modeling_failed", error);
    }
    else if (action == "delete")
    {
        juce::String error;
        if (!model.remove(id, error)) return fail("dependency_error", error);
        reply->setProperty("ok", true); reply->setProperty("id", id); reply->setProperty("deleted", true); return result;
    }
    else if (action != "get") return fail("invalid_arguments", "Revolve action must be create, get, update or delete.");
    feature = model.find(id);
    auto params = revolveParametersToJson(feature->revolve); params.getDynamicObject()->setProperty("sketch", feature->sketchId);
    reply->setProperty("ok", true); reply->setProperty("id", id); reply->setProperty("type", "revolve"); reply->setProperty("parameters", params);
    return result;
}
}
