#pragma once

#include "CadDocument.h"
#include <TopoDS_Shape.hxx>
#include <juce_core/juce_core.h>
#include <vector>
#include <string>

namespace djehuti::cad::coilwinder
{

// ---------------------------------------------------------------------------------------------------------------
// Parametric Engineering Dimensions and Printability Tolerances
// ---------------------------------------------------------------------------------------------------------------
struct CoilWinderParameters
{
    // Print-volume envelope constraint (mm)
    static constexpr double kMaxPrintDimensionMm = 100.0;

    // Standard FDM fit tolerances (diametral / gap clearance in mm)
    double runningFitClearanceMm = 0.30;   // rotating shafts in bushings
    double slidingFitClearanceMm = 0.25;   // sliding keyways / shift collar / carriage rails
    double pushFitClearanceMm    = 0.15;   // interlocking dovetails / pins / dowels
    double gearBacklashMm        = 0.20;   // tooth backlash for printed involute gears

    // Frame parameters (mm)
    double frameBaseLengthMm     = 88.0;   // fits well inside 100 mm cube
    double frameBaseWidthMm      = 88.0;
    double frameBaseThicknessMm  = 12.0;
    double frameUprightHeightMm  = 96.0;   // fits inside 100 mm cube
    double frameUprightWidthMm   = 86.0;
    double frameUprightThickMm   = 16.0;

    // Transmission & Shafts (mm)
    double shaftCenterDistanceMm = 54.0;   // exact center distance for both 1:1 and 3:1 gears
    double inputShaftDiameterMm  = 10.0;
    double spindleShaftDiameterMm= 12.0;
    double bushingWallThicknessMm= 2.5;

    // Gear tooth geometry (Module m = 1.5 mm, alpha = 20 deg)
    double gearModuleMm          = 1.50;
    double gearFaceWidthMm       = 10.0;
    int directCrankTeeth         = 36;     // d = 54 mm, Do = 57 mm
    int directSpindleTeeth       = 36;     // d = 54 mm, Do = 57 mm
    int speedupCrankTeeth        = 54;     // d = 81 mm, Do = 84 mm (fits in 100 mm cube!)
    int speedupSpindleTeeth      = 18;     // d = 27 mm, Do = 30 mm -> 3.0:1 ratio

    // Counter & Preset geometry (mm)
    double counterWheelDiameterMm= 34.0;
    double counterWheelWidthMm   = 7.5;
    double counterShaftDiameterMm= 6.0;

    // Clutch & Trip geometry (mm)
    double clutchDiameterMm      = 28.0;
    double clutchJawDepthMm      = 4.0;
    int clutchJawCount           = 3;

    // Traverse & Wire Guide (mm)
    double leadScrewDiameterMm   = 14.0;
    double leadScrewPitchMm      = 2.0;
    double guideRailDiameterMm   = 8.0;
    double maxTraverseTravelMm   = 50.0;
};

// ---------------------------------------------------------------------------------------------------------------
// Part Definition & Bounding Box Inspection
// ---------------------------------------------------------------------------------------------------------------
struct ComponentSummary
{
    juce::String id;
    juce::String instanceId;
    juce::String name;
    juce::String subassembly;
    double dimX = 0.0;
    double dimY = 0.0;
    double dimZ = 0.0;
    double maxDimension = 0.0;
    bool fitsPrintVolume = false;
    bool isValidSolid = false;
};

// ---------------------------------------------------------------------------------------------------------------
// Solid Modeling Functions for Component Families
// ---------------------------------------------------------------------------------------------------------------
namespace parts
{
    // Family 1: Modular Frame
    TopoDS_Shape makeBasePlateLeft(const CoilWinderParameters& p = {});
    TopoDS_Shape makeBasePlateRight(const CoilWinderParameters& p = {});
    TopoDS_Shape makeFrameInterlockPin(const CoilWinderParameters& p = {});
    TopoDS_Shape makeMainUprightLeft(const CoilWinderParameters& p = {});
    TopoDS_Shape makeMainUprightRight(const CoilWinderParameters& p = {});
    TopoDS_Shape makeTraverseEndBracket(const CoilWinderParameters& p = {});

    // Family 2: Hand Crank, Shafts, Bushings & Collars
    TopoDS_Shape makeHandCrankArm(const CoilWinderParameters& p = {});
    TopoDS_Shape makeCrankHandleGrip(const CoilWinderParameters& p = {});
    TopoDS_Shape makeInputShaft(const CoilWinderParameters& p = {});
    TopoDS_Shape makeSpindleShaft(const CoilWinderParameters& p = {});
    TopoDS_Shape makeFlangedBushing(double innerDiam, double length, double flangeDiam, double flangeThick);
    TopoDS_Shape makeSnapRetainingCollar(double shaftDiam);

    // Family 3: Bobbin Mounting & Clamping
    TopoDS_Shape makeBobbinArborFaceplate(const CoilWinderParameters& p = {});
    TopoDS_Shape makeCenteringCone(const CoilWinderParameters& p = {});
    TopoDS_Shape makeBobbinQuickNut(const CoilWinderParameters& p = {});
    TopoDS_Shape makeSampleBobbinWorkpiece(const CoilWinderParameters& p = {});

    // Family 4: Gear Sets
    TopoDS_Shape makeSpurGear(int teeth, double moduleMm, double faceWidthMm, double boreDiamMm, bool addKeyway);

    // Family 5 & 6: Actual & Preset Decimal Counter Wheels
    TopoDS_Shape makeCounterWheel(int digit, bool hasGenevaPin, const CoilWinderParameters& p = {});
    TopoDS_Shape makeTransferPinion(const CoilWinderParameters& p = {});
    TopoDS_Shape makePresetWheel(int presetDigit, const CoilWinderParameters& p = {});

    // Family 7 & 8: Clutch, Trip & Stopping Mechanism
    TopoDS_Shape makeClutchDriver(const CoilWinderParameters& p = {});
    TopoDS_Shape makeClutchSlidingCollar(const CoilWinderParameters& p = {});
    TopoDS_Shape makeShiftForkLever(const CoilWinderParameters& p = {});
    TopoDS_Shape makeTripBailFeeler(const CoilWinderParameters& p = {});
    TopoDS_Shape makeBrakeShoe(const CoilWinderParameters& p = {});

    // Family 9 & 10: Reciprocating Traverse, Lead Screw & Carriage
    TopoDS_Shape makeLeadScrew(double lengthMm, const CoilWinderParameters& p = {});
    TopoDS_Shape makeGuideRail(double lengthMm, const CoilWinderParameters& p = {});
    TopoDS_Shape makeTraverseCarriage(const CoilWinderParameters& p = {});
    TopoDS_Shape makeAdjustableTravelStop(const CoilWinderParameters& p = {});
    TopoDS_Shape makeTumblerReverseArm(const CoilWinderParameters& p = {});

    // Family 11: Wire Guide & Tensioner
    TopoDS_Shape makeWireGuideArm(const CoilWinderParameters& p = {});
    TopoDS_Shape makeTensionerDisk(const CoilWinderParameters& p = {});
    TopoDS_Shape makeTensionerThumbNut(const CoilWinderParameters& p = {});
}

// ---------------------------------------------------------------------------------------------------------------
// Assembly Construction and Validation
// ---------------------------------------------------------------------------------------------------------------
bool buildCoilWinderDocument(CadDocument& doc,
                            const CoilWinderParameters& params,
                            std::vector<ComponentSummary>& partSummaries,
                            juce::String& error);

bool validatePartPrintEnvelope(const TopoDS_Shape& shape,
                               const juce::String& name,
                               ComponentSummary& summary,
                               double maxAllowedMm = CoilWinderParameters::kMaxPrintDimensionMm);

} // namespace djehuti::cad::coilwinder
