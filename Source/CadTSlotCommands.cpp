#include "CadDocument.h"
#include "CadTSlot.h"

#include <algorithm>
#include <cmath>

// cad_tslot: the parametric T-slot structural system. Features go through the shared modeling command (so the GUI, the
// agent and scripts edit the same OCAF definitions); the optional material is assigned through the existing material system.
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
    if (part.isEmpty() || part == "rail") return "tslot_rail";
    return {};
}
}

juce::var CadDocument::tslotCommand(const juce::var& args)
{
    if (!args.isObject()) return failure("invalid_arguments", "Arguments must be an object.");
    const auto action = args.getProperty("action", "get").toString();
    // Keeps the printed pieces of a two-half rail in step with the rail definition: creates, updates and removes pieces in one
    // undoable transaction, hides the whole-rail reference solid while pieces exist and gives pieces the rail's material.
    const auto syncPieces = [this](const juce::String& railId, juce::var& summary, juce::String& error)
    {
        const auto* rail = find(railId);
        if (rail == nullptr) { error = "Unknown rail."; return false; }
        const auto parameters = copyJson(rail->modeling);
        const bool twoHalf = parameters["construction"].toString() == "twoHalf";
        std::vector<tslot::Piece> plan;
        if (twoHalf && !tslot::planPieces(parameters, plan, summary, error)) return false;
        const double x = rail->x, y = rail->y, z = rail->z; const auto railName = rail->name; const auto material = parameters["material"].toString();
        return mutate([&](juce::String&)
        {
            std::vector<juce::String> existing;
            for (const auto& f : items) if (f.type == "tslot_half" && f.modeling["rail"].toString() == railId) existing.push_back(f.id);
            std::vector<juce::String> keep;
            for (const auto& piece : plan)
            {
                CadFeature* target = nullptr;
                for (auto& f : items)
                    if (f.type == "tslot_half" && f.modeling["rail"].toString() == railId && f.modeling["half"].toString() == piece.half && (int) f.modeling["index"] == piece.index) { target = &f; break; }
                if (target == nullptr)
                {
                    CadFeature created; created.id = newId("tslot_half", nextNumber++); created.type = "tslot_half"; created.x = x; created.y = y; created.z = z; created.visible = true;
                    items.push_back(created); target = &items.back();
                }
                target->modeling = tslot::pieceParameters(parameters, piece, railId);
                target->name = railName + (piece.half == "top" ? " top half " : " bottom half ") + juce::String(piece.index + 1) + (plan.size() > 2 ? "/" + juce::String((int) plan.size() / 2) : juce::String());
                target->visible = true; keep.push_back(target->id);
            }
            for (const auto& id : existing)
            {
                if (std::find(keep.begin(), keep.end(), id) != keep.end()) continue;
                items.erase(std::remove_if(items.begin(), items.end(), [&](const CadFeature& f) { return f.id == id; }), items.end());
                materials.assignments.erase(std::remove_if(materials.assignments.begin(), materials.assignments.end(), [&](const CadMaterialAssignment& a) { return a.featureId == id; }), materials.assignments.end());
            }
            for (auto& f : items) if (f.id == railId) f.visible = !twoHalf;
            if (material.isNotEmpty() && materials.find(material) != nullptr)
                for (const auto& id : keep)
                {
                    bool found = false;
                    for (auto& a : materials.assignments) if (a.featureId == id) { a.materialId = material; found = true; }
                    if (!found) materials.assignments.push_back({ id, material });
                }
            return true;
        }, error);
    };
    if (action == "describe")
    {
        auto r = object(); auto* o = r.getDynamicObject();
        o->setProperty("standard", tslot::standard()); o->setProperty("defaults", tslot::defaults("tslot_rail"));
        juce::String error; const auto section = tslot::sectionProperties(tslot::defaults("tslot_rail"), error);
        if (section.isObject()) o->setProperty("defaultSection", section);
        o->setProperty("ok", true); return r;
    }
    if (action == "list")
    {
        auto r = object(); juce::Array<juce::var> parts;
        for (const auto& f : items)
            if (tslot::isTSlotType(f.type))
            {
                auto p = object(); auto* o = p.getDynamicObject();
                o->setProperty("id", f.id); o->setProperty("name", f.name); o->setProperty("type", f.type); o->setProperty("parameters", copyJson(f.modeling));
                o->setProperty("positionMm", juce::Array<juce::var> { f.x, f.y, f.z }); parts.add(p);
            }
        r.getDynamicObject()->setProperty("features", parts); r.getDynamicObject()->setProperty("ok", true); return r;
    }
    // Printing layout: lay every piece of a two-half rail flat on the bed (slotted face down), one row per piece; assemble undoes it.
    if (action == "printLayout" || action == "assemble")
    {
        const auto* rail = find(args["id"].toString());
        if (rail == nullptr || rail->type != "tslot_rail") return failure("not_found", "Unknown T-slot rail: " + args["id"].toString());
        const auto railId = rail->id; const double rx0 = rail->x, ry0 = rail->y, rz0 = rail->z, width = rail->modeling["widthMm"], height = rail->modeling["heightMm"];
        const bool layout = action == "printLayout"; int placed = 0; juce::String error;
        const auto done = mutate([&](juce::String&)
        {
            int row = 0;
            for (auto& f : items)
            {
                if (f.type != "tslot_half" || f.modeling["rail"].toString() != railId) continue;
                if (layout)
                {
                    const bool top = f.modeling["half"].toString() == "top";
                    f.x = rx0 - (double) f.modeling["x0Mm"]; f.y = ry0 + width / 2.0 + 20.0 + row * (width + 8.0); f.z = rz0 + height / 2.0;
                    f.rx = top ? 180.0 : 0.0; f.ry = 0.0; f.rz = 0.0;
                }
                else { f.x = rx0; f.y = ry0; f.z = rz0; f.rx = 0.0; f.ry = 0.0; f.rz = 0.0; }
                ++row; ++placed;
            }
            return true;
        }, error);
        if (!done) return failure("modeling_failed", error);
        auto r = object(); r.getDynamicObject()->setProperty("ok", true); r.getDynamicObject()->setProperty("pieces", placed); r.getDynamicObject()->setProperty("layout", layout ? "print" : "assembled");
        return r;
    }
    const auto type = typeOfPart(args["part"].toString());
    if (type.isEmpty()) return failure("invalid_arguments", "part must be rail (or use action describe/list/printLayout/assemble).");
    // Deleting a two-half rail deletes its pieces first (in the same request).
    if (action == "delete" && type == "tslot_rail")
    {
        const auto railId = args["id"].toString(); juce::String error;
        const auto gone = mutate([&](juce::String&)
        {
            std::vector<juce::String> ids; for (const auto& f : items) if (f.type == "tslot_half" && f.modeling["rail"].toString() == railId) ids.push_back(f.id);
            for (const auto& id : ids)
            {
                items.erase(std::remove_if(items.begin(), items.end(), [&](const CadFeature& f) { return f.id == id; }), items.end());
                materials.assignments.erase(std::remove_if(materials.assignments.begin(), materials.assignments.end(), [&](const CadMaterialAssignment& a) { return a.featureId == id; }), materials.assignments.end());
            }
            return true;
        }, error);
        if (!gone) return failure("modeling_failed", error);
    }
    // Validate the material first so a bad id does not leave a half-configured rail behind.
    if ((action == "create" || action == "update") && args["parameters"].isObject())
    {
        const auto material = args["parameters"]["material"].toString();
        if (material.isNotEmpty() && materials.find(material) == nullptr) return failure("invalid_reference", "Unknown material: " + material + ". Create it with cad_material_create or import it with cad_catalog useMaterial first.");
    }
    auto reply = modelingCommand(type, copyJson(args));
    if (!(bool) reply["ok"]) return reply;
    const auto id = reply["id"].toString();
    if (action != "delete" && action != "preview" && id.isNotEmpty())
    {
        juce::String error;
        if (const auto* f = find(id))
        {
            const auto material = f->modeling["material"].toString();
            if (material.isNotEmpty() && !assignMaterial(id, material, error)) return failure("material_error", "The rail was created, but its material could not be assigned: " + error);
            if (args["placement"].isObject())
            {
                const auto placement = args["placement"];
                const auto moved = mutate([&](juce::String&)
                {
                    for (auto& g : items)
                        if (g.id == id)
                        {
                            if (numeric(placement["x"])) g.x = placement["x"];
                            if (numeric(placement["y"])) g.y = placement["y"];
                            if (numeric(placement["z"])) g.z = placement["z"];
                        }
                    for (const auto& r : items)
                        if (r.id == id)
                            for (auto& g : items) if (g.type == "tslot_half" && g.modeling["rail"].toString() == id) { g.x = r.x; g.y = r.y; g.z = r.z; g.rx = g.ry = g.rz = 0.0; }
                    return true;
                }, error);
                if (!moved) return failure("modeling_failed", error);
            }
            if (f->type == "tslot_rail")
            {
                juce::String syncError; juce::var summary;
                if (!syncPieces(id, summary, syncError)) return failure("modeling_failed", "The rail was updated, but its pieces could not be regenerated: " + syncError);
                if (summary.isObject()) reply.getDynamicObject()->setProperty("pieces", summary);
                f = find(id);
            }
            juce::String sectionError; const auto section = tslot::sectionProperties(f->modeling, sectionError);
            if (section.isObject())
            {
                reply.getDynamicObject()->setProperty("section", section);
                reply.getDynamicObject()->setProperty("volumeMm3", (double) section["areaMm2"] * (double) f->modeling["lengthMm"]);
            }
            if (const auto* g = find(id)) reply.getDynamicObject()->setProperty("positionMm", juce::Array<juce::var> { g->x, g->y, g->z });
        }
    }
    return reply;
}
}
