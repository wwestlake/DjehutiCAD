#include "CadEngineeringCatalog.h"
#include "CadDocument.h"
#include <EngineeringData.h>
#include <algorithm>

namespace djehuti::cad
{
namespace
{
juce::String fresh(const char* prefix) { return juce::String(prefix) + juce::Uuid().toDashedString(); }
juce::var encode(const CadMaterials& materials, const CadSheetStocks& stocks, const juce::StringArray& excluded = {})
{
    auto* o = new juce::DynamicObject(); o->setProperty("format", "djehuti-engineering-catalog"); o->setProperty("schemaVersion", 1);
    o->setProperty("materialSchemaVersion", CadMaterials::kSchemaVersion); o->setProperty("stockSchemaVersion", CadSheetStocks::kSchemaVersion);
    juce::Array<juce::var> m, s;
    for (const auto& item : materials.materials) if (!excluded.contains(item.id)) m.add(matl::materialToJson(item));
    for (const auto& item : stocks.stocks) if (!excluded.contains(item.id)) s.add(stock::toJson(item));
    o->setProperty("materials", m); o->setProperty("stocks", s); return o;
}
bool decode(const juce::var& json, CadMaterials& materials, CadSheetStocks& stocks, juce::String& error)
{
    if (!json.isObject() || json["format"].toString() != "djehuti-engineering-catalog" || (int) json["schemaVersion"] != 1
        || (int) json.getProperty("materialSchemaVersion", 1) < 1 || (int) json.getProperty("materialSchemaVersion", 1) > CadMaterials::kSchemaVersion
        || (int) json.getProperty("stockSchemaVersion", 1) != CadSheetStocks::kSchemaVersion
        || !json["materials"].isArray() || !json["stocks"].isArray()) { error = "Unsupported or malformed engineering catalog."; return false; }
    for (const auto& value : *json["materials"].getArray()) { CadMaterial m; if (!matl::materialFromJson(value, m, error)) return false; materials.materials.push_back(m); }
    for (const auto& value : *json["stocks"].getArray()) { CadSheetStock s; if (!stock::fromJson(value, s, error)) return false; stocks.stocks.push_back(s); }
    return matl::validateLibrary(materials, error) && stock::validateLibrary(stocks, materials, error);
}
bool writeAtomic(const juce::File& destination, const juce::String& text, juce::String& error)
{
    if (!destination.getParentDirectory().createDirectory().wasOk()) { error = "Cannot create catalog directory: " + destination.getParentDirectory().getFullPathName(); return false; }
    juce::TemporaryFile temp(destination);
    if (!temp.getFile().replaceWithText(text) || !temp.overwriteTargetFileWithTemporary()) { error = "Cannot save catalog: " + destination.getFullPathName(); return false; }
    return true;
}
}
juce::File CadEngineeringCatalog::defaultFile()
{
    // Explicit override supports isolated interactive checks and portable shop catalog locations.
    const auto arguments = juce::JUCEApplication::getCommandLineParameterArray();
    const auto index = arguments.indexOf("--engineering-catalog");
    if (index >= 0 && index + 1 < arguments.size() && juce::File::isAbsolutePath(arguments[index + 1])) return juce::File(arguments[index + 1]);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DjehutiCAD").getChildFile("engineering-catalog.json");
}
void CadEngineeringCatalog::setFile(const juce::File& target) { file = target; loaded = false; materials = {}; stocks = {}; bundledIds.clear(); diskText.clear(); }
bool CadEngineeringCatalog::ensureLoaded(juce::String& error)
{
    if (loaded) return true;
    CadMaterials nextMaterials; CadSheetStocks nextStocks; juce::StringArray ids;
    const auto bundledJson = juce::JSON::parse(juce::String::fromUTF8(EngineeringData::engineeringreference_json, EngineeringData::engineeringreference_jsonSize));
    if (!decode(bundledJson, nextMaterials, nextStocks, error)) return false;
    for (const auto& m : nextMaterials.materials) ids.add(m.id);
    for (const auto& s : nextStocks.stocks) ids.add(s.id);
    if (file == juce::File()) file = defaultFile();
    const auto text = file.existsAsFile() ? file.loadFileAsString() : juce::String();
    if (file.existsAsFile() && !decode(juce::JSON::parse(text), nextMaterials, nextStocks, error)) { error = file.getFullPathName() + ": " + error; return false; }
    materials = std::move(nextMaterials); stocks = std::move(nextStocks); bundledIds = ids; diskText = text; loaded = true; return true;
}
bool CadEngineeringCatalog::save(const CadMaterials& nextMaterials, const CadSheetStocks& nextStocks, juce::String& error)
{
    if (!matl::validateLibrary(nextMaterials, error) || !stock::validateLibrary(nextStocks, nextMaterials, error)) return false;
    if (file.loadFileAsString() != diskText) { error = "Catalog changed outside this session. Cancel the edit, then Reload the catalog before saving."; return false; }
    const auto text = juce::JSON::toString(encode(nextMaterials, nextStocks, bundledIds));
    if (!writeAtomic(file, text, error)) return false;
    diskText = text; return true;
}
juce::var CadEngineeringCatalog::describe() const
{
    auto result = encode(materials, stocks);
    for (auto key : {"materials", "stocks"}) for (auto& value : *result[key].getArray())
    {
        value.getDynamicObject()->setProperty("bundled", bundled(value["id"].toString()));
        if (juce::String(key) == "materials") value.getDynamicObject()->setProperty("usedByStock", juce::var(stocks.usingMaterial(value["id"].toString())));
    }
    result.getDynamicObject()->setProperty("path", file.getFullPathName()); return result;
}
bool CadEngineeringCatalog::mutate(const juce::String& action, const juce::var& args, juce::String& id, juce::String& error)
{
    if (!ensureLoaded(error)) return false;
    auto nextMaterials = materials; auto nextStocks = stocks;
    const auto definition = args.getProperty("definition", juce::var(new juce::DynamicObject()));
    const bool isMaterial = action.endsWith("Material");
    id = args[isMaterial ? "material" : "stock"].toString();
    const bool creating = action.startsWith("create"), copying = action.startsWith("duplicate"), updating = action.startsWith("update"), deleting = action.startsWith("delete");
    if (!creating && !copying && !updating && !deleting) { error = "Unknown catalog action: " + action; return false; }
    if ((updating || deleting) && bundled(id)) { error = "Bundled entries are read-only. Duplicate into the user catalog to customize."; return false; }
    if (isMaterial)
    {
        const auto* old = materials.find(id); CadMaterial m;
        if (!creating && !old) { error = "Unknown catalog material: " + id; return false; }
        if (!creating && old) m = *old;
        if (deleting)
        {
            if (!stocks.usingMaterial(id).isEmpty()) { error = "Material is required by stock: " + stocks.usingMaterial(id).joinIntoString(", "); return false; }
            std::erase_if(nextMaterials.materials, [&](const auto& item) { return item.id == id; });
        }
        else
        {
            if (copying) { m.name += " copy"; m.catalogId = old->id; m.catalogRevision = old->revision; }
            if (!matl::readArgs(definition, m, error)) return false;
            m.id = updating ? id : fresh("mat-"); m.revision = updating ? old->revision + 1 : 1;
            if (!matl::validateMaterial(m, error)) return false;
            if (updating) std::erase_if(nextMaterials.materials, [&](const auto& item) { return item.id == id; });
            id = m.id; nextMaterials.materials.push_back(m);
        }
    }
    else if (action.endsWith("Stock"))
    {
        const auto* old = stocks.find(id); CadSheetStock s;
        if (!creating && !old) { error = "Unknown catalog stock: " + id; return false; }
        if (!creating && old) s = *old;
        if (deleting) std::erase_if(nextStocks.stocks, [&](const auto& item) { return item.id == id; });
        else
        {
            if (copying) { s.name += " copy"; s.catalogId = old->id; s.catalogRevision = old->revision; }
            if (!stock::readArgs(definition, s, error)) return false;
            s.id = updating ? id : fresh("stock-"); s.revision = updating ? old->revision + 1 : 1;
            if (!stock::validate(s, error)) return false;
            if (updating) std::erase_if(nextStocks.stocks, [&](const auto& item) { return item.id == id; });
            id = s.id; nextStocks.stocks.push_back(s);
        }
    }
    else { error = "Unknown catalog action: " + action; return false; }
    if (!save(nextMaterials, nextStocks, error)) return false;
    materials = std::move(nextMaterials); stocks = std::move(nextStocks); return true;
}
bool CadEngineeringCatalog::exportFile(const juce::File& target, juce::String& error) const
{
    if (target == file) { error = "Choose a different file for portable export."; return false; }
    return writeAtomic(target, juce::JSON::toString(encode(materials, stocks)), error);
}
bool CadEngineeringCatalog::importFile(const juce::File& target, juce::String& error)
{
    CadMaterials incoming; CadSheetStocks incomingStocks;
    if (!target.existsAsFile() || !decode(juce::JSON::parse(target.loadFileAsString()), incoming, incomingStocks, error)) { if (error.isEmpty()) error = "Catalog file is unreadable."; return false; }
    auto nextMaterials = materials; auto nextStocks = stocks; std::map<juce::String, juce::String> mapping;
    for (auto m : incoming.materials)
    {
        const auto sourceId = m.id;
        if (const auto* old = nextMaterials.find(m.id))
        {
            if (juce::JSON::toString(matl::materialToJson(*old)) == juce::JSON::toString(matl::materialToJson(m))) { mapping[sourceId] = m.id; continue; }
            m.catalogId = m.id; m.catalogRevision = m.revision; m.id = fresh("mat-"); m.revision = 1;
        }
        mapping[sourceId] = m.id; nextMaterials.materials.push_back(m);
    }
    for (auto s : incomingStocks.stocks)
    {
        s.materialId = mapping.at(s.materialId);
        if (const auto* old = nextStocks.find(s.id))
        {
            if (juce::JSON::toString(stock::toJson(*old)) == juce::JSON::toString(stock::toJson(s))) continue;
            s.catalogId = s.id; s.catalogRevision = s.revision; s.id = fresh("stock-"); s.revision = 1;
        }
        nextStocks.stocks.push_back(s);
    }
    if (!save(nextMaterials, nextStocks, error)) return false;
    materials = std::move(nextMaterials); stocks = std::move(nextStocks); return true;
}
juce::var CadCommandService::catalogCommand(const juce::var& args)
{
    const auto action = args.getProperty("action", "list").toString();
    if (action == "reload") engineeringCatalog.reload();
    auto* reply = new juce::DynamicObject(); juce::String error, id; bool ok = engineeringCatalog.ensureLoaded(error);
    if (ok)
    {
        if (action == "list" || action == "reload") reply->setProperty("catalog", engineeringCatalog.describe());
        else if (action == "importFile" || action == "exportFile")
        {
            const auto path = args["path"].toString();
            if (!juce::File::isAbsolutePath(path)) { ok = false; error = "Choose an absolute catalog file path."; }
            else if (action == "exportFile" && juce::File(path).existsAsFile() && !(bool) args.getProperty("overwrite", false)) { ok = false; error = "Catalog destination exists. Use overwrite=true to replace it."; }
            else ok = action == "importFile" ? engineeringCatalog.importFile(juce::File(path), error) : engineeringCatalog.exportFile(juce::File(path), error);
        }
        else if (action == "useMaterial")
        {
            const auto* m = engineeringCatalog.materialsData().find(args["material"].toString());
            if (!m) { ok = false; error = "Unknown catalog material."; }
            else ok = model.importMaterial(*m, args["feature"].toString(), id, error);
        }
        else if (action == "useStock")
        {
            const auto* s = engineeringCatalog.stocksData().find(args["stock"].toString());
            const auto* m = s ? engineeringCatalog.materialsData().find(s->materialId) : nullptr;
            if (!s || !m) { ok = false; error = "Unknown stock or missing catalog material."; }
            else ok = model.importStock(*s, *m, (bool) args.getProperty("setDefault", true), id, error);
        }
        else ok = engineeringCatalog.mutate(action, args, id, error);
    }
    reply->setProperty("ok", ok); if (!ok) reply->setProperty("error", error); if (ok && id.isNotEmpty()) reply->setProperty("id", id); return reply;
}
}
