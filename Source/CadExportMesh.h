#pragma once

#include "CadExport.h"

#include <array>
#include <map>
#include <vector>

#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace djehuti::cad::exporting
{
// Triangle mesh for shaded formats (OBJ, PLY, glTF). Vertices are shared inside a B-rep face and never across faces, so
// every face keeps the analytic surface normals of its own surface and hard edges stay hard. Source-body coordinates, mm.
struct ShadedMesh
{
    std::vector<gp_Pnt> points;
    std::vector<gp_Vec> normals;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

// Reads the triangulation that BRepMesh_IncrementalMesh already attached to the (export-private) shape copy.
bool extractShadedMesh(const TopoDS_Shape& shape, ShadedMesh& mesh, juce::String& error);

// The destination coordinate system: world mm (Z up) -> length unit and up axis of the file.
struct OutputFrame
{
    double scale = 1.0;   // file units per mm
    bool yUp = false;     // +Z (CAD) becomes +Y; right-handed: (x, y, z) -> (x, z, -y)
    gp_Pnt point(const gp_Pnt& p) const { return yUp ? gp_Pnt(p.X() * scale, p.Z() * scale, -p.Y() * scale) : gp_Pnt(p.X() * scale, p.Y() * scale, p.Z() * scale); }
    gp_Vec direction(const gp_Vec& v) const { return yUp ? gp_Vec(v.X(), v.Z(), -v.Y()) : v; }
    gp_Trsf rotation() const;   // rotation part only
};

using MeshSet = std::map<juce::String, ShadedMesh>;   // by source-body id

// Each writer writes the given (temporary) file(s) and returns false with a message on failure.
// OBJ writes the geometry file and its companion material library; part placements are applied to the vertices.
bool writeObj(const CadExportPlan& plan, const MeshSet& meshes, const OutputFrame& frame, const juce::File& objFile, const juce::File& mtlFile, const juce::String& mtlName, juce::String& error);
// Binary little-endian PLY with vertex normals; all parts are merged into one mesh (PLY has no objects or hierarchy).
bool writePly(const CadExportPlan& plan, const MeshSet& meshes, const OutputFrame& frame, const juce::File& file, juce::String& error);
// glTF 2.0. binary = .glb container; otherwise a single .gltf with the buffer embedded as a base64 data URI.
// glTF is metres and +Y up by specification; assembly hierarchy and joint pivots become the node tree. The cancel hook is polled per part.
bool writeGltf(const CadExportPlan& plan, const MeshSet& meshes, bool binary, const juce::File& file, juce::String& error,
               const std::function<bool(size_t)>& partProgress);

juce::String materialLabel(const CadExportPart& part);   // document material name, or "Default" when none is assigned
}
