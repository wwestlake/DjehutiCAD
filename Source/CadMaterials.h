#pragma once

#include <JuceHeader.h>

#include <optional>
#include <vector>

// Physical engineering materials owned by the CAD document, and their assignment to parts.
//
// Physical materials are separate from appearance: glTF/3MF colours are unrelated and unaffected. Nothing here
// computes mass, stiffness or stress; later services read these definitions together with the geometric
// properties (CadGeometricProperties.h).
//
// Units are part of the contract and stored with every value: density kg/m^3, Young's modulus MPa, Poisson's ratio
// dimensionless ("1"), yield strength MPa. An absent property is "unspecified", which is different from zero.
//
// Constitutive models: "densityOnly" supports mass without elastic assumptions; "isotropic" requires E and nu.
// "referenceOnly" retains incomplete published properties for selection/provenance but is not a constitutive model.
// The model name and the keyed
// property map leave room for direction-dependent models (e.g. orthotropic properties of 3-D printed plastics)
// as new model names in a later schema, without changing how isotropic materials are stored.
namespace djehuti::cad
{
struct CadMaterial
{
    juce::String id;                          // stable: "mat-<n>", never reused within a document
    juce::String name;
    int revision = 1;                         // advances on every edit of this definition; the id never changes
    juce::String model = "isotropic"; // "densityOnly" supports mass, never elastic analysis.
    std::optional<double> densityKgPerM3;     // required for "isotropic"
    std::optional<double> youngsModulusMPa;   // required for "isotropic"
    std::optional<double> poissonsRatio;      // required for "isotropic"
    std::optional<double> yieldStrengthMPa;   // optional
    std::optional<double> shearModulusMPa;        // optional (derived from E and nu if absent for isotropic)
    std::optional<double> tensileStrengthMPa;     // optional
    std::optional<double> compressiveStrengthMPa; // optional
    std::optional<double> shearStrengthMPa;       // optional
    juce::String note;                        // free text, e.g. source of the values
    juce::String family, specification, condition, source;
    // Origin of a document snapshot (or a customized catalog entry); never a live external link.
    juce::String catalogId;
    int catalogRevision = 0;
};

// One material per part. A part is a document feature (body) identified by its persistent feature id, never by
// an OCCT shape or topology index, so the assignment survives parametric rebuilds. All assembly instances of the
// body share it.
struct CadMaterialAssignment
{
    juce::String featureId, materialId;
};

struct CadMaterials
{
    static constexpr int kSchemaVersion = 3;
    int nextNumber = 1;
    std::vector<CadMaterial> materials;
    std::vector<CadMaterialAssignment> assignments;

    const CadMaterial* find(const juce::String& id) const;
    const CadMaterialAssignment* assignmentFor(const juce::String& featureId) const;
    juce::StringArray featuresUsing(const juce::String& materialId) const;
    bool empty() const { return materials.empty() && assignments.empty(); }
};

namespace matl
{
// Models this build understands, and the canonical unit of each property key.
juce::StringArray knownModels();
juce::String unitOf(const juce::String& propertyKey);   // "density" -> "kg/m^3", ...; empty if unknown
// Values: finite and physically admissible where specified (density > 0, E > 0, -1 < nu < 0.5, yield > 0);
// the model's required properties present; a non-empty name.
bool validateMaterial(const CadMaterial& material, juce::String& error);
// Structure only: unique ids, every assignment names an existing material, at most one per feature.
bool validateLibrary(const CadMaterials& library, juce::String& error);
juce::var materialToJson(const CadMaterial& material);
bool materialFromJson(const juce::var& json, CadMaterial& material, juce::String& error);
bool readArgs(const juce::var& args, CadMaterial& material, juce::String& error);
juce::var toJson(const CadMaterials& library);
bool fromJson(const juce::var& json, CadMaterials& library, juce::String& error);
}
}
