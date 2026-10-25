#include "CadTSlot.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Mat.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace djehuti::cad::tslot
{
namespace
{
juce::var object() { return juce::var(new juce::DynamicObject()); }
bool numeric(const juce::var& v) { return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite((double) v); }
juce::String mm(double v) { return juce::String(v, 2) + " mm"; }

constexpr double minSide = 10.0, maxSide = 200.0, minLength = 10.0, maxLength = 3000.0;
constexpr int maxSlotsPerFace = 8;

struct Profile
{
    double width = 20, height = 20, length = 200;
    int top = 1, bottom = 1, left = 1, right = 1;
    double opening = 6.0, lip = 1.8, head = 10.0, straight = 1.5, depth = 6.0;
    juce::String material, construction = "onePiece";
    // Two-half construction.
    double printable = 200, overlap = 40, pinSpacing = 30, armLength = 14, armWidth = 2.0, slit = 0.8, hookHeight = 2.0, snapLip = 0.7, hookLength = 4.0, clearance = 0.25, endMargin = 6.0;
    juce::String half; double x0 = 0, x1 = 0; std::vector<double> windows;   // set only for tslot_half pieces
    double floorHalf() const { return head / 2.0 - (depth - lip - straight); }
    int count(int face) const { return face == 0 ? top : face == 1 ? bottom : face == 2 ? left : right; }
};
const char* faceName(int face) { return face == 0 ? "top" : face == 1 ? "bottom" : face == 2 ? "left" : "right"; }

bool readInt(const juce::var& v, const char* key, int lo, int hi, int& out, juce::String& error)
{
    if (!numeric(v)) { error = juce::String(key) + " must be a whole number."; return false; }
    const double d = v;
    if (std::abs(d - std::round(d)) > 1.0e-9) { error = juce::String(key) + " must be a whole number."; return false; }
    if (d < lo || d > hi) { error = juce::String(key) + " must be between " + juce::String(lo) + " and " + juce::String(hi) + "."; return false; }
    out = (int) std::lround(d); return true;
}
bool readNumber(const juce::var& v, const char* key, double lo, double hi, double& out, juce::String& error)
{
    if (!numeric(v)) { error = juce::String(key) + " must be a finite number (mm)."; return false; }
    const double d = v;
    if (d < lo || d > hi) { error = juce::String(key) + " must be between " + mm(lo) + " and " + mm(hi) + "."; return false; }
    out = d; return true;
}

bool readProfile(const juce::var& p, Profile& r, juce::String& error, bool piece = false)
{
    if (!p.isObject()) { error = "Invalid T-slot rail definition."; return false; }
    for (const auto& v : p.getDynamicObject()->getProperties())
        if (!juce::StringArray { "widthMm", "heightMm", "lengthMm", "slotsTop", "slotsBottom", "slotsLeft", "slotsRight", "slotOpeningMm", "slotLipMm",
                                 "slotHeadWidthMm", "slotHeadStraightMm", "slotDepthMm", "material", "construction", "printableLengthMm", "overlapMm", "snapPinSpacingMm",
                                 "snapArmLengthMm", "snapArmWidthMm", "snapSlitMm", "snapHookHeightMm", "snapLipMm", "snapHookLengthMm", "assemblyClearanceMm", "snapEndMarginMm" }.contains(v.name.toString())
            && !(piece && juce::StringArray { "half", "x0Mm", "x1Mm", "windows", "rail", "index" }.contains(v.name.toString())))
        { error = "Unknown parameter: " + v.name.toString(); return false; }
    if (!readNumber(p["widthMm"], "widthMm", minSide, maxSide, r.width, error) || !readNumber(p["heightMm"], "heightMm", minSide, maxSide, r.height, error)
        || !readNumber(p["lengthMm"], "lengthMm", minLength, maxLength, r.length, error)) return false;
    if (!readInt(p["slotsTop"], "slotsTop", 0, maxSlotsPerFace, r.top, error) || !readInt(p["slotsBottom"], "slotsBottom", 0, maxSlotsPerFace, r.bottom, error)
        || !readInt(p["slotsLeft"], "slotsLeft", 0, maxSlotsPerFace, r.left, error) || !readInt(p["slotsRight"], "slotsRight", 0, maxSlotsPerFace, r.right, error)) return false;
    if (!readNumber(p["slotOpeningMm"], "slotOpeningMm", 2.0, 40.0, r.opening, error) || !readNumber(p["slotLipMm"], "slotLipMm", 1.0, 10.0, r.lip, error)
        || !readNumber(p["slotHeadWidthMm"], "slotHeadWidthMm", 4.0, 60.0, r.head, error) || !readNumber(p["slotHeadStraightMm"], "slotHeadStraightMm", 0.3, 20.0, r.straight, error)
        || !readNumber(p["slotDepthMm"], "slotDepthMm", 3.0, 80.0, r.depth, error)) return false;
    r.material = p["material"].toString();
    r.construction = p["construction"].toString();
    if (piece)
    {
        r.half = p["half"].toString();
        if (r.half != "top" && r.half != "bottom") { error = "half must be top or bottom."; return false; }
        if (!numeric(p["x0Mm"]) || !numeric(p["x1Mm"]) || (double) p["x1Mm"] - (double) p["x0Mm"] < 5.0) { error = "x0Mm and x1Mm must give a piece at least 5 mm long."; return false; }
        r.x0 = p["x0Mm"]; r.x1 = p["x1Mm"];
        if (const auto* list = p["windows"].getArray()) for (const auto& w : *list) { if (!numeric(w)) { error = "windows must be numbers."; return false; } r.windows.push_back((double) w); }
    }
    if (r.construction != "onePiece" && r.construction != "twoHalf") { error = r.construction == "fourPiece" ? "Four-piece construction is not available yet; use onePiece or twoHalf." : "construction must be onePiece or twoHalf."; return false; }
    if (r.construction == "twoHalf")
    {
        if (!readNumber(p["printableLengthMm"], "printableLengthMm", 40.0, 1000.0, r.printable, error) || !readNumber(p["overlapMm"], "overlapMm", 10.0, 500.0, r.overlap, error)
            || !readNumber(p["snapPinSpacingMm"], "snapPinSpacingMm", 10.0, 200.0, r.pinSpacing, error) || !readNumber(p["snapArmLengthMm"], "snapArmLengthMm", 6.0, 60.0, r.armLength, error)
            || !readNumber(p["snapArmWidthMm"], "snapArmWidthMm", 1.2, 6.0, r.armWidth, error) || !readNumber(p["snapSlitMm"], "snapSlitMm", 0.5, 3.0, r.slit, error)
            || !readNumber(p["snapHookHeightMm"], "snapHookHeightMm", 1.0, 6.0, r.hookHeight, error) || !readNumber(p["snapLipMm"], "snapLipMm", 0.3, 2.0, r.snapLip, error)
            || !readNumber(p["snapHookLengthMm"], "snapHookLengthMm", 2.0, 20.0, r.hookLength, error) || !readNumber(p["assemblyClearanceMm"], "assemblyClearanceMm", 0.05, 1.0, r.clearance, error)
            || !readNumber(p["snapEndMarginMm"], "snapEndMarginMm", 2.0, 30.0, r.endMargin, error)) return false;
        if (r.left != 0 || r.right != 0 || r.top < 1 || r.bottom < 1) { error = "Two-half construction needs T-slots on two opposite faces: slotsTop and slotsBottom at least 1, slotsLeft and slotsRight 0."; return false; }
        if (r.hookHeight < 2.0 * r.clearance + 0.4) { error = "snapHookHeightMm is too small for the assembly clearance."; return false; }
        if (r.slit < r.snapLip - r.clearance + 0.2) { error = "snapSlitMm (" + mm(r.slit) + ") must exceed snapLipMm minus the clearance by at least 0.2 mm so the arm can deflect while the barb passes."; return false; }
        if (r.snapLip < 2.0 * r.clearance) { error = "snapLipMm must be at least twice the assemblyClearanceMm so the hook is retained."; return false; }
        const double corridor = r.width / 2.0 - r.head / 2.0, need = r.armWidth + 2.0 * r.slit + 2.0 * r.snapLip + r.clearance + 2.0 * minWeb * 0.5;
        if (corridor < need) { error = "The side blocks beside the T-slots (" + mm(corridor) + " wide) are too narrow for the snap arms (they need " + mm(need) + "). Increase widthMm, reduce slotHeadWidthMm or make the snap arm, slit or lip smaller."; return false; }
        const double web = r.height / 2.0 - r.depth;
        if (web < r.hookHeight + r.clearance + minWeb) { error = "The web under the slot floors (" + mm(web) + ") is too thin for the snap sockets (needs " + mm(r.hookHeight + r.clearance + minWeb) + "). Reduce slotDepthMm or snapHookHeightMm, or increase heightMm."; return false; }
        if (r.printable <= r.overlap) { error = "printableLengthMm must be larger than overlapMm."; return false; }
    }
    if (r.head < r.opening + 1.5) { error = "slotHeadWidthMm (" + mm(r.head) + ") must exceed slotOpeningMm (" + mm(r.opening) + ") by at least 1.5 mm so the lips can retain a T-nut."; return false; }
    if (r.depth < r.lip + r.straight + 0.5) { error = "slotDepthMm must be at least slotLipMm + slotHeadStraightMm + 0.5 mm."; return false; }
    if (r.floorHalf() < 0.6) { error = "The slot walls (45 degrees) would close before the floor: reduce slotDepthMm or increase slotHeadWidthMm."; return false; }
    for (int face = 0; face < 4; ++face)
    {
        const int n = r.count(face); if (n == 0) continue;
        const bool horizontal = face < 2;
        const double span = horizontal ? r.width : r.height, across = horizontal ? r.height : r.width, pitch = span / n;
        if (r.head > pitch - 2.0 * minWeb) { error = juce::String(n) + " " + faceName(face) + " slot(s) do not fit: the head width " + mm(r.head) + " leaves only " + mm((pitch - r.head) / 2.0) + " beside each slot (minimum " + mm(minWeb) + "). Reduce the count or the head width."; return false; }
        if (r.depth > across - minWeb) { error = "slotDepthMm (" + mm(r.depth) + ") leaves less than " + mm(minWeb) + " of material through the " + faceName(face) + " section (" + mm(across) + ")."; return false; }
    }
    return true;
}

struct Pt { double y, z; };

// Slot cavity outline in the YZ plane for slot i of the given face. Local frame: s along the face, d inward from the face.
std::vector<Pt> slotPolygon(const Profile& r, int face, int i, Pt& floorA, Pt& floorB)
{
    const bool horizontal = face < 2;
    const double span = horizontal ? r.width : r.height, pitch = span / r.count(face), centre = -span / 2.0 + pitch * (i + 0.5);
    const double f = r.floorHalf(), o = r.opening / 2.0, h = r.head / 2.0, lip = r.lip, s2 = r.lip + r.straight;
    const double local[10][2] = { { -o, -0.5 }, { o, -0.5 }, { o, lip }, { h, lip }, { h, s2 }, { f, r.depth }, { -f, r.depth }, { -h, s2 }, { -h, lip }, { -o, lip } };
    const auto map = [&](double s, double d) -> Pt
    {
        switch (face)
        {
            case 0: return { centre + s, r.height / 2.0 - d };
            case 1: return { centre + s, -r.height / 2.0 + d };
            case 2: return { -r.width / 2.0 + d, centre + s };
            default: return { r.width / 2.0 - d, centre + s };
        }
    };
    std::vector<Pt> pts; for (const auto& q : local) pts.push_back(map(q[0], q[1]));
    floorA = map(f, r.depth); floorB = map(-f, r.depth);
    return pts;
}

TopoDS_Face polygonFace(const std::vector<Pt>& pts, double x = 0.0)
{
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto& p : pts) polygon.Add(gp_Pnt(x, p.y, p.z));
    polygon.Close();
    return BRepBuilderAPI_MakeFace(polygon.Wire(), true).Face();
}

struct Cutter { TopoDS_Face face; juce::String token; Pt floorA, floorB; };
struct Plan { std::vector<Piece> pieces; double pieceLength = 0, overlapUsed = 0, windowLength = 0, segmentMin = 0; int sections = 1; };

std::vector<Cutter> cutters(const Profile& r, double x = 0.0)
{
    std::vector<Cutter> out;
    for (int face = 0; face < 4; ++face)
        for (int i = 0; i < r.count(face); ++i)
        {
            Cutter c; c.token = juce::String("slot:") + faceName(face) + ":" + juce::String(i);
            c.face = polygonFace(slotPolygon(r, face, i, c.floorA, c.floorB), x);
            out.push_back(c);
        }
    return out;
}

// The material between any two cavities must stay at least minWeb thick (this is what protects corners and cores).
bool checkWebs(const Profile& r, juce::String& error)
{
    const auto list = cutters(r);
    for (size_t a = 0; a < list.size(); ++a)
        for (size_t b = a + 1; b < list.size(); ++b)
        {
            BRepExtrema_DistShapeShape distance(list[a].face, list[b].face);
            distance.Perform();
            if (!distance.IsDone()) { error = "The slot clearance could not be evaluated."; return false; }
            if (distance.Value() < minWeb - 1.0e-6)
            { error = "The webs between " + list[a].token + " and " + list[b].token + " would be " + mm(distance.Value()) + " thick (minimum " + mm(minWeb) + "). Reduce the slot depth or head width, or remove a slot."; return false; }
        }
    return true;
}

TopoDS_Face profileFace(const Profile& r, juce::String& error, double x = 0.0)
{
    const double hw = r.width / 2.0, hh = r.height / 2.0;
    TopoDS_Shape shape = polygonFace({ { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh } }, x);
    TopTools_ListOfShape tools;
    for (const auto& c : cutters(r, x)) tools.Append(c.face);
    if (tools.Extent() > 0)
    {
        TopTools_ListOfShape arguments; arguments.Append(shape);
        BRepAlgoAPI_Cut cut; cut.SetArguments(arguments); cut.SetTools(tools); cut.Build();
        if (!cut.IsDone() || cut.Shape().IsNull()) { error = "The rail cross-section could not be constructed (slot cut failed)."; return {}; }
        shape = cut.Shape();
    }
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) return TopoDS::Face(ex.Current());
    error = "The rail cross-section is empty."; return {};
}

// ---- Two-half construction --------------------------------------------------------------------------------------
// The rail is split on the z = 0 plane: the top half carries the top slots and the snap arms, the bottom half carries the
// bottom slots and the matching sockets. Printed with the slotted outer face on the bed, the split face is up, the slots
// need no support (45 degree walls and a short floor bridge) and every arm stands in the layer planes.
double windowLength(const Profile& r) { return r.armLength + r.slit; }

bool makePlan(const Profile& r, Plan& plan, juce::String& error)
{
    plan = {};
    const double L = r.length, win = windowLength(r), segMin = win + 2.0 * r.endMargin;
    plan.windowLength = win; plan.segmentMin = segMin;
    int N = 1;
    if (L > r.printable + 1.0e-9) N = (int) std::ceil(L / (r.printable - r.overlap) - 1.0e-9);
    const double Lp = L / N;
    plan.sections = N; plan.pieceLength = Lp; plan.overlapUsed = N > 1 ? r.overlap : 0.0;
    if (N > 1 && (r.overlap < segMin - 1.0e-9 || Lp - r.overlap < segMin - 1.0e-9))
    {
        error = "The staggered joints leave overlap segments shorter than the " + mm(segMin) + " a snap arm needs (arm " + mm(win) + " plus two end margins). Increase overlapMm (now " + mm(r.overlap)
              + ") or printableLengthMm, or shorten the snap arm. The pieces would be " + mm(Lp) + " long.";
        return false;
    }
    if (Lp < segMin - 1.0e-9) { error = "The rail is too short for a snap arm: it needs at least " + mm(segMin) + "."; return false; }
    std::vector<double> topBreaks { 0.0 }, bottomBreaks { 0.0 };
    for (int k = 1; k < N; ++k) { topBreaks.push_back(k * Lp); bottomBreaks.push_back(k * Lp - r.overlap); }
    topBreaks.push_back(L); bottomBreaks.push_back(L);
    for (size_t i = 0; i + 1 < topBreaks.size(); ++i) { Piece p; p.half = "top"; p.index = (int) i; p.x0 = topBreaks[i]; p.x1 = topBreaks[i + 1]; plan.pieces.push_back(p); }
    for (size_t i = 0; i + 1 < bottomBreaks.size(); ++i) { Piece p; p.half = "bottom"; p.index = (int) i; p.x0 = bottomBreaks[i]; p.x1 = bottomBreaks[i + 1]; plan.pieces.push_back(p); }
    // Snap windows: in every overlap segment between a top and a bottom piece, evenly spaced and centred.
    for (auto& a : plan.pieces)
    {
        if (a.half != "top") continue;
        for (auto& b : plan.pieces)
        {
            if (b.half != "bottom") continue;
            const double u = std::max(a.x0, b.x0), v = std::min(a.x1, b.x1);
            if (v - u < 1.0e-6) continue;
            const double lo = u + r.endMargin, hi = v - r.endMargin, usable = hi - lo;
            if (usable < win - 1.0e-9) { error = "An overlap segment of " + mm(v - u) + " cannot hold a snap arm (needs " + mm(segMin) + ")."; return false; }
            const int n = (int) std::floor((usable - win) / r.pinSpacing + 1.0e-9) + 1;
            const double span = (n - 1) * r.pinSpacing + win, first = lo + (usable - span) / 2.0;
            for (int k = 0; k < n; ++k) { const double s0 = first + k * r.pinSpacing; a.windows.push_back(s0); b.windows.push_back(s0); }
        }
    }
    return true;
}

double sideRowY(const Profile& r) { return (r.head / 2.0 + r.width / 2.0) / 2.0; }

// Arm window for one side row: two slits and the end gap, cut through the full height of the top half.
TopoDS_Shape windowCutter(const Profile& r, double s0, double rowY)
{
    const double x0 = s0, x1 = s0 + r.armLength + r.slit, h = r.height / 2.0 + 2.0, aw = r.armWidth / 2.0;
    BRepAlgoAPI_Fuse f1(BRepPrimAPI_MakeBox(gp_Pnt(x0, rowY - aw - r.slit, -1.0), x1 - x0, r.slit, h).Shape(), BRepPrimAPI_MakeBox(gp_Pnt(x0, rowY + aw, -1.0), x1 - x0, r.slit, h).Shape());
    BRepAlgoAPI_Fuse f2(f1.Shape(), BRepPrimAPI_MakeBox(gp_Pnt(s0 + r.armLength, rowY - aw, -1.0), r.slit, 2.0 * aw, h).Shape());
    return f2.Shape();
}

// Hook on the arm tip protruding below the split face (z < 0): a neck, then a barb that sticks out toward the rail centre.
TopoDS_Shape hookShape(const Profile& r, double s0, double rowY)
{
    const double tip = s0 + r.armLength, aw = r.armWidth / 2.0, e = r.hookHeight / 2.0, hh = r.hookHeight, lip = r.snapLip;
    const double sgn = rowY >= 0.0 ? 1.0 : -1.0;
    const double yo = sgn * (std::abs(rowY) + aw), yi = sgn * (std::abs(rowY) - aw), tipY = yi - sgn * lip, ramp = std::min(0.7, lip);
    const std::vector<Pt> pts { { yi, 0.1 }, { yo, 0.1 }, { yo, -hh }, { tipY + sgn * ramp, -hh }, { tipY, -hh + ramp }, { tipY, -e }, { yi, -e } };
    return BRepPrimAPI_MakePrism(polygonFace(pts, tip - r.hookLength), gp_Vec(r.hookLength, 0, 0)).Shape();
}

// Socket pocket in the bottom half: a mouth through the ledge and a wider chamber below it (the barb snaps into the chamber).
TopoDS_Shape socketCutter(const Profile& r, double s0, double rowY)
{
    const double tip = s0 + r.armLength, aw = r.armWidth / 2.0, e = r.hookHeight / 2.0, hh = r.hookHeight, c = r.clearance, lip = r.snapLip;
    const double sgn = rowY >= 0.0 ? 1.0 : -1.0;
    const double yo = std::abs(rowY) + aw + c + lip, yi = std::abs(rowY) - aw - c, yiChamber = std::abs(rowY) - aw - lip - c;   // the mouth reaches lip beyond the arm so the arm can deflect outward while the barb passes
    const double xa = tip - r.hookLength - c, len = r.hookLength + 2.0 * c;
    const auto box = [&](double ya, double yb, double za, double zb)
    {
        const double y0 = sgn > 0 ? ya : -yb, y1 = sgn > 0 ? yb : -ya;
        return BRepPrimAPI_MakeBox(gp_Pnt(xa, y0, za), len, y1 - y0, zb - za).Shape();
    };
    BRepAlgoAPI_Fuse fused(box(yi, yo, -(e - c), 0.2), box(yiChamber, yo, -(hh + c), -(e - c)));
    return fused.Shape();
}

TopoDS_Face halfFace(const Profile& r, bool top, double x, juce::String& error)
{
    const auto profile = profileFace(r, error, x);
    if (profile.IsNull()) return {};
    const double hw = r.width / 2.0 + 1.0, hh = r.height / 2.0 + 1.0;
    const auto rect = top ? polygonFace({ { -hw, 0.0 }, { hw, 0.0 }, { hw, hh }, { -hw, hh } }, x) : polygonFace({ { -hw, -hh }, { hw, -hh }, { hw, 0.0 }, { -hw, 0.0 } }, x);
    BRepAlgoAPI_Common common(profile, rect);
    if (!common.IsDone() || common.Shape().IsNull()) { error = "The rail half cross-section could not be constructed."; return {}; }
    for (TopExp_Explorer ex(common.Shape(), TopAbs_FACE); ex.More(); ex.Next()) return TopoDS::Face(ex.Current());
    error = "The rail half cross-section is empty."; return {};
}

TopoDS_Shape buildHalf(const Profile& r, juce::String& error, FaceNames& names, const juce::String& owner)
{
    const bool top = r.half == "top";
    const auto face = halfFace(r, top, r.x0, error);
    if (face.IsNull()) return {};
    BRepPrimAPI_MakePrism prism(face, gp_Vec(r.x1 - r.x0, 0, 0));
    if (!prism.IsDone() || prism.Shape().IsNull()) { error = "The rail half could not be extruded."; return {}; }
    names.push_back({ prism.FirstShape(), { owner + "/start" } });
    names.push_back({ prism.LastShape(), { owner + "/end" } });
    TopoDS_Shape shape = prism.Shape();
    const double row = sideRowY(r);
    for (const double s0 : r.windows)
        for (const double rowY : { -row, row })
        {
            if (top)
            {
                BRepAlgoAPI_Cut cut(shape, windowCutter(r, s0, rowY));
                if (!cut.IsDone() || cut.Shape().IsNull()) { error = "A snap-arm window could not be cut."; return {}; }
                shape = cut.Shape();
                BRepAlgoAPI_Fuse fuse(shape, hookShape(r, s0, rowY));
                if (!fuse.IsDone() || fuse.Shape().IsNull()) { error = "A snap hook could not be added."; return {}; }
                shape = fuse.Shape();
            }
            else
            {
                BRepAlgoAPI_Cut cut(shape, socketCutter(r, s0, rowY));
                if (!cut.IsDone() || cut.Shape().IsNull()) { error = "A snap socket could not be cut."; return {}; }
                shape = cut.Shape();
            }
        }
    return shape;
}
}

juce::var defaults(const juce::String& type)
{
    auto p = object(); auto* o = p.getDynamicObject();
    o->setProperty("widthMm", 20.0); o->setProperty("heightMm", 20.0); o->setProperty("lengthMm", 200.0);
    o->setProperty("slotsTop", 1); o->setProperty("slotsBottom", 1); o->setProperty("slotsLeft", 1); o->setProperty("slotsRight", 1);
    o->setProperty("slotOpeningMm", 6.0); o->setProperty("slotLipMm", 1.8); o->setProperty("slotHeadWidthMm", 10.0); o->setProperty("slotHeadStraightMm", 1.5); o->setProperty("slotDepthMm", 6.0);
    o->setProperty("material", ""); o->setProperty("construction", "onePiece");
    o->setProperty("printableLengthMm", 200.0); o->setProperty("overlapMm", 40.0); o->setProperty("snapPinSpacingMm", 30.0); o->setProperty("snapArmLengthMm", 14.0);
    o->setProperty("snapArmWidthMm", 2.0); o->setProperty("snapSlitMm", 0.8); o->setProperty("snapHookHeightMm", 2.0); o->setProperty("snapLipMm", 0.7);
    o->setProperty("snapHookLengthMm", 4.0); o->setProperty("assemblyClearanceMm", 0.25); o->setProperty("snapEndMarginMm", 6.0);
    if (type == "tslot_half") { o->setProperty("half", "top"); o->setProperty("x0Mm", 0.0); o->setProperty("x1Mm", 100.0); o->setProperty("windows", juce::Array<juce::var>()); o->setProperty("rail", ""); o->setProperty("index", 0); }
    return p;
}

bool validate(const juce::String& type, const juce::var& p, juce::String& error)
{
    if (type != "tslot_rail" && type != "tslot_half") { error = "Unknown T-slot feature type."; return false; }
    Profile r;
    const bool piece = type == "tslot_half";
    if (!readProfile(p, r, error, piece)) return false;
    if (!checkWebs(r, error)) return false;
    if (piece && r.construction != "twoHalf") { error = "A rail half needs construction twoHalf."; return false; }
    if (r.construction == "twoHalf" && !piece) { Plan plan; if (!makePlan(r, plan, error)) return false; }
    return true;
}

TopoDS_Shape build(const juce::String& type, const juce::var& p, const juce::String& owner, FaceNames& names, juce::String& error)
{
    if (!validate(type, p, error)) return {};
    names.clear();
    try
    {
        Profile r; if (!readProfile(p, r, error, type == "tslot_half")) return {};
        if (type == "tslot_half")
        {
            const auto shape = buildHalf(r, error, names, owner);
            if (shape.IsNull()) return {};
            if (!BRepCheck_Analyzer(shape, true).IsValid()) { error = "The rail half produced an invalid solid."; return {}; }
            return shape;
        }
        const auto face = profileFace(r, error);
        if (face.IsNull()) return {};
        BRepPrimAPI_MakePrism prism(face, gp_Vec(r.length, 0, 0));
        if (!prism.IsDone() || prism.Shape().IsNull()) { error = "The rail could not be extruded."; return {}; }
        names.push_back({ prism.FirstShape(), { owner + "/start" } });
        names.push_back({ prism.LastShape(), { owner + "/end" } });
        // Slot floors: the lateral face generated by the profile edge that lies on each cavity's floor segment.
        const auto cavities = cutters(r);
        for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next())
        {
            BRepAdaptor_Curve curve(TopoDS::Edge(ex.Current()));
            const auto a = curve.Value(curve.FirstParameter()), b = curve.Value(curve.LastParameter());
            for (const auto& c : cavities)
            {
                const bool match = (std::abs(a.Y() - c.floorA.y) < 1e-6 && std::abs(a.Z() - c.floorA.z) < 1e-6 && std::abs(b.Y() - c.floorB.y) < 1e-6 && std::abs(b.Z() - c.floorB.z) < 1e-6)
                                || (std::abs(a.Y() - c.floorB.y) < 1e-6 && std::abs(a.Z() - c.floorB.z) < 1e-6 && std::abs(b.Y() - c.floorA.y) < 1e-6 && std::abs(b.Z() - c.floorA.z) < 1e-6);
                if (!match) continue;
                for (TopTools_ListIteratorOfListOfShape it(prism.Generated(ex.Current())); it.More(); it.Next())
                    if (it.Value().ShapeType() == TopAbs_FACE) names.push_back({ it.Value(), { owner + "/" + c.token + "/floor" } });
            }
        }
        if (!BRepCheck_Analyzer(prism.Shape(), true).IsValid()) { error = "The rail produced an invalid solid."; return {}; }
        return prism.Shape();
    }
    catch (const Standard_Failure& failure) { error = juce::String("T-slot rail construction failed: ") + failure.GetMessageString(); return {}; }
}

juce::var sectionProperties(const juce::var& p, juce::String& error)
{
    Profile r;
    if (!readProfile(p, r, error) || !checkWebs(r, error)) return {};
    try
    {
        const auto face = profileFace(r, error);
        if (face.IsNull()) return {};
        GProp_GProps props; BRepGProp::SurfaceProperties(face, props);
        const auto centroid = props.CentreOfMass(); const auto m = props.MatrixOfInertia();
        auto out = object(); auto* o = out.getDynamicObject();
        o->setProperty("areaMm2", props.Mass());
        o->setProperty("centroidYMm", centroid.Y()); o->setProperty("centroidZMm", centroid.Z());
        o->setProperty("secondMomentAboutYMm4", m(2, 2));   // integral of z^2 dA: bending about the Y axis (loads along Z)
        o->setProperty("secondMomentAboutZMm4", m(3, 3));   // integral of y^2 dA: bending about the Z axis (loads along Y)
        o->setProperty("polarMomentMm4", m(1, 1));          // planar polar second moment (not the torsion constant)
        o->setProperty("solidAreaOfEnvelopeMm2", r.width * r.height);
        o->setProperty("note", "Computed from the actual cross-section. The polar second moment is not a torsion constant for this open profile.");
        return out;
    }
    catch (const Standard_Failure& failure) { error = juce::String("Section properties failed: ") + failure.GetMessageString(); return {}; }
}

bool planPieces(const juce::var& p, std::vector<Piece>& pieces, juce::var& summary, juce::String& error)
{
    Profile r;
    if (!readProfile(p, r, error)) return false;
    if (r.construction != "twoHalf") { error = "construction is not twoHalf."; return false; }
    Plan plan;
    if (!makePlan(r, plan, error)) return false;
    pieces = plan.pieces;
    auto out = object(); auto* o = out.getDynamicObject();
    o->setProperty("sections", plan.sections); o->setProperty("pieceLengthMm", plan.pieceLength); o->setProperty("overlapUsedMm", plan.overlapUsed);
    o->setProperty("snapWindowLengthMm", plan.windowLength); o->setProperty("minimumOverlapSegmentMm", plan.segmentMin);
    juce::Array<juce::var> list;
    for (const auto& q : plan.pieces)
    {
        auto e = object(); auto* eo = e.getDynamicObject();
        eo->setProperty("half", q.half); eo->setProperty("index", q.index); eo->setProperty("x0Mm", q.x0); eo->setProperty("x1Mm", q.x1); eo->setProperty("lengthMm", q.x1 - q.x0);
        eo->setProperty("snapArms", (int) q.windows.size()); list.add(e);
    }
    o->setProperty("pieces", list);
    summary = out;
    return true;
}

juce::var pieceParameters(const juce::var& rail, const Piece& piece, const juce::String& railId)
{
    auto p = juce::JSON::fromString(juce::JSON::toString(rail, false, 17));
    auto* o = p.getDynamicObject();
    o->setProperty("half", piece.half); o->setProperty("x0Mm", piece.x0); o->setProperty("x1Mm", piece.x1); o->setProperty("index", piece.index); o->setProperty("rail", railId);
    juce::Array<juce::var> w; for (const auto v : piece.windows) w.add(v);
    o->setProperty("windows", w);
    return p;
}

juce::var standard()
{
    auto r = object(); auto* o = r.getDynamicObject();
    o->setProperty("frame", "Rail along +X from x = 0 to lengthMm. Cross-section centred on the axis (y = z = 0): width along Y, height along Z; top +Z, bottom -Z, left -Y, right +Y.");
    o->setProperty("slotGeometry", "Each T-slot: opening width, lip thickness, head width (straight for slotHeadStraightMm, then 45 degree walls) down to a flat floor at slotDepthMm. Slots on a face are spaced evenly; each cavity keeps at least 1.6 mm of material from every other cavity and from the outside.");
    auto ranges = object(); auto* g = ranges.getDynamicObject();
    g->setProperty("widthMm", "10 to 200"); g->setProperty("heightMm", "10 to 200"); g->setProperty("lengthMm", "10 to 3000"); g->setProperty("slotsPerFace", "0 to 8 (limited by the geometry)");
    o->setProperty("ranges", ranges);
    o->setProperty("material", "Optional document material id (mat-N) from the material system; assigned to the rail. Materials are isotropic: directional FDM properties are not representable.");
    o->setProperty("twoHalf", "construction twoHalf (slots on top and bottom only) splits the rail on z = 0 into a top half (snap arms) and a bottom half (sockets), printed with the slotted face on the bed and the split face up. Rails longer than printableLengthMm are made of staggered pieces: top-half joints at multiples of the piece length, bottom-half joints overlapMm earlier, which also yields a short filler at one end. Snap arms lie in the layer planes and bend sideways; the barb snaps under a ledge in the socket.");
    o->setProperty("status", "Original design, not copied from any manufacturer's profile. Strength is not validated: no analysis has been run on this geometry.");
    return r;
}
}
