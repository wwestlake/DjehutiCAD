#pragma once

#include <JuceHeader.h>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <functional>
#include <vector>

namespace djehuti::cad
{
class CadDocument;

// Export of evaluated CAD solids: 3MF (lib3mf), binary STL, STEP (OCCT STEPCAFControl), OBJ + MTL, binary PLY and
// glTF 2.0 (.gltf with embedded buffer, or .glb). Export is a read operation: plan() copies the chosen B-rep geometry on
// the message thread without touching the document, and run() meshes, checks and writes those copies on any thread.
enum class CadExportFormat { threeMF, stl, step, glb, obj, ply, gltf };

// Length unit written to the file. glTF is always metres and 3MF/STEP always declare the unit they were given.
enum class CadExportUnit { millimetre, centimetre, metre, inch };

// Printing and real-time mesh tolerances. chordalMm is the largest distance between the mesh and the true surface;
// angularDeg is the largest angle between neighbouring facet normals along a curve.
struct CadMeshQuality
{
    juce::String preset = "standard";   // "standard", "fine", "low", "medium", "high" or "custom"
    double chordalMm = 0.05, angularDeg = 15.0;
};

struct CadExportSettings
{
    CadExportFormat format = CadExportFormat::threeMF;
    juce::StringArray partIds;   // feature or instance ids; empty = every visible part of the modeled assembly
    CadMeshQuality quality;
    juce::File output;           // STL with several parts: one file per part beside it, "<name>-<part>.stl"
    bool overwrite = false;
    CadExportUnit units = CadExportUnit::millimetre;   // ignored by glTF/GLB (always metres)
    bool yUp = false;            // Y-up instead of CAD Z-up for STL, OBJ and PLY; glTF/GLB are always Y-up; 3MF and STEP always Z-up
};

// One exported part: a copy of a source body and the placement applied to it exactly once.
struct CadExportPart
{
    juce::String id, name, sourceId;
    juce::String materialName;    // document material assigned to the source body; empty when none
    bool instance = false;
    gp_Trsf placement;
    double min[3] {}, max[3] {};   // placed bounds, mm
    // Hierarchical assembly & pivot representation (especially for glTF / game-ready export)
    bool hasPivot = false;
    gp_Trsf pivotFrame;           // world frame of pivot/joint (mm)
    juce::String parentPartId;    // empty if parent is root / world
};

struct CadExportSource
{
    juce::String id, name;
    TopoDS_Shape shape;   // independent copy (geometry included); never shared with the live document
};

struct CadExportPlan
{
    CadExportSettings settings;
    juce::String scope, assemblyName;   // human-readable scope, and the STEP/3MF assembly name
    std::vector<CadExportSource> sources;
    std::vector<CadExportPart> parts;
    double min[3] {}, max[3] {};
    juce::Array<juce::File> outputs;   // destination per part for STL, else one file
};

struct CadExportPartReport
{
    juce::String id, name, material;
    int solids = 0, triangles = 0, vertices = 0;
    double size[3] {}, solidVolume = 0.0, meshVolume = 0.0;
    juce::StringArray warnings;
};

struct CadExportResult
{
    bool ok = false, cancelled = false;
    juce::String error;
    juce::StringArray files, warnings;
    std::vector<CadExportPartReport> parts;
    double size[3] {};
    juce::var toVar(const CadExportPlan& plan) const;
};

namespace exporting
{
juce::String formatName(CadExportFormat format);    // "3MF", "Binary STL", "STEP AP214"
juce::String formatKey(CadExportFormat format);     // "3mf", "stl", "step"
juce::String extension(CadExportFormat format);     // ".3mf", ".stl", ".step", ".glb", ".obj", ".ply", ".gltf"
bool parseFormat(const juce::String& text, CadExportFormat& format);
juce::String unitKey(CadExportUnit unit);           // "mm", "cm", "m", "in"
juce::String unitName(CadExportUnit unit);          // "millimetres" ...
bool parseUnit(const juce::String& text, CadExportUnit& unit);
double unitsPerMillimetre(CadExportUnit unit);
// What the file really contains once format rules are applied (glTF: metres and Y-up; 3MF/STEP: Z-up).
CadExportUnit effectiveUnits(const CadExportSettings& settings);
bool effectiveYUp(const CadExportSettings& settings);
bool qualityPreset(const juce::String& name, CadMeshQuality& quality, juce::String& error);
bool validateQuality(const CadMeshQuality& quality, juce::String& error);
// Arguments shared by the agent tool, FRust and nodes:
// { format, path, parts: [ids] or "a,b", quality: low|standard|fine|custom, chordal_mm, angular_deg, units: mm|cm|m|in,
//   up_axis: "z"|"y", overwrite }
bool settingsFromJson(const juce::var& args, CadExportSettings& settings, juce::String& error);
// Message thread. Resolves the parts, copies their geometry and computes placed bounds and destinations.
// With no output file it only previews the parts and bounds (outputs stays empty).
bool plan(const CadDocument& document, const CadExportSettings& settings, CadExportPlan& plan, juce::String& error);
// Any thread. progress(fraction, stage) returns false to cancel. Writes through temporary files.
CadExportResult run(const CadExportPlan& plan, const std::function<bool(double, const juce::String&)>& progress = {});
}
}
