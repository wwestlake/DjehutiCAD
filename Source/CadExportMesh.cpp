#include "CadExportMesh.h"

#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

namespace djehuti::cad::exporting
{
gp_Trsf OutputFrame::rotation() const
{
    gp_Trsf t;
    if (yUp) t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), -juce::MathConstants<double>::halfPi);   // y' = z, z' = -y
    return t;
}

juce::String materialLabel(const CadExportPart& part) { return part.materialName.isNotEmpty() ? part.materialName : juce::String("Default"); }

bool extractShadedMesh(const TopoDS_Shape& shape, ShadedMesh& mesh, juce::String& error)
{
    int faceNumber = 0;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next())
    {
        ++faceNumber;
        const auto face = TopoDS::Face(ex.Current());
        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull() || triangulation->NbTriangles() == 0) { error = "face " + juce::String(faceNumber) + " could not be meshed"; return false; }
        if (!triangulation->HasNormals()) BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);

        const auto& transform = location.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const auto base = static_cast<std::uint32_t>(mesh.points.size());
        for (int i = 1; i <= triangulation->NbNodes(); ++i)
        {
            const auto p = triangulation->Node(i).Transformed(transform);
            if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z())) { error = "the mesh has a non-finite coordinate on face " + juce::String(faceNumber); return false; }
            gp_Vec n(0, 0, 1);
            if (triangulation->HasNormals())
            {
                gp_Dir d = triangulation->Normal(i);
                if (reversed) d.Reverse();
                n = gp_Vec(d).Transformed(transform);
                if (n.Magnitude() > 1.0e-9) n.Normalize();
            }
            mesh.points.push_back(p);
            mesh.normals.push_back(n);
        }
        for (int t = 1; t <= triangulation->NbTriangles(); ++t)
        {
            int a = 0, b = 0, c = 0;
            triangulation->Triangle(t).Get(a, b, c);
            if (reversed) std::swap(b, c);
            if (a == b || b == c || a == c) continue;   // collapsed: no area
            mesh.triangles.push_back({ base + (std::uint32_t) a - 1, base + (std::uint32_t) b - 1, base + (std::uint32_t) c - 1 });
        }
    }
    return true;
}

namespace
{
const ShadedMesh* meshFor(const MeshSet& meshes, const juce::String& id)
{
    const auto it = meshes.find(id);
    return it == meshes.end() ? nullptr : &it->second;
}

// Unique material labels in first-use order.
std::vector<juce::String> materialLabels(const CadExportPlan& plan)
{
    std::vector<juce::String> labels;
    for (const auto& part : plan.parts)
    {
        const auto label = materialLabel(part);
        bool known = false;
        for (const auto& l : labels) known = known || l == label;
        if (!known) labels.push_back(label);
    }
    return labels;
}

int labelIndex(const std::vector<juce::String>& labels, const juce::String& label)
{
    for (size_t i = 0; i < labels.size(); ++i) if (labels[i] == label) return (int) i;
    return 0;
}

// MTL and OBJ names are whitespace-delimited tokens or single lines.
juce::String token(const juce::String& text)
{
    auto t = text.trim().replaceCharacters(" \t\r\n", "____");
    return t.isEmpty() ? juce::String("unnamed") : t;
}

juce::String oneLine(const juce::String& text) { return text.replaceCharacters("\r\n\t", "   ").trim(); }

bool writeText(juce::FileOutputStream& stream, const juce::String& text)
{
    return stream.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
}

double r6(double v) { return std::round(v * 1.0e6) / 1.0e6; }

juce::var object(const std::initializer_list<std::pair<const char*, juce::var>>& properties)
{
    auto* o = new juce::DynamicObject();
    for (const auto& p : properties) o->setProperty(p.first, p.second);
    return juce::var(o);
}
}

bool writeObj(const CadExportPlan& plan, const MeshSet& meshes, const OutputFrame& frame, const juce::File& objFile, const juce::File& mtlFile, const juce::String& mtlName, juce::String& error)
{
    const auto labels = materialLabels(plan);
    {
        juce::FileOutputStream mtl(mtlFile);
        if (!mtl.openedOk()) { error = "Unable to open the temporary material file."; return false; }
        mtl.setPosition(0); mtl.truncate();
        writeText(mtl, "# DjehutiCAD material library\n# DjehutiCAD stores no display colour; every material is neutral grey.\n");
        for (const auto& label : labels)
            writeText(mtl, "\nnewmtl " + token(label) + "\nKa 0.000000 0.000000 0.000000\nKd 0.800000 0.800000 0.800000\nKs 0.000000 0.000000 0.000000\nd 1.000000\nillum 1\n");
        mtl.flush();
    }
    juce::FileOutputStream obj(objFile);
    if (!obj.openedOk()) { error = "Unable to open the temporary OBJ file."; return false; }
    obj.setPosition(0); obj.truncate();
    writeText(obj, "# DjehutiCAD OBJ export\n# Units: " + unitName(frame.scale == 1.0 ? CadExportUnit::millimetre : frame.scale == 0.1 ? CadExportUnit::centimetre : frame.scale == 0.001 ? CadExportUnit::metre : CadExportUnit::inch)
                      + "\n# Up axis: " + (frame.yUp ? "+Y (right-handed, CAD Z mapped to Y)" : "+Z (CAD)") + "\n# Model: " + oneLine(plan.assemblyName) + "\nmtllib " + mtlName + "\n");
    std::uint64_t offset = 0;
    char line[160];
    for (const auto& part : plan.parts)
    {
        const auto* mesh = meshFor(meshes, part.sourceId);
        if (mesh == nullptr) { error = "No mesh was prepared for " + part.name + "."; return false; }
        writeText(obj, "o " + oneLine(part.name) + "\nusemtl " + token(materialLabel(part)) + "\n");
        for (const auto& p : mesh->points)
        {
            const auto q = frame.point(p.Transformed(part.placement));
            const int n = std::snprintf(line, sizeof line, "v %.6f %.6f %.6f\n", q.X(), q.Y(), q.Z());
            obj.write(line, (size_t) n);
        }
        for (const auto& v : mesh->normals)
        {
            const auto q = frame.direction(v.Transformed(part.placement));
            const int n = std::snprintf(line, sizeof line, "vn %.6f %.6f %.6f\n", q.X(), q.Y(), q.Z());
            obj.write(line, (size_t) n);
        }
        for (const auto& t : mesh->triangles)
        {
            const auto a = offset + t[0] + 1, b = offset + t[1] + 1, c = offset + t[2] + 1;
            const int n = std::snprintf(line, sizeof line, "f %llu//%llu %llu//%llu %llu//%llu\n", (unsigned long long) a, (unsigned long long) a, (unsigned long long) b, (unsigned long long) b, (unsigned long long) c, (unsigned long long) c);
            obj.write(line, (size_t) n);
        }
        offset += mesh->points.size();
    }
    obj.flush();
    if (obj.getStatus().failed()) { error = "Writing the OBJ file failed."; return false; }
    return true;
}

bool writePly(const CadExportPlan& plan, const MeshSet& meshes, const OutputFrame& frame, const juce::File& file, juce::String& error)
{
    std::uint64_t vertexCount = 0, faceCount = 0;
    juce::String comments;
    for (const auto& part : plan.parts)
    {
        const auto* mesh = meshFor(meshes, part.sourceId);
        if (mesh == nullptr) { error = "No mesh was prepared for " + part.name + "."; return false; }
        comments += "comment part \"" + oneLine(part.name) + "\" vertices " + juce::String((juce::int64) vertexCount) + ".." + juce::String((juce::int64) (vertexCount + mesh->points.size()) - 1)
                    + " faces " + juce::String((juce::int64) faceCount) + ".." + juce::String((juce::int64) (faceCount + mesh->triangles.size()) - 1) + "\n";
        vertexCount += mesh->points.size(); faceCount += mesh->triangles.size();
    }
    if (vertexCount > 0xFFFFFFFFull) { error = "The mesh has too many vertices for PLY."; return false; }
    juce::FileOutputStream out(file);
    if (!out.openedOk()) { error = "Unable to open the temporary PLY file."; return false; }
    out.setPosition(0); out.truncate();
    const auto unit = frame.scale == 1.0 ? "millimetres" : frame.scale == 0.1 ? "centimetres" : frame.scale == 0.001 ? "metres" : "inches";
    writeText(out, juce::String("ply\nformat binary_little_endian 1.0\ncomment DjehutiCAD PLY export\ncomment units ") + unit + "\ncomment up axis " + (frame.yUp ? "+Y" : "+Z") + "\n" + comments
                       + "element vertex " + juce::String((juce::int64) vertexCount) + "\nproperty float x\nproperty float y\nproperty float z\nproperty float nx\nproperty float ny\nproperty float nz\n"
                       + "element face " + juce::String((juce::int64) faceCount) + "\nproperty list uchar uint vertex_indices\nend_header\n");
    std::vector<char> buffer;
    for (const auto& part : plan.parts)
    {
        const auto& mesh = *meshFor(meshes, part.sourceId);
        buffer.resize(mesh.points.size() * 24);
        for (size_t v = 0; v < mesh.points.size(); ++v)
        {
            const auto p = frame.point(mesh.points[v].Transformed(part.placement));
            const auto n = frame.direction(mesh.normals[v].Transformed(part.placement));
            const float f[6] { (float) p.X(), (float) p.Y(), (float) p.Z(), (float) n.X(), (float) n.Y(), (float) n.Z() };
            std::memcpy(buffer.data() + v * 24, f, 24);
        }
        out.write(buffer.data(), buffer.size());
    }
    std::uint32_t base = 0;
    for (const auto& part : plan.parts)
    {
        const auto& mesh = *meshFor(meshes, part.sourceId);
        buffer.resize(mesh.triangles.size() * 13);
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
        {
            char* at = buffer.data() + t * 13;
            at[0] = 3;
            const std::uint32_t idx[3] { base + mesh.triangles[t][0], base + mesh.triangles[t][1], base + mesh.triangles[t][2] };
            std::memcpy(at + 1, idx, 12);
        }
        out.write(buffer.data(), buffer.size());
        base += (std::uint32_t) mesh.points.size();
    }
    out.flush();
    if (out.getStatus().failed()) { error = "Writing the PLY file failed."; return false; }
    return true;
}

bool writeGltf(const CadExportPlan& plan, const MeshSet& meshes, bool binary, const juce::File& file, juce::String& error, const std::function<bool(size_t)>& partProgress)
{
    const OutputFrame frame { 0.001, true };   // glTF: metres, +Y up
    const auto rotation = frame.rotation();
    const auto labels = materialLabels(plan);
    juce::MemoryOutputStream bin;
    juce::Array<juce::var> bufferViews, accessors, meshList, nodeList;
    const auto pad4 = [&] { while (bin.getDataSize() % 4 != 0) bin.writeByte(0); };
    const auto addView = [&](const void* data, size_t bytes, int target)
    {
        pad4();
        const auto offset = bin.getDataSize();
        bin.write(data, bytes);
        bufferViews.add(object({ { "buffer", 0 }, { "byteOffset", (juce::int64) offset }, { "byteLength", (juce::int64) bytes }, { "target", target } }));
        return bufferViews.size() - 1;
    };
    const auto part_index = [&](const juce::String& id) { for (size_t i = 0; i < plan.parts.size(); ++i) if (plan.parts[i].id == id) return (int) i; return -1; };

    std::vector<std::vector<int>> children(plan.parts.size());
    std::vector<int> roots;
    for (size_t i = 0; i < plan.parts.size(); ++i)
    {
        const int parent = plan.parts[i].parentPartId.isNotEmpty() ? part_index(plan.parts[i].parentPartId) : -1;
        if (parent >= 0 && parent != (int) i) children[(size_t) parent].push_back((int) i); else roots.push_back((int) i);
    }

    for (size_t i = 0; i < plan.parts.size(); ++i)
    {
        if (partProgress && !partProgress(i)) { error.clear(); return false; }
        const auto& part = plan.parts[i];
        const auto* mesh = meshFor(meshes, part.sourceId);
        if (mesh == nullptr) { error = "No mesh was prepared for " + part.name + "."; return false; }

        // With a joint pivot the vertices are expressed in the pivot frame and the node carries the pivot; otherwise the node carries the placement.
        const gp_Trsf meshRelative = part.hasPivot ? part.pivotFrame.Inverted().Multiplied(part.placement) : gp_Trsf();
        const gp_Trsf nodeWorld = part.hasPivot ? part.pivotFrame : part.placement;
        gp_Trsf nodeLocal = nodeWorld;
        if (part.parentPartId.isNotEmpty())
            if (const int parent = part_index(part.parentPartId); parent >= 0)
            {
                const auto& pp = plan.parts[(size_t) parent];
                nodeLocal = (pp.hasPivot ? pp.pivotFrame : pp.placement).Inverted().Multiplied(nodeWorld);
            }

        std::vector<float> positions, normals;
        positions.reserve(mesh->points.size() * 3); normals.reserve(mesh->points.size() * 3);
        float lo[3] { 1e30f, 1e30f, 1e30f }, hi[3] { -1e30f, -1e30f, -1e30f };
        for (size_t v = 0; v < mesh->points.size(); ++v)
        {
            const auto p = frame.point(mesh->points[v].Transformed(meshRelative));
            const float c[3] { (float) p.X(), (float) p.Y(), (float) p.Z() };
            for (int k = 0; k < 3; ++k) { positions.push_back(c[k]); lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
            const auto n = frame.direction(mesh->normals[v].Transformed(meshRelative));
            normals.push_back((float) n.X()); normals.push_back((float) n.Y()); normals.push_back((float) n.Z());
        }
        const int posView = addView(positions.data(), positions.size() * sizeof(float), 34962);
        accessors.add(object({ { "bufferView", posView }, { "componentType", 5126 }, { "count", (juce::int64) mesh->points.size() }, { "type", "VEC3" },
                               { "min", juce::Array<juce::var> { r6(lo[0]), r6(lo[1]), r6(lo[2]) } }, { "max", juce::Array<juce::var> { r6(hi[0]), r6(hi[1]), r6(hi[2]) } } }));
        const int posAccessor = accessors.size() - 1;
        const int normView = addView(normals.data(), normals.size() * sizeof(float), 34962);
        accessors.add(object({ { "bufferView", normView }, { "componentType", 5126 }, { "count", (juce::int64) mesh->points.size() }, { "type", "VEC3" } }));
        const int normAccessor = accessors.size() - 1;

        const bool narrow = mesh->points.size() < 65536;
        std::uint32_t minIndex = 0xFFFFFFFFu, maxIndex = 0;
        int indexView = 0;
        if (narrow)
        {
            std::vector<std::uint16_t> idx; idx.reserve(mesh->triangles.size() * 3);
            for (const auto& t : mesh->triangles) for (int k = 0; k < 3; ++k) { idx.push_back((std::uint16_t) t[(size_t) k]); minIndex = std::min(minIndex, t[(size_t) k]); maxIndex = std::max(maxIndex, t[(size_t) k]); }
            indexView = addView(idx.data(), idx.size() * 2, 34963);
        }
        else
        {
            std::vector<std::uint32_t> idx; idx.reserve(mesh->triangles.size() * 3);
            for (const auto& t : mesh->triangles) for (int k = 0; k < 3; ++k) { idx.push_back(t[(size_t) k]); minIndex = std::min(minIndex, t[(size_t) k]); maxIndex = std::max(maxIndex, t[(size_t) k]); }
            indexView = addView(idx.data(), idx.size() * 4, 34963);
        }
        accessors.add(object({ { "bufferView", indexView }, { "componentType", narrow ? 5123 : 5125 }, { "count", (juce::int64) mesh->triangles.size() * 3 }, { "type", "SCALAR" },
                               { "min", juce::Array<juce::var> { (juce::int64) minIndex } }, { "max", juce::Array<juce::var> { (juce::int64) maxIndex } } }));
        const int indexAccessor = accessors.size() - 1;

        meshList.add(object({ { "name", part.name },
                              { "primitives", juce::Array<juce::var> { object({ { "attributes", object({ { "POSITION", posAccessor }, { "NORMAL", normAccessor } }) },
                                                                                  { "indices", indexAccessor }, { "material", labelIndex(labels, materialLabel(part)) }, { "mode", 4 } }) } } }));

        // Node matrix in glTF space: rotation conjugated into +Y up, translation in metres.
        const gp_Trsf local = rotation.Multiplied(nodeLocal).Multiplied(rotation.Inverted());
        juce::Array<juce::var> matrix;
        for (int col = 0; col < 3; ++col) { for (int row = 0; row < 3; ++row) matrix.add(r6(local.Value(row + 1, col + 1))); matrix.add(0.0); }
        for (int row = 0; row < 3; ++row) matrix.add(r6(local.Value(row + 1, 4) * frame.scale));
        matrix.add(1.0);
        juce::Array<juce::var> childList;
        for (int c : children[i]) childList.add(c);
        auto node = object({ { "name", part.name }, { "mesh", meshList.size() - 1 }, { "matrix", matrix },
                             { "extras", object({ { "djehutiId", part.id }, { "sourceBodyId", part.sourceId }, { "material", part.materialName } }) } });
        if (!childList.isEmpty()) node.getDynamicObject()->setProperty("children", childList);
        nodeList.add(node);
    }
    pad4();

    juce::Array<juce::var> materials;
    for (const auto& label : labels)
        materials.add(object({ { "name", label }, { "pbrMetallicRoughness", object({ { "baseColorFactor", juce::Array<juce::var> { 0.8, 0.8, 0.8, 1.0 } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 0.5 } }) } }));
    juce::Array<juce::var> rootList;
    for (int r : roots) rootList.add(r);

    auto buffer = object({ { "byteLength", (juce::int64) bin.getDataSize() } });
    if (!binary) buffer.getDynamicObject()->setProperty("uri", "data:application/octet-stream;base64," + juce::Base64::toBase64(bin.getData(), bin.getDataSize()));
    auto document = object({ { "asset", object({ { "version", "2.0" }, { "generator", "DjehutiCAD" },
                                                 { "extras", object({ { "units", "metres" }, { "authoredUnits", "millimetres" }, { "upAxis", "+Y" }, { "cadUpAxis", "+Z" }, { "model", plan.assemblyName },
                                                                      { "note", "DjehutiCAD stores no display colours; materials carry the document material name and a neutral colour." } }) } }) },
                             { "scene", 0 }, { "scenes", juce::Array<juce::var> { object({ { "name", plan.assemblyName }, { "nodes", rootList } }) } },
                             { "nodes", nodeList }, { "meshes", meshList }, { "materials", materials },
                             { "buffers", juce::Array<juce::var> { buffer } }, { "bufferViews", bufferViews }, { "accessors", accessors } });
    const auto json = juce::JSON::toString(document, binary);

    juce::FileOutputStream out(file);
    if (!out.openedOk()) { error = "Unable to open the temporary glTF file."; return false; }
    out.setPosition(0); out.truncate();
    if (!binary)
    {
        writeText(out, json);
    }
    else
    {
        juce::MemoryOutputStream jsonChunk;
        jsonChunk.write(json.toRawUTF8(), json.getNumBytesAsUTF8());
        while (jsonChunk.getDataSize() % 4 != 0) jsonChunk.writeByte(' ');
        const auto jsonLength = (std::uint32_t) jsonChunk.getDataSize(), binLength = (std::uint32_t) bin.getDataSize();
        out.writeInt((int) 0x46546C67); out.writeInt(2); out.writeInt((int) (12 + 8 + jsonLength + 8 + binLength));
        out.writeInt((int) jsonLength); out.writeInt((int) 0x4E4F534A); out.write(jsonChunk.getData(), jsonChunk.getDataSize());
        out.writeInt((int) binLength); out.writeInt((int) 0x004E4942); out.write(bin.getData(), bin.getDataSize());
    }
    out.flush();
    if (out.getStatus().failed() || file.getSize() < 20) { error = "Writing the glTF file failed."; return false; }
    return true;
}
}
