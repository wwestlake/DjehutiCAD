#include "CadSheetStock.h"
#include "CadDocument.h"
#include <cmath>
#include <set>
#include <algorithm>

namespace djehuti::cad
{
const CadSheetStock* CadSheetStocks::find(const juce::String& id) const
{
    for (const auto& s : stocks) if (s.id == id) return &s;
    return nullptr;
}
juce::StringArray CadSheetStocks::usingMaterial(const juce::String& id) const
{
    juce::StringArray result;
    for (const auto& s : stocks) if (s.materialId == id) result.add(s.id);
    return result;
}
namespace stock
{
namespace
{
struct OptionalField { const char* key; std::optional<double> CadSheetStock::* member; };
const OptionalField optionalFields[] {
    {"finishedThicknessMm", &CadSheetStock::finishedThicknessMm}, {"sheetWidthMm", &CadSheetStock::sheetWidthMm},
    {"sheetLengthMm", &CadSheetStock::sheetLengthMm}, {"insideRadiusMm", &CadSheetStock::insideRadiusMm}, {"kFactor", &CadSheetStock::kFactor}
};
bool numeric(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }
}
bool validate(const CadSheetStock& s, juce::String& error)
{
    const auto fail = [&](const juce::String& message) { error = s.name + ": " + message; return false; };
    if (s.name.trim().isEmpty()) return fail("a stock name is required.");
    if (s.materialId.isEmpty()) return fail("an engineering material is required.");
    if (s.revision < 1 || s.catalogRevision < 0 || (s.catalogId.isEmpty() != (s.catalogRevision == 0))) return fail("invalid revision or catalog provenance.");
    if (!std::isfinite(s.thicknessMm) || s.thicknessMm <= 0) return fail("substrate thickness must be finite and greater than zero mm.");
    for (const auto& f : optionalFields)
        if (const auto& v = s.*(f.member); v && !std::isfinite(*v)) return fail(juce::String(f.key) + " must be finite.");
    if (s.finishedThicknessMm && *s.finishedThicknessMm < s.thicknessMm) return fail("finished coated thickness cannot be less than substrate thickness.");
    if (s.sheetWidthMm.has_value() != s.sheetLengthMm.has_value()) return fail("specify both sheet width and length, or leave both unspecified.");
    if ((s.sheetWidthMm && *s.sheetWidthMm <= 0) || (s.sheetLengthMm && *s.sheetLengthMm <= 0)) return fail("sheet dimensions must be greater than zero mm.");
    if (s.insideRadiusMm && *s.insideRadiusMm <= 0) return fail("inside bend radius must be greater than zero mm when specified.");
    if (s.kFactor && (*s.kFactor < 0 || *s.kFactor > 1)) return fail("K-factor must be between 0 and 1.");
    if (s.gauge.trim().isEmpty() != s.gaugeStandard.trim().isEmpty()) return fail("a gauge designation needs its gauge standard, and vice versa.");
    if (s.formingMethod != "kFactor") return fail("this version supports only the kFactor forming method.");
    if (s.qualification != "unqualified" && s.qualification != "reference" && s.qualification != "shopQualified") return fail("qualification must be unqualified, reference or shopQualified.");
    if (s.qualification != "unqualified" && (!s.kFactor || s.formingSource.trim().isEmpty())) return fail("a reference or shop-qualified preset needs a K-factor and its qualification source.");
    return true;
}
bool validateLibrary(const CadSheetStocks& data, const CadMaterials& materials, juce::String& error)
{
    std::set<juce::String> ids;
    if (data.nextNumber < 1) { error = "Invalid stock numbering."; return false; }
    for (const auto& s : data.stocks)
    {
        if (!s.id.startsWith("stock-") || s.id.length() <= 6 || !ids.insert(s.id).second) { error = "Invalid or duplicate stock id: " + s.id; return false; }
        if (!validate(s, error)) return false;
        if (!materials.find(s.materialId)) { error = s.name + " refers to missing material " + s.materialId; return false; }
    }
    if (data.defaultStockId.isNotEmpty() && !data.find(data.defaultStockId)) { error = "Default stock refers to a missing definition."; return false; }
    return true;
}
juce::var toJson(const CadSheetStock& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", s.id); o->setProperty("name", s.name); o->setProperty("revision", s.revision);
    o->setProperty("material", s.materialId); o->setProperty("lengthUnit", "mm"); o->setProperty("thicknessBasis", "substrate"); o->setProperty("thicknessMm", s.thicknessMm);
    for (const auto& f : optionalFields) if (const auto& v = s.*(f.member); v) o->setProperty(f.key, *v);
    o->setProperty("gauge", s.gauge); o->setProperty("gaugeStandard", s.gaugeStandard);
    o->setProperty("finish", s.finish); o->setProperty("coating", s.coating); o->setProperty("note", s.note); o->setProperty("source", s.source);
    o->setProperty("formingMethod", s.formingMethod); o->setProperty("qualification", s.qualification); o->setProperty("formingSource", s.formingSource);
    if (s.catalogId.isNotEmpty()) { o->setProperty("catalogId", s.catalogId); o->setProperty("catalogRevision", s.catalogRevision); }
    return o;
}
bool readArgs(const juce::var& args, CadSheetStock& s, juce::String& error)
{
    if (!args.isObject()) { error = "Stock definition must be an object."; return false; }
    if (args.hasProperty("lengthUnit") && args["lengthUnit"].toString() != "mm") { error = "Stock lengths must use mm."; return false; }
    if (args.hasProperty("thicknessBasis") && args["thicknessBasis"].toString() != "substrate") { error = "thicknessMm describes the substrate; use finishedThicknessMm separately."; return false; }
    struct TextField { const char* key; juce::String CadSheetStock::* member; };
    const TextField fields[] { {"name",&CadSheetStock::name}, {"material",&CadSheetStock::materialId}, {"gauge",&CadSheetStock::gauge}, {"gaugeStandard",&CadSheetStock::gaugeStandard},
        {"finish",&CadSheetStock::finish}, {"coating",&CadSheetStock::coating}, {"note",&CadSheetStock::note}, {"source",&CadSheetStock::source},
        {"formingMethod",&CadSheetStock::formingMethod}, {"qualification",&CadSheetStock::qualification}, {"formingSource",&CadSheetStock::formingSource} };
    for (const auto& f : fields) if (args.hasProperty(f.key)) s.*(f.member) = args[f.key].toString().trim();
    if (args.hasProperty("thicknessMm"))
    {
        if (!numeric(args["thicknessMm"])) { error = "thicknessMm must be a number."; return false; }
        s.thicknessMm = (double) args["thicknessMm"];
    }
    for (const auto& f : optionalFields) if (args.hasProperty(f.key))
    {
        if (args[f.key].isVoid()) (s.*(f.member)).reset();
        else if (!numeric(args[f.key])) { error = juce::String(f.key) + " must be numeric or null (unspecified)."; return false; }
        else s.*(f.member) = (double) args[f.key];
    }
    return true;
}
bool fromJson(const juce::var& json, CadSheetStock& s, juce::String& error)
{
    s = {};
    if (!json.hasProperty("lengthUnit") || !json.hasProperty("thicknessBasis")) { error = "Stock data must declare lengthUnit and thicknessBasis."; return false; }
    if (!readArgs(json, s, error)) return false;
    s.id = json["id"].toString(); s.revision = (int) json.getProperty("revision", 0);
    s.catalogId = json["catalogId"].toString(); s.catalogRevision = (int) json.getProperty("catalogRevision", 0);
    return validate(s, error);
}
juce::var toJson(const CadSheetStocks& data)
{
    auto* o = new juce::DynamicObject(); o->setProperty("schemaVersion", CadSheetStocks::kSchemaVersion); o->setProperty("nextNumber", data.nextNumber);
    o->setProperty("defaultStock", data.defaultStockId);
    juce::Array<juce::var> entries; for (const auto& s : data.stocks) entries.add(toJson(s)); o->setProperty("stocks", entries); return o;
}
bool fromJson(const juce::var& json, CadSheetStocks& data, const CadMaterials& materials, juce::String& error)
{
    if (!json.isObject() || (int) json["schemaVersion"] != CadSheetStocks::kSchemaVersion || !json["stocks"].isArray()) { error = "Unsupported or malformed sheet-stock schema."; return false; }
    CadSheetStocks loaded; loaded.nextNumber = (int) json.getProperty("nextNumber", 1); loaded.defaultStockId = json["defaultStock"].toString();
    for (const auto& value : *json["stocks"].getArray()) { CadSheetStock s; if (!fromJson(value, s, error)) return false; loaded.stocks.push_back(s); }
    if (!validateLibrary(loaded, materials, error)) return false;
    data = std::move(loaded); return true;
}
}

bool CadDocument::createStock(const CadSheetStock& definition, juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        auto s = definition;
        while (sheetStocks.find("stock-" + juce::String(sheetStocks.nextNumber))) ++sheetStocks.nextNumber;
        s.id = "stock-" + juce::String(sheetStocks.nextNumber++); s.revision = 1;
        if (!stock::validate(s, e)) return false;
        sheetStocks.stocks.push_back(s); id = s.id; return true;
    }, error);
}
bool CadDocument::updateStock(const juce::String& id, const CadSheetStock& definition, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& s : sheetStocks.stocks) if (s.id == id)
        {
            auto next = definition; next.id = id; next.revision = s.revision + 1;
            if (!stock::validate(next, e)) return false;
            s = next; return true;
        }
        e = "Unknown document stock: " + id; return false;
    }, error);
}
bool CadDocument::removeStock(const juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        if (sheetStocks.defaultStockId == id) { e = "Clear or change the document default before deleting this stock."; return false; }
        auto& list = sheetStocks.stocks; const auto before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& s) { return s.id == id; }), list.end());
        if (list.size() == before) { e = "Unknown document stock: " + id; return false; }
        return true;
    }, error);
}
bool CadDocument::setDefaultStock(const juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        if (id.isNotEmpty() && !sheetStocks.find(id)) { e = "Unknown document stock: " + id; return false; }
        sheetStocks.defaultStockId = id; return true;
    }, error);
}
bool CadDocument::importMaterial(const CadMaterial& source, const juce::String& featureId, juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        if (!matl::validateMaterial(source, e)) return false;
        if (featureId.isNotEmpty())
        {
            const auto* f = find(featureId);
            if (!f || f->shape.IsNull() || !TopExp_Explorer(f->shape, TopAbs_SOLID).More()) { e = "Select an existing solid feature for material assignment."; return false; }
        }
        auto m = source; m.catalogId = source.id; m.catalogRevision = source.revision;
        while (materials.find("mat-" + juce::String(materials.nextNumber))) ++materials.nextNumber;
        m.id = "mat-" + juce::String(materials.nextNumber++); m.revision = 1; materials.materials.push_back(m); id = m.id;
        if (featureId.isNotEmpty())
        {
            auto& assignments = materials.assignments;
            assignments.erase(std::remove_if(assignments.begin(), assignments.end(), [&](const auto& a) { return a.featureId == featureId; }), assignments.end());
            assignments.push_back({featureId, id});
        }
        return true;
    }, error);
}
bool CadDocument::importStock(const CadSheetStock& source, const CadMaterial& material, bool selectDefault, juce::String& id, juce::String& error)
{
    // One OCAF command creates both snapshots and their remapped reference; no partial import is observable.
    return mutate([&](juce::String& e)
    {
        if (source.materialId != material.id) { e = "The imported stock and material references disagree."; return false; }
        if (!matl::validateMaterial(material, e) || !stock::validate(source, e)) return false;
        auto m = material; m.catalogId = material.id; m.catalogRevision = material.revision;
        while (materials.find("mat-" + juce::String(materials.nextNumber))) ++materials.nextNumber;
        m.id = "mat-" + juce::String(materials.nextNumber++); m.revision = 1;
        auto s = source; s.catalogId = source.id; s.catalogRevision = source.revision;
        while (sheetStocks.find("stock-" + juce::String(sheetStocks.nextNumber))) ++sheetStocks.nextNumber;
        s.id = "stock-" + juce::String(sheetStocks.nextNumber++); s.revision = 1; s.materialId = m.id;
        materials.materials.push_back(m); sheetStocks.stocks.push_back(s); id = s.id;
        if (selectDefault) sheetStocks.defaultStockId = id;
        return true;
    }, error);
}
juce::var CadCommandService::stockCommand(const juce::var& args)
{
    auto* reply = new juce::DynamicObject(); juce::String error, id = args["stock"].toString(); bool ok = true;
    const auto action = args.getProperty("action", "list").toString(); const auto definition = args.getProperty("definition", juce::var(new juce::DynamicObject()));
    if (action == "list")
    {
        if (id.isEmpty()) reply->setProperty("data", stock::toJson(model.stocksData()));
        else if (const auto* s = model.stocksData().find(id)) reply->setProperty("stock", stock::toJson(*s));
        else { ok = false; error = "Unknown document stock: " + id; }
    }
    else if (action == "setDefault") ok = model.setDefaultStock(id, error);
    else if (action == "delete") ok = model.removeStock(id, error);
    else if (action == "create") { CadSheetStock s; ok = stock::readArgs(definition, s, error) && model.createStock(s, id, error); }
    else if (action == "update" || action == "duplicate")
    {
        const auto* old = model.stocksData().find(id);
        if (!old) { ok = false; error = "Unknown document stock: " + id; }
        else
        {
            auto s = *old; if (action == "duplicate") s.name += " copy";
            ok = stock::readArgs(definition, s, error) && (action == "update" ? model.updateStock(id, s, error) : model.createStock(s, id, error));
        }
    }
    else { ok = false; error = "Unknown stock action: " + action; }
    reply->setProperty("ok", ok); if (!ok) reply->setProperty("error", error);
    if (ok && id.isNotEmpty()) { reply->setProperty("id", id); if (const auto* s = model.stocksData().find(id)) reply->setProperty("stock", stock::toJson(*s)); }
    return reply;
}
}
