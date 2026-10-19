#pragma once

#include <JuceHeader.h>

// Engineering units for structural work.
//
// Internal convention for structural quantities: coherent SI. Length m, force N, moment N*m, mass kg, stress and
// pressure Pa, density kg/m^3, acceleration m/s^2. Every structural value stored by DjehutiCAD (analysis cases,
// later results) is SI; every value crossing the API or a file carries an explicit unit string.
//
// Existing conventions are unchanged and are converted at the boundary:
// - CAD geometry, placements and frames are millimetres (lengths mm, areas mm^2, volumes mm^3, inertia integrals mm^5);
// - joint coordinates are radians and metres (CadMechanics.h);
// - physical materials store density in kg/m^3 and moduli/strengths in MPa (CadMaterials.h).
// Application points of loads stay in model millimetres, like every other geometric position in the document.
namespace djehuti::cad::units
{
// Quantity names accepted by toSI/fromSI.
// "length", "area", "volume", "secondMomentOfArea", "force", "moment", "mass", "stress", "density", "acceleration".
juce::StringArray quantities();
juce::StringArray unitsFor(const juce::String& quantity);   // accepted unit strings, SI unit first
juce::String siUnit(const juce::String& quantity);          // e.g. "N*m" for moment; empty if unknown
// Factor f with value_SI = f * value_unit. False (with a message) for an unknown quantity or unit.
bool factorToSI(const juce::String& quantity, const juce::String& unit, double& factor, juce::String& error);
bool toSI(const juce::String& quantity, double value, const juce::String& unit, double& si, juce::String& error);
bool fromSI(const juce::String& quantity, double si, const juce::String& unit, double& value, juce::String& error);

// Fixed conversions used where the units are part of a stored convention.
constexpr double kMillimetre = 1.0e-3;                    // m per mm
constexpr double kStandardGravity = 9.80665;              // m/s^2
inline double mmToM(double mm) { return mm * 1.0e-3; }
inline double mToMm(double m) { return m * 1.0e3; }
inline double mm2ToM2(double mm2) { return mm2 * 1.0e-6; }
inline double mm3ToM3(double mm3) { return mm3 * 1.0e-9; }
inline double mm4ToM4(double mm4) { return mm4 * 1.0e-12; }
inline double mm5ToM5(double mm5) { return mm5 * 1.0e-15; }   // geometric inertia integrals; * density -> kg*m^2
inline double mpaToPa(double mpa) { return mpa * 1.0e6; }
inline double paToMpa(double pa) { return pa * 1.0e-6; }
inline double nmmToNm(double nmm) { return nmm * 1.0e-3; }
inline double nmToNmm(double nm) { return nm * 1.0e3; }
// Mass of a CAD volume: density kg/m^3 times volume mm^3 -> kg.
inline double massKg(double densityKgPerM3, double volumeMm3) { return densityKgPerM3 * mm3ToM3(volumeMm3); }
}
