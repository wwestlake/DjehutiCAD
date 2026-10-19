#include "CadExport.h"
#include "CadDocument.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Quaternion.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <StepData_ConfParameters.hxx>
#include <StlAPI_Writer.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include "lib3mf_implicit.hpp"

#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <set>

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) int __stdcall MoveFileExW(const wchar_t*, const wchar_t*, unsigned long);
#endif

namespace djehuti::cad::exporting
{
namespace
{
bool hasSolid(const TopoDS_Shape& shape)
{
    return !shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More();
}

int countSolids(const TopoDS_Shape& shape)
{
    int n = 0;
    for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) ++n;
    return n;
}

void placedBounds(const TopoDS_Shape& shape, const gp_Trsf& placement, double min[3], double max[3])
{
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape.Moved(TopLoc_Location(placement)), box, false, false);
    if (box.IsVoid()) { for (int k = 0; k < 3; ++k) min[k] = max[k] = 0.0; return; }
    box.Get(min[0], min[1], min[2], max[0], max[1], max[2]);
}

// Sibling temporary file; finalize() replaces the destination atomically only after a complete write.
juce::File temporaryFor(const juce::File& destination)
{
    return destination.getSiblingFile(destination.getFileNameWithoutExtension() + ".partial-" + juce::Uuid().toString().substring(0, 8) + destination.getFileExtension());
}

bool finalize(const juce::File& temporary, const juce::File& destination)
{
#if JUCE_WINDOWS
    return MoveFileExW(temporary.getFullPathName().toWideCharPointer(), destination.getFullPathName().toWideCharPointer(), 0x1 /*REPLACE_EXISTING*/ | 0x8 /*WRITE_THROUGH*/) != 0;
#else
    return temporary.moveFileTo(destination);
#endif
}

// Lets OCCT algorithms (meshing, STEP transfer) stop when the export is cancelled.
class CancelIndicator final : public Message_ProgressIndicator
{
public:
    explicit CancelIndicator(std::function<bool()> cancelledIn) : cancelled(std::move(cancelledIn)) {}
    Standard_Boolean UserBreak() override { return cancelled(); }
    void Show(const Message_ProgressScope&, const Standard_Boolean) override {}
private:
    std::function<bool()> cancelled;
};

// Welded triangle mesh of one source body in its own coordinates, oriented outward.
struct MeshData
{
    std::vector<gp_Pnt> points;
    std::vector<gp_Vec> normals;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    int collapsed = 0, zeroArea = 0;
};

bool extractMesh(const TopoDS_Shape& shape, MeshData& mesh, juce::String& error)
{
    std::map<std::array<std::int64_t, 3>, std::uint32_t> welded;
    constexpr double quantum = 1.0e-6;   // mm; boundary nodes of neighbouring faces come from the same edge discretisation
    int faceNumber = 0;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next())
    {
        ++faceNumber;
        const auto face = TopoDS::Face(ex.Current());
        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull() || triangulation->NbTriangles() == 0) { error = "face " + juce::String(faceNumber) + " could not be meshed"; return false; }
        
        // Ensure triangulation has normals
        BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);

        const auto& transform = location.Transformation();
        std::vector<std::uint32_t> node(static_cast<size_t>(triangulation->NbNodes()) + 1);
        const bool reversed = face.Orientation() == TopAbs_REVERSED;

        for (int i = 1; i <= triangulation->NbNodes(); ++i)
        {
            const auto p = triangulation->Node(i).Transformed(transform);
            if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z())) { error = "the mesh has a non-finite coordinate on face " + juce::String(faceNumber); return false; }
            
            gp_Vec normalVec(0, 0, 1);
            if (triangulation->HasNormals())
            {
                gp_Dir normalDir = triangulation->Normal(i);
                if (reversed) normalDir.Reverse();
                normalVec = gp_Vec(normalDir).Transformed(transform);
                if (normalVec.Magnitude() > 1.0e-9) normalVec.Normalize();
            }

            const std::array<std::int64_t, 3> key { std::llround(p.X() / quantum), std::llround(p.Y() / quantum), std::llround(p.Z() / quantum) };
            const auto [it, added] = welded.emplace(key, static_cast<std::uint32_t>(mesh.points.size()));
            if (added)
            {
                mesh.points.push_back(p);
                mesh.normals.push_back(normalVec);
            }
            else
            {
                // Accumulate normal for shared vertices along edges to smooth shading
                mesh.normals[it->second] += normalVec;
            }
            node[static_cast<size_t>(i)] = it->second;
        }

        for (int t = 1; t <= triangulation->NbTriangles(); ++t)
        {
            int a = 0, b = 0, c = 0;
            triangulation->Triangle(t).Get(a, b, c);
            if (reversed) std::swap(b, c);
            const std::array<std::uint32_t, 3> tri { node[(size_t) a], node[(size_t) b], node[(size_t) c] };
            if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2]) { ++mesh.collapsed; continue; }   // two corners coincide: no area, no edges
            const gp_Vec ab(mesh.points[tri[0]], mesh.points[tri[1]]), ac(mesh.points[tri[0]], mesh.points[tri[2]]);
            if (ab.Crossed(ac).Magnitude() < 1.0e-12) ++mesh.zeroArea;
            mesh.triangles.push_back(tri);
        }
    }

    // Normalize accumulated normals
    for (auto& n : mesh.normals)
    {
        const double mag = n.Magnitude();
        if (mag > 1.0e-9) n /= mag;
        else n = gp_Vec(0, 0, 1);
    }

    return true;
}

// Closed 2-manifold with consistent orientation: every edge is used exactly twice, once in each direction.
void edgeCheck(const MeshData& mesh, int& open, int& nonManifold, int& misoriented)
{
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, int>> uses;   // count, direction balance
    for (const auto& t : mesh.triangles)
        for (int k = 0; k < 3; ++k)
        {
            const auto a = t[(size_t) k], b = t[(size_t) (k + 1) % 3];
            auto& use = uses[{ std::min(a, b), std::max(a, b) }];
            ++use.first; use.second += a < b ? 1 : -1;
        }
    open = nonManifold = misoriented = 0;
    for (const auto& [edge, use] : uses)
    {
        if (use.first == 1) ++open;
        else if (use.first > 2) ++nonManifold;
        else if (use.second != 0) ++misoriented;
    }
}

double signedVolume(const MeshData& mesh)
{
    double volume = 0.0;
    for (const auto& t : mesh.triangles)
        volume += gp_Vec(mesh.points[t[0]].XYZ()).Dot(gp_Vec(mesh.points[t[1]].XYZ()).Crossed(gp_Vec(mesh.points[t[2]].XYZ())));
    return volume / 6.0;
}

juce::String partFileName(const CadExportPart& part)
{
    return juce::File::createLegalFileName(part.name.trim().isNotEmpty() ? part.name.trim() : part.id).replaceCharacter(' ', '_');
}

juce::var vec3(const double v[3]) { return juce::Array<juce::var> { v[0], v[1], v[2] }; }

TCollection_ExtendedString extended(const juce::String& text) { return TCollection_ExtendedString(text.toRawUTF8(), Standard_True); }

std::mutex& stepMutex() { static std::mutex m; return m; }   // STEP translation uses process-wide OCCT state

Lib3MF_single toSingle(double v) { return static_cast<Lib3MF_single>(v); }
}

juce::String formatName(CadExportFormat format)
{
    return format == CadExportFormat::threeMF ? "3MF" : format == CadExportFormat::stl ? "Binary STL" : format == CadExportFormat::step ? "STEP AP214" : "glTF 2.0 Binary (GLB)";
}

juce::String formatKey(CadExportFormat format)
{
    return format == CadExportFormat::threeMF ? "3mf" : format == CadExportFormat::stl ? "stl" : format == CadExportFormat::step ? "step" : "glb";
}

juce::String extension(CadExportFormat format)
{
    return format == CadExportFormat::threeMF ? ".3mf" : format == CadExportFormat::stl ? ".stl" : format == CadExportFormat::step ? ".step" : ".glb";
}

bool parseFormat(const juce::String& text, CadExportFormat& format)
{
    const auto key = text.trim().toLowerCase().trimCharactersAtStart(".");
    if (key == "3mf") format = CadExportFormat::threeMF;
    else if (key == "stl") format = CadExportFormat::stl;
    else if (key == "step" || key == "stp") format = CadExportFormat::step;
    else if (key == "glb" || key == "gltf") format = CadExportFormat::glb;
    else return false;
    return true;
}

bool qualityPreset(const juce::String& name, CadMeshQuality& quality, juce::String& error)
{
    const auto key = name.trim().toLowerCase();
    if (key.isEmpty() || key == "standard" || key == "medium") quality = { key == "medium" ? "medium" : "standard", 0.05, 15.0 };
    else if (key == "low" || key == "coarse") quality = { "low", 0.2, 28.0 };
    else if (key == "fine" || key == "high") quality = { key == "high" ? "high" : "fine", 0.01, 5.0 };
    else if (key == "custom") quality.preset = "custom";
    else { error = "Unknown mesh quality \"" + name + "\"; use low, medium, high, standard, fine or custom."; return false; }
    return true;
}

bool validateQuality(const CadMeshQuality& quality, juce::String& error)
{
    if (!std::isfinite(quality.chordalMm) || quality.chordalMm < 0.001 || quality.chordalMm > 1.0)
    { error = "Surface (chordal) tolerance must be between 0.001 and 1 mm."; return false; }
    if (!std::isfinite(quality.angularDeg) || quality.angularDeg < 0.5 || quality.angularDeg > 45.0)
    { error = "Angular tolerance must be between 0.5 and 45 degrees."; return false; }
    return true;
}

bool settingsFromJson(const juce::var& args, CadExportSettings& settings, juce::String& error)
{
    settings = {};
    if (!parseFormat(args.getProperty("format", "").toString(), settings.format)) { error = "format must be 3mf, stl, step or glb."; return false; }
    const auto path = args.getProperty("path", "").toString().trim();
    if (path.isEmpty() || !juce::File::isAbsolutePath(path)) { error = "path must be an absolute file path."; return false; }
    settings.output = juce::File(path);
    const auto parts = args.getProperty("parts", juce::var());
    if (const auto* list = parts.getArray()) for (const auto& p : *list) settings.partIds.addIfNotAlreadyThere(p.toString().trim());
    else settings.partIds.addTokens(parts.toString(), ",", "\"");
    settings.partIds.trim(); settings.partIds.removeEmptyStrings();
    if (!qualityPreset(args.getProperty("quality", "standard").toString(), settings.quality, error)) return false;
    if (settings.quality.preset == "custom")
    {
        settings.quality.chordalMm = args.getProperty("chordal_mm", juce::var()).isVoid() ? std::nan("") : (double) args.getProperty("chordal_mm", 0.0);
        settings.quality.angularDeg = args.getProperty("angular_deg", juce::var()).isVoid() ? std::nan("") : (double) args.getProperty("angular_deg", 0.0);
    }
    settings.overwrite = (bool) args.getProperty("overwrite", false);
    return true;
}

bool plan(const CadDocument& document, const CadExportSettings& settings, CadExportPlan& out, juce::String& error)
{
    out = {};
    out.settings = settings;
    if (settings.format != CadExportFormat::step && !validateQuality(settings.quality, error)) return false;
    if (settings.output != juce::File() && !settings.output.getParentDirectory().isDirectory()) { error = "The folder " + settings.output.getParentDirectory().getFullPathName() + " does not exist."; return false; }

    const auto& mechanics = document.mechanicsData();
    std::set<juce::String> instanced;
    for (const auto& instance : mechanics.instances) if (document.find(instance.featureId) != nullptr) instanced.insert(instance.featureId);
    const auto addSource = [&](const CadFeature& feature)
    {
        for (const auto& s : out.sources) if (s.id == feature.id) return;
        BRepBuilderAPI_Copy copy(feature.shape, Standard_True, Standard_False);   // geometry too; no display mesh
        out.sources.push_back({ feature.id, feature.name, copy.Shape() });
    };
    const auto addInstance = [&](const CadMechanicalInstance& instance) -> bool
    {
        const auto* feature = document.find(instance.featureId);
        if (feature == nullptr || !hasSolid(feature->shape)) { error = "Instance " + instance.name + " has no solid body to export."; return false; }
        addSource(*feature);
        CadExportPart part;
        part.id = instance.id; part.name = instance.name; part.sourceId = feature->id; part.instance = true;
        part.placement = document.instanceShapeTransform(feature->id, instance.placement.toTrsf());   // reference configuration
        out.parts.push_back(part);
        return true;
    };
    const auto addFeature = [&](const CadFeature& feature)
    {
        addSource(feature);
        CadExportPart part;
        part.id = feature.id; part.name = feature.name; part.sourceId = feature.id;   // shape already carries its placement
        out.parts.push_back(part);
    };

    if (settings.partIds.isEmpty())
    {
        // The visible modeled assembly: visible solid bodies, with each instanced body replaced by its instances.
        for (const auto& feature : document.features())
        {
            if (!feature.visible || feature.type == "sketch" || !hasSolid(feature.shape)) continue;
            if (instanced.count(feature.id) == 0) { addFeature(feature); continue; }
            for (const auto& instance : mechanics.instances)
                if (instance.featureId == feature.id && !addInstance(instance)) return false;
        }
        if (out.parts.empty()) { error = "The model has no visible solid bodies to export."; return false; }
        out.scope = "Visible model: " + juce::String((int) out.parts.size()) + (out.parts.size() == 1 ? " part" : " parts");
    }
    else
    {
        juce::StringArray seen;
        for (const auto& id : settings.partIds)
        {
            if (seen.contains(id)) continue;
            seen.add(id);
            if (const auto* instance = mechanics.findInstance(id)) { if (!addInstance(*instance)) return false; continue; }
            const auto* feature = document.find(id);
            if (feature == nullptr) { error = "There is no body or instance with id " + id + "."; return false; }
            if (feature->type == "sketch") { error = feature->name + " is a sketch; only solid bodies can be exported."; return false; }
            if (!feature->visible) { error = feature->name + " is hidden (for example a Boolean operand consumed by another body). Export the resulting body instead, or show it first."; return false; }
            if (!hasSolid(feature->shape)) { error = feature->name + " has no solid geometry to export."; return false; }
            addFeature(*feature);
        }
        out.scope = out.parts.size() == 1 ? "Selected part: " + out.parts.front().name : juce::String((int) out.parts.size()) + " selected parts";
    }

    // Resolve joint hierarchy and pivot frames for assembly instances
    for (auto& part : out.parts)
    {
        if (!part.instance) continue;
        for (const auto& joint : mechanics.joints)
        {
            if (joint.childInstance == part.id)
            {
                part.hasPivot = true;
                part.parentPartId = joint.parentInstance; // may be empty if joint connects to world
                // jointWorld = world(child) * jointToChild^-1
                gp_Trsf childWorld;
                if (document.referenceWorld(part.id, childWorld, error))
                {
                    part.pivotFrame = childWorld.Multiplied(joint.jointToChild.toTrsf().Inverted());
                }
                break;
            }
        }
    }

    for (auto& part : out.parts)
        for (const auto& source : out.sources)
            if (source.id == part.sourceId) placedBounds(source.shape, part.placement, part.min, part.max);
    for (int k = 0; k < 3; ++k)
    {
        out.min[k] = out.parts.front().min[k]; out.max[k] = out.parts.front().max[k];
        for (const auto& part : out.parts) { out.min[k] = std::min(out.min[k], part.min[k]); out.max[k] = std::max(out.max[k], part.max[k]); }
    }
    out.assemblyName = document.file() != juce::File() ? document.file().getFileNameWithoutExtension() : juce::String("DjehutiCAD model");

    if (settings.output == juce::File()) return true;   // preview: parts and bounds only
    auto base = settings.output;
    if (!base.hasFileExtension(extension(settings.format).substring(1)) && !(settings.format == CadExportFormat::step && base.hasFileExtension("stp")))
        base = base.withFileExtension(extension(settings.format));
    if (settings.format == CadExportFormat::stl && out.parts.size() > 1)
    {
        juce::StringArray names;
        for (const auto& part : out.parts)
        {
            auto name = partFileName(part);
            if (names.contains(name, true)) name += "_" + part.id;
            names.add(name);
            out.outputs.add(base.getSiblingFile(base.getFileNameWithoutExtension() + "-" + name + ".stl"));
        }
    }
    else out.outputs.add(base);
    if (!settings.overwrite)
        for (const auto& file : out.outputs)
            if (file.exists()) { error = file.getFullPathName() + " already exists. Choose another name or allow overwriting."; return false; }
    return true;
}

}

juce::var djehuti::cad::CadExportResult::toVar(const CadExportPlan& plan) const
{
    using namespace exporting;
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("ok", ok);
    if (cancelled) root->setProperty("cancelled", true);
    if (error.isNotEmpty()) root->setProperty("error", error);
    root->setProperty("format", formatKey(plan.settings.format));
    root->setProperty("scope", plan.scope);
    root->setProperty("units", "mm");
    root->setProperty("placement", "reference configuration (authored placements; no simulation or IK pose)");
    if (plan.settings.format != CadExportFormat::step)
    {
        auto q = std::make_unique<juce::DynamicObject>();
        q->setProperty("preset", plan.settings.quality.preset); q->setProperty("chordal_mm", plan.settings.quality.chordalMm); q->setProperty("angular_deg", plan.settings.quality.angularDeg);
        root->setProperty("quality", juce::var(q.release()));
    }
    juce::Array<juce::var> fileList; for (const auto& f : files) fileList.add(f);
    root->setProperty("files", fileList);
    root->setProperty("size_mm", vec3(size));
    juce::Array<juce::var> partList;
    for (const auto& p : parts)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("id", p.id); o->setProperty("name", p.name); o->setProperty("solids", p.solids);
        o->setProperty("size_mm", vec3(p.size)); o->setProperty("volume_mm3", p.solidVolume);
        if (p.triangles > 0) { o->setProperty("triangles", p.triangles); o->setProperty("mesh_volume_mm3", p.meshVolume); }
        if (!p.warnings.isEmpty()) { juce::Array<juce::var> w; for (const auto& s : p.warnings) w.add(s); o->setProperty("warnings", w); }
        partList.add(juce::var(o.release()));
    }
    root->setProperty("parts", partList);
    if (!warnings.isEmpty()) { juce::Array<juce::var> w; for (const auto& s : warnings) w.add(s); root->setProperty("warnings", w); }
    return juce::var(root.release());
}

namespace djehuti::cad::exporting
{
CadExportResult run(const CadExportPlan& plan, const std::function<bool(double, const juce::String&)>& progress)
{
    CadExportResult result;
    bool cancelled = false;
    double fraction = 0.0;
    juce::String stage;
    const auto report = [&](double f, const juce::String& s) { fraction = f; stage = s; if (progress && !progress(f, s)) cancelled = true; return !cancelled; };
    Handle(CancelIndicator) indicator = new CancelIndicator([&] { if (!cancelled && progress && !progress(fraction, stage)) cancelled = true; return cancelled; });
    juce::Array<juce::File> temporaries;
    const auto fail = [&](const juce::String& message)
    {
        for (const auto& t : temporaries) t.deleteFile();
        result.ok = false; result.cancelled = cancelled;
        result.error = cancelled ? juce::String("Export cancelled; no file was written and existing files were left unchanged.") : message + " No file was written.";
        return result;
    };
    if (plan.outputs.isEmpty() || plan.parts.empty()) { result.error = "Choose an output file."; return result; }
    const bool meshing = plan.settings.format != CadExportFormat::step;
    const auto total = (double) plan.sources.size() + 1.0;

    // 1. Solid checks and, for printing formats, the manufacturing mesh of every source body.
    std::map<juce::String, MeshData> meshes;
    std::map<juce::String, std::pair<int, double>> solidInfo;
    for (size_t i = 0; i < plan.sources.size(); ++i)
    {
        const auto& source = plan.sources[i];
        if (!report((double) i / total, (meshing ? "Meshing " : "Checking ") + source.name)) return fail({});
        const auto solids = countSolids(source.shape);
        if (solids == 0) return fail(source.name + " contains no closed solid.");
        if (!BRepCheck_Analyzer(source.shape).IsValid()) return fail(source.name + " fails the OCCT solid validity check (BRepCheck); repair the model before exporting.");
        GProp_GProps props; BRepGProp::VolumeProperties(source.shape, props);
        if (!(props.Mass() > 0.0)) return fail(source.name + " has no positive solid volume.");
        solidInfo[source.id] = { solids, props.Mass() };
        if (!meshing) continue;

        IMeshTools_Parameters parameters;
        parameters.Deflection = plan.settings.quality.chordalMm;
        parameters.Angle = juce::degreesToRadians(plan.settings.quality.angularDeg);
        parameters.Relative = Standard_False;
        parameters.InParallel = Standard_False;
        BRepMesh_IncrementalMesh mesher(source.shape, parameters, indicator->Start());
        if (cancelled) return fail({});
        if (!mesher.IsDone()) return fail(source.name + ": OCCT could not mesh the solid.");
        MeshData mesh; juce::String meshError;
        if (!extractMesh(source.shape, mesh, meshError)) return fail(source.name + ": " + meshError + ".");
        int open = 0, nonManifold = 0, misoriented = 0;
        edgeCheck(mesh, open, nonManifold, misoriented);
        if (open + nonManifold + misoriented > 0)
            return fail(source.name + ": the printing mesh is not a closed, consistently oriented surface (" + juce::String(open) + " open edges, "
                        + juce::String(nonManifold) + " non-manifold edges, " + juce::String(misoriented) + " inconsistently oriented edges).");
        if (!(signedVolume(mesh) > 0.0)) return fail(source.name + ": the printing mesh is inside out (negative volume).");
        meshes[source.id] = std::move(mesh);
    }

    // 2. Per-part report from the placed geometry.
    for (const auto& part : plan.parts)
    {
        CadExportPartReport r;
        r.id = part.id; r.name = part.name;
        r.solids = solidInfo[part.sourceId].first; r.solidVolume = solidInfo[part.sourceId].second;
        for (int k = 0; k < 3; ++k) r.size[k] = part.max[k] - part.min[k];
        if (meshing)
        {
            const auto& mesh = meshes[part.sourceId];
            r.triangles = (int) mesh.triangles.size(); r.vertices = (int) mesh.points.size(); r.meshVolume = signedVolume(mesh);
            const auto deviation = std::abs(r.meshVolume - r.solidVolume) / r.solidVolume;
            if (deviation > 0.02) r.warnings.add("Mesh volume differs from the solid by " + juce::String(deviation * 100.0, 1) + " %; consider Fine quality.");
            if (mesh.collapsed > 0) r.warnings.add(juce::String(mesh.collapsed) + " collapsed triangles (two corners at the same point) were omitted; the surface is unchanged.");
            if (mesh.zeroArea > 0) r.warnings.add(juce::String(mesh.zeroArea) + " near-zero-area triangles are present.");
        }
        for (const auto& w : r.warnings) result.warnings.add(part.name + ": " + w);
        result.parts.push_back(r);
    }
    for (int k = 0; k < 3; ++k) result.size[k] = plan.max[k] - plan.min[k];
    if (!report((total - 1.0) / total, "Writing " + formatName(plan.settings.format))) return fail({});

    // 3. Write to temporary files, then replace the destinations.
    const auto sourceShape = [&](const juce::String& id) { for (const auto& s : plan.sources) if (s.id == id) return s.shape; return TopoDS_Shape(); };
    for (const auto& file : plan.outputs) temporaries.add(temporaryFor(file));
    try
    {
        if (plan.settings.format == CadExportFormat::stl)
        {
            for (size_t i = 0; i < plan.parts.size(); ++i)
            {
                const auto& part = plan.parts[i];
                if (!report((total - 1.0 + (double) i / (double) plan.parts.size()) / total, "Writing " + part.name)) return fail({});
                StlAPI_Writer writer;
                writer.ASCIIMode() = Standard_False;
                const auto placed = sourceShape(part.sourceId).Moved(TopLoc_Location(part.placement));   // placement applied once
                if (!writer.Write(placed, temporaries[(int) i].getFullPathName().toRawUTF8()) || temporaries[(int) i].getSize() < 84)
                    return fail("Writing the STL file for " + part.name + " failed.");
            }
        }
        else if (plan.settings.format == CadExportFormat::threeMF)
        {
            auto wrapper = Lib3MF::CWrapper::loadLibrary();
            auto model = wrapper->CreateModel();
            model->SetUnit(Lib3MF::eModelUnit::MilliMeter);
            auto metadata = model->GetMetaDataGroup();
            metadata->AddMetaData("", "Title", plan.assemblyName.toStdString(), "string", false);
            metadata->AddMetaData("", "Application", "DjehutiCAD", "string", false);
            for (const auto& part : plan.parts)
            {
                // Vertices in the source body's coordinates; the build item transform carries the placement once.
                const auto& mesh = meshes[part.sourceId];
                std::vector<Lib3MF::sPosition> vertices(mesh.points.size());
                for (size_t v = 0; v < mesh.points.size(); ++v) vertices[v] = { { toSingle(mesh.points[v].X()), toSingle(mesh.points[v].Y()), toSingle(mesh.points[v].Z()) } };
                std::vector<Lib3MF::sTriangle> triangles(mesh.triangles.size());
                for (size_t t = 0; t < mesh.triangles.size(); ++t) triangles[t] = { { mesh.triangles[t][0], mesh.triangles[t][1], mesh.triangles[t][2] } };
                auto object = model->AddMeshObject();
                object->SetName(part.name.toStdString());
                object->SetPartNumber(part.id.toStdString());
                object->SetGeometry(vertices, triangles);
                if (!object->IsManifoldAndOriented()) return fail(part.name + ": lib3mf reports the mesh is not manifold and oriented.");
                Lib3MF::sTransform transform {};
                for (int axis = 0; axis < 3; ++axis)
                    for (int c = 0; c < 3; ++c) transform.m_Fields[axis][c] = toSingle(part.placement.Value(c + 1, axis + 1));
                for (int c = 0; c < 3; ++c) transform.m_Fields[3][c] = toSingle(part.placement.Value(c + 1, 4));
                auto item = model->AddBuildItem(object.get(), transform);
                item->SetPartNumber(part.id.toStdString());
            }
            model->QueryWriter("3mf")->WriteToFile(temporaries[0].getFullPathName().toStdString());
        }
        else if (plan.settings.format == CadExportFormat::glb)
        {
            // glTF 2.0 Binary (.glb) Writer
            // Convert meshes (points in mm -> meters, outward normals, 16-bit or 32-bit indices)
            // Hierarchical scene graph with nodes, meshes, accessors, bufferViews, and 1 bin buffer.
            juce::MemoryOutputStream binBuffer;

            struct GlbPrimitive
            {
                int positionAccessor = -1;
                int normalAccessor = -1;
                int indexAccessor = -1;
                int material = 0;
            };

            struct GlbMesh
            {
                juce::String name;
                std::vector<GlbPrimitive> primitives;
            };

            struct GlbNode
            {
                juce::String name;
                int meshIndex = -1;
                float matrix[16] {};
                bool hasMatrix = false;
                std::vector<int> children;
            };

            std::vector<GlbMesh> glbMeshes;
            std::vector<GlbNode> glbNodes;
            juce::Array<juce::var> bufferViews;
            juce::Array<juce::var> accessors;

            // Track bufferView offset
            size_t currentOffset = 0;

            const auto pad4 = [&](juce::MemoryOutputStream& stream, char padByte = 0)
            {
                while (stream.getDataSize() % 4 != 0) stream.writeByte(padByte);
                currentOffset = stream.getDataSize();
            };

            // Part colors: assign distinct pleasing CAD colors to distinct parts
            const juce::Colour defaultPalette[] = {
                juce::Colour(0xff2e9ec7), // cyan / primary
                juce::Colour(0xffe8a448), // orange / secondary
                juce::Colour(0xff48b478), // green
                juce::Colour(0xffa068d4), // purple
                juce::Colour(0xffe05060), // red
                juce::Colour(0xffe8d27a)  // yellow
            };
            const int numPalette = (int)(sizeof(defaultPalette) / sizeof(defaultPalette[0]));

            // Build mesh data for each part
            for (size_t i = 0; i < plan.parts.size(); ++i)
            {
                const auto& part = plan.parts[i];
                if (!report((total - 1.0 + (double) i / (double) plan.parts.size()) / total, "Encoding " + part.name)) return fail({});
                const auto& sourceMesh = meshes[part.sourceId];

                // Determine pivot transformation:
                // If part has a pivot frame (from joint), mesh vertices are expressed relative to that pivot frame:
                //   P_mesh = (pivotFrame^-1 * placement) * P_source
                // Node transform in parent/world is:
                //   T_node = parentFrame^-1 * pivotFrame
                // If part has no pivot frame:
                //   P_mesh = P_source
                //   T_node = parentFrame^-1 * placement
                gp_Trsf meshRelTrsf;
                gp_Trsf nodeWorldTrsf;

                if (part.hasPivot)
                {
                    meshRelTrsf = part.pivotFrame.Inverted().Multiplied(part.placement);
                    nodeWorldTrsf = part.pivotFrame;
                }
                else
                {
                    meshRelTrsf = gp_Trsf(); // identity
                    nodeWorldTrsf = part.placement;
                }

                // If part has a parentPartId in the assembly, compute relative transform to parent's node transform
                gp_Trsf nodeLocalTrsf = nodeWorldTrsf;
                if (part.parentPartId.isNotEmpty())
                {
                    for (const auto& parentCandidate : plan.parts)
                    {
                        if (parentCandidate.id == part.parentPartId)
                        {
                            gp_Trsf parentNodeWorld = parentCandidate.hasPivot ? parentCandidate.pivotFrame : parentCandidate.placement;
                            nodeLocalTrsf = parentNodeWorld.Inverted().Multiplied(nodeWorldTrsf);
                            break;
                        }
                    }
                }

                // Transform points and normals into part-local mesh space (in meters)
                std::vector<float> positions;
                positions.reserve(sourceMesh.points.size() * 3);
                std::vector<float> normals;
                normals.reserve(sourceMesh.normals.size() * 3);

                float pMin[3] = { 1e9f, 1e9f, 1e9f };
                float pMax[3] = { -1e9f, -1e9f, -1e9f };

                for (size_t v = 0; v < sourceMesh.points.size(); ++v)
                {
                    const auto pt = sourceMesh.points[v].Transformed(meshRelTrsf);
                    // Convert mm to meters: 1 mm = 0.001 m
                    const float x = static_cast<float>(pt.X() * 0.001);
                    const float y = static_cast<float>(pt.Y() * 0.001);
                    const float z = static_cast<float>(pt.Z() * 0.001);
                    positions.push_back(x);
                    positions.push_back(y);
                    positions.push_back(z);

                    pMin[0] = std::min(pMin[0], x); pMin[1] = std::min(pMin[1], y); pMin[2] = std::min(pMin[2], z);
                    pMax[0] = std::max(pMax[0], x); pMax[1] = std::max(pMax[1], y); pMax[2] = std::max(pMax[2], z);

                    const auto n = sourceMesh.normals[v].Transformed(meshRelTrsf);
                    normals.push_back(static_cast<float>(n.X()));
                    normals.push_back(static_cast<float>(n.Y()));
                    normals.push_back(static_cast<float>(n.Z()));
                }

                // 1. Write positions bufferView & accessor
                pad4(binBuffer);
                const size_t posByteOffset = binBuffer.getDataSize();
                const size_t posByteLength = positions.size() * sizeof(float);
                binBuffer.write(positions.data(), posByteLength);

                auto posBv = std::make_unique<juce::DynamicObject>();
                posBv->setProperty("buffer", 0);
                posBv->setProperty("byteOffset", (juce::int64)posByteOffset);
                posBv->setProperty("byteLength", (juce::int64)posByteLength);
                posBv->setProperty("target", 34962); // ARRAY_BUFFER
                const int posBvIdx = bufferViews.size();
                bufferViews.add(juce::var(posBv.release()));

                auto posAcc = std::make_unique<juce::DynamicObject>();
                posAcc->setProperty("bufferView", posBvIdx);
                posAcc->setProperty("byteOffset", 0);
                posAcc->setProperty("componentType", 5126); // FLOAT
                posAcc->setProperty("count", (juce::int64)sourceMesh.points.size());
                posAcc->setProperty("type", "VEC3");
                posAcc->setProperty("min", juce::Array<juce::var>{ pMin[0], pMin[1], pMin[2] });
                posAcc->setProperty("max", juce::Array<juce::var>{ pMax[0], pMax[1], pMax[2] });
                const int posAccIdx = accessors.size();
                accessors.add(juce::var(posAcc.release()));

                // 2. Write normals bufferView & accessor
                pad4(binBuffer);
                const size_t normByteOffset = binBuffer.getDataSize();
                const size_t normByteLength = normals.size() * sizeof(float);
                binBuffer.write(normals.data(), normByteLength);

                auto normBv = std::make_unique<juce::DynamicObject>();
                normBv->setProperty("buffer", 0);
                normBv->setProperty("byteOffset", (juce::int64)normByteOffset);
                normBv->setProperty("byteLength", (juce::int64)normByteLength);
                normBv->setProperty("target", 34962); // ARRAY_BUFFER
                const int normBvIdx = bufferViews.size();
                bufferViews.add(juce::var(normBv.release()));

                auto normAcc = std::make_unique<juce::DynamicObject>();
                normAcc->setProperty("bufferView", normBvIdx);
                normAcc->setProperty("byteOffset", 0);
                normAcc->setProperty("componentType", 5126); // FLOAT
                normAcc->setProperty("count", (juce::int64)sourceMesh.normals.size());
                normAcc->setProperty("type", "VEC3");
                const int normAccIdx = accessors.size();
                accessors.add(juce::var(normAcc.release()));

                // 3. Write indices bufferView & accessor
                pad4(binBuffer);
                const size_t idxByteOffset = binBuffer.getDataSize();
                const bool useUint16 = sourceMesh.points.size() < 65536;
                const size_t indexCount = sourceMesh.triangles.size() * 3;
                size_t idxByteLength = 0;

                std::uint32_t minIdx = 0xFFFFFFFF, maxIdx = 0;
                if (useUint16)
                {
                    std::vector<std::uint16_t> idx16(indexCount);
                    for (size_t t = 0; t < sourceMesh.triangles.size(); ++t)
                    {
                        for (int k = 0; k < 3; ++k)
                        {
                            const auto idx = static_cast<std::uint16_t>(sourceMesh.triangles[t][k]);
                            idx16[t * 3 + k] = idx;
                            minIdx = std::min(minIdx, (std::uint32_t)idx);
                            maxIdx = std::max(maxIdx, (std::uint32_t)idx);
                        }
                    }
                    idxByteLength = idx16.size() * sizeof(std::uint16_t);
                    binBuffer.write(idx16.data(), idxByteLength);
                }
                else
                {
                    std::vector<std::uint32_t> idx32(indexCount);
                    for (size_t t = 0; t < sourceMesh.triangles.size(); ++t)
                    {
                        for (int k = 0; k < 3; ++k)
                        {
                            const auto idx = sourceMesh.triangles[t][k];
                            idx32[t * 3 + k] = idx;
                            minIdx = std::min(minIdx, idx);
                            maxIdx = std::max(maxIdx, idx);
                        }
                    }
                    idxByteLength = idx32.size() * sizeof(std::uint32_t);
                    binBuffer.write(idx32.data(), idxByteLength);
                }

                auto idxBv = std::make_unique<juce::DynamicObject>();
                idxBv->setProperty("buffer", 0);
                idxBv->setProperty("byteOffset", (juce::int64)idxByteOffset);
                idxBv->setProperty("byteLength", (juce::int64)idxByteLength);
                idxBv->setProperty("target", 34963); // ELEMENT_ARRAY_BUFFER
                const int idxBvIdx = bufferViews.size();
                bufferViews.add(juce::var(idxBv.release()));

                auto idxAcc = std::make_unique<juce::DynamicObject>();
                idxAcc->setProperty("bufferView", idxBvIdx);
                idxAcc->setProperty("byteOffset", 0);
                idxAcc->setProperty("componentType", useUint16 ? 5123 : 5125); // UNSIGNED_SHORT : UNSIGNED_INT
                idxAcc->setProperty("count", (juce::int64)indexCount);
                idxAcc->setProperty("type", "SCALAR");
                idxAcc->setProperty("min", juce::Array<juce::var>{ (int)minIdx });
                idxAcc->setProperty("max", juce::Array<juce::var>{ (int)maxIdx });
                const int idxAccIdx = accessors.size();
                accessors.add(juce::var(idxAcc.release()));

                // Create GlbPrimitive and GlbMesh
                GlbPrimitive prim;
                prim.positionAccessor = posAccIdx;
                prim.normalAccessor = normAccIdx;
                prim.indexAccessor = idxAccIdx;
                prim.material = static_cast<int>(i % numPalette);

                GlbMesh glbMesh;
                glbMesh.name = part.name;
                glbMesh.primitives.push_back(prim);
                const int meshIdx = (int)glbMeshes.size();
                glbMeshes.push_back(std::move(glbMesh));

                // Create GlbNode
                GlbNode glbNode;
                glbNode.name = part.name;
                glbNode.meshIndex = meshIdx;
                glbNode.hasMatrix = true;

                // Column-major 4x4 matrix for glTF (with translation in meters)
                // glTF matrix: m[0..3] is col 0, m[4..7] is col 1, m[8..11] is col 2, m[12..15] is col 3
                for (int col = 0; col < 3; ++col)
                {
                    for (int row = 0; row < 3; ++row)
                        glbNode.matrix[col * 4 + row] = static_cast<float>(nodeLocalTrsf.Value(row + 1, col + 1));
                    glbNode.matrix[col * 4 + 3] = 0.0f;
                }
                // Translation in meters (1 mm = 0.001 m)
                glbNode.matrix[12] = static_cast<float>(nodeLocalTrsf.Value(1, 4) * 0.001);
                glbNode.matrix[13] = static_cast<float>(nodeLocalTrsf.Value(2, 4) * 0.001);
                glbNode.matrix[14] = static_cast<float>(nodeLocalTrsf.Value(3, 4) * 0.001);
                glbNode.matrix[15] = 1.0f;

                glbNodes.push_back(std::move(glbNode));
            }

            // Build parent-child relationships between nodes
            std::vector<int> rootNodeIndices;
            for (size_t i = 0; i < plan.parts.size(); ++i)
            {
                const auto& part = plan.parts[i];
                int parentIdx = -1;
                if (part.parentPartId.isNotEmpty())
                {
                    for (size_t p = 0; p < plan.parts.size(); ++p)
                    {
                        if (plan.parts[p].id == part.parentPartId)
                        {
                            parentIdx = (int)p;
                            break;
                        }
                    }
                }
                if (parentIdx >= 0 && parentIdx < (int)glbNodes.size())
                {
                    glbNodes[parentIdx].children.push_back((int)i);
                }
                else
                {
                    rootNodeIndices.push_back((int)i);
                }
            }

            // Ensure 4-byte padding on binary buffer
            pad4(binBuffer);

            // Construct JSON document
            auto gltf = std::make_unique<juce::DynamicObject>();

            // Asset
            auto asset = std::make_unique<juce::DynamicObject>();
            asset->setProperty("version", "2.0");
            asset->setProperty("generator", "DjehutiCAD Game-Ready Mesh Exporter");
            gltf->setProperty("asset", juce::var(asset.release()));

            // Materials
            juce::Array<juce::var> materials;
            for (int m = 0; m < numPalette; ++m)
            {
                auto mat = std::make_unique<juce::DynamicObject>();
                mat->setProperty("name", "CAD_Material_" + juce::String(m + 1));
                auto pbr = std::make_unique<juce::DynamicObject>();
                const auto col = defaultPalette[m];
                pbr->setProperty("baseColorFactor", juce::Array<juce::var>{
                    col.getFloatRed(), col.getFloatGreen(), col.getFloatBlue(), 1.0f
                });
                pbr->setProperty("metallicFactor", 0.2f);
                pbr->setProperty("roughnessFactor", 0.5f);
                mat->setProperty("pbrMetallicRoughness", juce::var(pbr.release()));
                materials.add(juce::var(mat.release()));
            }
            gltf->setProperty("materials", materials);

            // Meshes
            juce::Array<juce::var> meshesVar;
            for (const auto& gm : glbMeshes)
            {
                auto mObj = std::make_unique<juce::DynamicObject>();
                mObj->setProperty("name", gm.name);
                juce::Array<juce::var> primsVar;
                for (const auto& prim : gm.primitives)
                {
                    auto pObj = std::make_unique<juce::DynamicObject>();
                    auto attrs = std::make_unique<juce::DynamicObject>();
                    attrs->setProperty("POSITION", prim.positionAccessor);
                    if (prim.normalAccessor >= 0) attrs->setProperty("NORMAL", prim.normalAccessor);
                    pObj->setProperty("attributes", juce::var(attrs.release()));
                    if (prim.indexAccessor >= 0) pObj->setProperty("indices", prim.indexAccessor);
                    pObj->setProperty("material", prim.material);
                    primsVar.add(juce::var(pObj.release()));
                }
                mObj->setProperty("primitives", primsVar);
                meshesVar.add(juce::var(mObj.release()));
            }
            gltf->setProperty("meshes", meshesVar);

            // Nodes
            juce::Array<juce::var> nodesVar;
            for (const auto& gn : glbNodes)
            {
                auto nObj = std::make_unique<juce::DynamicObject>();
                nObj->setProperty("name", gn.name);
                if (gn.meshIndex >= 0) nObj->setProperty("mesh", gn.meshIndex);
                if (gn.hasMatrix)
                {
                    juce::Array<juce::var> mArray;
                    for (int k = 0; k < 16; ++k) mArray.add(gn.matrix[k]);
                    nObj->setProperty("matrix", mArray);
                }
                if (!gn.children.empty())
                {
                    juce::Array<juce::var> chArray;
                    for (int ch : gn.children) chArray.add(ch);
                    nObj->setProperty("children", chArray);
                }
                nodesVar.add(juce::var(nObj.release()));
            }
            gltf->setProperty("nodes", nodesVar);

            // Scene
            gltf->setProperty("scene", 0);
            auto sceneObj = std::make_unique<juce::DynamicObject>();
            sceneObj->setProperty("name", plan.assemblyName);
            juce::Array<juce::var> rootNodesVar;
            for (int rIdx : rootNodeIndices) rootNodesVar.add(rIdx);
            sceneObj->setProperty("nodes", rootNodesVar);
            gltf->setProperty("scenes", juce::Array<juce::var>{ juce::var(sceneObj.release()) });

            // Buffers, BufferViews, Accessors
            auto bufObj = std::make_unique<juce::DynamicObject>();
            bufObj->setProperty("byteLength", (juce::int64)binBuffer.getDataSize());
            gltf->setProperty("buffers", juce::Array<juce::var>{ juce::var(bufObj.release()) });
            gltf->setProperty("bufferViews", bufferViews);
            gltf->setProperty("accessors", accessors);

            // Format JSON
            const auto jsonString = juce::JSON::toString(juce::var(gltf.release()), false);
            juce::MemoryOutputStream jsonChunk;
            jsonChunk.write(jsonString.toRawUTF8(), (size_t)jsonString.getNumBytesAsUTF8());
            while (jsonChunk.getDataSize() % 4 != 0) jsonChunk.writeByte(' '); // pad JSON chunk with spaces

            // Construct GLB file
            // Header: 12 bytes: magic (0x46546C67), version (2), length
            // Chunk 0: length, type (0x4E4F534A 'JSON'), data
            // Chunk 1: length, type (0x004E4942 'BIN\0'), data
            const uint32_t glbMagic = 0x46546C67;
            const uint32_t glbVersion = 2;
            const uint32_t jsonChunkLength = (uint32_t)jsonChunk.getDataSize();
            const uint32_t jsonChunkType = 0x4E4F534A;
            const uint32_t binChunkLength = (uint32_t)binBuffer.getDataSize();
            const uint32_t binChunkType = 0x004E4942;
            const uint32_t totalGlbLength = 12 + 8 + jsonChunkLength + 8 + binChunkLength;

            juce::FileOutputStream fileStream(temporaries[0]);
            if (!fileStream.openedOk()) return fail("Unable to open temporary file for writing GLB.");

            fileStream.writeInt((int)glbMagic);
            fileStream.writeInt((int)glbVersion);
            fileStream.writeInt((int)totalGlbLength);

            fileStream.writeInt((int)jsonChunkLength);
            fileStream.writeInt((int)jsonChunkType);
            fileStream.write(jsonChunk.getData(), jsonChunk.getDataSize());

            fileStream.writeInt((int)binChunkLength);
            fileStream.writeInt((int)binChunkType);
            fileStream.write(binBuffer.getData(), binBuffer.getDataSize());

            fileStream.flush();
            if (temporaries[0].getSize() < 20) return fail("Writing GLB file failed.");
        }
        else
        {
            std::lock_guard<std::mutex> lock(stepMutex());
            // Export-only XCAF document: one product per source body, one named component per part.
            Handle(TDocStd_Document) xdoc;
            const auto app = XCAFApp_Application::GetApplication();
            app->NewDocument("MDTV-XCAF", xdoc);
            const auto shapes = XCAFDoc_DocumentTool::ShapeTool(xdoc->Main());
            std::map<juce::String, TDF_Label> products;
            for (const auto& source : plan.sources) { const auto label = shapes->AddShape(source.shape, Standard_False); TDataStd_Name::Set(label, extended(source.name)); products[source.id] = label; }
            if (plan.parts.size() > 1 || plan.parts.front().instance)
            {
                const auto root = shapes->NewShape();
                TDataStd_Name::Set(root, extended(plan.assemblyName));
                for (const auto& part : plan.parts)
                {
                    const auto component = shapes->AddComponent(root, products[part.sourceId], TopLoc_Location(part.placement));
                    TDataStd_Name::Set(component, extended(part.name));
                }
                shapes->UpdateAssemblies();
            }
            STEPCAFControl_Writer writer;
            writer.SetNameMode(Standard_True);
            StepData_ConfParameters parameters;
            parameters.InitFromStatic();
            parameters.WriteSchema = StepData_ConfParameters::WriteMode_StepSchema_AP214IS;
            parameters.WriteUnit = UnitsMethods_LengthUnit_Millimeter;
            parameters.WriteName = true;
            const bool transferred = writer.Transfer(xdoc, parameters, STEPControl_AsIs, nullptr, indicator->Start());
            const bool written = transferred && !cancelled && writer.Write(temporaries[0].getFullPathName().toRawUTF8()) == IFSelect_RetDone;
            app->Close(xdoc);
            if (cancelled) return fail({});
            if (!written) return fail("The STEP writer could not translate or write the model.");
        }
    }
    catch (const Lib3MF::ELib3MFException& e) { return fail(juce::String("lib3mf: ") + e.what() + "."); }
    catch (const Standard_Failure& e) { return fail(juce::String("OCCT: ") + e.GetMessageString() + "."); }
    catch (const std::exception& e) { return fail(juce::String(e.what()) + "."); }

    if (!report(0.99, "Saving")) return fail({});
    for (int i = 0; i < plan.outputs.size(); ++i)
    {
        if (!finalize(temporaries[i], plan.outputs[i]))
        {
            for (int j = i; j < temporaries.size(); ++j) temporaries[j].deleteFile();
            result.error = "Could not replace " + plan.outputs[i].getFullPathName() + " (is it open in another program?)."
                           + (i > 0 ? juce::String(" ") + juce::String(i) + " earlier file(s) were already saved." : juce::String(" No file was written."));
            return result;
        }
        result.files.add(plan.outputs[i].getFullPathName());
    }
    result.ok = true;
    report(1.0, "Done");
    return result;
}
}
