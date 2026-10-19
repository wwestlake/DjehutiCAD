#include "CadAnalysisCases.h"
#include "CadDocument.h"
#include "CadEngineeringUnits.h"

#include <cmath>
#include <set>

namespace djehuti::cad
{
const CadAnalysisCase* CadAnalysisCases::find(const juce::String& id) const
{
    for (const auto& c : cases) if (c.id == id) return &c;
    return nullptr;
}

namespace analysis
{
namespace
{
bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }
juce::var triple(const std::array<double, 3>& v) { return juce::var(juce::Array<juce::var> { v[0], v[1], v[2] }); }
juce::var quantity(const juce::var& value, const juce::String& unit) { auto* o = new juce::DynamicObject(); o->setProperty("value", value); o->setProperty("unit", unit); return juce::var(o); }

bool readTriple(const juce::var& v, std::array<double, 3>& out, const juce::String& what, juce::String& error)
{
    const auto* list = v.getArray();
    if (list == nullptr || list->size() != 3) { error = what + " must be an array of three numbers."; return false; }
    for (int i = 0; i < 3; ++i)
    {
        if (!isNumber((*list)[i]) || !std::isfinite((double) (*list)[i])) { error = what + " must contain finite numbers."; return false; }
        out[(size_t) i] = (double) (*list)[i];
    }
    return true;
}

// { "value": number | [x,y,z], "unit": "..." } in any unit of the quantity, converted to `targetUnit`.
bool readQuantity(const juce::var& v, const juce::String& kind, const juce::String& targetUnit, double& out, juce::String& what, juce::String& error)
{
    if (!v.isObject() || !isNumber(v.getProperty("value", {}))) { error = what + " must be {\"value\": number, \"unit\": \"...\"}."; return false; }
    double si = 0.0, f = 1.0;
    if (!units::toSI(kind, (double) v.getProperty("value", 0.0), v.getProperty("unit", "").toString(), si, error)) { error = what + ": " + error; return false; }
    if (!units::factorToSI(kind, targetUnit, f, error)) return false;
    out = si / f;
    return true;
}

bool readVectorQuantity(const juce::var& v, const juce::String& kind, const juce::String& targetUnit, std::array<double, 3>& out, const juce::String& what, juce::String& error)
{
    if (!v.isObject()) { error = what + " must be {\"value\": [x, y, z], \"unit\": \"...\"}."; return false; }
    std::array<double, 3> raw {};
    if (!readTriple(v.getProperty("value", {}), raw, what, error)) return false;
    double from = 1.0, to = 1.0;
    if (!units::factorToSI(kind, v.getProperty("unit", "").toString(), from, error) || !units::factorToSI(kind, targetUnit, to, error)) { error = what + ": " + error; return false; }
    for (size_t i = 0; i < 3; ++i) out[i] = raw[i] * from / to;
    return true;
}

bool readTarget(const juce::var& v, CadAnalysisTarget& t, const juce::String& what, juce::String& error)
{
    if (!v.isObject()) { error = what + " must be an object with \"feature\" or \"instance\" (and optionally \"face\")."; return false; }
    for (const auto& p : v.getDynamicObject()->getProperties())
        if (!juce::StringArray { "feature", "instance", "face" }.contains(p.name.toString())) { error = what + ": unknown field \"" + p.name.toString() + "\"."; return false; }
    t.feature = v.getProperty("feature", "").toString(); t.instance = v.getProperty("instance", "").toString(); t.face = v.getProperty("face", "").toString();
    if (t.feature.isEmpty() == t.instance.isEmpty()) { error = what + " must name exactly one of \"feature\" (a part) or \"instance\"."; return false; }
    return true;
}

juce::var targetToJson(const CadAnalysisTarget& t)
{
    auto* o = new juce::DynamicObject();
    if (t.feature.isNotEmpty()) o->setProperty("feature", t.feature); else o->setProperty("instance", t.instance);
    if (t.face.isNotEmpty()) o->setProperty("face", t.face);
    return juce::var(o);
}

bool sameScope(const CadAnalysisTarget& a, const CadAnalysisTarget& b) { return a.feature == b.feature && a.instance == b.instance; }
}

juce::StringArray degreesOfFreedom() { return { "ux", "uy", "uz", "rx", "ry", "rz" }; }
juce::StringArray analysisTypes() { return { "linearStatic" }; }

bool validateCase(const CadAnalysisCase& c, juce::String& error)
{
    const auto label = c.name.isNotEmpty() ? c.name : c.id;
    if (c.name.trim().isEmpty()) { error = "An analysis case needs a name."; return false; }
    if (!analysisTypes().contains(c.analysisType)) { error = label + ": unknown analysis type \"" + c.analysisType + "\" (supported: " + analysisTypes().joinIntoString(", ") + ")."; return false; }
    if (!c.options.isVoid() && !c.options.isObject()) { error = label + ": settings options must be an object."; return false; }
    if (c.revision < 1 || c.nextItem < 1) { error = label + ": invalid revision or numbering."; return false; }
    for (size_t i = 0; i < c.parts.size(); ++i)
    {
        if (c.parts[i].face.isNotEmpty()) { error = label + ": the analysed parts are whole parts or instances; faces belong to loads and supports."; return false; }
        for (size_t j = 0; j < i; ++j) if (sameScope(c.parts[i], c.parts[j])) { error = label + ": part " + c.parts[i].part() + " is listed twice."; return false; }
    }
    const auto inScope = [&](const CadAnalysisTarget& t) { for (const auto& p : c.parts) if (sameScope(p, t)) return true; return false; };
    std::set<juce::String> ids;
    for (const auto& l : c.loads)
    {
        if (l.id.isEmpty() || !ids.insert(l.id).second) { error = label + ": load ids must be present and unique (" + l.id + ")."; return false; }
        if (l.type != "force" && l.type != "moment") { error = label + ": " + l.id + " type must be force or moment."; return false; }
        if (!inScope(l.target)) { error = label + ": " + l.id + " acts on " + l.target.part() + ", which is not one of the case's parts."; return false; }
        if (l.frame.isEmpty()) { error = label + ": " + l.id + " needs a frame (\"world\" or an instance id)."; return false; }
        const double n = std::sqrt(l.direction[0] * l.direction[0] + l.direction[1] * l.direction[1] + l.direction[2] * l.direction[2]);
        if (!std::isfinite(n) || std::abs(n - 1.0) > 1.0e-9) { error = label + ": " + l.id + " direction must be a unit vector."; return false; }
        if (!std::isfinite(l.magnitude) || l.magnitude <= 0.0) { error = label + ": " + l.id + " magnitude must be greater than zero (the direction gives the sense)."; return false; }
        if (l.pointMm) for (const auto v : *l.pointMm) if (!std::isfinite(v)) { error = label + ": " + l.id + " point must be finite."; return false; }
    }
    for (const auto& s : c.supports)
    {
        if (s.id.isEmpty() || !ids.insert(s.id).second) { error = label + ": support ids must be present and unique (" + s.id + ")."; return false; }
        if (!inScope(s.target)) { error = label + ": " + s.id + " restrains " + s.target.part() + ", which is not one of the case's parts."; return false; }
        if (s.frame.isEmpty()) { error = label + ": " + s.id + " needs a frame (\"world\" or an instance id)."; return false; }
        if (s.fixed.isEmpty()) { error = label + ": " + s.id + " fixes no degree of freedom."; return false; }
        juce::StringArray seen;
        for (const auto& d : s.fixed)
        {
            if (!degreesOfFreedom().contains(d)) { error = label + ": " + s.id + " has unknown degree of freedom \"" + d + "\" (use ux, uy, uz, rx, ry, rz)."; return false; }
            if (seen.contains(d)) { error = label + ": " + s.id + " lists " + d + " twice."; return false; }
            seen.add(d);
        }
    }
    if (c.gravity) for (const auto v : *c.gravity) if (!std::isfinite(v)) { error = label + ": gravity must be finite."; return false; }
    return true;
}

bool caseFromJson(const juce::var& json, CadAnalysisCase& out, juce::String& error)
{
    if (!json.isObject()) { error = "An analysis case must be a JSON object."; return false; }
    CadAnalysisCase c;
    c.id = json.getProperty("id", "").toString(); c.name = json.getProperty("name", "").toString().trim(); c.description = json.getProperty("description", "").toString();
    c.revision = (int) json.getProperty("revision", 1); c.nextItem = (int) json.getProperty("nextItem", 1);
    if (auto* list = json.getProperty("parts", {}).getArray())
        for (int i = 0; i < list->size(); ++i) { CadAnalysisTarget t; if (!readTarget((*list)[i], t, "parts[" + juce::String(i) + "]", error)) return false; c.parts.push_back(t); }
    const auto nextId = [&](const juce::String& prefix, const juce::var& given)
    {
        const auto id = given.toString();
        if (id.startsWith(prefix + "-")) c.nextItem = juce::jmax(c.nextItem, id.fromFirstOccurrenceOf("-", false, false).getIntValue() + 1);
        return id.isNotEmpty() ? id : prefix + "-" + juce::String(c.nextItem++);
    };
    // Explicit ids are read first so new items never reuse a number already present.
    for (const auto* key : { "loads", "supports" })
        if (auto* list = json.getProperty(key, {}).getArray())
            for (const auto& item : *list) { const auto id = item.getProperty("id", "").toString(); if (id.contains("-")) c.nextItem = juce::jmax(c.nextItem, id.fromLastOccurrenceOf("-", false, false).getIntValue() + 1); }
    if (auto* list = json.getProperty("loads", {}).getArray())
        for (int i = 0; i < list->size(); ++i)
        {
            const auto& v = (*list)[i];
            juce::String what = "loads[" + juce::String(i) + "]";
            if (!v.isObject()) { error = what + " must be an object."; return false; }
            CadAnalysisLoad l;
            l.id = nextId("load", v.getProperty("id", "")); l.name = v.getProperty("name", "").toString(); l.type = v.getProperty("type", "force").toString();
            if (l.type != "force" && l.type != "moment") { error = what + ": type must be \"force\" or \"moment\"."; return false; }
            if (!readTarget(v.getProperty("target", {}), l.target, what + ".target", error)) return false;
            l.frame = v.getProperty("frame", "world").toString();
            std::array<double, 3> d {};
            if (!readTriple(v.getProperty("direction", {}), d, what + ".direction", error)) return false;
            const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (n < 1.0e-12) { error = what + ".direction must not be zero."; return false; }
            for (auto& x : d) x /= n;
            l.direction = d;
            juce::String label = what + ".magnitude";
            if (!readQuantity(v.getProperty("magnitude", {}), l.type == "force" ? "force" : "moment", l.type == "force" ? "N" : "N*m", l.magnitude, label, error)) return false;
            if (v.hasProperty("point")) { std::array<double, 3> p {}; if (!readVectorQuantity(v.getProperty("point", {}), "length", "mm", p, what + ".point", error)) return false; l.pointMm = p; }
            c.loads.push_back(l);
        }
    if (auto* list = json.getProperty("supports", {}).getArray())
        for (int i = 0; i < list->size(); ++i)
        {
            const auto& v = (*list)[i];
            juce::String what = "supports[" + juce::String(i) + "]";
            if (!v.isObject()) { error = what + " must be an object."; return false; }
            CadAnalysisSupport s;
            s.id = nextId("support", v.getProperty("id", "")); s.name = v.getProperty("name", "").toString();
            if (!readTarget(v.getProperty("target", {}), s.target, what + ".target", error)) return false;
            s.frame = v.getProperty("frame", "world").toString();
            const auto kind = v.getProperty("kind", "").toString();
            if (kind == "fixed") s.fixed = degreesOfFreedom();
            else if (kind == "pinned") s.fixed = { "ux", "uy", "uz" };
            else if (kind.isNotEmpty()) { error = what + ": kind must be \"fixed\" or \"pinned\" (or list \"fixed\" degrees of freedom)."; return false; }
            if (auto* dofs = v.getProperty("fixed", {}).getArray()) { if (kind.isNotEmpty()) { error = what + ": give either kind or fixed, not both."; return false; } for (const auto& d : *dofs) s.fixed.add(d.toString()); }
            c.supports.push_back(s);
        }
    if (const auto g = json.getProperty("gravity", {}); !g.isVoid())
    {
        if (!g.isObject()) { error = "gravity must be {\"acceleration\": {\"value\": [x,y,z], \"unit\": \"m/s^2\"}} or absent."; return false; }
        std::array<double, 3> a {};
        if (!readVectorQuantity(g.getProperty("acceleration", {}), "acceleration", "m/s^2", a, "gravity.acceleration", error)) return false;
        c.gravity = a;
    }
    if (const auto s = json.getProperty("settings", {}); s.isObject())
    {
        c.analysisType = s.getProperty("analysisType", "linearStatic").toString();
        c.options = s.getProperty("options", {});
    }
    else if (!s.isVoid()) { error = "settings must be an object."; return false; }
    if (!validateCase(c, error)) return false;
    out = std::move(c);
    return true;
}

juce::var caseToJson(const CadAnalysisCase& c)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", c.id); o->setProperty("name", c.name);
    if (c.description.isNotEmpty()) o->setProperty("description", c.description);
    o->setProperty("revision", c.revision); o->setProperty("nextItem", c.nextItem);
    juce::Array<juce::var> parts, loads, supports;
    for (const auto& p : c.parts) parts.add(targetToJson(p));
    for (const auto& l : c.loads)
    {
        auto* lo = new juce::DynamicObject();
        lo->setProperty("id", l.id); if (l.name.isNotEmpty()) lo->setProperty("name", l.name);
        lo->setProperty("type", l.type); lo->setProperty("target", targetToJson(l.target)); lo->setProperty("frame", l.frame);
        lo->setProperty("direction", triple(l.direction));
        lo->setProperty("magnitude", quantity(l.magnitude, l.type == "force" ? "N" : "N*m"));
        if (l.pointMm) lo->setProperty("point", quantity(triple(*l.pointMm), "mm"));
        loads.add(juce::var(lo));
    }
    for (const auto& s : c.supports)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty("id", s.id); if (s.name.isNotEmpty()) so->setProperty("name", s.name);
        so->setProperty("target", targetToJson(s.target)); so->setProperty("frame", s.frame); so->setProperty("fixed", juce::var(s.fixed));
        supports.add(juce::var(so));
    }
    o->setProperty("parts", parts); o->setProperty("loads", loads); o->setProperty("supports", supports);
    if (c.gravity) { auto* g = new juce::DynamicObject(); g->setProperty("acceleration", quantity(triple(*c.gravity), "m/s^2")); o->setProperty("gravity", juce::var(g)); }
    auto* settings = new juce::DynamicObject();
    settings->setProperty("analysisType", c.analysisType);
    if (c.options.isObject()) settings->setProperty("options", c.options);
    o->setProperty("settings", juce::var(settings));
    return juce::var(o);
}

juce::var toJson(const CadAnalysisCases& cases)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", CadAnalysisCases::kSchemaVersion);
    root->setProperty("internalUnits", "SI (N, N*m, m/s^2); positions mm");
    root->setProperty("nextNumber", cases.nextNumber);
    juce::Array<juce::var> list;
    for (const auto& c : cases.cases) list.add(caseToJson(c));
    root->setProperty("cases", list);
    return juce::var(root);
}

bool fromJson(const juce::var& json, CadAnalysisCases& cases, juce::String& error)
{
    if (!json.isObject()) { error = "Stored analysis-case data is not valid JSON."; return false; }
    const int schema = (int) json.getProperty("schemaVersion", 0);
    if (schema < 1) { error = "Stored analysis-case data has no schema version."; return false; }
    if (schema > CadAnalysisCases::kSchemaVersion) { error = "Analysis cases use schema " + juce::String(schema) + "; this DjehutiCAD reads up to schema " + juce::String(CadAnalysisCases::kSchemaVersion) + "."; return false; }
    CadAnalysisCases loaded;
    loaded.nextNumber = (int) json.getProperty("nextNumber", 1);
    std::set<juce::String> ids;
    if (auto* list = json.getProperty("cases", {}).getArray())
        for (const auto& item : *list)
        {
            CadAnalysisCase c;
            if (!caseFromJson(item, c, error)) { error = "Stored analysis case " + item.getProperty("id", "").toString() + ": " + error; return false; }
            if (!c.id.startsWith("case-") || !ids.insert(c.id).second) { error = "Stored analysis case id \"" + c.id + "\" is missing or duplicated."; return false; }
            loaded.cases.push_back(std::move(c));
        }
    if (loaded.nextNumber < 1) { error = "Analysis-case numbering is invalid."; return false; }
    cases = std::move(loaded);
    return true;
}

juce::StringArray referenceProblems(const CadDocument& document, const CadAnalysisCase& c)
{
    juce::StringArray problems;
    const auto& mechanics = document.mechanicsData();
    const auto checkTarget = [&](const CadAnalysisTarget& t, const juce::String& owner)
    {
        const CadFeature* body = nullptr;
        if (t.instance.isNotEmpty())
        {
            const auto* inst = mechanics.findInstance(t.instance);
            if (inst == nullptr) { problems.add(owner + ": assembly instance " + t.instance + " does not exist."); return; }
            body = document.find(inst->featureId);
            if (body == nullptr) { problems.add(owner + ": instance " + inst->name + " refers to missing body " + inst->featureId + "."); return; }
        }
        else
        {
            body = document.find(t.feature);
            if (body == nullptr) { problems.add(owner + ": part " + t.feature + " does not exist."); return; }
        }
        if (body->type == "sketch" || body->shape.IsNull()) { problems.add(owner + ": " + body->name + " is not a solid part."); return; }
        if (t.face.isEmpty()) return;
        for (const auto& origin : body->faceOrigins) if (origin.second.contains(t.face)) return;
        problems.add(owner + ": face " + t.face + " does not exist on " + body->name + "; the reference was not moved to another face.");
    };
    const auto checkFrame = [&](const juce::String& frame, const juce::String& owner)
    {
        if (frame != "world" && mechanics.findInstance(frame) == nullptr) problems.add(owner + ": frame " + frame + " is neither \"world\" nor an existing assembly instance.");
    };
    for (const auto& p : c.parts) checkTarget(p, "part " + p.part());
    for (const auto& l : c.loads) { checkTarget(l.target, l.id); checkFrame(l.frame, l.id); }
    for (const auto& s : c.supports) { checkTarget(s.target, s.id); checkFrame(s.frame, s.id); }
    return problems;
}
}

// ---------------------------------------------------------------------------------------------------------------
// Command/API layer shared by the agent tools and the local automation API.
juce::var CadCommandService::analysisCommand(const juce::String& tool, const juce::var& args)
{
    auto* reply = new juce::DynamicObject();
    juce::var result(reply);
    juce::String error, id;
    bool ok = true;
    const auto describe = [this](const CadAnalysisCase& c)
    {
        auto v = analysis::caseToJson(c);
        const auto problems = analysis::referenceProblems(model, c);
        v.getDynamicObject()->setProperty("status", problems.isEmpty() ? "valid" : "invalid");
        v.getDynamicObject()->setProperty("problems", juce::var(problems));
        return v;
    };
    const auto& cases = model.analysisData();
    if (tool == "cad_analysis_list")
    {
        const auto caseId = args.getProperty("case", "").toString();
        if (caseId.isNotEmpty())
        {
            if (const auto* c = cases.find(caseId)) reply->setProperty("case", describe(*c));
            else { ok = false; error = "Unknown analysis case: " + caseId; }
        }
        else
        {
            juce::Array<juce::var> list;
            for (const auto& c : cases.cases) list.add(describe(c));
            reply->setProperty("schemaVersion", CadAnalysisCases::kSchemaVersion);
            reply->setProperty("cases", list);
        }
    }
    else if (tool == "cad_analysis_create")
    {
        CadAnalysisCase c;
        auto input = juce::JSON::parse(juce::JSON::toString(args));   // deep copy; ids and revision are assigned by the document
        if (auto* o = input.getDynamicObject()) { o->removeProperty("id"); o->removeProperty("revision"); }
        ok = analysis::caseFromJson(input, c, error) && model.createAnalysisCase(c, id, error);
        if (ok) reply->setProperty("case", describe(*model.analysisData().find(id)));
    }
    else if (tool == "cad_analysis_update")
    {
        const auto caseId = args.getProperty("case", "").toString();
        const auto* existing = cases.find(caseId);
        if (existing == nullptr) { ok = false; error = "Unknown analysis case: " + caseId; }
        else
        {
            // Fields given replace the stored ones; others are kept. gravity: null removes gravity.
            auto merged = analysis::caseToJson(*existing);
            for (const auto& p : args.getDynamicObject()->getProperties())
            {
                const auto key = p.name.toString();
                if (key == "case" || key == "id" || key == "revision" || key == "nextItem") continue;
                if (!juce::StringArray { "name", "description", "parts", "loads", "supports", "gravity", "settings" }.contains(key)) { ok = false; error = "Unknown analysis-case field \"" + key + "\"."; break; }
                if (key == "gravity" && p.value.isVoid()) merged.getDynamicObject()->removeProperty("gravity");
                else merged.getDynamicObject()->setProperty(p.name, p.value);
            }
            CadAnalysisCase c;
            ok = ok && analysis::caseFromJson(merged, c, error) && model.updateAnalysisCase(caseId, c, error);
            if (ok) reply->setProperty("case", describe(*model.analysisData().find(caseId)));
        }
    }
    else if (tool == "cad_analysis_delete") ok = model.removeAnalysisCase(args.getProperty("case", "").toString(), error);
    else { ok = false; error = "Unknown analysis tool: " + tool; }
    reply->setProperty("ok", ok);
    if (!ok) reply->setProperty("error", error);
    if (id.isNotEmpty()) reply->setProperty("id", id);
    return result;
}
}
