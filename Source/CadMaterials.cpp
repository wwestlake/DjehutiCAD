#include "CadMaterials.h"
#include "CadDocument.h"

#include <cmath>
#include <set>

namespace djehuti::cad
{
const CadMaterial* CadMaterials::find(const juce::String& id) const
{
    for (const auto& m : materials) if (m.id == id) return &m;
    return nullptr;
}

const CadMaterialAssignment* CadMaterials::assignmentFor(const juce::String& featureId) const
{
    for (const auto& a : assignments) if (a.featureId == featureId) return &a;
    return nullptr;
}

juce::StringArray CadMaterials::featuresUsing(const juce::String& materialId) const
{
    juce::StringArray ids;
    for (const auto& a : assignments) if (a.materialId == materialId) ids.add(a.featureId);
    return ids;
}

namespace matl
{
namespace
{
struct PropertySlot { const char* key; const char* unit; std::optional<double> CadMaterial::* member; };
const PropertySlot slots[] {
    { "density", "kg/m^3", &CadMaterial::densityKgPerM3 },
    { "youngsModulus", "MPa", &CadMaterial::youngsModulusMPa },
    { "poissonsRatio", "1", &CadMaterial::poissonsRatio },
    { "yieldStrength", "MPa", &CadMaterial::yieldStrengthMPa },
};
bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }
}

juce::StringArray knownModels() { return { "isotropic" }; }

juce::String unitOf(const juce::String& propertyKey)
{
    for (const auto& s : slots) if (propertyKey == s.key) return s.unit;
    return {};
}

bool validateMaterial(const CadMaterial& m, juce::String& error)
{
    const auto label = m.name.isNotEmpty() ? m.name : (m.id.isNotEmpty() ? m.id : juce::String("Material"));
    if (m.name.trim().isEmpty()) { error = "A material needs a name."; return false; }
    if (!knownModels().contains(m.model)) { error = label + ": unknown material model \"" + m.model + "\" (supported: " + knownModels().joinIntoString(", ") + ")."; return false; }
    for (const auto& s : slots)
        if (const auto& value = m.*(s.member); value.has_value() && !std::isfinite(*value)) { error = label + ": " + juce::String(s.key) + " must be a finite number."; return false; }
    if (m.model == "isotropic")
    {
        if (!m.densityKgPerM3) { error = label + ": an isotropic material needs a density (kg/m^3)."; return false; }
        if (!m.youngsModulusMPa) { error = label + ": an isotropic material needs a Young's modulus (MPa)."; return false; }
        if (!m.poissonsRatio) { error = label + ": an isotropic material needs a Poisson's ratio."; return false; }
    }
    if (m.densityKgPerM3 && *m.densityKgPerM3 <= 0.0) { error = label + ": density must be greater than 0 kg/m^3."; return false; }
    if (m.youngsModulusMPa && *m.youngsModulusMPa <= 0.0) { error = label + ": Young's modulus must be greater than 0 MPa."; return false; }
    if (m.poissonsRatio && !(*m.poissonsRatio > -1.0 && *m.poissonsRatio < 0.5)) { error = label + ": Poisson's ratio must lie between -1 and 0.5 (exclusive)."; return false; }
    if (m.yieldStrengthMPa && *m.yieldStrengthMPa <= 0.0) { error = label + ": yield strength must be greater than 0 MPa when specified."; return false; }
    if (m.revision < 1) { error = label + ": revision must be 1 or greater."; return false; }
    return true;
}

bool validateLibrary(const CadMaterials& library, juce::String& error)
{
    std::set<juce::String> ids, features;
    for (const auto& m : library.materials)
    {
        if (!m.id.startsWith("mat-") || m.id.length() < 5) { error = "Material id \"" + m.id + "\" is not a valid material id."; return false; }
        if (!ids.insert(m.id).second) { error = "Material id " + m.id + " is used twice."; return false; }
        if (!validateMaterial(m, error)) return false;
    }
    for (const auto& a : library.assignments)
    {
        if (a.featureId.isEmpty()) { error = "A material assignment has no part."; return false; }
        if (library.find(a.materialId) == nullptr) { error = "Part " + a.featureId + " refers to missing material " + a.materialId + "."; return false; }
        if (!features.insert(a.featureId).second) { error = "Part " + a.featureId + " has more than one material assignment."; return false; }
    }
    if (library.nextNumber < 1) { error = "Material numbering is invalid."; return false; }
    return true;
}

juce::var materialToJson(const CadMaterial& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", m.id); o->setProperty("name", m.name); o->setProperty("revision", m.revision); o->setProperty("model", m.model);
    auto* properties = new juce::DynamicObject();
    for (const auto& s : slots)
        if (const auto& value = m.*(s.member); value.has_value())
        {
            auto* p = new juce::DynamicObject(); p->setProperty("value", *value); p->setProperty("unit", s.unit);
            properties->setProperty(s.key, juce::var(p));
        }
    o->setProperty("properties", juce::var(properties));
    if (m.note.isNotEmpty()) o->setProperty("note", m.note);
    return juce::var(o);
}

bool materialFromJson(const juce::var& json, CadMaterial& m, juce::String& error)
{
    auto* o = json.getDynamicObject();
    if (o == nullptr) { error = "A material entry is not an object."; return false; }
    m = {};
    m.id = o->getProperty("id").toString(); m.name = o->getProperty("name").toString();
    m.revision = (int) o->getProperty("revision"); m.model = o->getProperty("model").toString(); m.note = o->getProperty("note").toString();
    if (auto* properties = o->getProperty("properties").getDynamicObject())
        for (const auto& entry : properties->getProperties())
        {
            const auto key = entry.name.toString();
            const PropertySlot* slot = nullptr;
            for (const auto& s : slots) if (key == s.key) slot = &s;
            if (slot == nullptr) { error = m.id + ": unknown material property \"" + key + "\"."; return false; }
            const auto value = entry.value.getProperty("value", {}), unit = entry.value.getProperty("unit", {});
            if (!isNumber(value)) { error = m.id + ": " + key + " has no numeric value."; return false; }
            if (unit.toString() != slot->unit) { error = m.id + ": " + key + " must be stored in " + slot->unit + ", not \"" + unit.toString() + "\"."; return false; }
            m.*(slot->member) = (double) value;
        }
    return true;
}

juce::var toJson(const CadMaterials& library)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", CadMaterials::kSchemaVersion);
    root->setProperty("nextNumber", library.nextNumber);
    juce::Array<juce::var> materials, assignments;
    for (const auto& m : library.materials) materials.add(materialToJson(m));
    for (const auto& a : library.assignments) { auto* o = new juce::DynamicObject(); o->setProperty("feature", a.featureId); o->setProperty("material", a.materialId); assignments.add(juce::var(o)); }
    root->setProperty("materials", materials);
    root->setProperty("assignments", assignments);
    return juce::var(root);
}

bool fromJson(const juce::var& json, CadMaterials& library, juce::String& error)
{
    if (!json.isObject()) { error = "Stored material data is not valid JSON."; return false; }
    const int schema = (int) json.getProperty("schemaVersion", 0);
    if (schema < 1) { error = "Stored material data has no schema version."; return false; }
    if (schema > CadMaterials::kSchemaVersion) { error = "Material data uses schema " + juce::String(schema) + "; this DjehutiCAD reads up to schema " + juce::String(CadMaterials::kSchemaVersion) + "."; return false; }
    CadMaterials loaded;
    loaded.nextNumber = (int) json.getProperty("nextNumber", 1);
    if (auto* list = json.getProperty("materials", {}).getArray())
        for (const auto& item : *list) { CadMaterial m; if (!materialFromJson(item, m, error)) return false; loaded.materials.push_back(m); }
    if (auto* list = json.getProperty("assignments", {}).getArray())
        for (const auto& item : *list) loaded.assignments.push_back({ item.getProperty("feature", "").toString(), item.getProperty("material", "").toString() });
    if (!validateLibrary(loaded, error)) return false;
    library = std::move(loaded);
    return true;
}
}

// ---------------------------------------------------------------------------------------------------------------
// Command/API layer: one entry point shared by the agent tools and the local automation API.
namespace
{
juce::var assignmentJson(const CadDocument& document, const CadMaterialAssignment& a)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("feature", a.featureId);
    if (const auto* f = document.find(a.featureId)) o->setProperty("featureName", f->name);
    o->setProperty("material", a.materialId);
    if (const auto* m = document.materialsData().find(a.materialId)) o->setProperty("materialName", m->name);
    return juce::var(o);
}

// Reads the flat API fields (explicit units in the key names) into a material. Missing keys leave the current
// value; keys listed in "clear" become unspecified.
bool readMaterialArgs(const juce::var& args, CadMaterial& m, juce::String& error)
{
    struct Field { const char* key; std::optional<double> CadMaterial::* member; const char* property; };
    const Field fields[] { { "densityKgPerM3", &CadMaterial::densityKgPerM3, "density" }, { "youngsModulusMPa", &CadMaterial::youngsModulusMPa, "youngsModulus" },
                           { "poissonsRatio", &CadMaterial::poissonsRatio, "poissonsRatio" }, { "yieldStrengthMPa", &CadMaterial::yieldStrengthMPa, "yieldStrength" } };
    if (args.hasProperty("name")) m.name = args.getProperty("name", "").toString().trim();
    if (args.hasProperty("model")) m.model = args.getProperty("model", "").toString();
    if (args.hasProperty("note")) m.note = args.getProperty("note", "").toString();
    for (const auto& f : fields)
        if (args.hasProperty(f.key))
        {
            const auto v = args.getProperty(f.key, {});
            if (!(v.isInt() || v.isInt64() || v.isDouble())) { error = juce::String(f.key) + " must be a number."; return false; }
            m.*(f.member) = (double) v;
        }
    if (auto* clear = args.getProperty("clear", {}).getArray())
        for (const auto& item : *clear)
        {
            bool known = false;
            for (const auto& f : fields) if (item.toString() == f.property || item.toString() == f.key) { (m.*(f.member)).reset(); known = true; }
            if (!known) { error = "Cannot clear unknown property \"" + item.toString() + "\"."; return false; }
        }
    return true;
}
}

juce::var CadCommandService::materialCommand(const juce::String& tool, const juce::var& args)
{
    auto* reply = new juce::DynamicObject();
    juce::var result(reply);
    juce::String error, id;
    bool ok = true;
    const auto& library = model.materialsData();
    if (tool == "cad_material_list")
    {
        const auto materialId = args.getProperty("material", "").toString(), featureId = args.getProperty("feature", "").toString();
        if (materialId.isNotEmpty())
        {
            if (const auto* m = library.find(materialId)) { reply->setProperty("material", matl::materialToJson(*m)); reply->setProperty("usedBy", juce::var(library.featuresUsing(materialId))); }
            else { ok = false; error = "Unknown material: " + materialId; }
        }
        else if (featureId.isNotEmpty())
        {
            if (model.find(featureId) == nullptr) { ok = false; error = "Unknown part: " + featureId; }
            else if (const auto* a = library.assignmentFor(featureId)) reply->setProperty("assignment", assignmentJson(model, *a));
            else reply->setProperty("assignment", juce::var());   // null: no material assigned
        }
        else
        {
            juce::Array<juce::var> materials, assignments;
            for (const auto& m : library.materials) materials.add(matl::materialToJson(m));
            for (const auto& a : library.assignments) assignments.add(assignmentJson(model, a));
            reply->setProperty("schemaVersion", CadMaterials::kSchemaVersion);
            reply->setProperty("materials", materials); reply->setProperty("assignments", assignments);
        }
    }
    else if (tool == "cad_material_create")
    {
        CadMaterial m;
        ok = readMaterialArgs(args, m, error) && model.createMaterial(m, id, error);
        if (ok) reply->setProperty("material", matl::materialToJson(*model.materialsData().find(id)));
    }
    else if (tool == "cad_material_update")
    {
        const auto materialId = args.getProperty("material", "").toString();
        const auto* existing = library.find(materialId);
        if (existing == nullptr) { ok = false; error = "Unknown material: " + materialId; }
        else
        {
            CadMaterial m = *existing;
            ok = readMaterialArgs(args, m, error) && model.updateMaterial(materialId, m, error);
            if (ok) reply->setProperty("material", matl::materialToJson(*model.materialsData().find(materialId)));
        }
    }
    else if (tool == "cad_material_delete") ok = model.removeMaterial(args.getProperty("material", "").toString(), static_cast<bool>(args.getProperty("clearAssignments", false)), error);
    else if (tool == "cad_material_assign") ok = model.assignMaterial(args.getProperty("feature", "").toString(), args.getProperty("material", "").toString(), error);
    else if (tool == "cad_material_clear") ok = model.clearMaterialAssignment(args.getProperty("feature", "").toString(), error);
    else { ok = false; error = "Unknown material tool: " + tool; }
    reply->setProperty("ok", ok);
    if (!ok) reply->setProperty("error", error);
    if (id.isNotEmpty()) reply->setProperty("id", id);
    return result;
}
}
