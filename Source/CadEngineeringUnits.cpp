#include "CadEngineeringUnits.h"

#include <cmath>

namespace djehuti::cad::units
{
namespace
{
struct UnitDef { const char* quantity; const char* unit; double factor; };
// First entry per quantity is the SI unit (factor 1). Imperial factors are exact by definition.
const UnitDef table[] {
    { "length", "m", 1.0 }, { "length", "mm", 1.0e-3 }, { "length", "cm", 1.0e-2 }, { "length", "in", 0.0254 }, { "length", "ft", 0.3048 },
    { "area", "m^2", 1.0 }, { "area", "mm^2", 1.0e-6 }, { "area", "cm^2", 1.0e-4 },
    { "volume", "m^3", 1.0 }, { "volume", "mm^3", 1.0e-9 }, { "volume", "cm^3", 1.0e-6 }, { "volume", "L", 1.0e-3 },
    { "secondMomentOfArea", "m^4", 1.0 }, { "secondMomentOfArea", "mm^4", 1.0e-12 }, { "secondMomentOfArea", "cm^4", 1.0e-8 },
    { "force", "N", 1.0 }, { "force", "kN", 1.0e3 }, { "force", "MN", 1.0e6 }, { "force", "lbf", 4.4482216152605 },
    { "moment", "N*m", 1.0 }, { "moment", "N*mm", 1.0e-3 }, { "moment", "kN*m", 1.0e3 }, { "moment", "lbf*in", 0.112984829027617 }, { "moment", "lbf*ft", 1.3558179483314 },
    { "mass", "kg", 1.0 }, { "mass", "g", 1.0e-3 }, { "mass", "t", 1.0e3 }, { "mass", "lb", 0.45359237 },
    { "stress", "Pa", 1.0 }, { "stress", "kPa", 1.0e3 }, { "stress", "MPa", 1.0e6 }, { "stress", "GPa", 1.0e9 }, { "stress", "N/mm^2", 1.0e6 }, { "stress", "psi", 6894.75729316836 },
    { "density", "kg/m^3", 1.0 }, { "density", "g/cm^3", 1.0e3 }, { "density", "kg/mm^3", 1.0e9 }, { "density", "t/mm^3", 1.0e12 },
    { "acceleration", "m/s^2", 1.0 }, { "acceleration", "mm/s^2", 1.0e-3 }, { "acceleration", "g0", kStandardGravity },
};
}

juce::StringArray quantities()
{
    juce::StringArray q;
    for (const auto& u : table) q.addIfNotAlreadyThere(u.quantity);
    return q;
}

juce::StringArray unitsFor(const juce::String& quantity)
{
    juce::StringArray list;
    for (const auto& u : table) if (quantity == u.quantity) list.add(u.unit);
    return list;
}

juce::String siUnit(const juce::String& quantity)
{
    for (const auto& u : table) if (quantity == u.quantity) return u.unit;
    return {};
}

bool factorToSI(const juce::String& quantity, const juce::String& unit, double& factor, juce::String& error)
{
    bool knownQuantity = false;
    for (const auto& u : table)
    {
        if (quantity != u.quantity) continue;
        knownQuantity = true;
        if (unit == u.unit) { factor = u.factor; return true; }
    }
    error = knownQuantity ? "Unit \"" + unit + "\" is not a " + quantity + " unit (use " + unitsFor(quantity).joinIntoString(", ") + ")."
                          : "Unknown quantity \"" + quantity + "\".";
    return false;
}

bool toSI(const juce::String& quantity, double value, const juce::String& unit, double& si, juce::String& error)
{
    double f = 1.0;
    if (!factorToSI(quantity, unit, f, error)) return false;
    if (!std::isfinite(value)) { error = "A " + quantity + " value must be a finite number."; return false; }
    si = value * f;
    return true;
}

bool fromSI(const juce::String& quantity, double si, const juce::String& unit, double& value, juce::String& error)
{
    double f = 1.0;
    if (!factorToSI(quantity, unit, f, error)) return false;
    if (!std::isfinite(si)) { error = "A " + quantity + " value must be a finite number."; return false; }
    value = si / f;
    return true;
}
}
