#include "CadCoilWinder.h"
#include "CadComponents.h"
#include "CadMechanics.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <algorithm>

namespace djehuti::cad::coilwinder
{

namespace
{
    // Helper to fuse safely
    TopoDS_Shape safeFuse(const TopoDS_Shape& a, const TopoDS_Shape& b)
    {
        if (a.IsNull()) return b;
        if (b.IsNull()) return a;
        try
        {
            BRepAlgoAPI_Fuse f(a, b);
            f.Build();
            if (f.IsDone() && !f.Shape().IsNull()) return f.Shape();
        }
        catch (...) {}
        return a;
    }

    // Helper to cut safely
    TopoDS_Shape safeCut(const TopoDS_Shape& target, const TopoDS_Shape& tool)
    {
        if (target.IsNull() || tool.IsNull()) return target;
        try
        {
            BRepAlgoAPI_Cut c(target, tool);
            c.Build();
            if (c.IsDone() && !c.Shape().IsNull()) return c.Shape();
        }
        catch (...) {}
        return target;
    }

    // Helper to translate a shape
    TopoDS_Shape translate(const TopoDS_Shape& shape, double dx, double dy, double dz)
    {
        gp_Trsf t;
        t.SetTranslation(gp_Vec(dx, dy, dz));
        return BRepBuilderAPI_Transform(shape, t, true).Shape();
    }

    // Helper to rotate a shape around an axis
    TopoDS_Shape rotate(const TopoDS_Shape& shape, const gp_Ax1& axis, double angleRad)
    {
        gp_Trsf t;
        t.SetRotation(axis, angleRad);
        return BRepBuilderAPI_Transform(shape, t, true).Shape();
    }
}

namespace parts
{

// ---------------------------------------------------------------------------------------------------------------
// Family 1: Modular Frame
// ---------------------------------------------------------------------------------------------------------------
TopoDS_Shape makeBasePlateLeft(const CoilWinderParameters& p)
{
    // Main base body: 88 x 88 x 12 mm
    TopoDS_Shape base = BRepPrimAPI_MakeBox(p.frameBaseLengthMm, p.frameBaseWidthMm, p.frameBaseThicknessMm).Shape();

    // Interlocking dovetail / tongue tabs protruding along X = 88 mm (right edge)
    // Tab 1 (Front): X from 88 to 96 mm, Y from 14 to 34 mm, Z from 2 to 10 mm
    const TopoDS_Shape tab1 = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(p.frameBaseLengthMm, 14.0, 2.0), gp_Dir(0, 0, 1)),
                                                 8.0, 20.0, 8.0).Shape();
    // Tab 2 (Rear): X from 88 to 96 mm, Y from 54 to 74 mm, Z from 2 to 10 mm
    const TopoDS_Shape tab2 = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(p.frameBaseLengthMm, 54.0, 2.0), gp_Dir(0, 0, 1)),
                                                 8.0, 20.0, 8.0).Shape();
    base = safeFuse(base, tab1);
    base = safeFuse(base, tab2);

    // Cross-pin locking holes (6.2 mm diameter, centered at X = 92 mm, passing through tabs)
    const TopoDS_Shape pinHole1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(92.0, 10.0, 6.0), gp_Dir(0, 1, 0)), 3.1, 28.0).Shape();
    const TopoDS_Shape pinHole2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(92.0, 50.0, 6.0), gp_Dir(0, 1, 0)), 3.1, 28.0).Shape();
    base = safeCut(base, pinHole1);
    base = safeCut(base, pinHole2);

    // Upright mounting mortise slot on top face: X from 12 to 28 mm (16 mm wide), Y from 9 to 79 mm (70 mm long), depth 6 mm
    const TopoDS_Shape mortise = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(12.0, 9.0, p.frameBaseThicknessMm - 6.0), gp_Dir(0, 0, 1)),
                                                    16.0, 70.0, 6.5).Shape();
    base = safeCut(base, mortise);

    // Bench mounting screw holes (5 mm diameter) in four corners
    const double corners[4][2] = { {8.0, 8.0}, {8.0, 80.0}, {76.0, 8.0}, {76.0, 80.0} };
    for (int i = 0; i < 4; ++i)
    {
        const TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(corners[i][0], corners[i][1], -0.5), gp_Dir(0, 0, 1)), 2.6, 13.0).Shape();
        base = safeCut(base, hole);
    }

    return base;
}

TopoDS_Shape makeBasePlateRight(const CoilWinderParameters& p)
{
    // Main base body: 88 x 88 x 12 mm
    TopoDS_Shape base = BRepPrimAPI_MakeBox(p.frameBaseLengthMm, p.frameBaseWidthMm, p.frameBaseThicknessMm).Shape();

    // Mating pockets for the tabs of BasePlateLeft on left edge (X = 0) with 0.15 mm clearance
    const double cl = p.pushFitClearanceMm;
    const TopoDS_Shape pocket1 = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-0.5, 14.0 - cl, 2.0 - cl), gp_Dir(0, 0, 1)),
                                                    8.5 + cl, 20.0 + 2.0 * cl, 8.0 + 2.0 * cl).Shape();
    const TopoDS_Shape pocket2 = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-0.5, 54.0 - cl, 2.0 - cl), gp_Dir(0, 0, 1)),
                                                    8.5 + cl, 20.0 + 2.0 * cl, 8.0 + 2.0 * cl).Shape();
    base = safeCut(base, pocket1);
    base = safeCut(base, pocket2);

    // Cross-pin holes aligning with Left Base (centered at X = 4 mm relative to right base)
    const TopoDS_Shape pinHole1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(4.0, 10.0, 6.0), gp_Dir(0, 1, 0)), 3.1, 28.0).Shape();
    const TopoDS_Shape pinHole2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(4.0, 50.0, 6.0), gp_Dir(0, 1, 0)), 3.1, 28.0).Shape();
    base = safeCut(base, pinHole1);
    base = safeCut(base, pinHole2);

    // Upright mounting mortise slot for Spindle Right Support (at X = 20 mm)
    const TopoDS_Shape mortise = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(20.0, 9.0, p.frameBaseThicknessMm - 6.0), gp_Dir(0, 0, 1)),
                                                    16.0, 70.0, 6.5).Shape();
    base = safeCut(base, mortise);

    // Traverse End Bracket socket (at X = 70 mm)
    const TopoDS_Shape travSocket = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(68.0, 15.0, p.frameBaseThicknessMm - 6.0), gp_Dir(0, 0, 1)),
                                                       16.0, 45.0, 6.5).Shape();
    base = safeCut(base, travSocket);

    // Bench mounting holes
    const double corners[2][2] = { {76.0, 8.0}, {76.0, 80.0} };
    for (int i = 0; i < 2; ++i)
    {
        const TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(corners[i][0], corners[i][1], -0.5), gp_Dir(0, 0, 1)), 2.6, 13.0).Shape();
        base = safeCut(base, hole);
    }

    return base;
}

TopoDS_Shape makeFrameInterlockPin(const CoilWinderParameters& p)
{
    // Stepped pin: shaft 6.0 mm diam x 26 mm length + knurled head 10.0 mm diam x 4 mm length
    // Fits into 6.2 mm cross-pin holes
    TopoDS_Shape pin = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 3.0, 26.0).Shape();
    const TopoDS_Shape head = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 26.0), gp_Dir(0, 0, 1)), 5.0, 4.0).Shape();
    return safeFuse(pin, head);
}

TopoDS_Shape makeMainUprightLeft(const CoilWinderParameters& p)
{
    // Base Tenon: 15.7 x 69.7 x 6.0 mm (fits into 16.0 x 70.0 mm base mortise)
    TopoDS_Shape tenon = BRepPrimAPI_MakeBox(15.7, 69.7, 6.0).Shape();

    // Main Column: width (Y) = 86 mm, thickness (X) = 16 mm, height (Z) = 90 mm (total Z = 96 mm)
    TopoDS_Shape column = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(0, -8.15, 6.0), gp_Dir(0, 0, 1)), 15.7, 86.0, 90.0).Shape();
    TopoDS_Shape upright = safeFuse(tenon, column);

    // Lower bearing bore (Input shaft): diameter 15.0 mm at Y = 34.85 mm, Z = 32.0 mm
    const TopoDS_Shape lowerBore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 34.85, 32.0), gp_Dir(1, 0, 0)), 7.5, 18.0).Shape();
    upright = safeCut(upright, lowerBore);

    // Upper bearing bore (Spindle shaft): diameter 17.0 mm at Y = 34.85 mm, Z = 32.0 + 54.0 = 86.0 mm
    // Center distance = 54.0 mm exact
    const TopoDS_Shape upperBore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 34.85, 86.0), gp_Dir(1, 0, 0)), 8.5, 18.0).Shape();
    upright = safeCut(upright, upperBore);

    // Counter shaft support socket: diameter 6.2 mm at Y = 68.0 mm, Z = 60.0 mm
    const TopoDS_Shape counterBore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 68.0, 60.0), gp_Dir(1, 0, 0)), 3.1, 12.0).Shape();
    upright = safeCut(upright, counterBore);

    // Trip lever pivot trunnion hole: diameter 4.2 mm at Y = 10.0 mm, Z = 65.0 mm
    const TopoDS_Shape tripBore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 10.0, 65.0), gp_Dir(1, 0, 0)), 2.1, 18.0).Shape();
    upright = safeCut(upright, tripBore);

    // Lightening / mass reduction cutouts in column web
    const TopoDS_Shape cutout = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-1.0, 2.0, 12.0), gp_Dir(0, 0, 1)), 18.0, 18.0, 35.0).Shape();
    upright = safeCut(upright, cutout);

    return upright;
}

TopoDS_Shape makeMainUprightRight(const CoilWinderParameters& p)
{
    // Right upright supports spindle tail end, traverse lead-screw drive, and guide rails
    TopoDS_Shape tenon = BRepPrimAPI_MakeBox(15.7, 69.7, 6.0).Shape();
    TopoDS_Shape column = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(0, -8.15, 6.0), gp_Dir(0, 0, 1)), 15.7, 86.0, 90.0).Shape();
    TopoDS_Shape upright = safeFuse(tenon, column);

    // Spindle through-bearing bore: diameter 17.0 mm at Y = 34.85 mm, Z = 86.0 mm
    const TopoDS_Shape spindleBore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 34.85, 86.0), gp_Dir(1, 0, 0)), 8.5, 18.0).Shape();
    upright = safeCut(upright, spindleBore);

    // Lead-screw bearing bore: diameter 14.2 mm at Y = 10.0 mm, Z = 45.0 mm
    const TopoDS_Shape leadScrewBore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 10.0, 45.0), gp_Dir(1, 0, 0)), 7.1, 18.0).Shape();
    upright = safeCut(upright, leadScrewBore);

    // Guide rail socket 1: diameter 8.1 mm at Y = 10.0 mm, Z = 65.0 mm, depth 10 mm
    const TopoDS_Shape railSocket1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(6.0, 10.0, 65.0), gp_Dir(1, 0, 0)), 4.05, 11.0).Shape();
    upright = safeCut(upright, railSocket1);

    // Guide rail socket 2: diameter 8.1 mm at Y = 10.0 mm, Z = 25.0 mm, depth 10 mm
    const TopoDS_Shape railSocket2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(6.0, 10.0, 25.0), gp_Dir(1, 0, 0)), 4.05, 11.0).Shape();
    upright = safeCut(upright, railSocket2);

    return upright;
}

TopoDS_Shape makeTraverseEndBracket(const CoilWinderParameters& p)
{
    // Outboard support for traverse lead screw and rails: 15.7 x 44.0 x 70.0 mm
    TopoDS_Shape tenon = BRepPrimAPI_MakeBox(15.7, 44.0, 6.0).Shape();
    TopoDS_Shape column = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(0, 0, 6.0), gp_Dir(0, 0, 1)), 15.7, 44.0, 64.0).Shape();
    TopoDS_Shape bracket = safeFuse(tenon, column);

    // Lead screw tail bearing bore: diameter 10.2 mm at Y = 22.0 mm, Z = 45.0 mm
    const TopoDS_Shape leadTail = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 22.0, 45.0), gp_Dir(1, 0, 0)), 5.1, 18.0).Shape();
    bracket = safeCut(bracket, leadTail);

    // Guide rail blind sockets (diameter 8.1 mm) at Z = 65.0 mm and Z = 25.0 mm
    const TopoDS_Shape rail1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 22.0, 65.0), gp_Dir(1, 0, 0)), 4.05, 11.0).Shape();
    const TopoDS_Shape rail2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 22.0, 25.0), gp_Dir(1, 0, 0)), 4.05, 11.0).Shape();
    bracket = safeCut(bracket, rail1);
    bracket = safeCut(bracket, rail2);

    return bracket;
}

// ---------------------------------------------------------------------------------------------------------------
// Family 2: Hand Crank, Shafts, Bushings & Retainers
// ---------------------------------------------------------------------------------------------------------------
TopoDS_Shape makeHandCrankArm(const CoilWinderParameters& p)
{
    // Center-to-center length: 75.0 mm. Total length: 94.0 mm <= 100 mm!
    // Shaft hub: diameter 22 mm, thickness 12 mm at origin
    TopoDS_Shape hub = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 11.0, 12.0).Shape();

    // Handle boss: diameter 18 mm, thickness 12 mm at X = 75 mm
    TopoDS_Shape boss = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(75.0, 0, 0), gp_Dir(0, 0, 1)), 9.0, 12.0).Shape();

    // Connecting beam: width (Y) = 14 mm, thickness (Z) = 8 mm
    TopoDS_Shape beam = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(0, -7.0, 2.0), gp_Dir(0, 0, 1)), 75.0, 14.0, 8.0).Shape();

    TopoDS_Shape arm = safeFuse(hub, boss);
    arm = safeFuse(arm, beam);

    // Shaft bore with keyway (10 mm diameter + 3x3 keyway)
    TopoDS_Shape shaftHole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 5.05, 14.0).Shape();
    TopoDS_Shape keyway = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-1.5, 4.0, -1.0), gp_Dir(0, 0, 1)), 3.0, 3.0, 14.0).Shape();
    arm = safeCut(arm, shaftHole);
    arm = safeCut(arm, keyway);

    // Handle pivot pin hole (8.1 mm diameter) at X = 75 mm
    TopoDS_Shape handleHole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(75.0, 0, -1.0), gp_Dir(0, 0, 1)), 4.05, 14.0).Shape();
    arm = safeCut(arm, handleHole);

    return arm;
}

TopoDS_Shape makeCrankHandleGrip(const CoilWinderParameters& p)
{
    // Ergonomic revolving handle: 22 mm OD x 55 mm length (fits in 100 mm cube)
    TopoDS_Shape grip = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 11.0, 52.0).Shape();
    // Inner sleeve bore: 8.3 mm diameter
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 4.15, 54.0).Shape();
    // Retaining screw counterbore: 13.0 mm diameter x 6 mm depth
    TopoDS_Shape counterbore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 48.0), gp_Dir(0, 0, 1)), 6.5, 6.0).Shape();

    grip = safeCut(grip, bore);
    grip = safeCut(grip, counterbore);
    return grip;
}

TopoDS_Shape makeInputShaft(const CoilWinderParameters& p)
{
    // Main shaft: diameter 10.0 mm, length 85.0 mm
    TopoDS_Shape shaft = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 5.0, 85.0).Shape();

    // Keyed drive flats / keyway along gear seat (Z = 20 to 45 mm)
    TopoDS_Shape key = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-1.5, 3.5, 20.0), gp_Dir(0, 0, 1)), 3.0, 2.0, 25.0).Shape();
    shaft = safeCut(shaft, key);

    // Retaining snap-ring grooves (9.0 mm diameter, 1.2 mm wide) at Z = 16 mm and Z = 60 mm
    TopoDS_Shape groove1 = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-6.0, -6.0, 16.0), gp_Dir(0, 0, 1)), 12.0, 12.0, 1.2).Shape();
    TopoDS_Shape ring1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 16.0), gp_Dir(0, 0, 1)), 4.5, 1.2).Shape();
    shaft = safeCut(shaft, safeCut(groove1, ring1));

    return shaft;
}

TopoDS_Shape makeSpindleShaft(const CoilWinderParameters& p)
{
    // Spindle Main Shaft: diameter 12.0 mm, length 95.0 mm (fits within 100 mm cube!)
    TopoDS_Shape shaft = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 6.0, 95.0).Shape();

    // Axial spline / sliding keyway for clutch collar: 3.0 mm wide x 2.0 mm deep x 35 mm long
    TopoDS_Shape spline = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-1.5, 4.5, 20.0), gp_Dir(0, 0, 1)), 3.0, 2.5, 35.0).Shape();
    shaft = safeCut(shaft, spline);

    // Stepped arbor end: 8.0 mm diameter x 25.0 mm length at Z = 70 to 95 mm
    TopoDS_Shape arborCut = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-7.0, -7.0, 70.0), gp_Dir(0, 0, 1)), 14.0, 14.0, 26.0).Shape();
    TopoDS_Shape arborCyl = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 70.0), gp_Dir(0, 0, 1)), 4.0, 26.0).Shape();
    shaft = safeCut(shaft, safeCut(arborCut, arborCyl));

    return shaft;
}

TopoDS_Shape makeFlangedBushing(double innerDiam, double length, double flangeDiam, double flangeThick)
{
    const double rIn = innerDiam / 2.0;
    const double rOut = (innerDiam / 2.0) + 2.5; // 2.5 mm wall
    const double rFlange = flangeDiam / 2.0;

    // Body cylinder
    TopoDS_Shape body = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), rOut, length).Shape();
    // Flange
    TopoDS_Shape flange = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), rFlange, flangeThick).Shape();
    TopoDS_Shape bushing = safeFuse(body, flange);

    // Through-bore
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), rIn, length + 2.0).Shape();
    bushing = safeCut(bushing, bore);

    return bushing;
}

TopoDS_Shape makeSnapRetainingCollar(double shaftDiam)
{
    const double rIn = shaftDiam / 2.0;
    const double rOut = rIn + 3.5;
    const double thick = 4.0;

    TopoDS_Shape collar = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), rOut, thick).Shape();
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -0.5), gp_Dir(0, 0, 1)), rIn, thick + 1.0).Shape();
    collar = safeCut(collar, bore);

    // Split opening for snap fit over shaft
    TopoDS_Shape slot = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(- (shaftDiam * 0.4), 0, -0.5), gp_Dir(0, 0, 1)),
                                           shaftDiam * 0.8, rOut + 2.0, thick + 1.0).Shape();
    collar = safeCut(collar, slot);

    return collar;
}

// ---------------------------------------------------------------------------------------------------------------
// Family 3: Bobbin Mounting & Clamping
// ---------------------------------------------------------------------------------------------------------------
TopoDS_Shape makeBobbinArborFaceplate(const CoilWinderParameters& p)
{
    // Drive faceplate: 38.0 mm diameter x 12.0 mm thick, 8.0 mm bore
    TopoDS_Shape disc = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 19.0, 12.0).Shape();
    // Two drive lugs / dogs: 5 x 5 x 6 mm at radius 12 mm
    TopoDS_Shape dog1 = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(9.5, -2.5, 12.0), gp_Dir(0, 0, 1)), 5.0, 5.0, 6.0).Shape();
    TopoDS_Shape dog2 = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-14.5, -2.5, 12.0), gp_Dir(0, 0, 1)), 5.0, 5.0, 6.0).Shape();
    disc = safeFuse(disc, dog1);
    disc = safeFuse(disc, dog2);

    // Spindle through bore (12.0 mm coupling seat)
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 6.05, 20.0).Shape();
    disc = safeCut(disc, bore);

    return disc;
}

TopoDS_Shape makeCenteringCone(const CoilWinderParameters& p)
{
    // Stepped conical centering adapter for bobbins:
    // Taper from 28.0 mm down to 10.0 mm over 22.0 mm length
    TopoDS_Shape cone = BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 14.0, 5.0, 22.0).Shape();
    // Knurled thumb base: 30 mm diameter x 6 mm
    TopoDS_Shape base = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -6.0), gp_Dir(0, 0, 1)), 15.0, 6.0).Shape();
    cone = safeFuse(cone, base);

    // 8.2 mm through bore for arbor spindle
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -7.0), gp_Dir(0, 0, 1)), 4.1, 32.0).Shape();
    cone = safeCut(cone, bore);

    return cone;
}

TopoDS_Shape makeBobbinQuickNut(const CoilWinderParameters& p)
{
    // Quick-action thumb nut: 32 mm OD x 12 mm length
    TopoDS_Shape nut = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 16.0, 12.0).Shape();
    // 8.2 mm bore
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 4.1, 14.0).Shape();
    nut = safeCut(nut, bore);

    // Finger grips (4 cylindrical scallops around rim)
    for (int i = 0; i < 4; ++i)
    {
        const double ang = i * (juce::MathConstants<double>::pi / 2.0);
        const double gx = 16.0 * std::cos(ang);
        const double gy = 16.0 * std::sin(ang);
        TopoDS_Shape grip = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(gx, gy, -1.0), gp_Dir(0, 0, 1)), 4.0, 14.0).Shape();
        nut = safeCut(nut, grip);
    }

    return nut;
}

TopoDS_Shape makeSampleBobbinWorkpiece(const CoilWinderParameters& p)
{
    // External workpiece (sample bobbin): flange OD 44 mm, barrel OD 20 mm, width 32 mm, bore 12 mm
    TopoDS_Shape f1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 22.0, 2.5).Shape();
    TopoDS_Shape core = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 2.5), gp_Dir(0, 0, 1)), 10.0, 27.0).Shape();
    TopoDS_Shape f2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 29.5), gp_Dir(0, 0, 1)), 22.0, 2.5).Shape();

    TopoDS_Shape bobbin = safeFuse(f1, core);
    bobbin = safeFuse(bobbin, f2);

    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 6.0, 34.0).Shape();
    bobbin = safeCut(bobbin, bore);

    return bobbin;
}

// ---------------------------------------------------------------------------------------------------------------
// Family 4: Gear Sets
// ---------------------------------------------------------------------------------------------------------------
TopoDS_Shape makeSpurGear(int teeth, double moduleMm, double faceWidthMm, double boreDiamMm, bool addKeyway)
{
    const double pitchDiam = teeth * moduleMm;
    const double outerDiam = pitchDiam + 2.0 * moduleMm;
    const double rootDiam  = pitchDiam - 2.5 * moduleMm;

    // Blank cylinder
    TopoDS_Shape blank = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), outerDiam / 2.0, faceWidthMm).Shape();

    // Central bore
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), boreDiamMm / 2.0, faceWidthMm + 2.0).Shape();
    blank = safeCut(blank, bore);

    if (addKeyway)
    {
        TopoDS_Shape key = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-1.5, (boreDiamMm / 2.0) - 0.5, -1.0), gp_Dir(0, 0, 1)), 3.0, 2.5, faceWidthMm + 2.0).Shape();
        blank = safeCut(blank, key);
    }

    // Tooth gaps: subtract cutter wedges around the perimeter
    const double dAngle = (2.0 * juce::MathConstants<double>::pi) / (double) teeth;
    const double cutterWidth = (pitchDiam * std::sin(dAngle / 4.0));
    for (int i = 0; i < teeth; ++i)
    {
        const double ang = i * dAngle;
        const double rMid = (outerDiam + rootDiam) / 4.0;
        const double cx = rMid * std::cos(ang);
        const double cy = rMid * std::sin(ang);

        // Cutter wedge
        TopoDS_Shape gap = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-cutterWidth / 2.0, -cutterWidth / 2.0, -0.5), gp_Dir(0, 0, 1)),
                                              cutterWidth, (outerDiam - rootDiam) / 2.0 + 1.0, faceWidthMm + 1.0).Shape();
        gap = rotate(gap, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), ang - (juce::MathConstants<double>::pi / 2.0));
        gap = translate(gap, (rootDiam / 2.0) * std::cos(ang), (rootDiam / 2.0) * std::sin(ang), 0.0);
        blank = safeCut(blank, gap);
    }

    // Weight reduction pockets for larger gears (teeth >= 36)
    if (teeth >= 36)
    {
        const double hubDiam = boreDiamMm + 10.0;
        const double rimInDiam = rootDiam - 8.0;
        if (rimInDiam > hubDiam + 8.0)
        {
            const int numSpokes = 4;
            for (int s = 0; s < numSpokes; ++s)
            {
                const double sAng = s * (2.0 * juce::MathConstants<double>::pi / (double) numSpokes) + (dAngle / 2.0);
                const double pr = (hubDiam + rimInDiam) / 4.0;
                const double px = pr * std::cos(sAng);
                const double py = pr * std::sin(sAng);
                const double holeR = (rimInDiam - hubDiam) / 4.5;
                if (holeR > 2.0)
                {
                    TopoDS_Shape spokeHole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(px, py, -1.0), gp_Dir(0, 0, 1)), holeR, faceWidthMm + 2.0).Shape();
                    blank = safeCut(blank, spokeHole);
                }
            }
        }
    }

    return blank;
}

// ---------------------------------------------------------------------------------------------------------------
// Family 5 & 6: Actual & Preset Decimal Counter Wheels
// ---------------------------------------------------------------------------------------------------------------
TopoDS_Shape makeCounterWheel(int digit, bool hasGenevaPin, const CoilWinderParameters& p)
{
    // Wheel body: 34 mm diameter x 7.5 mm thickness
    TopoDS_Shape wheel = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), p.counterWheelDiameterMm / 2.0, p.counterWheelWidthMm).Shape();

    // 6.1 mm bore for counter shaft
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 3.05, p.counterWheelWidthMm + 2.0).Shape();
    wheel = safeCut(wheel, bore);

    // 10 decimal numeral index notches around periphery (spaced at 36 degrees)
    for (int i = 0; i < 10; ++i)
    {
        const double ang = i * (2.0 * juce::MathConstants<double>::pi / 10.0);
        const double nx = (p.counterWheelDiameterMm / 2.0) * std::cos(ang);
        const double ny = (p.counterWheelDiameterMm / 2.0) * std::sin(ang);
        TopoDS_Shape notch = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(nx - 1.0, ny - 1.0, 1.0), gp_Dir(0, 0, 1)), 2.0, 2.0, 5.5).Shape();
        wheel = safeCut(wheel, notch);
    }

    // Intermittent Geneva carry pin on wheel side face (active during 9 -> 0 rollover)
    if (hasGenevaPin)
    {
        const double pinR = (p.counterWheelDiameterMm / 2.0) - 4.5;
        TopoDS_Shape pin = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(pinR, 0, p.counterWheelWidthMm), gp_Dir(0, 0, 1)), 1.5, 3.5).Shape();
        wheel = safeFuse(wheel, pin);
    }

    return wheel;
}

TopoDS_Shape makeTransferPinion(const CoilWinderParameters& p)
{
    // 6-tooth transfer pinion: diameter 14.0 mm, width 6.0 mm, bore 4.1 mm
    return makeSpurGear(6, 1.5, 6.0, 4.1, false);
}

TopoDS_Shape makePresetWheel(int presetDigit, const CoilWinderParameters& p)
{
    // Preset target wheel: 34 mm diameter x 7.5 mm thickness
    TopoDS_Shape wheel = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), p.counterWheelDiameterMm / 2.0, p.counterWheelWidthMm).Shape();

    // 6.1 mm bore
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 3.05, p.counterWheelWidthMm + 2.0).Shape();
    wheel = safeCut(wheel, bore);

    // Knurled rim scallops for manual digit setting
    for (int i = 0; i < 20; ++i)
    {
        const double ang = i * (juce::MathConstants<double>::pi / 10.0);
        const double rx = (p.counterWheelDiameterMm / 2.0) * std::cos(ang);
        const double ry = (p.counterWheelDiameterMm / 2.0) * std::sin(ang);
        TopoDS_Shape scallop = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(rx, ry, -0.5), gp_Dir(0, 0, 1)), 1.2, p.counterWheelWidthMm + 1.0).Shape();
        wheel = safeCut(wheel, scallop);
    }

    // Target trip gate notch (internal pocket aligned at the selected digit position)
    const double tripAngle = (presetDigit % 10) * (2.0 * juce::MathConstants<double>::pi / 10.0);
    const double tx = ((p.counterWheelDiameterMm / 2.0) - 3.0) * std::cos(tripAngle);
    const double ty = ((p.counterWheelDiameterMm / 2.0) - 3.0) * std::sin(tripAngle);
    TopoDS_Shape gate = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(tx - 2.0, ty - 2.0, -0.5), gp_Dir(0, 0, 1)), 4.0, 4.0, p.counterWheelWidthMm + 1.0).Shape();
    wheel = safeCut(wheel, gate);

    return wheel;
}

// ---------------------------------------------------------------------------------------------------------------
// Family 7 & 8: Clutch, Trip & Stopping Mechanism
// ---------------------------------------------------------------------------------------------------------------
TopoDS_Shape makeClutchDriver(const CoilWinderParameters& p)
{
    // Fixed driver collar attached to gear hub: diameter 28.0 mm x 10.0 mm
    TopoDS_Shape collar = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), p.clutchDiameterMm / 2.0, 10.0).Shape();
    // 12.1 mm bore
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 6.05, 12.0).Shape();
    collar = safeCut(collar, bore);

    // 3 axial dog jaws (4.0 mm depth, 60 deg arc each)
    for (int i = 0; i < 3; ++i)
    {
        const double ang = i * (2.0 * juce::MathConstants<double>::pi / 3.0);
        TopoDS_Shape jaw = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(6.5, -4.0, 10.0), gp_Dir(0, 0, 1)), 6.0, 8.0, p.clutchJawDepthMm).Shape();
        jaw = rotate(jaw, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), ang);
        collar = safeFuse(collar, jaw);
    }

    return collar;
}

TopoDS_Shape makeClutchSlidingCollar(const CoilWinderParameters& p)
{
    // Sliding collar: diameter 28.0 mm x 16.0 mm length
    TopoDS_Shape collar = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), p.clutchDiameterMm / 2.0, 16.0).Shape();

    // 12.1 mm bore with axial spline key
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 6.05, 18.0).Shape();
    collar = safeCut(collar, bore);
    TopoDS_Shape splineKey = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-1.4, 4.2, 0.0), gp_Dir(0, 0, 1)), 2.8, 2.0, 16.0).Shape();
    collar = safeFuse(collar, splineKey);

    // Annular shift fork groove: diameter 20 mm, width 4.0 mm at Z = 6.0 to 10.0 mm
    TopoDS_Shape grooveBox = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-16.0, -16.0, 6.0), gp_Dir(0, 0, 1)), 32.0, 32.0, 4.0).Shape();
    TopoDS_Shape innerCore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 5.5), gp_Dir(0, 0, 1)), 10.0, 5.0).Shape();
    collar = safeCut(collar, safeCut(grooveBox, innerCore));

    // 3 mating dog jaws on face
    for (int i = 0; i < 3; ++i)
    {
        const double ang = i * (2.0 * juce::MathConstants<double>::pi / 3.0) + (juce::MathConstants<double>::pi / 3.0);
        TopoDS_Shape jaw = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(6.5, -4.0, -p.clutchJawDepthMm), gp_Dir(0, 0, 1)), 6.0, 8.0, p.clutchJawDepthMm).Shape();
        jaw = rotate(jaw, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), ang);
        collar = safeFuse(collar, jaw);
    }

    return collar;
}

TopoDS_Shape makeShiftForkLever(const CoilWinderParameters& p)
{
    // Pivot arm: 70 mm length, 12 mm width, 8 mm thickness
    TopoDS_Shape arm = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-6.0, -4.0, 0), gp_Dir(0, 0, 1)), 12.0, 70.0, 8.0).Shape();
    // Semi-circular fork yoke engaging the collar groove (inner radius 10.5 mm, outer radius 16 mm)
    TopoDS_Shape yoke = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 60.0, 0), gp_Dir(0, 0, 1)), 16.0, 8.0).Shape();
    TopoDS_Shape yokeSlot = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 60.0, -1.0), gp_Dir(0, 0, 1)), 10.5, 10.0).Shape();
    TopoDS_Shape openCut = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-20.0, 60.0, -1.0), gp_Dir(0, 0, 1)), 40.0, 20.0, 10.0).Shape();
    yoke = safeCut(yoke, yokeSlot);
    yoke = safeCut(yoke, openCut);
    arm = safeFuse(arm, yoke);

    // Pivot hole (4.1 mm diameter) at Y = 10.0 mm
    TopoDS_Shape pivot = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 10.0, -1.0), gp_Dir(0, 0, 1)), 2.05, 10.0).Shape();
    arm = safeCut(arm, pivot);

    return arm;
}

TopoDS_Shape makeTripBailFeeler(const CoilWinderParameters& p)
{
    // Comparator bail feeler bar spanning across the 4 decimal counter & preset wheels:
    // Main bar: 55 mm length (X), 8 mm width (Y), 8 mm height (Z)
    TopoDS_Shape bar = BRepPrimAPI_MakeBox(55.0, 8.0, 8.0).Shape();

    // 4 sensing fingers spaced at 8.0 mm along X
    for (int i = 0; i < 4; ++i)
    {
        const double fx = 8.0 + i * 10.0;
        TopoDS_Shape finger = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(fx, -6.0, 1.0), gp_Dir(0, 0, 1)), 3.0, 6.0, 6.0).Shape();
        bar = safeFuse(bar, finger);
    }

    // Pivot trunnions on both ends (3.8 mm diameter x 6 mm length)
    TopoDS_Shape t1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 4.0, 4.0), gp_Dir(-1, 0, 0)), 1.9, 6.0).Shape();
    TopoDS_Shape t2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(55.0, 4.0, 4.0), gp_Dir(1, 0, 0)), 1.9, 6.0).Shape();
    bar = safeFuse(bar, t1);
    bar = safeFuse(bar, t2);

    return bar;
}

TopoDS_Shape makeBrakeShoe(const CoilWinderParameters& p)
{
    // Controlled friction brake pad holder: 16 x 14 x 10 mm with concave friction face
    TopoDS_Shape pad = BRepPrimAPI_MakeBox(16.0, 14.0, 10.0).Shape();
    // Concave arc to match arbor brake drum (radius 19.5 mm)
    TopoDS_Shape drumCut = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(8.0, 26.0, -1.0), gp_Dir(0, 0, 1)), 19.5, 12.0).Shape();
    pad = safeCut(pad, drumCut);
    return pad;
}

// ---------------------------------------------------------------------------------------------------------------
// Family 9 & 10: Reciprocating Traverse, Lead Screw & Carriage
// ---------------------------------------------------------------------------------------------------------------
TopoDS_Shape makeLeadScrew(double lengthMm, const CoilWinderParameters& p)
{
    // Clamp to 100 mm printer volume
    const double effLen = std::min(lengthMm, p.kMaxPrintDimensionMm - 2.0);

    // Core shaft: diameter 12.0 mm
    TopoDS_Shape screw = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 6.0, effLen).Shape();

    // Coarse helical trapezoidal thread ridge: outer diameter 14.0 mm
    // Represented parametrically with alternating thread rings for 3D printability
    const double pitch = p.leadScrewPitchMm;
    const int numThreads = (int)(effLen / (pitch * 2.0));
    for (int i = 2; i < numThreads - 2; ++i)
    {
        const double zPos = i * (pitch * 2.0);
        TopoDS_Shape ring = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, zPos), gp_Dir(0, 0, 1)), 7.0, pitch).Shape();
        screw = safeFuse(screw, ring);
    }

    // Journal bearings at both ends (diameter 10.0 mm x 10 mm length)
    TopoDS_Shape cut1 = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-8.0, -8.0, -1.0), gp_Dir(0, 0, 1)), 16.0, 16.0, 11.0).Shape();
    TopoDS_Shape j1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 5.0, 11.0).Shape();
    screw = safeCut(screw, safeCut(cut1, j1));

    return screw;
}

TopoDS_Shape makeGuideRail(double lengthMm, const CoilWinderParameters& p)
{
    const double effLen = std::min(lengthMm, p.kMaxPrintDimensionMm - 2.0);
    // Cylindrical rail: diameter 8.0 mm
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 4.0, effLen).Shape();
}

TopoDS_Shape makeTraverseCarriage(const CoilWinderParameters& p)
{
    // Carriage block: 42.0 mm (X) x 32.0 mm (Y) x 28.0 mm (Z)
    TopoDS_Shape block = BRepPrimAPI_MakeBox(42.0, 32.0, 28.0).Shape();

    // Dual 8.25 mm guide rail through-bores (spaced at 20.0 mm along Z)
    TopoDS_Shape railBore1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 16.0, 6.0), gp_Dir(1, 0, 0)), 4.15, 44.0).Shape();
    TopoDS_Shape railBore2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 16.0, 22.0), gp_Dir(1, 0, 0)), 4.15, 44.0).Shape();
    block = safeCut(block, railBore1);
    block = safeCut(block, railBore2);

    // Lead screw nut tunnel / thread follower: diameter 14.3 mm at Z = 14.0 mm
    TopoDS_Shape screwBore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 16.0, 14.0), gp_Dir(1, 0, 0)), 7.15, 44.0).Shape();
    block = safeCut(block, screwBore);

    // Mounting dovetail for wire guide arm on front face (Y = 0)
    TopoDS_Shape mountSlot = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(13.5, -1.0, 4.0), gp_Dir(0, 0, 1)), 15.0, 6.0, 20.0).Shape();
    block = safeCut(block, mountSlot);

    return block;
}

TopoDS_Shape makeAdjustableTravelStop(const CoilWinderParameters& p)
{
    // Sliding travel stop collar clamped onto guide rail: 18 x 16 x 14 mm
    TopoDS_Shape stop = BRepPrimAPI_MakeBox(18.0, 16.0, 14.0).Shape();
    // 8.1 mm bore for rail
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1.0, 8.0, 7.0), gp_Dir(1, 0, 0)), 4.05, 20.0).Shape();
    stop = safeCut(stop, bore);

    // Thumbscrew clamping hole (4.0 mm diameter)
    TopoDS_Shape screwHole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(9.0, 8.0, -1.0), gp_Dir(0, 0, 1)), 2.0, 16.0).Shape();
    stop = safeCut(stop, screwHole);

    return stop;
}

TopoDS_Shape makeTumblerReverseArm(const CoilWinderParameters& p)
{
    // Reversing rocker mechanism carrying two idler gears:
    // Rocker arm: 48 mm length, 12 mm width, 6 mm thickness
    TopoDS_Shape arm = BRepPrimAPI_MakeBox(48.0, 12.0, 6.0).Shape();
    // Central pivot hole (6.1 mm diameter) at X = 24 mm
    TopoDS_Shape pivot = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(24.0, 6.0, -1.0), gp_Dir(0, 0, 1)), 3.05, 8.0).Shape();
    arm = safeCut(arm, pivot);

    // Idler gear axles at X = 6 mm and X = 42 mm (5.0 mm diameter x 12 mm length)
    TopoDS_Shape axle1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(6.0, 6.0, 6.0), gp_Dir(0, 0, 1)), 2.5, 12.0).Shape();
    TopoDS_Shape axle2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(42.0, 6.0, 6.0), gp_Dir(0, 0, 1)), 2.5, 12.0).Shape();
    arm = safeFuse(arm, axle1);
    arm = safeFuse(arm, axle2);

    return arm;
}

// ---------------------------------------------------------------------------------------------------------------
// Family 11: Wire Guide & Tensioner
// ---------------------------------------------------------------------------------------------------------------
TopoDS_Shape makeWireGuideArm(const CoilWinderParameters& p)
{
    // Mount tenon: 14.7 x 5.7 x 19.7 mm (fits into carriage mount slot)
    TopoDS_Shape tenon = BRepPrimAPI_MakeBox(14.7, 5.7, 19.7).Shape();

    // Cantilever guide arm extending forward 45 mm toward bobbin:
    TopoDS_Shape arm = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(2.35, -45.0, 12.0), gp_Dir(0, 0, 1)), 10.0, 45.0, 8.0).Shape();
    TopoDS_Shape guide = safeFuse(tenon, arm);

    // Smooth radiused wire eyelet at tip (Y = -45.0 mm): 3.0 mm through-bore with flared funnel
    TopoDS_Shape eyelet = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(7.35, -40.0, 10.0), gp_Dir(0, 0, 1)), 1.5, 12.0).Shape();
    guide = safeCut(guide, eyelet);

    // Drop-in wire threading slot (0.8 mm wide) from side
    TopoDS_Shape slot = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-1.0, -40.4, 10.0), gp_Dir(0, 0, 1)), 10.0, 0.8, 12.0).Shape();
    guide = safeCut(guide, slot);

    return guide;
}

TopoDS_Shape makeTensionerDisk(const CoilWinderParameters& p)
{
    // Dual friction tensioning disk: 22.0 mm diameter x 4.0 mm thickness
    TopoDS_Shape disk = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 11.0, 4.0).Shape();
    // 4.2 mm center mounting hole
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 2.1, 6.0).Shape();
    disk = safeCut(disk, bore);

    // Smooth wire channel groove around rim
    TopoDS_Shape rim = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(-12.0, -12.0, 1.5), gp_Dir(0, 0, 1)), 24.0, 24.0, 1.0).Shape();
    TopoDS_Shape inner = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 1.0), gp_Dir(0, 0, 1)), 9.5, 2.0).Shape();
    disk = safeCut(disk, safeCut(rim, inner));

    return disk;
}

TopoDS_Shape makeTensionerThumbNut(const CoilWinderParameters& p)
{
    // Knurled tension adjustment thumb nut: 22 mm diameter x 10 mm length
    TopoDS_Shape nut = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 11.0, 10.0).Shape();
    TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), 2.1, 12.0).Shape();
    nut = safeCut(nut, bore);
    return nut;
}

} // namespace parts

// ---------------------------------------------------------------------------------------------------------------
// Bounding Volume Verification
// ---------------------------------------------------------------------------------------------------------------
bool validatePartPrintEnvelope(const TopoDS_Shape& shape,
                               const juce::String& name,
                               ComponentSummary& summary,
                               double maxAllowedMm)
{
    summary.name = name;
    summary.isValidSolid = false;
    summary.fitsPrintVolume = false;

    if (shape.IsNull()) return false;

    // Solid validity check
    TopExp_Explorer exp(shape, TopAbs_SOLID);
    if (!exp.More()) return false;

    BRepCheck_Analyzer analyzer(shape);
    if (!analyzer.IsValid()) return false;
    summary.isValidSolid = true;

    // Compute bounding box
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return false;

    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    summary.dimX = xmax - xmin;
    summary.dimY = ymax - ymin;
    summary.dimZ = zmax - zmin;
    summary.maxDimension = std::max({summary.dimX, summary.dimY, summary.dimZ});

    // Fit within 100 mm cube constraint
    summary.fitsPrintVolume = (summary.dimX <= maxAllowedMm + 0.01) &&
                              (summary.dimY <= maxAllowedMm + 0.01) &&
                              (summary.dimZ <= maxAllowedMm + 0.01);

    return summary.isValidSolid && summary.fitsPrintVolume;
}

// ---------------------------------------------------------------------------------------------------------------
// Master Assembly Construction
// ---------------------------------------------------------------------------------------------------------------
bool buildCoilWinderDocument(CadDocument& doc,
                            const CoilWinderParameters& p,
                            std::vector<ComponentSummary>& summaries,
                            juce::String& error)
{
    summaries.clear();

    struct PartEntry
    {
        juce::String id;
        juce::String name;
        juce::String subassembly;
        TopoDS_Shape shape;
        CadFrame placement;
    };

    std::vector<PartEntry> catalog;

    // 1. Modular Frame Subassembly
    catalog.push_back({ "frame-base-left", "Base Plate (Left Drive)", "Subassembly 1: Modular Frame", parts::makeBasePlateLeft(p), {} });
    CadFrame pBaseRight; pBaseRight.x = p.frameBaseLengthMm; // Placed adjacent at X = 88 mm
    catalog.push_back({ "frame-base-right", "Base Plate (Right Traverse)", "Subassembly 1: Modular Frame", parts::makeBasePlateRight(p), pBaseRight });
    CadFrame pPin1; pPin1.x = 88.0; pPin1.y = 10.0; pPin1.z = 6.0;
    catalog.push_back({ "frame-lock-pin-front", "Frame Interlock Pin (Front)", "Subassembly 1: Modular Frame", parts::makeFrameInterlockPin(p), pPin1 });
    CadFrame pPin2; pPin2.x = 88.0; pPin2.y = 50.0; pPin2.z = 6.0;
    catalog.push_back({ "frame-lock-pin-rear", "Frame Interlock Pin (Rear)", "Subassembly 1: Modular Frame", parts::makeFrameInterlockPin(p), pPin2 });
    CadFrame pUpLeft; pUpLeft.x = 12.0; pUpLeft.y = 9.0; pUpLeft.z = 6.0;
    catalog.push_back({ "frame-upright-left", "Main Bearing Upright (Drive Left)", "Subassembly 1: Modular Frame", parts::makeMainUprightLeft(p), pUpLeft });
    CadFrame pUpRight; pUpRight.x = p.frameBaseLengthMm + 20.0; pUpRight.y = 9.0; pUpRight.z = 6.0;
    catalog.push_back({ "frame-upright-right", "Main Bearing Upright (Spindle Right)", "Subassembly 1: Modular Frame", parts::makeMainUprightRight(p), pUpRight });
    CadFrame pTravEnd; pTravEnd.x = p.frameBaseLengthMm + 68.0; pTravEnd.y = 15.0; pTravEnd.z = 6.0;
    catalog.push_back({ "frame-traverse-bracket", "Traverse End Support Bracket", "Subassembly 1: Modular Frame", parts::makeTraverseEndBracket(p), pTravEnd });

    // 2. Hand Crank & Primary Drive Subassembly
    CadFrame pCrank; pCrank.x = 0.0; pCrank.y = 44.0; pCrank.z = 38.0;
    catalog.push_back({ "drive-crank-arm", "Ergonomic Hand Crank Arm", "Subassembly 2: Hand Crank Drive", parts::makeHandCrankArm(p), pCrank });
    CadFrame pHandle; pHandle.x = 75.0; pHandle.y = 44.0; pHandle.z = 38.0;
    catalog.push_back({ "drive-crank-handle", "Revolving Crank Handle Grip", "Subassembly 2: Hand Crank Drive", parts::makeCrankHandleGrip(p), pHandle });
    CadFrame pInShaft; pInShaft.x = 12.0; pInShaft.y = 44.0; pInShaft.z = 38.0;
    catalog.push_back({ "drive-input-shaft", "Main Drive Input Shaft (10mm)", "Subassembly 2: Hand Crank Drive", parts::makeInputShaft(p), pInShaft });
    CadFrame pBushing1; pBushing1.x = 12.0; pBushing1.y = 44.0; pBushing1.z = 38.0;
    catalog.push_back({ "bushing-input-shaft", "Flanged Bushing (Input Shaft 10mm)", "Subassembly 2: Hand Crank Drive", parts::makeFlangedBushing(10.0, 16.0, 18.0, 3.0), pBushing1 });

    // 3. Winding Spindle Subassembly
    CadFrame pSpindle; pSpindle.x = 12.0; pSpindle.y = 44.0; pSpindle.z = 92.0;
    catalog.push_back({ "spindle-main-shaft", "Winding Spindle Main Shaft (12mm)", "Subassembly 3: Winding Spindle", parts::makeSpindleShaft(p), pSpindle });
    CadFrame pBushing2; pBushing2.x = 12.0; pBushing2.y = 44.0; pBushing2.z = 92.0;
    catalog.push_back({ "bushing-spindle-left", "Flanged Bushing (Spindle Left 12mm)", "Subassembly 3: Winding Spindle", parts::makeFlangedBushing(12.0, 16.0, 20.0, 3.0), pBushing2 });

    // 4. Interchangeable Bobbin Fixture Subassembly
    CadFrame pArbor; pArbor.x = 90.0; pArbor.y = 44.0; pArbor.z = 92.0;
    catalog.push_back({ "fixture-arbor-plate", "Bobbin Drive Faceplate Arbor", "Subassembly 4: Bobbin Fixtures", parts::makeBobbinArborFaceplate(p), pArbor });
    CadFrame pCone; pCone.x = 104.0; pCone.y = 44.0; pCone.z = 92.0;
    catalog.push_back({ "fixture-centering-cone", "Stepped Bobbin Centering Cone", "Subassembly 4: Bobbin Fixtures", parts::makeCenteringCone(p), pCone });
    CadFrame pNut; pNut.x = 145.0; pNut.y = 44.0; pNut.z = 92.0;
    catalog.push_back({ "fixture-quick-nut", "Quick-Release Bobbin Clamp Nut", "Subassembly 4: Bobbin Fixtures", parts::makeBobbinQuickNut(p), pNut });
    CadFrame pBobbin; pBobbin.x = 110.0; pBobbin.y = 44.0; pBobbin.z = 92.0;
    catalog.push_back({ "sample-bobbin-workpiece", "Sample Winding Bobbin (Workpiece)", "Subassembly 4: Bobbin Fixtures", parts::makeSampleBobbinWorkpiece(p), pBobbin });

    // 5. Gear Transmission Subassembly (1:1 and 3:1 options)
    // 3:1 Speedup Gear Pair: 54T crank gear + 18T spindle pinion
    CadFrame pGear54; pGear54.x = 35.0; pGear54.y = 44.0; pGear54.z = 38.0;
    catalog.push_back({ "gear-speedup-54t", "Crank Drive Gear (54T, m=1.5)", "Subassembly 5: Gear Transmission", parts::makeSpurGear(54, 1.5, 10.0, 10.0, true), pGear54 });
    CadFrame pPinion18; pPinion18.x = 35.0; pPinion18.y = 44.0; pPinion18.z = 92.0;
    catalog.push_back({ "gear-speedup-18t", "Spindle Driven Pinion (18T, m=1.5)", "Subassembly 5: Gear Transmission", parts::makeSpurGear(18, 1.5, 10.0, 12.0, true), pPinion18 });
    // 1:1 Direct Gear Pair: 36T
    CadFrame pGear36; pGear36.x = 50.0; pGear36.y = 44.0; pGear36.z = 38.0;
    catalog.push_back({ "gear-direct-36t", "Direct Drive Gear (36T, m=1.5)", "Subassembly 5: Gear Transmission", parts::makeSpurGear(36, 1.5, 10.0, 10.0, true), pGear36 });

    // 6. Decimal Turn Counter Subassembly (4 Digit Wheels: Units, Tens, Hundreds, Thousands)
    for (int d = 0; d < 4; ++d)
    {
        CadFrame pCw; pCw.x = 20.0 + d * 8.0; pCw.y = 77.0; pCw.z = 66.0;
        const juce::String dName = d == 0 ? "Units" : (d == 1 ? "Tens" : (d == 2 ? "Hundreds" : "Thousands"));
        catalog.push_back({ "counter-wheel-" + juce::String(d), "Actual Turn Counter Wheel (" + dName + ")", "Subassembly 6: Decimal Turn Counter", parts::makeCounterWheel(0, d < 3, p), pCw });
    }
    CadFrame pPinionT; pPinionT.x = 24.0; pPinionT.y = 70.0; pPinionT.z = 66.0;
    catalog.push_back({ "counter-transfer-pinion", "Geneva Intermittent Transfer Pinion", "Subassembly 6: Decimal Turn Counter", parts::makeTransferPinion(p), pPinionT });

    // 7. Mechanical Preset Counter Subassembly (4 Preset Wheels)
    for (int d = 0; d < 4; ++d)
    {
        CadFrame pPw; pPw.x = 20.0 + d * 8.0; pPw.y = 86.0; pPw.z = 78.0;
        const juce::String dName = d == 0 ? "Units" : (d == 1 ? "Tens" : (d == 2 ? "Hundreds" : "Thousands"));
        catalog.push_back({ "preset-wheel-" + juce::String(d), "Preset Target Wheel (" + dName + ")", "Subassembly 7: Preset Counter", parts::makePresetWheel(0, p), pPw });
    }

    // 8. Trip Mechanism Subassembly
    CadFrame pBail; pBail.x = 18.0; pBail.y = 80.0; pBail.z = 72.0;
    catalog.push_back({ "trip-comparator-bail", "Preset Comparison Feeler Bail", "Subassembly 8: Trip Mechanism", parts::makeTripBailFeeler(p), pBail });

    // 9. Clutch and Controlled Stop Subassembly
    CadFrame pClutch1; pClutch1.x = 48.0; pClutch1.y = 44.0; pClutch1.z = 92.0;
    catalog.push_back({ "clutch-jaw-driver", "Axial Dog Clutch Driver Jaw", "Subassembly 9: Clutch & Stop", parts::makeClutchDriver(p), pClutch1 });
    CadFrame pClutch2; pClutch2.x = 60.0; pClutch2.y = 44.0; pClutch2.z = 92.0;
    catalog.push_back({ "clutch-sliding-collar", "Axial Dog Clutch Sliding Collar", "Subassembly 9: Clutch & Stop", parts::makeClutchSlidingCollar(p), pClutch2 });
    CadFrame pFork; pFork.x = 60.0; pFork.y = 20.0; pFork.z = 70.0;
    catalog.push_back({ "clutch-shift-fork", "Clutch Shift Fork & Throwout Lever", "Subassembly 9: Clutch & Stop", parts::makeShiftForkLever(p), pFork });
    CadFrame pBrake; pBrake.x = 65.0; pBrake.y = 35.0; pBrake.z = 85.0;
    catalog.push_back({ "brake-arrestor-shoe", "Controlled Spindle Brake Shoe", "Subassembly 9: Clutch & Stop", parts::makeBrakeShoe(p), pBrake });

    // 10. Reciprocating Traverse & Reversal Subassembly
    CadFrame pLeadScrew; pLeadScrew.x = 108.0; pLeadScrew.y = 19.0; pLeadScrew.z = 51.0;
    catalog.push_back({ "traverse-lead-screw", "Level-Wind Lead Screw (Pitch 2mm)", "Subassembly 10: Traverse Mechanism", parts::makeLeadScrew(75.0, p), pLeadScrew });
    CadFrame pRail1; pRail1.x = 108.0; pRail1.y = 19.0; pRail1.z = 71.0;
    catalog.push_back({ "traverse-guide-rail-upper", "Traverse Guide Rail (Upper 8mm)", "Subassembly 10: Traverse Mechanism", parts::makeGuideRail(75.0, p), pRail1 });
    CadFrame pRail2; pRail2.x = 108.0; pRail2.y = 19.0; pRail2.z = 31.0;
    catalog.push_back({ "traverse-guide-rail-lower", "Traverse Guide Rail (Lower 8mm)", "Subassembly 10: Traverse Mechanism", parts::makeGuideRail(75.0, p), pRail2 });
    CadFrame pCarriage; pCarriage.x = 125.0; pCarriage.y = 19.0; pCarriage.z = 45.0;
    catalog.push_back({ "traverse-carriage", "Traverse Slider Carriage Block", "Subassembly 10: Traverse Mechanism", parts::makeTraverseCarriage(p), pCarriage });
    CadFrame pStop1; pStop1.x = 112.0; pStop1.y = 19.0; pStop1.z = 71.0;
    catalog.push_back({ "traverse-stop-left", "Adjustable Width Limit Stop (Left)", "Subassembly 10: Traverse Mechanism", parts::makeAdjustableTravelStop(p), pStop1 });
    CadFrame pStop2; pStop2.x = 160.0; pStop2.y = 19.0; pStop2.z = 71.0;
    catalog.push_back({ "traverse-stop-right", "Adjustable Width Limit Stop (Right)", "Subassembly 10: Traverse Mechanism", parts::makeAdjustableTravelStop(p), pStop2 });
    CadFrame pTumbler; pTumbler.x = 100.0; pTumbler.y = 19.0; pTumbler.z = 51.0;
    catalog.push_back({ "traverse-tumbler-reverse", "Mechanical Tumbler Reversing Arm", "Subassembly 10: Traverse Mechanism", parts::makeTumblerReverseArm(p), pTumbler });

    // 11. Wire Guide & Tensioner Subassembly
    CadFrame pGuideArm; pGuideArm.x = 125.0; pGuideArm.y = 19.0; pGuideArm.z = 45.0;
    catalog.push_back({ "wire-guide-arm", "Cantilever Wire Delivery Guide Arm", "Subassembly 11: Wire Guide & Tensioner", parts::makeWireGuideArm(p), pGuideArm });
    CadFrame pTensionDisc1; pTensionDisc1.x = 128.0; pTensionDisc1.y = -10.0; pTensionDisc1.z = 57.0;
    catalog.push_back({ "wire-tensioner-disc-1", "Passive Friction Tensioning Disc A", "Subassembly 11: Wire Guide & Tensioner", parts::makeTensionerDisk(p), pTensionDisc1 });
    CadFrame pTensionDisc2; pTensionDisc2.x = 128.0; pTensionDisc2.y = -6.0; pTensionDisc2.z = 57.0;
    catalog.push_back({ "wire-tensioner-disc-2", "Passive Friction Tensioning Disc B", "Subassembly 11: Wire Guide & Tensioner", parts::makeTensionerDisk(p), pTensionDisc2 });
    CadFrame pTensionNut; pTensionNut.x = 128.0; pTensionNut.y = -2.0; pTensionNut.z = 57.0;
    catalog.push_back({ "wire-tensioner-thumb-nut", "Adjustable Tensioner Thumb Nut", "Subassembly 11: Wire Guide & Tensioner", parts::makeTensionerThumbNut(p), pTensionNut });

    // Validate print volume on every component and register into CadDocument
    bool allValid = true;
    for (const auto& entry : catalog)
    {
        ComponentSummary s;
        s.id = entry.id;
        s.subassembly = entry.subassembly;
        const bool fits = validatePartPrintEnvelope(entry.shape, entry.name, s, CoilWinderParameters::kMaxPrintDimensionMm);
        summaries.push_back(s);

        if (!fits)
        {
            error = "Component " + entry.id + " (" + entry.name + ") exceeds 100mm print volume: " +
                    juce::String(s.maxDimension, 2) + " mm.";
            allValid = false;
        }

        // Insert as parametric BRep component into document
        auto record = components::object();
        components::put(record, "id", entry.id);
        components::put(record, "name", entry.name);
        components::put(record, "category", entry.subassembly);
        components::put(record, "origin", "custom");
        components::put(record, "revision", 1);
        components::put(record, "properties", juce::Array<juce::var>());

        auto geom = components::object();
        components::put(geom, "kind", "brep");
        components::put(geom, "level", "custom");
        components::put(geom, "label", entry.name + " exact BRep solid");
        components::put(geom, "data", components::encodeShape(entry.shape));
        components::put(record, "geometry", geom);

        juce::String instId;
        if (!doc.insertComponent(record, entry.placement, instId, error))
        {
            allValid = false;
        }
        summaries.back().instanceId = instId;
    }

    return allValid;
}

} // namespace djehuti::cad::coilwinder
