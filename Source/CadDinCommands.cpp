#include "CadDocument.h"
#include "CadDinRail.h"

#include <cmath>

// cad_din: the parametric DIN-rail system. Single parts go through the shared modeling command (so the GUI, the agent and
// scripts operate on the same OCAF feature definitions); a module (bottom + clip + friction screw) is created and kept
// aligned in one undoable transaction.
namespace djehuti::cad
{
namespace
{
juce::var object() { return juce::var(new juce::DynamicObject()); }
juce::var copyJson(const juce::var& v) { return juce::JSON::fromString(juce::JSON::toString(v, false, 17)); }
juce::var failure(const char* code, const juce::String& error)
{
    auto r = object(); r.getDynamicObject()->setProperty("ok", false); r.getDynamicObject()->setProperty("code", code); r.getDynamicObject()->setProperty("error", error); return r;
}
bool numeric(const juce::var& v) { return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite((double) v); }
juce::String typeOfPart(const juce::String& part)
{
    if (part == "rail") return "din_rail";
    if (part == "bottom") return "din_bottom";
    if (part == "clip") return "din_clip";
    if (part == "screw") return "din_screw";
    return {};
}
juce::var featureJson(const CadFeature& f)
{
    auto r = object(); auto* o = r.getDynamicObject();
    o->setProperty("id", f.id); o->setProperty("name", f.name); o->setProperty("type", f.type);
    o->setProperty("parameters", copyJson(f.modeling));
    juce::Array<juce::var> placement { f.x, f.y, f.z }; o->setProperty("positionMm", placement);
    return r;
}
}

juce::var CadDocument::dinCommand(const juce::var& args)
{
    if (!args.isObject()) return failure("invalid_arguments", "Arguments must be an object.");
    const auto action = args.getProperty("action", "get").toString();
    const auto ok = [](juce::var reply) { reply.getDynamicObject()->setProperty("ok", true); return reply; };

    if (action == "describe")
    {
        auto r = object(); auto* o = r.getDynamicObject();
        o->setProperty("standard", din::standard());
        auto d = object();
        for (const auto* part : { "rail", "bottom", "clip", "screw" }) d.getDynamicObject()->setProperty(part, din::defaults(typeOfPart(part)));
        o->setProperty("defaults", d);
        return ok(r);
    }

    if (action == "list")
    {
        auto r = object(); juce::Array<juce::var> parts;
        for (const auto& f : items) if (din::isDinType(f.type)) parts.add(featureJson(f));
        r.getDynamicObject()->setProperty("features", parts);
        return ok(r);
    }

    const auto placementOf = [&](const juce::var& source, double& x, double& y, double& z, juce::String& error)
    {
        x = numeric(source["xMm"]) ? (double) source["xMm"] : 0.0; y = 0.0; z = 0.0;
        const auto railId = source["rail"].toString();
        if (railId.isNotEmpty())
        {
            const auto* rail = find(railId);
            if (rail == nullptr || rail->type != "din_rail") { error = "Unknown DIN rail: " + railId; return false; }
            x += rail->x; y = rail->y - din::railCentreFromEdge; z = rail->z;
        }
        else if (numeric(source["yMm"])) y = source["yMm"];
        return true;
    };

    if (action == "module")
    {
        auto parameters = din::defaults("din_bottom");
        const auto request = args["parameters"];
        if (request.isObject())
            for (const auto& v : request.getDynamicObject()->getProperties())
            {
                const auto key = v.name.toString();
                if (juce::StringArray { "lengthMm", "widthMm", "thicknessMm", "tabs" }.contains(key)) parameters.getDynamicObject()->setProperty(v.name, copyJson(v.value));
                else if (!juce::StringArray { "xMm", "yMm", "rail", "name", "screwAdvanceMm" }.contains(key)) return failure("invalid_arguments", "Unknown module parameter: " + key);
            }
        juce::String error;
        if (!din::validate("din_bottom", parameters, error)) return failure("invalid_arguments", error);
        const double advance = numeric(request["screwAdvanceMm"]) ? (double) request["screwAdvanceMm"] : 0.0;
        auto screwParameters = din::defaults("din_screw"); screwParameters.getDynamicObject()->setProperty("advanceMm", advance);
        if (!din::validate("din_screw", screwParameters, error)) return failure("invalid_arguments", error);
        double x, y, z;
        if (!placementOf(request, x, y, z, error)) return failure("invalid_reference", error);
        int existing = 0; for (const auto& f : items) if (f.type == "din_bottom") ++existing;
        const auto label = request["name"].toString().isNotEmpty() ? request["name"].toString() : "DIN module " + juce::String(existing + 1);
        juce::String bottomId, clipId, screwId;
        const auto done = mutate([&](juce::String&)
        {
            CadFeature clip; clip.id = newId("din_clip", nextNumber++); clip.type = "din_clip"; clip.name = label + " clip"; clip.modeling = din::defaults("din_clip");
            CadFeature screw; screw.id = newId("din_screw", nextNumber++); screw.type = "din_screw"; screw.name = label + " friction screw"; screw.modeling = screwParameters;
            CadFeature bottom; bottom.id = newId("din_bottom", nextNumber++); bottom.type = "din_bottom"; bottom.name = label + " bottom";
            auto module = object(); module.getDynamicObject()->setProperty("clip", clip.id); module.getDynamicObject()->setProperty("screw", screw.id);
            parameters.getDynamicObject()->setProperty("module", module); bottom.modeling = parameters;
            for (auto* f : { &bottom, &clip, &screw }) { f->x = x; f->y = y; f->z = z; f->visible = true; }
            items.push_back(bottom); items.push_back(clip); items.push_back(screw);
            bottomId = bottom.id; clipId = clip.id; screwId = screw.id;
            return true;
        }, error);
        if (!done) return failure("modeling_failed", error.isEmpty() ? "The module could not be constructed." : error);
        auto r = object(); auto* o = r.getDynamicObject();
        o->setProperty("bottom", bottomId); o->setProperty("clip", clipId); o->setProperty("screw", screwId);
        o->setProperty("positionMm", juce::Array<juce::var> { x, y, z });
        o->setProperty("parameters", copyJson(find(bottomId)->modeling));
        return ok(r);
    }

    if (action == "updateModule" || action == "deleteModule")
    {
        const auto* bottom = find(args["id"].toString());
        if (bottom == nullptr || bottom->type != "din_bottom") return failure("not_found", "Unknown DIN module bottom: " + args["id"].toString());
        const auto clipId = bottom->modeling["module"]["clip"].toString(), screwId = bottom->modeling["module"]["screw"].toString();
        const auto bottomId = bottom->id;
        if (action == "deleteModule")
        {
            juce::String error;
            for (const auto& id : { screwId, clipId, bottomId }) if (id.isNotEmpty() && find(id) != nullptr && !remove(id, error)) return failure("dependency_error", error);
            auto r = object(); r.getDynamicObject()->setProperty("deleted", bottomId); return ok(r);
        }
        auto request = object(); request.getDynamicObject()->setProperty("action", "update"); request.getDynamicObject()->setProperty("id", bottomId);
        auto changes = object(); double newAdvance = -1.0; bool move = false;
        const auto source = args["parameters"];
        if (source.isObject())
            for (const auto& v : source.getDynamicObject()->getProperties())
            {
                const auto key = v.name.toString();
                if (juce::StringArray { "lengthMm", "widthMm", "thicknessMm", "tabs" }.contains(key)) changes.getDynamicObject()->setProperty(v.name, copyJson(v.value));
                else if (key == "screwAdvanceMm") { if (!numeric(v.value)) return failure("invalid_arguments", "screwAdvanceMm must be a number."); newAdvance = v.value; }
                else if (juce::StringArray { "xMm", "yMm", "rail" }.contains(key)) move = true;
                else if (key != "name") return failure("invalid_arguments", "Unknown module parameter: " + key);
            }
        juce::String error;
        if (changes.getDynamicObject()->getProperties().size() > 0)
        {
            request.getDynamicObject()->setProperty("parameters", changes);
            const auto reply = modelingCommand("din_bottom", request);
            if (!(bool) reply["ok"]) return reply;
        }
        if (newAdvance >= 0.0)
        {
            auto screwRequest = object(); screwRequest.getDynamicObject()->setProperty("action", "update"); screwRequest.getDynamicObject()->setProperty("id", screwId);
            auto screwChanges = object(); screwChanges.getDynamicObject()->setProperty("advanceMm", newAdvance); screwRequest.getDynamicObject()->setProperty("parameters", screwChanges);
            const auto reply = modelingCommand("din_screw", screwRequest);
            if (!(bool) reply["ok"]) return reply;
        }
        if (move || source["name"].toString().isNotEmpty())
        {
            double x = 0, y = 0, z = 0;
            if (move && !placementOf(source, x, y, z, error)) return failure("invalid_reference", error);
            const auto name = source["name"].toString();
            const auto moved = mutate([&](juce::String&)
            {
                for (auto& f : items)
                    if (f.id == bottomId || f.id == clipId || f.id == screwId)
                    {
                        if (move) { f.x = x; f.y = y; f.z = z; }
                        if (name.isNotEmpty()) f.name = name + (f.id == bottomId ? " bottom" : f.id == clipId ? " clip" : " friction screw");
                    }
                return true;
            }, error);
            if (!moved) return failure("modeling_failed", error);
        }
        auto r = object(); const auto* now = find(bottomId);
        r.getDynamicObject()->setProperty("bottom", bottomId); r.getDynamicObject()->setProperty("clip", clipId); r.getDynamicObject()->setProperty("screw", screwId);
        r.getDynamicObject()->setProperty("parameters", copyJson(now->modeling));
        r.getDynamicObject()->setProperty("positionMm", juce::Array<juce::var> { now->x, now->y, now->z });
        return ok(r);
    }

    // Single parts: rail, bottom, clip, screw through the shared modeling command.
    const auto type = typeOfPart(args["part"].toString());
    if (type.isEmpty()) return failure("invalid_arguments", "part must be rail, bottom, clip or screw (or use action module/updateModule/deleteModule/describe/list).");
    auto forwarded = copyJson(args);
    auto reply = modelingCommand(type, forwarded);
    if (!(bool) reply["ok"]) return reply;
    const auto id = reply["id"].toString();
    if (action != "delete" && action != "preview" && id.isNotEmpty() && args["placement"].isObject())
    {
        const auto placement = args["placement"];
        juce::String error;
        const auto moved = mutate([&](juce::String&)
        {
            for (auto& f : items)
                if (f.id == id)
                {
                    if (numeric(placement["x"])) f.x = placement["x"];
                    if (numeric(placement["y"])) f.y = placement["y"];
                    if (numeric(placement["z"])) f.z = placement["z"];
                }
            return true;
        }, error);
        if (!moved) return failure("modeling_failed", error);
        if (const auto* f = find(id)) reply.getDynamicObject()->setProperty("positionMm", juce::Array<juce::var> { f->x, f->y, f->z });
    }
    return reply;
}
}
