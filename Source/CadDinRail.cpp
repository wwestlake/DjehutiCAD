#include "CadDinRail.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <map>

namespace djehuti::cad::din
{
namespace
{
juce::var object() { return juce::var(new juce::DynamicObject()); }
bool numeric(const juce::var& v) { return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite((double) v); }
juce::String mm(double v) { return juce::String(v, 3) + " mm"; }

// TS35 x 7.5 top-hat rail. Overall width 35 mm, height 7.5 mm and sheet thickness 1.0 mm are documented manufacturer
// values (OBO Bettermann and WAGO data sheets for EN 60715 rails). The 27 mm outer wall spacing is the dimension DIN
// clips engage; the sloped wall detail of the standard is not modelled. See standard() for the provenance.
constexpr double railWidth = 35.0, railHeight = 7.5, railSheet = 1.0, railWallOuter = 27.0;
constexpr double maxRailLength = 6000.0, maxBottomSide = 1000.0, maxBottomThickness = 30.0;
constexpr double maxAdvance = 0.9;

// Fixed mounting geometry in the module frame (see the header). All values mm.
constexpr double clipX0 = 2.0, clipX1 = 26.0, clipY0 = 8.0, clipY1 = 58.0, clipPlate = 2.0, pocketDepth = 2.2, pocketClear = 0.2;
constexpr double screwX = 22.25, screwZ = -2.0, screwBackedOffY = 49.2, screwLength = 8.0, screwRadius = 1.2, pilotRadius = 1.25;

struct Pt { double y, z; };

TopoDS_Shape box(double x, double y, double z, double dx, double dy, double dz)
{
    return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Shape();
}

// Polygon in the YZ plane at x = x0, extruded +X by depth.
TopoDS_Shape prismYZ(const std::vector<Pt>& points, double x0, double depth)
{
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto& p : points) polygon.Add(gp_Pnt(x0, p.y, p.z));
    polygon.Close();
    BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
    return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(depth, 0, 0)).Shape();
}

bool fuse(TopoDS_Shape& target, const TopoDS_Shape& tool, juce::String& error)
{
    BRepAlgoAPI_Fuse op(target, tool);
    if (!op.IsDone() || op.Shape().IsNull()) { error = "A DIN-rail feature could not be combined (boolean union failed)."; return false; }
    target = op.Shape(); return true;
}
bool cut(TopoDS_Shape& target, const TopoDS_Shape& tool, juce::String& error)
{
    BRepAlgoAPI_Cut op(target, tool);
    if (!op.IsDone() || op.Shape().IsNull()) { error = "A DIN-rail feature could not be cut (boolean subtraction failed)."; return false; }
    target = op.Shape(); return true;
}

TopoDS_Shape unify(const TopoDS_Shape& shape)
{
    ShapeUpgrade_UnifySameDomain merge(shape, true, true, false);
    merge.Build();
    return merge.Shape().IsNull() ? shape : merge.Shape();
}

// Gives each named plate face its token when exactly one face qualifies, so tokens stay unambiguous.
void nameBoxFaces(const TopoDS_Shape& shape, const juce::String& owner, double length, double width, double thickness, FaceNames& names)
{
    std::map<juce::String, std::vector<TopoDS_Shape>> found;
    const double tol = 1.0e-6;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next())
    {
        const auto face = TopoDS::Face(ex.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane) continue;
        auto normal = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
        const auto at = surface.Plane().Location();
        const auto is = [&](double a, double b) { return std::abs(a - b) < tol; };
        if (is(normal.Z(), 1.0) && is(at.Z(), thickness)) found["top"].push_back(face);
        else if (is(normal.Z(), -1.0) && is(at.Z(), 0.0)) found["bottom"].push_back(face);
        else if (is(normal.X(), -1.0) && is(at.X(), 0.0)) found["xmin"].push_back(face);
        else if (is(normal.X(), 1.0) && is(at.X(), length)) found["xmax"].push_back(face);
        else if (is(normal.Y(), -1.0) && is(at.Y(), 0.0)) found["ymin"].push_back(face);
        else if (is(normal.Y(), 1.0) && is(at.Y(), width)) found["ymax"].push_back(face);
    }
    for (const auto& [token, faces] : found)
        if (faces.size() == 1) names.push_back({ faces.front(), { owner + "/" + token } });
}

TopoDS_Shape railShape(double length, juce::String& error)
{
    const double h = railWidth / 2.0, wallOuter = railWallOuter / 2.0, wallInner = wallOuter - railSheet;
    const std::vector<Pt> points {
        { -h, 0 }, { -wallInner, 0 }, { -wallInner, -(railHeight - railSheet) }, { wallInner, -(railHeight - railSheet) }, { wallInner, 0 }, { h, 0 },
        { h, -railSheet }, { wallOuter, -railSheet }, { wallOuter, -railHeight }, { -wallOuter, -railHeight }, { -wallOuter, -railSheet }, { -h, -railSheet } };
    const auto shape = prismYZ(points, 0.0, length);
    if (shape.IsNull()) error = "The DIN-rail profile could not be extruded.";
    return shape;
}

// The replaceable mounting foot in the module frame. Fixed hook (rail lip at y = 13.5), spring-clip arm with a ramped hook
// (opposite lip at y = 48.5), release lug with a screwdriver slot, and the friction-screw block with its pilot hole.
TopoDS_Shape clipShape(juce::String& error)
{
    auto shape = box(clipX0, clipY0, 0.0, clipX1 - clipX0, clipY1 - clipY0, clipPlate);
    // The arm runs along X (x 2..18). Slits free it; the outer slit is 2 mm wide so the arm can deflect 1.6 mm outward.
    if (!cut(shape, box(1.0, 47.9, -1.0, 17.0, 1.0, 4.0), error)) return {};
    if (!cut(shape, box(1.0, 50.5, -1.0, 17.0, 2.0, 4.0), error)) return {};
    const auto addPrism = [&](const std::vector<Pt>& pts, double x0, double depth) { return fuse(shape, prismYZ(pts, x0, depth), error); };
    if (!addPrism({ { 11.5, 0.5 }, { 13.1, 0.5 }, { 13.1, -1.5 }, { 15.0, -1.5 }, { 15.0, -2.0 }, { 11.5, -3.0 } }, clipX0, clipX1 - clipX0)) return {};
    if (!addPrism({ { 48.9, 0.5 }, { 50.5, 0.5 }, { 50.5, -3.0 }, { 47.0, -2.0 }, { 47.0, -1.5 }, { 48.9, -1.5 } }, clipX0, 4.0)) return {};
    if (!addPrism({ { 50.0, -1.5 }, { 54.5, -1.5 }, { 54.5, -3.0 }, { 50.0, -3.0 } }, clipX0, 4.0)) return {};
    if (!addPrism({ { 48.9, 0.5 }, { 58.0, 0.5 }, { 58.0, -4.0 }, { 48.9, -4.0 } }, 18.5, 7.5)) return {};
    if (!cut(shape, box(3.4, 51.6, -3.2, 1.2, 1.8, 1.9), error)) return {};
    const auto pilot = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(screwX, 48.8, screwZ), gp_Dir(0, 1, 0)), pilotRadius, 9.5).Shape();
    if (!cut(shape, pilot, error)) return {};
    return shape;
}

struct Tab { juce::String edge; double offset = 0, width = 0, projection = 0, thickness = 0, hole = 0; };

bool readTab(const juce::var& v, double thickness, Tab& tab, juce::String& error)
{
    if (!v.isObject()) { error = "Each tab must be an object."; return false; }
    for (const auto& p : v.getDynamicObject()->getProperties())
        if (!juce::StringArray { "edge", "offsetMm", "widthMm", "projectionMm", "thicknessMm", "holeDiameterMm" }.contains(p.name.toString())) { error = "Unknown tab parameter: " + p.name.toString(); return false; }
    tab.edge = v["edge"].toString();
    if (tab.edge == "ymin") { error = "Tabs are not allowed on the alignment edge (ymin): it must stay straight so modules line up on the rail."; return false; }
    if (!juce::StringArray { "xmin", "xmax", "ymax" }.contains(tab.edge)) { error = "Tab edge must be xmin, xmax or ymax."; return false; }
    for (const auto* key : { "offsetMm", "widthMm", "projectionMm" }) if (!numeric(v[key])) { error = juce::String("Tab ") + key + " must be a number."; return false; }
    tab.offset = v["offsetMm"]; tab.width = v["widthMm"]; tab.projection = v["projectionMm"];
    tab.thickness = numeric(v["thicknessMm"]) ? (double) v["thicknessMm"] : thickness;
    tab.hole = numeric(v["holeDiameterMm"]) ? (double) v["holeDiameterMm"] : 0.0;
    if (tab.offset < 0.0 || tab.width < 3.0 || tab.projection < 3.0) { error = "A tab needs offsetMm >= 0, widthMm >= 3 and projectionMm >= 3."; return false; }
    if (tab.thickness < 1.2 || tab.thickness > thickness + 1.0e-9) { error = "Tab thicknessMm must be between 1.2 mm and the bottom thickness (" + mm(thickness) + ")."; return false; }
    if (tab.hole < 0.0 || (tab.hole > 0.0 && (tab.hole > tab.width - 3.2 || tab.hole > tab.projection - 3.2))) { error = "The tab hole needs at least 1.6 mm of material on every side."; return false; }
    return true;
}

bool tabsFit(const std::vector<Tab>& tabs, double length, double width, juce::String& error)
{
    for (const auto& t : tabs)
    {
        const double edgeLength = t.edge == "ymax" ? length : width;
        if (t.offset + t.width > edgeLength + 1.0e-9) { error = "A " + t.edge + " tab (offset " + mm(t.offset) + ", width " + mm(t.width) + ") extends past the " + mm(edgeLength) + " edge."; return false; }
    }
    return true;
}

TopoDS_Shape bottomShape(const juce::var& p, const juce::String& owner, FaceNames& names, juce::String& error)
{
    const double length = p["lengthMm"], width = p["widthMm"], thickness = p["thicknessMm"];
    auto shape = box(0.0, 0.0, 0.0, length, width, thickness);
    // Pocket for the replaceable clip. Its position is fixed in the module frame, so resizing never moves it.
    if (!cut(shape, box(clipX0 - pocketClear, clipY0 - pocketClear, -0.1, clipX1 - clipX0 + 2 * pocketClear, clipY1 - clipY0 + 2 * pocketClear, pocketDepth + 0.1), error)) return {};
    std::vector<Tab> tabs;
    if (const auto* list = p["tabs"].getArray())
        for (const auto& v : *list) { Tab t; if (!readTab(v, thickness, t, error)) return {}; tabs.push_back(t); }
    if (!tabsFit(tabs, length, width, error)) return {};
    for (const auto& t : tabs)
    {
        TopoDS_Shape ear; gp_Pnt centre;
        if (t.edge == "xmin") { ear = box(-t.projection, t.offset, 0.0, t.projection + 0.1, t.width, t.thickness); centre = gp_Pnt(-t.projection / 2.0, t.offset + t.width / 2.0, 0.0); }
        else if (t.edge == "xmax") { ear = box(length - 0.1, t.offset, 0.0, t.projection + 0.1, t.width, t.thickness); centre = gp_Pnt(length + t.projection / 2.0, t.offset + t.width / 2.0, 0.0); }
        else { ear = box(t.offset, width - 0.1, 0.0, t.width, t.projection + 0.1, t.thickness); centre = gp_Pnt(t.offset + t.width / 2.0, width + t.projection / 2.0, 0.0); }
        if (!fuse(shape, ear, error)) return {};
        if (t.hole > 0.0)
        {
            const auto hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(centre.X(), centre.Y(), -0.1), gp::DZ()), t.hole / 2.0, t.thickness + 0.2).Shape();
            if (!cut(shape, hole, error)) return {};
        }
    }
    shape = unify(shape);
    nameBoxFaces(shape, owner, length, width, thickness, names);
    return shape;
}

bool checkRange(const juce::var& p, const char* key, double lo, double hi, const char* unit, juce::String& error)
{
    if (!numeric(p[key])) { error = juce::String(key) + " must be a finite number (" + unit + ")."; return false; }
    const double v = p[key];
    if (v < lo || v > hi) { error = juce::String(key) + " must be between " + mm(lo) + " and " + mm(hi) + "."; return false; }
    return true;
}
}

juce::var defaults(const juce::String& type)
{
    auto p = object(); auto* o = p.getDynamicObject();
    if (type == "din_rail") o->setProperty("lengthMm", 500.0);
    else if (type == "din_bottom")
    {
        o->setProperty("lengthMm", 60.0); o->setProperty("widthMm", 60.0); o->setProperty("thicknessMm", 3.0);
        o->setProperty("tabs", juce::Array<juce::var>());
    }
    else if (type == "din_screw") o->setProperty("advanceMm", 0.0);
    return p;
}

bool validate(const juce::String& type, const juce::var& p, juce::String& error)
{
    const auto fail = [&](const juce::String& e) { error = e; return false; };
    if (!p.isObject()) return fail("Invalid DIN-rail definition.");
    const auto allowed = [&](const juce::StringArray& keys)
    {
        for (const auto& v : p.getDynamicObject()->getProperties()) if (!keys.contains(v.name.toString())) return fail("Unknown parameter: " + v.name.toString());
        return true;
    };
    if (type == "din_rail") return allowed({ "lengthMm" }) && checkRange(p, "lengthMm", 10.0, maxRailLength, "mm", error);
    if (type == "din_clip") return allowed({});
    if (type == "din_screw") return allowed({ "advanceMm" }) && checkRange(p, "advanceMm", 0.0, maxAdvance, "mm", error);
    if (type != "din_bottom") return fail("Unknown DIN-rail feature type.");
    if (!allowed({ "lengthMm", "widthMm", "thicknessMm", "tabs", "module" })) return false;
    if (!checkRange(p, "lengthMm", minLength, maxBottomSide, "mm", error) || !checkRange(p, "widthMm", minWidth, maxBottomSide, "mm", error)
        || !checkRange(p, "thicknessMm", minThickness, maxBottomThickness, "mm", error)) return false;
    if (!p["tabs"].isArray() && !p["tabs"].isVoid()) return fail("tabs must be an array.");
    std::vector<Tab> tabs;
    if (const auto* list = p["tabs"].getArray())
    {
        if (list->size() > 16) return fail("At most 16 tabs are supported.");
        for (const auto& v : *list) { Tab t; if (!readTab(v, p["thicknessMm"], t, error)) return false; tabs.push_back(t); }
    }
    return tabsFit(tabs, p["lengthMm"], p["widthMm"], error);
}

TopoDS_Shape build(const juce::String& type, const juce::var& p, const juce::String& owner, FaceNames& names, juce::String& error)
{
    if (!validate(type, p, error)) return {};
    names.clear();
    try
    {
        TopoDS_Shape shape;
        if (type == "din_rail")
        {
            const double length = p["lengthMm"];
            shape = railShape(length, error);
            if (shape.IsNull()) return {};
            for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next())
            {
                const auto face = TopoDS::Face(ex.Current()); BRepAdaptor_Surface s(face);
                if (s.GetType() != GeomAbs_Plane) continue;
                auto n = s.Plane().Axis().Direction(); if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
                if (std::abs(n.X() + 1.0) < 1e-6) names.push_back({ face, { owner + "/start" } });
                else if (std::abs(n.X() - 1.0) < 1e-6) names.push_back({ face, { owner + "/end" } });
            }
        }
        else if (type == "din_bottom") shape = bottomShape(p, owner, names, error);
        else if (type == "din_clip") shape = clipShape(error);
        else
        {
            const double advance = p["advanceMm"];
            shape = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(screwX, screwBackedOffY - advance, screwZ), gp_Dir(0, 1, 0)), screwRadius, screwLength).Shape();
        }
        if (shape.IsNull()) { if (error.isEmpty()) error = "The DIN-rail feature could not be constructed."; return {}; }
        if (!BRepCheck_Analyzer(shape, true).IsValid()) { error = "The DIN-rail feature produced an invalid solid."; return {}; }
        return shape;
    }
    catch (const Standard_Failure& failure) { error = juce::String("DIN-rail construction failed: ") + failure.GetMessageString(); return {}; }
}

juce::var standard()
{
    auto r = object(); auto* o = r.getDynamicObject();
    auto rail = object(); auto* rr = rail.getDynamicObject();
    rr->setProperty("designation", "TS35 x 7.5 top-hat rail, IEC/EN 60715");
    rr->setProperty("widthMm", railWidth); rr->setProperty("heightMm", railHeight); rr->setProperty("sheetThicknessMm", railSheet); rr->setProperty("wallOuterSpacingMm", railWallOuter);
    rr->setProperty("documented", "Width 35 mm, height 7.5 mm and 1.0 mm sheet thickness are manufacturer-documented (OBO Bettermann and WAGO TS35 x 7.5 data sheets, EN 60715).");
    rr->setProperty("provisional", "The 27 mm outer wall spacing, the vertical wall shape and the 4 mm flange width are NOT confirmed against the EN 60715 drawing or a manufacturer drawing; the sloped walls of the real profile are not modelled. Confirm before relying on lip clearances.");
    rr->setProperty("frame", "Rail axis on y = 0, flange (clip) plane z = 0, flanges at +/-17.5 mm, rail starts at x = 0 and runs +X.");
    o->setProperty("rail", rail);
    auto ref = object(); auto* rf = ref.getDynamicObject();
    rf->setProperty("frame", "Module frame: origin at the alignment corner, +X along the rail, +Y across it, +Z up from the rail-contact plane z = 0.");
    rf->setProperty("alignmentEdge", "The plane y = 0 (ymin edge). It stays straight; tabs are refused on it.");
    rf->setProperty("railCentreFromAlignmentEdgeMm", railCentreFromEdge);
    rf->setProperty("placement", "Place a module at y = railY - 31 so its alignment edge is 31 mm in front of the rail axis. All modules share this reference.");
    rf->setProperty("clipFootprintMm", "x 2 to 26, y 8 to 58 (plate 2 mm thick, hooks and screw block below z = 0)");
    rf->setProperty("fixedHookLipY", railCentreFromEdge - railWidth / 2.0); rf->setProperty("springClipLipY", railCentreFromEdge + railWidth / 2.0);
    o->setProperty("reference", ref);
    auto ranges = object(); auto* rg = ranges.getDynamicObject();
    rg->setProperty("railLengthMm", "10 to 6000"); rg->setProperty("bottomLengthMm", "28 to 1000"); rg->setProperty("bottomWidthMm", "60 to 1000"); rg->setProperty("bottomThicknessMm", "3 to 30");
    rg->setProperty("screwAdvanceMm", "0 to 0.9 (about 0.7 touches the rail flange edge)");
    o->setProperty("ranges", ranges);
    o->setProperty("material", "Designed for PETG; strain and force figures in the agent card are hand calculations, not simulation results.");
    return r;
}
}
