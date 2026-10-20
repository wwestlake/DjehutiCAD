#pragma once
#include "CadMaterials.h"

namespace djehuti::cad
{
// Stock describes purchasable substrate. Finished coated thickness is a separate optional measurement.
// All lengths are explicitly millimetres. Gauge is descriptive and never converted into a thickness.
struct CadSheetStock
{
    juce::String id, name, materialId;
    int revision = 1;
    double thicknessMm = 0.0;
    std::optional<double> finishedThicknessMm, sheetWidthMm, sheetLengthMm, insideRadiusMm, kFactor;
    juce::String gauge, gaugeStandard, finish, coating, note, source;
    // Tagged forming model, extensible to allowance/deduction tables in a future schema.
    juce::String formingMethod = "kFactor", qualification = "unqualified", formingSource;
    juce::String catalogId;
    int catalogRevision = 0;
};

struct CadSheetStocks
{
    static constexpr int kSchemaVersion = 1;
    int nextNumber = 1;
    std::vector<CadSheetStock> stocks;
    juce::String defaultStockId;
    const CadSheetStock* find(const juce::String& id) const;
    juce::StringArray usingMaterial(const juce::String& id) const;
    bool empty() const { return stocks.empty() && defaultStockId.isEmpty(); }
};

namespace stock
{
bool validate(const CadSheetStock&, juce::String& error);
bool validateLibrary(const CadSheetStocks&, const CadMaterials&, juce::String& error);
juce::var toJson(const CadSheetStock&);
bool readArgs(const juce::var&, CadSheetStock&, juce::String& error);
bool fromJson(const juce::var&, CadSheetStock&, juce::String& error);
juce::var toJson(const CadSheetStocks&);
bool fromJson(const juce::var&, CadSheetStocks&, const CadMaterials&, juce::String& error);
}
}
