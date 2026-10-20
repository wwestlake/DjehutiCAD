#pragma once
#include "CadSheetStock.h"

namespace djehuti::cad
{
// Application catalog templates. A document import is a snapshot, never a subscription.
// Mutations are staged and atomically saved before replacing the live catalog.
class CadEngineeringCatalog
{
public:
    static juce::File defaultFile();
    bool ensureLoaded(juce::String& error);
    void setFile(const juce::File& file); // Isolated catalogs for diagnostics; production uses defaultFile().
    void reload() { loaded = false; }
    const CadMaterials& materialsData() const { return materials; }
    const CadSheetStocks& stocksData() const { return stocks; }
    bool bundled(const juce::String& id) const { return bundledIds.contains(id); }
    juce::var describe() const;
    bool mutate(const juce::String& action, const juce::var& args, juce::String& id, juce::String& error);
    bool importFile(const juce::File&, juce::String& error);
    bool exportFile(const juce::File&, juce::String& error) const;
private:
    bool save(const CadMaterials&, const CadSheetStocks&, juce::String& error);
    bool loaded = false;
    juce::File file;
    CadMaterials materials;
    CadSheetStocks stocks;
    juce::StringArray bundledIds;
    juce::String diskText;
};
}
