#pragma once

#include <JuceHeader.h>

#include <functional>
#include <array>
#include <vector>

namespace djehuti::cad
{
// A picked body, face or edge. drawId is what was drawn and hit: the assembly instance id for an instanced body,
// otherwise the feature id. featureId owns the geometry; face and edge are transient indices into that feature's
// shape (TopExp face order, unique-edge order) that the document resolves to persistent names when needed.
struct CadPick
{
    juce::String drawId, featureId;
    int face = -1, edge = -1;
    std::array<double,3> point {0,0,0};
    bool hasPoint = false; // Perspective-correct hit in the owning feature frame, for region picking.
    bool valid() const { return featureId.isNotEmpty(); }
    bool operator== (const CadPick& o) const { return drawId == o.drawId && featureId == o.featureId && face == o.face && edge == o.edge && hasPoint==o.hasPoint && (!hasPoint||point==o.point); }
    bool operator!= (const CadPick& o) const { return !(*this == o); }
};

// The one selection shared by the model view and every 3D view (message thread only): the pick mode, the
// hovered item and the selected item. Views redraw when the serial changes; listeners update inspectors.
class CadSelection
{
public:
    enum class Mode { body, face, edge };

    enum class Tool { pointer, box, circle };
    Tool tool() const { return activeTool; }
    void setTool(Tool next) { if (next != activeTool) { activeTool = next; changed(); } }
    const std::vector<CadPick>& items() const { return chosenItems; }
    void setChosenItemsQuietly(std::vector<CadPick> next) { chosenItems = std::move(next); chosen = chosenItems.empty() ? CadPick() : chosenItems.back(); ++changeSerial; }
    void setSelectedMany(std::vector<CadPick> next) { chosenItems = std::move(next); chosen = chosenItems.empty() ? CadPick() : chosenItems.back(); changed(); }

    Mode mode() const { return currentMode; }
    const CadPick& hover() const { return hovered; }
    const CadPick& selected() const { return chosen; }
    std::uint64_t serial() const { return changeSerial; }

    void setMode(Mode next) { if (next == currentMode) return; currentMode = next; hovered = {}; chosen = {}; chosenItems.clear(); changed(); }
    void setHover(const CadPick& next) { if (next == hovered) return; hovered = next; ++changeSerial; }   // hover only redraws
    void setSelected(const CadPick& next) { if (next == chosen && chosenItems.size() <= 1 && currentMode != Mode::edge) return; chosen = next; chosenItems = next.valid() ? std::vector<CadPick>{next} : std::vector<CadPick>(); changed(); }
    void clear() { if (!hovered.valid() && !chosen.valid()) return; hovered = {}; chosen = {}; chosenItems.clear(); changed(); }
    void addListener(std::function<void()> listener) { listeners.push_back(std::move(listener)); }

private:
    void changed() { ++changeSerial; for (const auto& l : listeners) if (l) l(); }

    Tool activeTool = Tool::pointer;
    std::vector<CadPick> chosenItems;
    Mode currentMode = Mode::body;
    CadPick hovered, chosen;
    std::uint64_t changeSerial = 1;
    std::vector<std::function<void()>> listeners;
};
}
