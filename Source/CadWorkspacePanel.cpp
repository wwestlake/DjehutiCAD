#include "CadMaterialsManager.h"
#include "CadWorkspacePanel.h"
#include "CadExportDialog.h"

#include <deque>
#include <cstdlib>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

using djehuti::cad::CadFeature;

namespace
{
const juce::Colour backgroundColour(0xff11161b), panelColour(0xff141a1f), toolbarColour(0xff161d23), borderColour(0xff26323a);
const juce::Colour textColour(0xffdce9ee), dimTextColour(0xff8fa4ad), accentColour(0xff2a8c99);
const juce::Colour selectedColour(0xffffb86b), targetColour(0xff63d48a), toolColour(0xffff7b72);
const juce::Colour interferenceColour(0xffe0303a);           // parts of an interfering pair
constexpr juce::uint32 interferenceRegionArgb = 0xffffe14d;  // outline of the common volume

bool isSolidType(const juce::String& type) { return type == "box" || type == "cylinder" || type == "extrude" || type == "revolve" || type == "cut" || type == "union" || type == "subtract" || type == "intersect" || type == "fillet"; }
bool isPrimitiveType(const juce::String& type) { return type == "box" || type == "cylinder"; }
// Joint coordinates are radians/metres everywhere except the UI, which shows degrees/millimetres.
double toUi(const juce::String& jointType, double value) { return jointType == "revolute" ? value * 180.0 / juce::MathConstants<double>::pi : value * 1000.0; }
double fromUi(const juce::String& jointType, double value) { return jointType == "revolute" ? value * juce::MathConstants<double>::pi / 180.0 : value / 1000.0; }
juce::String uiUnit(const juce::String& jointType) { return jointType == "revolute" ? "deg" : "mm"; }
juce::String friendlyEntity(const juce::String& id) { const auto type = id.upToFirstOccurrenceOf("-", false, false); if (type == "bezier") return "Curve " + id.fromLastOccurrenceOf("-", false, false); return type.substring(0, 1).toUpperCase() + type.substring(1) + " " + id.fromLastOccurrenceOf("-", false, false); }
double volumeOf(const TopoDS_Shape& shape) { if (shape.IsNull()) return 0.0; GProp_GProps properties; BRepGProp::VolumeProperties(shape, properties); return properties.Mass(); }
bool circleDiameterDriven(const djehuti::cad::CadSketch& sketch, const juce::String& entityId)
{
    bool radius = false, diameter = false;
    for (const auto& c : sketch.constraints) if (c.firstEntity == entityId) { radius = radius || c.type == "radius"; diameter = diameter || c.type == "diameter"; }
    return diameter && !radius;
}
void styleButton(juce::TextButton& button, const juce::String& tip, bool accent = false)
{
    button.setColour(juce::TextButton::buttonColourId, accent ? accentColour : juce::Colour(0xff1c252c));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2f5560));
    button.setColour(juce::TextButton::textColourOffId, textColour);
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    button.setTooltip(tip);
}
}

//==============================================================================
// A small vertical form whose rows are created for the active task only.
class CadWorkspacePanel::TaskForm final : public juce::Component
{
public:
    std::function<void()> onChange;

    void addHeader(const juce::String& text)
    {
        auto& row = add({}, 28);
        row.caption = makeLabel(text, textColour, 13.5f, true);
    }
    void addNote(const juce::String& key, const juce::String& text, int lines = 2)
    {
        auto& row = add(key, 18 * lines + 2);
        row.caption = makeLabel(text, dimTextColour, 13.0f, false);
        row.caption->setJustificationType(juce::Justification::topLeft);
        row.wide = true;
    }
    juce::TextEditor& addNumber(const juce::String& key, const juce::String& caption, double value, const juce::String& unit, const juce::String& tip)
    {
        auto& editor = addText(key, caption, juce::String(value, 4).trimCharactersAtEnd("0").trimCharactersAtEnd("."), tip);
        editor.setInputRestrictions(24, "0123456789.-");
        rows.back().unit = makeLabel(unit, dimTextColour, 13.0f, false);
        return editor;
    }
    juce::TextEditor& addText(const juce::String& key, const juce::String& caption, const juce::String& value, const juce::String& tip)
    {
        auto& row = add(key, 28);
        row.caption = makeLabel(caption, dimTextColour, 13.5f, false);
        auto editor = std::make_unique<juce::TextEditor>();
        editor->setText(value, false); editor->setTooltip(tip);
        editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1c252c)); editor->setColour(juce::TextEditor::textColourId, textColour);
        editor->setColour(juce::TextEditor::outlineColourId, borderColour);
        editor->onTextChange = [this] { if (onChange) onChange(); };
        auto& result = *editor; row.control = std::move(editor); addAndMakeVisible(*row.control); return result;
    }
    juce::TextEditor& addMultiline(const juce::String& key, const juce::String& caption, const juce::String& value, const juce::String& tip)
    {
        auto& editor=addText(key,caption,value,tip);editor.setMultiLine(true);editor.setReturnKeyStartsNewLine(true);rows.back().height=116;return editor;
    }
    juce::ToggleButton& addToggle(const juce::String& key, const juce::String& caption, bool state, const juce::String& tip)
    {
        auto& row = add(key, 26);
        auto toggle = std::make_unique<juce::ToggleButton>(caption);
        toggle->setToggleState(state, juce::dontSendNotification); toggle->setTooltip(tip);
        toggle->setColour(juce::ToggleButton::textColourId, textColour);
        toggle->onClick = [this] { if (onChange) onChange(); };
        row.wide = true;
        auto& result = *toggle; row.control = std::move(toggle); addAndMakeVisible(*row.control); return result;
    }
    juce::ComboBox& addChoice(const juce::String& key, const juce::String& caption, const juce::StringArray& items, int selected, const juce::String& tip)
    {
        auto& row = add(key, 28);
        row.caption = makeLabel(caption, dimTextColour, 13.5f, false);
        auto box = std::make_unique<juce::ComboBox>();
        box->addItemList(items, 1); box->setSelectedItemIndex(selected, juce::dontSendNotification); box->setTooltip(tip);
        box->onChange = [this] { if (onChange) onChange(); };
        auto& result = *box; row.control = std::move(box); addAndMakeVisible(*row.control); return result;
    }
    juce::TextButton& addSlot(const juce::String& key, const juce::String& caption, const juce::String& value, juce::Colour colour, bool active, std::function<void()> click, const juce::String& tip)
    {
        auto& row = add(key, 32);
        row.caption = makeLabel(caption, colour, 13.5f, true);
        auto button = std::make_unique<juce::TextButton>(value);
        styleButton(*button, tip);
        button->setColour(juce::TextButton::buttonColourId, active ? juce::Colour(0xff23343b) : juce::Colour(0xff1a2228));
        button->setColour(juce::TextButton::textColourOffId, value.startsWith("Select") ? dimTextColour : textColour);
        button->onClick = std::move(click);
        auto& result = *button; row.control = std::move(button); addAndMakeVisible(*row.control); return result;
    }
    juce::TextButton& addButton(const juce::String& key, const juce::String& text, std::function<void()> click, const juce::String& tip)
    {
        auto& row = add(key, 30);
        auto button = std::make_unique<juce::TextButton>(text);
        styleButton(*button, tip);
        button->onClick = std::move(click);
        row.wide = true;
        auto& result = *button; row.control = std::move(button); addAndMakeVisible(*row.control); return result;
    }
    void setRowVisible(const juce::String& key, bool visible)
    {
        for (auto& row : rows) if (row.key == key) { row.visible = visible; if (row.caption) row.caption->setVisible(visible); if (row.unit) row.unit->setVisible(visible); if (row.control) row.control->setVisible(visible); }
        resized();
    }
    void setCaption(const juce::String& key, const juce::String& text) { for (auto& row : rows) if (row.key == key && row.caption) row.caption->setText(text, juce::dontSendNotification); }
    juce::Component* control(const juce::String& key) const { for (const auto& row : rows) if (row.key == key) return row.control.get(); return nullptr; }
    juce::String text(const juce::String& key) const { if (auto* e = dynamic_cast<juce::TextEditor*>(control(key))) return e->getText().trim(); return {}; }
    double number(const juce::String& key) const { return text(key).getDoubleValue(); }
    bool numberValid(const juce::String& key) const { const auto t = text(key); return t.isNotEmpty() && t.containsAnyOf("0123456789") && std::isfinite(t.getDoubleValue()); }
    bool toggle(const juce::String& key) const { if (auto* b = dynamic_cast<juce::ToggleButton*>(control(key))) return b->getToggleState(); return false; }
    int choice(const juce::String& key) const { if (auto* b = dynamic_cast<juce::ComboBox*>(control(key))) return b->getSelectedItemIndex(); return -1; }
    juce::String choiceText(const juce::String& key) const { if (auto* b = dynamic_cast<juce::ComboBox*>(control(key))) return b->getText(); return {}; }
    bool rowVisible(const juce::String& key) const { for (const auto& row : rows) if (row.key == key) return row.visible; return false; }

    juce::NamedValueSet values() const
    {
        juce::NamedValueSet result;
        for (const auto& row : rows)
        {
            if (row.key.isEmpty() || row.control == nullptr) continue;
            if (auto* e = dynamic_cast<juce::TextEditor*>(row.control.get())) result.set(row.key, e->getText());
            else if (auto* b = dynamic_cast<juce::ToggleButton*>(row.control.get())) result.set(row.key, b->getToggleState());
            else if (auto* c = dynamic_cast<juce::ComboBox*>(row.control.get())) result.set(row.key, c->getSelectedItemIndex());
        }
        return result;
    }
    void restore(const juce::NamedValueSet& saved)
    {
        for (auto& row : rows)
        {
            if (!saved.contains(row.key) || row.control == nullptr) continue;
            const auto value = saved[row.key];
            if (auto* e = dynamic_cast<juce::TextEditor*>(row.control.get())) e->setText(value.toString(), false);
            else if (auto* b = dynamic_cast<juce::ToggleButton*>(row.control.get())) b->setToggleState((bool) value, juce::dontSendNotification);
            else if (auto* c = dynamic_cast<juce::ComboBox*>(row.control.get())) c->setSelectedItemIndex((int) value, juce::dontSendNotification);
        }
    }


    juce::var describe() const
    {
        juce::Array<juce::var> result;
        for (const auto& row : rows)
        {
            if (row.key.isEmpty() || !row.visible || !row.control) continue;
            auto* item = new juce::DynamicObject(); item->setProperty("id", row.key);
            item->setProperty("label", row.caption ? row.caption->getText() : row.key);
            item->setProperty("enabled", row.control->isEnabled());
            if (auto* e = dynamic_cast<juce::TextEditor*>(row.control.get()))
            { item->setProperty("type", row.unit ? "number" : "text"); item->setProperty("value", e->getText()); if (row.unit) item->setProperty("unit", row.unit->getText()); }
            else if (auto* t = dynamic_cast<juce::ToggleButton*>(row.control.get()))
            { item->setProperty("type", "toggle"); item->setProperty("label", t->getButtonText()); item->setProperty("value", t->getToggleState()); }
            else if (auto* c = dynamic_cast<juce::ComboBox*>(row.control.get()))
            {
                item->setProperty("type", "choice"); item->setProperty("value", c->getText());
                juce::Array<juce::var> options; for (int i = 0; i < c->getNumItems(); ++i) options.add(c->getItemText(i)); item->setProperty("options", options);
            }
            else if (auto* b = dynamic_cast<juce::TextButton*>(row.control.get()))
            { item->setProperty("type", "button"); item->setProperty("text", b->getButtonText()); }
            result.add(item);
        }
        return result;
    }
    bool assign(const juce::String& key, const juce::var& value, juce::String& error)
    {
        auto* c = control(key);
        if (!c || !rowVisible(key) || !c->isEnabled()) { error = "Field is absent, hidden or disabled: " + key; return false; }
        std::function<void()> changed;
        if (auto* e = dynamic_cast<juce::TextEditor*>(c))
        {
            bool numeric = false; for (const auto& row : rows) if (row.key == key) numeric = row.unit != nullptr;
            if (numeric)
            {
                // Consume the whole value: JUCE's getDoubleValue alone accepts trailing garbage.
                const auto text = value.toString().trim().toStdString(); char* end = nullptr;
                const auto number = std::strtod(text.c_str(), &end);
                if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(number)) { error = "Expected a finite number for " + key; return false; }
                e->setText(juce::String(number, 8), false);
            }
            else { if (!value.isString()) { error = "Expected text for " + key; return false; } e->setText(value.toString(), false); }
            changed = e->onTextChange;
        }
        else if (auto* b = dynamic_cast<juce::ToggleButton*>(c))
        { if (!value.isBool()) { error = "Expected a boolean for " + key; return false; } b->setToggleState((bool)value, juce::dontSendNotification); changed = b->onClick; }
        else if (auto* b = dynamic_cast<juce::ComboBox*>(c))
        {
            int index = -1;
            for (int i = 0; i < b->getNumItems(); ++i) if (b->getItemText(i) == value.toString()) index = i;
            if (index < 0) { error = "Choose a listed option for " + key; return false; }
            b->setSelectedItemIndex(index, juce::dontSendNotification); changed = b->onChange;
        }
        else { error = "Use click for buttons."; return false; }
        if (changed) changed(); // Copy before invoking: a callback may rebuild and destroy this form.
        return true;
    }
    int preferredHeight() const { int h = 4; for (const auto& row : rows) if (row.visible) h += row.height + 4; return h + 4; }
    void resized() override
    {
        auto area = getLocalBounds().reduced(2, 4);
        const auto captionWidth = juce::jlimit(70, 140, area.getWidth() * 2 / 5);
        for (auto& row : rows)
        {
            if (!row.visible) continue;
            auto line = area.removeFromTop(row.height); area.removeFromTop(4);
            if (row.control == nullptr) { if (row.caption) row.caption->setBounds(line); continue; }
            if (row.wide) { row.control->setBounds(line.withSizeKeepingCentre(line.getWidth(), juce::jmin(line.getHeight(), 28))); continue; }
            if (row.caption) row.caption->setBounds(line.removeFromLeft(captionWidth));
            if (row.unit) row.unit->setBounds(line.removeFromRight(30).withTrimmedLeft(6));
            row.control->setBounds(line.withSizeKeepingCentre(line.getWidth(), juce::jmin(line.getHeight(), 28)));
        }
    }

private:
    struct Row
    {
        juce::String key;
        int height = 26;
        bool visible = true, wide = false;
        std::unique_ptr<juce::Label> caption, unit;
        std::unique_ptr<juce::Component> control;
    };
    Row& add(const juce::String& key, int height) { rows.emplace_back(); rows.back().key = key; rows.back().height = height; return rows.back(); }
    std::unique_ptr<juce::Label> makeLabel(const juce::String& text, juce::Colour colour, float size, bool bold)
    {
        auto label = std::make_unique<juce::Label>(juce::String(), text);
        label->setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain)); label->setColour(juce::Label::textColourId, colour);
        label->setMinimumHorizontalScale(0.8f); label->setBorderSize({ 1, 2, 1, 4 });
        addAndMakeVisible(*label);
        return label;
    }
    std::deque<Row> rows;
};

//==============================================================================
class CadWorkspacePanel::GroupItem final : public juce::TreeViewItem
{
public:
    explicit GroupItem(juce::String textIn) : text(std::move(textIn)) {}
    bool mightContainSubItems() override { return getNumSubItems() > 0; }
    bool canBeSelected() const override { return false; }
    int getItemHeight() const override { return 24; }
    void paintItem(juce::Graphics& g, int w, int h) override { g.setColour(dimTextColour); g.setFont(juce::Font(13.0f)); g.drawText(text, 4, 0, w - 8, h, juce::Justification::centredLeft, true); }
private:
    juce::String text;
};

class CadWorkspacePanel::FeatureItem final : public juce::TreeViewItem
{
public:
    FeatureItem(CadWorkspacePanel& owner, const CadFeature& feature) : panel(owner), id(feature.id), name(feature.name), type(feature.type), visible(feature.visible) {}
    FeatureItem(CadWorkspacePanel& owner, juce::String idIn, juce::String nameIn, juce::String typeIn) : panel(owner), id(std::move(idIn)), name(std::move(nameIn)), type(std::move(typeIn)), visible(true) {}
    bool mightContainSubItems() override { return false; }
    int getItemHeight() const override { return 26; }
    juce::String getUniqueName() const override { return id; }
    const juce::String& featureId() const { return id; }
    void paintItem(juce::Graphics& g, int w, int h) override
    {
        const auto highlight = panel.colourFor(id);
        if (isSelected() || highlight != juce::Colour()) { g.setColour(juce::Colour(0xff22313a)); g.fillRoundedRectangle(1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 4.0f); }
        g.setColour(highlight != juce::Colour() ? highlight : (visible ? textColour : dimTextColour.withAlpha(0.8f)));
        g.setFont(juce::Font(14.0f));
        g.drawText(name, 8, 0, w - 66, h, juce::Justification::centredLeft, true);
        g.setColour(dimTextColour); g.setFont(juce::Font(11.5f));
        g.drawText(visible || type == "sketch" ? type : "hidden", w - 60, 0, 56, h, juce::Justification::centredRight, true);
    }
    void itemClicked(const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) panel.showFeatureMenu(id); else panel.featureClicked(id); }
    void itemDoubleClicked(const juce::MouseEvent&) override { if (type == "sketch") panel.enterSketch(id); }
    juce::String getTooltip() override { return name + " (" + type + (visible ? "" : ", hidden") + ")  Right-click for actions."; }
private:
    CadWorkspacePanel& panel;
    juce::String id, name, type;
    bool visible;
};

//==============================================================================
// Large sketch drawing surface in plane coordinates (millimetres) with zoom, pan, previews and snapping.
class CadWorkspacePanel::SketchCanvas final : public juce::Component
{
public:
    explicit SketchCanvas(CadWorkspacePanel& ownerIn) : owner(ownerIn) { setWantsKeyboardFocus(true); }
    void setSketch(const juce::String& id) { sketchId = id; gestureActive = false; needsFit = true; if (getWidth() > 0) fitToContent(); repaint(); }
    void setSelection(const juce::String& first, const juce::String& second) { selectedEntity = first; secondEntity = second; repaint(); }
    juce::String tool() const { return mode; }
    void setTool(const juce::String& next) { cancelGesture(); mode = next; setMouseCursor(mode == "select" ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor); repaint(); }
    bool escape()
    {
        if (gestureActive || pointDragActive) { cancelGesture(); owner.setMessage("Gesture cancelled; committed sketch geometry is unchanged.", 0); return true; }
        if (mode != "select") { owner.setSketchTool("select"); return true; }
        if (selectedEntity.isNotEmpty()) { owner.selectSketchEntity({}, false); return true; }
        return false;
    }
    bool hasActiveGesture() const { return gestureActive || pointDragActive; }
    void cancelGesture() { gestureActive = false; panning = false; pointDragActive = false; dragRef.clear(); snapTarget.clear(); repaint(); }
    void fitToContent()
    {
        needsFit = false;
        double minX = 0, minY = 0, maxX = 0, maxY = 0; bool found = false;
        auto include = [&](double x, double y) { if (!found) { minX = maxX = x; minY = maxY = y; found = true; } else { minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y); } };
        if (const auto* sketch = owner.service->document().findSketch(sketchId))
            for (const auto& e : sketch->entities)
            {
                if (e.type == "circle") { include(e.x1 - e.radius, e.y1 - e.radius); include(e.x1 + e.radius, e.y1 + e.radius); }
                else { include(e.x1, e.y1); include(e.x2, e.y2); if (e.type == "bezier") { include(e.cx1, e.cy1); include(e.cx2, e.cy2); } }
            }
        if (!found) { minX = minY = -50.0; maxX = maxY = 50.0; }
        const auto span = std::max({ maxX - minX, maxY - minY, 10.0 });
        centre = { (minX + maxX) * 0.5, (minY + maxY) * 0.5 };
        scale = juce::jlimit(0.05, 400.0, 0.75 * std::max(1, std::min(getWidth(), getHeight())) / span);
        repaint();
    }
    void resized() override { if (needsFit && getWidth() > 0) fitToContent(); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b1014));
        const auto* sketch = owner.service->document().findSketch(sketchId);
        const auto plane = sketch != nullptr ? (sketch->onFace() ? juce::String("UV") : sketch->plane) : juce::String("XY");
        const auto step = gridStep();
        const auto bottomLeft = fromPoint({ 0.0f, (float) getHeight() }), topRight = fromPoint({ (float) getWidth(), 0.0f });
        for (auto i = std::floor(bottomLeft.x / step); i * step <= topRight.x; i += 1.0)
        {
            g.setColour(std::fmod(std::abs(i), 5.0) < 0.5 ? juce::Colour(0xff1d2e35) : juce::Colour(0xff142026));
            g.drawVerticalLine(juce::roundToInt(toPoint(i * step, 0).x), 0.0f, (float) getHeight());
        }
        for (auto i = std::floor(bottomLeft.y / step); i * step <= topRight.y; i += 1.0)
        {
            g.setColour(std::fmod(std::abs(i), 5.0) < 0.5 ? juce::Colour(0xff1d2e35) : juce::Colour(0xff142026));
            g.drawHorizontalLine(juce::roundToInt(toPoint(0, i * step).y), 0.0f, (float) getWidth());
        }
        const auto origin = toPoint(0, 0);
        g.setColour(juce::Colour(0xffc0605a)); g.drawHorizontalLine(juce::roundToInt(origin.y), 0.0f, (float) getWidth());
        g.setColour(juce::Colour(0xff5aa06a)); g.drawVerticalLine(juce::roundToInt(origin.x), 0.0f, (float) getHeight());
        g.setFont(juce::Font(12.0f));
        g.setColour(juce::Colour(0xffc0605a)); g.drawText(plane.substring(0, 1), getWidth() - 24, juce::roundToInt(origin.y) - 18, 20, 16, juce::Justification::centred);
        g.setColour(juce::Colour(0xff5aa06a)); g.drawText(plane.substring(1, 2), juce::roundToInt(origin.x) + 4, 4, 20, 16, juce::Justification::centredLeft);

        if (sketch != nullptr)
        {
            // While a vertex or handle is dragged the canvas shows the moved geometry; the document changes once, on release.
            const auto entities = pointDragActive ? draggedEntities(*sketch) : sketch->entities;
            for (const auto& entity : entities)
            {
                g.setColour(entity.id == selectedEntity ? selectedColour : entity.id == secondEntity ? juce::Colour(0xffc39bff) : juce::Colour(0xff78dcca));
                const auto thickness = entity.id == selectedEntity || entity.id == secondEntity ? 3.0f : 2.0f;
                if (entity.type == "circle") { const auto c = toPoint(entity.x1, entity.y1); const auto r = (float) (entity.radius * scale); g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, thickness); }
                else if (entity.type == "bezier") g.strokePath(curvePath(entity), juce::PathStrokeType(thickness));
                else { const auto a = toPoint(entity.x1, entity.y1), b = toPoint(entity.x2, entity.y2); g.drawLine(a.x, a.y, b.x, b.y, thickness); }
            }
            // Handles of the selected curve: thin lines from each endpoint to its square control point.
            for (const auto& entity : entities)
                if (entity.type == "bezier" && entity.id == selectedEntity)
                    for (const auto& [px, py, cx, cy, ref] : { std::tuple<double, double, double, double, juce::String> { entity.x1, entity.y1, entity.cx1, entity.cy1, entity.id + ":c1" }, { entity.x2, entity.y2, entity.cx2, entity.cy2, entity.id + ":c2" } })
                    {
                        const auto a = toPoint(px, py), c = toPoint(cx, cy);
                        g.setColour(juce::Colour(0xffb0c4cc)); g.drawLine(a.x, a.y, c.x, c.y, 1.0f);
                        const bool hot = ref == hoverRef || (pointDragActive && ref == dragRef);
                        g.setColour(hot ? juce::Colour(0xffffdf7e) : juce::Colour(0xffe8f0f2));
                        g.fillRect(c.x - (hot ? 5.0f : 4.0f), c.y - (hot ? 5.0f : 4.0f), hot ? 10.0f : 8.0f, hot ? 10.0f : 8.0f);
                    }
            // Endpoints of the selected entity as small dots; the selected vertex filled; the hovered one ringed.
            for (const auto& entity : entities)
                if (entity.id == selectedEntity && entity.type != "circle")
                    for (const auto& [px, py, ref] : { std::tuple<double, double, juce::String> { entity.x1, entity.y1, entity.id + ":p1" }, { entity.x2, entity.y2, entity.id + ":p2" } })
                    {
                        const auto p = toPoint(px, py);
                        g.setColour(ref == selectedPoint ? juce::Colour(0xffff9f43) : selectedColour);
                        const auto r = ref == selectedPoint ? 5.0f : 3.0f;
                        g.fillEllipse(p.x - r, p.y - r, r * 2.0f, r * 2.0f);
                    }
            if (hoverRef.isNotEmpty() && !hoverRef.endsWith(":c1") && !hoverRef.endsWith(":c2") && !gestureActive)
            {
                const auto p = toPoint(hoverPoint.x, hoverPoint.y);
                g.setColour(juce::Colour(0xffffdf7e)); g.drawEllipse(p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f, 2.0f);
            }
        }

        if (gestureActive && mode != "select")
        {
            const auto a = toPoint(start.x, start.y), b = toPoint(current.x, current.y);
            g.setColour(juce::Colour(0xffffdf7e));
            juce::String readout;
            if (mode == "rectangle") { g.drawRect(juce::Rectangle<float>(a, b), 1.5f); readout = juce::String(std::abs(current.x - start.x), 2) + " x " + juce::String(std::abs(current.y - start.y), 2) + " mm"; }
            else if (mode == "circle") { const auto r = (float) (std::hypot(current.x - start.x, current.y - start.y) * scale); g.drawEllipse(a.x - r, a.y - r, r * 2.0f, r * 2.0f, 1.5f); g.drawLine(a.x, a.y, b.x, b.y, 1.0f); readout = "R " + juce::String(std::hypot(current.x - start.x, current.y - start.y), 2) + " mm"; }
            else if (mode == "curve") { g.strokePath(curvePath(defaultCurve(start, current)), juce::PathStrokeType(1.5f)); readout = "Curve, chord " + juce::String(std::hypot(current.x - start.x, current.y - start.y), 2) + " mm"; }
            else { g.drawLine(a.x, a.y, b.x, b.y, 1.5f); readout = "L " + juce::String(std::hypot(current.x - start.x, current.y - start.y), 2) + " mm"; }
            g.setFont(juce::Font(13.0f)); g.drawText(readout, juce::roundToInt(b.x) + 12, juce::roundToInt(b.y) + 6, 160, 18, juce::Justification::centredLeft);
        }
        if (snapTarget.isNotEmpty()) { g.setColour(juce::Colour(0xffffdf7e)); const auto p = toPoint(snapPoint.x, snapPoint.y); g.drawEllipse(p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f, 2.0f); }

        g.setColour(dimTextColour); g.setFont(juce::Font(12.5f));
        g.drawText(plane + " plane  |  grid " + juce::String(step, step < 1.0 ? 1 : 0) + " mm  |  wheel zooms, right-drag pans", 10, getHeight() - 24, getWidth() - 220, 18, juce::Justification::centredLeft);
        if (hasCursor) g.drawText(plane.substring(0, 1) + " " + juce::String(cursor.x, 2) + "   " + plane.substring(1, 2) + " " + juce::String(cursor.y, 2) + " mm", getWidth() - 210, getHeight() - 24, 200, 18, juce::Justification::centredRight);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown()) { panning = true; panStart = e.position; return; }
        if (mode == "select")
        {
            // Pressing on a hovered vertex or handle starts a point drag; it commits on release (one undo step).
            updateHover(e.position);
            if (hoverRef.isNotEmpty() && !e.mods.isShiftDown()) { pointDragActive = true; dragMoved = false; dragRef = hoverRef; dragPress = e.position; dragTo = hoverPoint; }
            return;
        }
        gestureActive = true; start = current = fromPoint(e.position); startConnection.clear(); snapTarget.clear();
        if (snapsEnds()) { const auto snap = nearestSnap(start); start = current = snap.point; startConnection = snap.target; snapTarget = snap.target; snapPoint = snap.point; }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        updateCursor(e.position);
        if (panning) { const auto d = e.position - panStart; panStart = e.position; centre.x -= d.x / scale; centre.y += d.y / scale; repaint(); return; }
        if (pointDragActive)
        {
            if (e.position.getDistanceFrom(dragPress) >= 3.0f) dragMoved = true;
            if (dragMoved) dragTo = fromPoint(e.position);
            repaint(); return;
        }
        if (!gestureActive) return;
        const auto raw = fromPoint(e.position); const auto snap = snapsEnds() ? nearestSnap(raw) : Snap { raw, {} };
        current = snap.point; snapTarget = snap.target; snapPoint = snap.point; repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (panning) { panning = false; return; }
        if (pointDragActive)
        {
            pointDragActive = false;
            const auto ref = dragRef; dragRef.clear();
            if (dragMoved) owner.moveSketchPoint(ref, dragTo.x, dragTo.y);
            else if (ref.endsWith(":p1") || ref.endsWith(":p2")) owner.selectSketchVertex(ref);
            else owner.selectSketchEntity(ref.upToFirstOccurrenceOf(":", false, false), false);
            updateHover(e.position);
            repaint(); return;
        }
        if (mode == "select")
        {
            const auto hit = entityAt(fromPoint(e.position));
            owner.selectSketchEntity(hit, e.mods.isShiftDown() && hit.isNotEmpty());
            return;
        }
        if (!gestureActive) return;
        gestureActive = false;
        const auto endSnap = snapsEnds() ? nearestSnap(fromPoint(e.position)) : Snap { fromPoint(e.position), {} };
        const auto end = endSnap.point; snapTarget.clear();
        if ((toPoint(end.x, end.y) - toPoint(start.x, start.y)).getDistanceFromOrigin() < 3.0f) { repaint(); return; }
        juce::String error; commitDrawing(end.x, end.y, endSnap.target, error);
    }
    bool commitDrawing(double x, double y, const juce::String& endConnection, juce::String& error)
    {
        const juce::Point<double> end(x, y);
        juce::String entityId; bool ok = false;
        if (mode == "rectangle") ok = owner.service->addSketchRectangle(sketchId, std::min(start.x, end.x), std::min(start.y, end.y), std::abs(end.x - start.x), std::abs(end.y - start.y), entityId, error);
        else if (mode == "circle") ok = owner.service->addSketchCircle(sketchId, start.x, start.y, std::hypot(end.x - start.x, end.y - start.y), entityId, error);
        else if (mode == "line") ok = owner.service->addSketchLine(sketchId, start.x, start.y, end.x, end.y, entityId, error, startConnection, endConnection);
        else if (mode == "curve")
        {
            const auto c = defaultCurve(start, { end.x, end.y });
            ok = owner.service->addSketchBezier(sketchId, c.x1, c.y1, c.cx1, c.cy1, c.cx2, c.cy2, c.x2, c.y2, entityId, error, startConnection, endConnection);
        }
        if (ok) owner.sketchGeometryAdded(mode); else owner.showError(error);
        repaint();
        return ok;
    }
    bool automateGesture(const juce::var& args, juce::String& error)
    {
        const auto phase = args.getProperty("phase", "").toString();
        if (phase == "cancel") { cancelGesture(); return true; }
        if (mode == "select") { error = "Choose Line, Rectangle, Circle or Curve first."; return false; }
        for (const auto* key : { "x", "y" })
        { const auto n = args.getProperty(key, {}); if (!(n.isInt() || n.isInt64() || n.isDouble()) || !std::isfinite((double)n)) { error = "Sketch x/y must be finite millimetre coordinates."; return false; } }
        const auto x = (double)args["x"], y = (double)args["y"];
        const auto snap = snapsEnds() ? nearestSnap({x,y}) : Snap{{x,y},{}};
        if (phase == "begin") { if (gestureActive) { error = "Finish or cancel the active gesture first."; return false; } start = current = snap.point; startConnection = snap.target; gestureActive = true; }
        else if (phase == "update" || phase == "commit")
        {
            if (!gestureActive) { error = "Begin a sketch gesture first."; return false; }
            current = snap.point;
            if (phase == "commit") { gestureActive = false; snapTarget.clear(); return commitDrawing(current.x, current.y, snap.target, error); }
        }
        else { error = "Unknown sketch phase."; return false; }
        snapTarget = snap.target; snapPoint = snap.point; repaint(); return true;
    }
    juce::var gestureState() const
    {
        auto* o = new juce::DynamicObject(); o->setProperty("active", gestureActive); o->setProperty("tool", mode);
        o->setProperty("startX", start.x); o->setProperty("startY", start.y); o->setProperty("x", current.x); o->setProperty("y", current.y); return o;
    }
    void mouseMove(const juce::MouseEvent& e) override { updateHover(e.position); updateCursor(e.position); }
    void mouseExit(const juce::MouseEvent&) override { hasCursor = false; hoverRef.clear(); repaint(); }
    // Double-click on a line (Select tool) inserts a vertex there.
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (mode != "select" || e.mods.isRightButtonDown()) return;
        const auto p = fromPoint(e.position);
        const auto hit = entityAt(p);
        if (hit.startsWith("line-") && hoverRef.isEmpty()) owner.insertSketchVertex(hit, p.x, p.y);
    }
    void setSelectedPoint(const juce::String& ref) { selectedPoint = ref; repaint(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto anchor = fromPoint(e.position);
        scale = juce::jlimit(0.05, 400.0, scale * std::exp(w.deltaY * 1.2));
        const auto moved = fromPoint(e.position);
        centre.x += anchor.x - moved.x; centre.y += anchor.y - moved.y; repaint();
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        return key == juce::KeyPress::escapeKey && escape();
    }

private:
    struct Point { double x = 0, y = 0; };
    struct Snap { Point point; juce::String target; };
    double gridStep() const { for (const auto s : { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0 }) if (s * scale >= 16.0) return s; return 1000.0; }
    juce::Point<float> toPoint(double x, double y) const { return { (float) (getWidth() * 0.5 + (x - centre.x) * scale), (float) (getHeight() * 0.5 - (y - centre.y) * scale) }; }
    Point fromPoint(juce::Point<float> p) const { return { centre.x + (p.x - getWidth() * 0.5) / scale, centre.y + (getHeight() * 0.5 - p.y) / scale }; }
    void updateCursor(juce::Point<float> p) { cursor = fromPoint(p); hasCursor = true; repaint(); }
    bool snapsEnds() const { return mode == "line" || mode == "curve"; }
    static double bezierAt(double t, double p0, double p1, double p2, double p3) { const auto s = 1.0 - t; return s * s * s * p0 + 3 * s * s * t * p1 + 3 * s * t * t * p2 + t * t * t * p3; }
    juce::Path curvePath(const djehuti::cad::CadSketchEntity& c) const
    {
        juce::Path path; const auto a = toPoint(c.x1, c.y1), c1 = toPoint(c.cx1, c.cy1), c2 = toPoint(c.cx2, c.cy2), b = toPoint(c.x2, c.y2);
        path.startNewSubPath(a); path.cubicTo(c1, c2, b); return path;
    }
    // A new curve from a drag: control points at the thirds of the chord, lifted to one side so it is visibly curved.
    static djehuti::cad::CadSketchEntity defaultCurve(Point a, Point b)
    {
        djehuti::cad::CadSketchEntity c; c.type = "bezier";
        const auto dx = b.x - a.x, dy = b.y - a.y; const auto nx = -dy * 0.3, ny = dx * 0.3;
        c.x1 = a.x; c.y1 = a.y; c.x2 = b.x; c.y2 = b.y;
        c.cx1 = a.x + dx / 3.0 + nx; c.cy1 = a.y + dy / 3.0 + ny; c.cx2 = a.x + 2.0 * dx / 3.0 + nx; c.cy2 = a.y + 2.0 * dy / 3.0 + ny;
        return c;
    }
    static double distanceToCurve(double px, double py, const djehuti::cad::CadSketchEntity& c)
    {
        double best = std::numeric_limits<double>::max(), ax = c.x1, ay = c.y1;
        for (int k = 1; k <= 48; ++k) { const auto t = k / 48.0; const auto bx = bezierAt(t, c.x1, c.cx1, c.cx2, c.x2), by = bezierAt(t, c.y1, c.cy1, c.cy2, c.y2); best = std::min(best, distanceToSegment(px, py, ax, ay, bx, by)); ax = bx; ay = by; }
        return best;
    }
    juce::String entityAt(Point p) const
    {
        juce::String hit; double best = 8.0 / scale;
        if (const auto* sketch = owner.service->document().findSketch(sketchId))
            for (const auto& entity : sketch->entities)
            {
                const auto d = entity.type == "circle" ? std::abs(std::hypot(p.x - entity.x1, p.y - entity.y1) - entity.radius)
                             : entity.type == "bezier" ? distanceToCurve(p.x, p.y, entity) : distanceToSegment(p.x, p.y, entity.x1, entity.y1, entity.x2, entity.y2);
                if (d < best) { best = d; hit = entity.id; }
            }
        return hit;
    }
    // The vertex or handle under the pointer (8 px, independent of zoom): endpoints of lines and curves, and the
    // handles of the selected curve. Shown before any click; drawing tools use the same rings as snap targets.
    void updateHover(juce::Point<float> position)
    {
        juce::String ref; Point at; float bestPixels = 8.0f;
        if (!pointDragActive && !gestureActive && (mode == "select" || snapsEnds()))
            if (const auto* sketch = owner.service->document().findSketch(sketchId))
                for (const auto& entity : sketch->entities)
                {
                    if (entity.type != "line" && entity.type != "bezier") continue;
                    std::vector<std::tuple<double, double, juce::String>> points { { entity.x1, entity.y1, entity.id + ":p1" }, { entity.x2, entity.y2, entity.id + ":p2" } };
                    if (entity.type == "bezier" && entity.id == selectedEntity && mode == "select") { points.push_back({ entity.cx1, entity.cy1, entity.id + ":c1" }); points.push_back({ entity.cx2, entity.cy2, entity.id + ":c2" }); }
                    for (const auto& [px, py, r] : points)
                    {
                        const auto pixels = toPoint(px, py).getDistanceFrom(position);
                        if (pixels < bestPixels || (pixels <= bestPixels && (r.endsWith(":c1") || r.endsWith(":c2")))) { bestPixels = pixels; ref = r; at = { px, py }; }
                    }
                }
        if (ref != hoverRef || (ref.isNotEmpty() && (at.x != hoverPoint.x || at.y != hoverPoint.y))) { hoverRef = ref; hoverPoint = at; repaint(); }
        setMouseCursor(mode != "select" ? juce::MouseCursor::CrosshairCursor : hoverRef.isNotEmpty() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    // The sketch as it will look if the dragged point is released here: an endpoint carries every endpoint joined to
    // it (coincident constraints and same position) and its curve handle; a handle moves alone.
    std::vector<djehuti::cad::CadSketchEntity> draggedEntities(const djehuti::cad::CadSketch& sketch) const
    {
        auto entities = sketch.entities;
        const auto ref = dragRef; const auto colon = ref.indexOfChar(':');
        if (!dragMoved || colon <= 0) return entities;
        const auto id = ref.substring(0, colon), which = ref.substring(colon + 1);
        const auto px = [&](djehuti::cad::CadSketchEntity& e, const juce::String& w) -> double& { return w == "p1" ? e.x1 : w == "p2" ? e.x2 : w == "c1" ? e.cx1 : e.cx2; };
        const auto py = [&](djehuti::cad::CadSketchEntity& e, const juce::String& w) -> double& { return w == "p1" ? e.y1 : w == "p2" ? e.y2 : w == "c1" ? e.cy1 : e.cy2; };
        const auto find = [&](const juce::String& eid) -> djehuti::cad::CadSketchEntity* { for (auto& e : entities) if (e.id == eid) return &e; return nullptr; };
        if (which.startsWith("c")) { if (auto* e = find(id)) { px(*e, which) = dragTo.x; py(*e, which) = dragTo.y; } return entities; }
        juce::StringArray joined { ref };
        for (int i = 0; i < joined.size(); ++i)
        {
            const auto jc = joined[i].indexOfChar(':'); auto* e = find(joined[i].substring(0, jc)); if (e == nullptr) continue;
            const auto w = joined[i].substring(jc + 1); const auto x = px(*e, w), y = py(*e, w);
            for (const auto& c : sketch.constraints) if (c.type == "coincident") { if (c.firstEntity == joined[i]) joined.addIfNotAlreadyThere(c.secondEntity); else if (c.secondEntity == joined[i]) joined.addIfNotAlreadyThere(c.firstEntity); }
            for (const auto& o : sketch.entities) if (o.type == "line" || o.type == "bezier") { if (std::hypot(o.x1 - x, o.y1 - y) < 1.0e-7) joined.addIfNotAlreadyThere(o.id + ":p1"); if (std::hypot(o.x2 - x, o.y2 - y) < 1.0e-7) joined.addIfNotAlreadyThere(o.id + ":p2"); }
        }
        auto* origin = find(id); if (origin == nullptr) return entities;
        const auto dx = dragTo.x - px(*origin, which), dy = dragTo.y - py(*origin, which);
        for (const auto& j : joined)
        {
            const auto jc = j.indexOfChar(':'); auto* e = find(j.substring(0, jc)); if (e == nullptr) continue;
            const auto w = j.substring(jc + 1);
            px(*e, w) = dragTo.x; py(*e, w) = dragTo.y;
            if (e->type == "bezier") { const auto c = w == "p1" ? juce::String("c1") : juce::String("c2"); px(*e, c) += dx; py(*e, c) += dy; }
        }
        return entities;
    }
    Snap nearestSnap(Point candidate) const
    {
        Snap result { candidate, {} }; double bestPixels = 8.0;
        if (const auto* sketch = owner.service->document().findSketch(sketchId))
            for (const auto& entity : sketch->entities)
                if (entity.type == "line" || entity.type == "bezier")
                    for (const auto& endpoint : { std::pair<double, double> { entity.x1, entity.y1 }, std::pair<double, double> { entity.x2, entity.y2 } })
                    {
                        const auto pixels = toPoint(endpoint.first, endpoint.second).getDistanceFrom(toPoint(candidate.x, candidate.y));
                        if (pixels < bestPixels) { bestPixels = pixels; result.point = { endpoint.first, endpoint.second }; result.target = entity.id + (endpoint.first == entity.x1 && endpoint.second == entity.y1 ? ":p1" : ":p2"); }
                    }
        return result;
    }
    static double distanceToSegment(double px, double py, double ax, double ay, double bx, double by) { const auto dx = bx - ax, dy = by - ay, q = dx * dx + dy * dy; const auto t = q > 0 ? juce::jlimit(0.0, 1.0, ((px - ax) * dx + (py - ay) * dy) / q) : 0.0; return std::hypot(px - (ax + t * dx), py - (ay + t * dy)); }

    CadWorkspacePanel& owner;
    juce::String sketchId, selectedEntity, secondEntity, mode { "select" }, snapTarget, startConnection;
    juce::String hoverRef, selectedPoint, dragRef;   // point references "<entity>:p1|p2|c1|c2"
    Point start, current, snapPoint, centre, cursor, hoverPoint, dragTo;
    juce::Point<float> panStart, dragPress;
    double scale = 4.0;
    bool gestureActive = false, panning = false, needsFit = true, hasCursor = false, pointDragActive = false, dragMoved = false;
};

//==============================================================================
CadWorkspacePanel::~CadWorkspacePanel() { if (keyHost != nullptr) keyHost->removeKeyListener(this); stopTimer(); delete materialsManager.getComponent(); tree.setRootItem(nullptr); }

CadWorkspacePanel::CadWorkspacePanel(std::shared_ptr<djehuti::cad::CadCommandService> s) : service(std::move(s)), sketchCanvas(std::make_unique<SketchCanvas>(*this))
{
    setWantsKeyboardFocus(true);
    modeLabel.setFont(juce::Font(17.0f, juce::Font::bold)); modeLabel.setColour(juce::Label::textColourId, textColour);
    treeHeading.setText("Model", juce::dontSendNotification); treeHeading.setFont(juce::Font(14.0f, juce::Font::bold)); treeHeading.setColour(juce::Label::textColourId, textColour);
    statusLabel.setColour(juce::Label::textColourId, dimTextColour); unitsLabel.setColour(juce::Label::textColourId, dimTextColour);
    unitsLabel.setText("Millimetres  |  Right-handed XYZ", juce::dontSendNotification); unitsLabel.setJustificationType(juce::Justification::centredRight);
    taskTitle.setFont(juce::Font(17.0f, juce::Font::bold)); taskTitle.setColour(juce::Label::textColourId, textColour);
    taskInstruction.setColour(juce::Label::textColourId, dimTextColour); taskInstruction.setJustificationType(juce::Justification::topLeft); taskInstruction.setMinimumHorizontalScale(1.0f);
    messageLabel.setJustificationType(juce::Justification::topLeft); messageLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(workspaceMenu);
    workspaceMenu.sections = [this]
    {
        using E = WorkspaceMenu::Entry; using S = WorkspaceMenu::Section;
        const bool sketching = editingSketchId.isNotEmpty();
        return std::vector<S> {
            { "Edit", { E{1,"Undo"}, E{2,"Redo"}, E{36,"Materials and Sheet Stock...",!sketching} } },
            { "Create", { E{3,"Sketch...",!sketching}, E{4,"Box...",!sketching}, E{5,"Cylinder...",!sketching} } },
            { "Modify", { E{6,"Extrude...",!sketching}, E{37,"Revolve...",!sketching}, E{38,"Virtual Lathe...",!sketching}, E{7,"Cut sketch...",!sketching}, E{34,"Fillet...",!sketching}, E{8,"Subtract...",!sketching}, E{9,"Union...",!sketching}, E{10,"Intersect...",!sketching} } },
            { "Sketch", { E{11,"Select",sketching}, E{12,"Line",sketching}, E{13,"Rectangle",sketching}, E{14,"Circle",sketching}, E{35,"Curve",sketching}, E{15,"Constrain...",sketching}, E{16,"Fit sketch",sketching}, E{17,"Finish sketch",sketching} } },
            { "Assembly", { E{18,"Instance...",!sketching}, E{19,"Joint...",!sketching}, E{20,"Coupling...",!sketching}, E{21,"Motion link..."}, E{22,"Inverse kinematics...",!sketching}, E{26,"Check interference",!sketching}, E{27,"Clear interference",interferenceActive} } },
            { "View", { E{23,"Fit all"}, E{24,"Reset camera"}, E{25,"3D Views"}, E{30,"Floor grid",true,modelView.guideEnabled("floor")}, E{31,"World planes",true,modelView.guideEnabled("world")}, E{32,"Selected local planes",true,modelView.guideEnabled("local")}, E{33,"Joint frames and limits",true,modelView.guideEnabled("joints")} } }
        };
    };
    workspaceMenu.invoke = [this](int id)
    {
        const juce::StringArray names { "undo","redo","sketch","box","cylinder","extrude","cut","subtract","union","intersect","sketchSelect","line","rectangle","circle","constrain","fitSketch","finishSketch","instance","joint","coupling","link","ik","fit","reset","views" };
        if (id >= 1 && id <= names.size()) executeUiCommand(names[id-1]);
        else if (id == 26) executeUiCommand("interference");
        else if (id == 27) executeUiCommand("clearInterference");
        else if (id == 38) executeUiCommand("lathe");
        else if (id == 37) executeUiCommand("revolve");
        else if (id == 34) executeUiCommand("fillet");
        else if (id == 35) executeUiCommand("curve");
        else if (id == 36) executeUiCommand("materials");
        else if (id >= 30 && id <= 33) modelView.toggleGuide(juce::StringArray{"floor","world","local","joints"}[id-30]);
    };
    addAndMakeVisible(modeLabel); addAndMakeVisible(statusLabel); addAndMakeVisible(unitsLabel);

    struct ToolSpec { juce::TextButton* button; const char* tip; };
    for (const auto& spec : { ToolSpec { &sketchButton, "Create a sketch on an origin plane (XY, XZ or YZ)." }, ToolSpec { &boxButton, "Create a box body from width, depth and height." },
                              ToolSpec { &cylinderButton, "Create a cylinder body from radius and height." }, ToolSpec { &extrudeButton, "Extrude a closed sketch profile into a solid body." }, ToolSpec { &revolveButton, "Revolve a closed sketch profile around an axis." },
                              ToolSpec { &cutButton, "Cut a closed sketch profile from a target body." }, ToolSpec { &filletButton, "Create a constant-radius fillet on selected edges of a body." },
                              ToolSpec { &subtractButton, "Subtract a Tool body from a Target body." },
                              ToolSpec { &unionButton, "Combine two bodies into one." }, ToolSpec { &intersectButton, "Keep only the volume shared by two bodies." },
                              ToolSpec { &undoButton, "Undo the last modeling command." }, ToolSpec { &redoButton, "Redo the last undone modeling command." },
                              ToolSpec { &fitButton, "Frame all visible bodies in the model view." }, ToolSpec { &resetViewButton, "Return the model view to the default orientation." },
                              ToolSpec { &viewsButton, "Open the multi-view 3D Views workspace." }, ToolSpec { &selectToolButton, "Select sketch entities. Shift-click selects a second entity." },
                              ToolSpec { &lineButton, "Draw lines: drag from start to end. Endpoints snap to existing line ends." }, ToolSpec { &rectangleButton, "Draw rectangles: drag from corner to corner." },
                              ToolSpec { &circleButton, "Draw circles: drag from the centre outwards." }, ToolSpec { &curveButton, "Draw a cubic Bezier curve: drag from start to end, then drag its square handles to shape it." }, ToolSpec { &constrainButton, "Add a constraint to the selected sketch entity." },
                              ToolSpec { &fitSketchButton, "Frame the whole sketch in the canvas." },
                              ToolSpec { &instanceButton, "Create an assembly instance of a solid body so it can move." },
                              ToolSpec { &jointButton, "Connect two instances (or an instance and the world) with a fixed, revolute or prismatic joint." },
                              ToolSpec { &couplingButton, "Couple two joint coordinates: gear ratio, linear ratio, rack and pinion, or screw lead." },
                              ToolSpec { &linkButton, "Connect joints to DEWB simulation signals over the motion link." },
                              ToolSpec { &ikButton, "Inverse kinematics: solve selected driver joints so an endpoint reaches a target pose." },
                              ToolSpec { &bodyModeButton, "Pick whole bodies in the model view (the default)." },
                              ToolSpec { &faceModeButton, "Pick faces: hover highlights, click selects. A planar face can carry a sketch." },
                              ToolSpec { &edgeModeButton, "Pick edges: hover highlights, click selects." } })
    {
        styleButton(*spec.button, spec.tip);
        addChildComponent(*spec.button);
    }
    styleButton(finishSketchButton, "Leave sketch editing and keep all completed geometry.", true); addChildComponent(finishSketchButton);
    for (auto* b : { &selectToolButton, &lineButton, &rectangleButton, &circleButton, &curveButton }) b->setClickingTogglesState(false);

    sketchButton.onClick = [this] { startTask(Task::createSketch); };
    boxButton.onClick = [this] { startTask(Task::newBox); };
    cylinderButton.onClick = [this] { startTask(Task::newCylinder); };
    extrudeButton.onClick = [this] { startTask(Task::extrude); };
    cutButton.onClick = [this] { startTask(Task::cut); };
    latheButton.onClick = [this] { startTask(Task::lathe); };
    revolveButton.onClick = [this] { startTask(Task::revolve); };
    filletButton.onClick = [this] { startTask(Task::fillet); };
    subtractButton.onClick = [this] { startTask(Task::boolean, "subtract"); };
    unionButton.onClick = [this] { startTask(Task::boolean, "union"); };
    intersectButton.onClick = [this] { startTask(Task::boolean, "intersect"); };
    instanceButton.onClick = [this] { startTask(Task::newInstance); };
    couplingButton.onClick = [this] { startTask(Task::newCoupling); };
    jointButton.onClick = [this] { startTask(Task::newJoint); };
    linkButton.onClick = [this] { startTask(Task::link); };
    const auto setMode = [this](djehuti::cad::CadSelection::Mode mode) { if (selection != nullptr) selection->setMode(mode); updateToolbar(); };
    bodyModeButton.onClick = [setMode] { setMode(djehuti::cad::CadSelection::Mode::body); };
    faceModeButton.onClick = [setMode] { setMode(djehuti::cad::CadSelection::Mode::face); };
    edgeModeButton.onClick = [setMode] { setMode(djehuti::cad::CadSelection::Mode::edge); };
    ikButton.onClick = [this] { loadIkDraft(isIkTask(selectedId) ? selectedId : juce::String()); startTask(Task::ik); };
    // After undo/redo the task panel and the sketch canvas show the restored values, not the ones of the undone edit.
    undoButton.onClick = [this] { juce::String e; if (!service->undo(e)) showError(e); else { refreshFromDocument(true); rebuildTaskPanel(false); sketchCanvas->repaint(); setMessage("Undone.", 0); } };
    redoButton.onClick = [this] { juce::String e; if (!service->redo(e)) showError(e); else { refreshFromDocument(true); rebuildTaskPanel(false); sketchCanvas->repaint(); setMessage("Redone.", 0); } };
    fitButton.onClick = [this] { modelView.fitAll(); };
    resetViewButton.onClick = [this] { modelView.resetCamera(); };
    viewsButton.onClick = [this] { if (onOpenViews) onOpenViews(); };
    selectToolButton.onClick = [this] { setSketchTool("select"); };
    lineButton.onClick = [this] { setSketchTool("line"); };
    rectangleButton.onClick = [this] { setSketchTool("rectangle"); };
    circleButton.onClick = [this] { setSketchTool("circle"); };
    curveButton.onClick = [this] { setSketchTool("curve"); };
    constrainButton.onClick = [this] { chooseConstraint(); };
    fitSketchButton.onClick = [this] { sketchCanvas->fitToContent(); };
    finishSketchButton.onClick = [this] { finishSketch(); };

    tree.setRootItemVisible(false); tree.setIndentSize(14);
    tree.setColour(juce::TreeView::backgroundColourId, panelColour);
    side.setOpaque(false); centre.setOpaque(false);
    addAndMakeVisible(treeHeading); addAndMakeVisible(tree); addAndMakeVisible(leftBar); addAndMakeVisible(centre); addAndMakeVisible(rightBar); addAndMakeVisible(side);
    centre.addAndMakeVisible(modelView); centre.addChildComponent(*sketchCanvas);
    modelView.setTitle("Perspective  |  Model  (drag to orbit, Shift/right-drag to pan, wheel to zoom, click to select)");
    modelView.onContextCommand = [this](const juce::String& command,const djehuti::cad::CadPick& pick) { viewContextCommand(command,pick); };
    modelView.onBodyPicked = [this](const juce::String& id) { bodyPicked(id); };
    modelView.onFrameRequested = [this] { wakeFrames(); };
    modelView.wantsBodyPick = [this] { return isCommandTask(); };
    modelView.onTargetDragged = [this](const juce::String& targetId, const std::array<float, 3>& point, bool finished) { ikTargetDragged(targetId, point, finished); };

    side.addAndMakeVisible(taskTitle); side.addAndMakeVisible(taskInstruction); side.addAndMakeVisible(taskViewport); side.addAndMakeVisible(messageLabel);
    side.addAndMakeVisible(acceptButton); side.addAndMakeVisible(cancelButton);
    taskViewport.setScrollBarsShown(true, false);
    styleButton(acceptButton, "Apply the current command.", true); styleButton(cancelButton, "Cancel the current command; the committed model is unchanged.");
    acceptButton.onClick = [this] { acceptTask(); };
    cancelButton.onClick = [this] { cancelTask(); };

    columns.setItemLayout(0, 170, 420, -0.18);
    columns.setItemLayout(1, 5, 5, 5);
    columns.setItemLayout(2, 240, -1.0, -0.57);
    columns.setItemLayout(3, 5, 5, 5);
    columns.setItemLayout(4, 250, 480, -0.25);

    refreshFromDocument(true);
    rebuildTaskPanel(false);
    startTimerHz(60);
}

// Column widths are stored as fractions of the workspace width so they scale with the window.
juce::String CadWorkspacePanel::layoutState() const
{
    if (getWidth() <= 0) return {};
    return juce::String(leftBar.getX() / (double) getWidth(), 4) + "," + juce::String((getWidth() - rightBar.getRight()) / (double) getWidth(), 4);
}
void CadWorkspacePanel::restoreLayoutState(const juce::String& state)
{
    const auto parts = juce::StringArray::fromTokens(state, ",", {});
    if (parts.size() != 2) return;
    const auto treeShare = juce::jlimit(0.08, 0.4, parts[0].getDoubleValue()), sideShare = juce::jlimit(0.12, 0.45, parts[1].getDoubleValue());
    columns.setItemLayout(0, 170, 420, -treeShare);
    columns.setItemLayout(2, 240, -1.0, -juce::jmax(0.2, 1.0 - treeShare - sideShare));
    columns.setItemLayout(4, 250, 480, -sideShare);
    resized();
}

void CadWorkspacePanel::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);
    g.setColour(toolbarColour); g.fillRect(0, 0, getWidth(), 58);
    g.setColour(borderColour); g.drawHorizontalLine(30, 0.0f, (float) getWidth()); g.drawHorizontalLine(58, 0.0f, (float) getWidth());
    g.setColour(panelColour); g.fillRect(tree.getX(), 58, tree.getWidth(), getHeight() - 58 - 28); g.fillRect(side.getX(), 58, side.getWidth(), getHeight() - 58 - 28);
    g.setColour(borderColour); g.drawHorizontalLine(getHeight() - 28, 0.0f, (float) getWidth());
    if (editingSketchId.isNotEmpty()) { g.setColour(accentColour.withAlpha(0.35f)); g.fillRect(0, 30, 4, 28); }
}

void CadWorkspacePanel::resized()
{
    auto area = getLocalBounds();
    workspaceMenu.setBounds(area.removeFromTop(30));
    auto context = area.removeFromTop(28).reduced(10, 2);
    finishSketchButton.setBounds(context.removeFromRight(140));
    modeLabel.setBounds(context);
    auto status = area.removeFromBottom(28).reduced(12, 2);
    unitsLabel.setBounds(status.removeFromRight(260)); statusLabel.setBounds(status);

    juce::Component* items[] = { &tree, &leftBar, &centre, &rightBar, &side };
    columns.layOutComponents(items, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);
    auto treeArea = tree.getBounds(); treeHeading.setBounds(treeArea.removeFromTop(30).reduced(10, 4)); tree.setBounds(treeArea.reduced(4, 0));
    modelView.setBounds(centre.getLocalBounds()); sketchCanvas->setBounds(centre.getLocalBounds());

    auto s = side.getLocalBounds().reduced(12, 10);
    taskTitle.setBounds(s.removeFromTop(28));
    const auto instructionHeight = juce::jlimit(20, 80, juce::roundToInt(juce::Font(14.0f).getStringWidthFloat(taskInstruction.getText()) / juce::jmax(1, s.getWidth() - 10) + 1.0f) * 18 + 4);
    taskInstruction.setBounds(s.removeFromTop(instructionHeight));
    auto buttons = s.removeFromBottom(34);
    if (cancelButton.isVisible()) { cancelButton.setBounds(buttons.removeFromRight(juce::jmax(90, buttons.getWidth() / 3))); buttons.removeFromRight(8); }
    acceptButton.setBounds(buttons);
    s.removeFromBottom(6);
    const auto messageText = messageLabel.getText();
    const auto messageHeight = messageText.isEmpty() ? 0 : juce::jlimit(22, 96, juce::roundToInt(juce::Font(14.0f).getStringWidthFloat(messageText) / juce::jmax(1, s.getWidth() - 10) + 1.0f) * 18 + 6);
    messageLabel.setBounds(s.removeFromBottom(messageHeight));
    s.removeFromBottom(4);
    taskViewport.setBounds(s);
    if (form != nullptr) form->setSize(juce::jmax(1, s.getWidth() - (form->preferredHeight() > s.getHeight() ? taskViewport.getScrollBarThickness() : 0)), form->preferredHeight());
}

// Document history keys: Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo, on the one OCAF document history. A focused text
// field consumes these keys for its own text undo before they get here.
bool CadWorkspacePanel::handleHistoryKey(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (!mods.isCommandDown() || mods.isAltDown()) return false;
    // Windows can report Ctrl+letter as the control character (Ctrl+Z = 26) when no scan code accompanies the key.
    auto keyCode = key.getKeyCode();
    if (keyCode >= 1 && keyCode <= 26) keyCode += 'A' - 1;
    const auto code = juce::CharacterFunctions::toUpperCase((juce::juce_wchar) keyCode);
    if (code != 'Z' && code != 'Y') return false;
    sketchCanvas->cancelGesture();
    executeUiCommand(code == 'Y' || mods.isShiftDown() ? "redo" : "undo");
    return true;
}

// Keyboard focus is often outside this panel (a rebuilt task form, another dock, nothing at all), and JUCE then
// delivers keys to the window's top-level component, which never passes them down. Listening on the top-level
// window catches those; the panel acts only while it is on screen.
bool CadWorkspacePanel::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (!isShowing()) return false;
    if (handleHistoryKey(key)) return true;
    // Esc cancels a sketch drag in progress wherever focus is; otherwise Esc is left to whoever has focus.
    if (key == juce::KeyPress::escapeKey && editingSketchId.isNotEmpty() && sketchCanvas->hasActiveGesture()) return sketchCanvas->escape();
    return false;
}

void CadWorkspacePanel::parentHierarchyChanged()
{
    auto* top = getTopLevelComponent();
    if (top == this) top = nullptr;
    if (top == keyHost.getComponent()) return;
    if (keyHost != nullptr) keyHost->removeKeyListener(this);
    keyHost = top;
    if (keyHost != nullptr) keyHost->addKeyListener(this);
}

bool CadWorkspacePanel::keyPressed(const juce::KeyPress& key)
{
    if (handleHistoryKey(key)) return true;
    if (key != juce::KeyPress::escapeKey) return false;
    if (editingSketchId.isNotEmpty()) return sketchCanvas->escape();
    if (task == Task::createSketch || task == Task::newBox || task == Task::newCylinder || task == Task::lathe || isCommandTask()) { cancelTask(); return true; }
    return false;
}

// The timer only polls for change: a frame is published and rendered when something changed. After a second
// without change it drops to 10 Hz; camera input, new geometry and poses bring it back to 60 Hz.
void CadWorkspacePanel::timerCallback()
{
    const auto revision = service->document().revision();
    const auto instance = service->document().documentInstance();
    const bool documentChanged = revision != shownRevision || instance != shownDocument;
    refreshFromDocument();
    const bool published = modelView.prepareFrame(0.0f, ++frameSerial);
    if (documentChanged || published) wakeFrames();
    else if (fastFrames && ++idleFrameTicks >= 60) { fastFrames = false; startTimerHz(10); }
}

void CadWorkspacePanel::wakeFrames()
{
    idleFrameTicks = 0;
    if (!fastFrames) { fastFrames = true; startTimerHz(60); }
}

void CadWorkspacePanel::refreshFromDocument(bool force)
{
    const auto& document = service->document();
    if (document.documentInstance() != shownDocument)
    {
        // New or Open: frame the new document once its geometry and pose are available.
        shownDocument = document.documentInstance();
        modelView.requestFit(shownDocument);
        force = true;
    }
    if (!force && document.revision() == shownRevision) return;
    shownRevision = document.revision();
    if (interferenceActive && (document.revision() != interferenceReport.documentRevision || document.documentInstance() != interferenceReport.documentInstance)) markInterferenceStale();
    modelView.setSnapshot(service->snapshot());
    auto missing = [&](const juce::String& id) { return id.isNotEmpty() && document.find(id) == nullptr && document.mechanicsData().findInstance(id) == nullptr && document.mechanicsData().findJoint(id) == nullptr && document.mechanicsData().findCoupling(id) == nullptr && document.mechanicsData().findIkTask(id) == nullptr; };
    if (missing(selectedId)) selectedId.clear();
    if (missing(targetId)) targetId.clear();
    if (missing(toolId)) toolId.clear();
    if (missing(profileId)) profileId.clear();
    if (editingSketchId.isNotEmpty() && document.findSketch(editingSketchId) == nullptr) { editingSketchId.clear(); sketchCanvas->setVisible(false); modelView.setVisible(true); resized(); }
    if (const auto* sketch = document.findSketch(editingSketchId))
    {
        auto exists = [&](const juce::String& id) { for (const auto& e : sketch->entities) if (e.id == id) return true; return false; };
        if (sketchEntityId.isNotEmpty() && !exists(sketchEntityId)) sketchEntityId.clear();
        if (sketchSecondEntityId.isNotEmpty() && !exists(sketchSecondEntityId)) sketchSecondEntityId.clear();
        if (sketchVertexRef.isNotEmpty() && !exists(sketchVertexRef.upToFirstOccurrenceOf(":", false, false))) { sketchVertexRef.clear(); sketchCanvas->setSelectedPoint({}); }
        sketchCanvas->setSelection(sketchEntityId, sketchSecondEntityId);
    }
    if (task == Task::body && selectedId.isEmpty()) task = Task::none;
    if (task == Task::sketch && selectedId.isEmpty()) task = Task::none;
    if ((task == Task::instance || task == Task::joint || task == Task::coupling) && selectedId.isEmpty()) task = Task::none;
    if (task == Task::sketchEntity && sketchEntityId.isEmpty()) task = Task::sketchEdit;
    if (ikDraft != nullptr && ikDocument != document.documentInstance()) { ikDraft.reset(); ikDirty = false; if (task == Task::ik) task = Task::none; }
    if (task == Task::ik && ikDraft != nullptr && ikDraft->id.isNotEmpty())
    {
        if (const auto* stored = document.mechanicsData().findIkTask(ikDraft->id)) { if (!ikDirty) { *ikDraft = *stored; ikTargetIndex = juce::jlimit(0, juce::jmax(0, (int) ikDraft->targets.size() - 1), ikTargetIndex); } }
        else { ikDraft->id.clear(); ikDirty = true; }   // e.g. its creation was undone: keep the values as an unsaved task
    }
    refreshTree();
    updateHighlights();
    updateToolbar();
    requestRebuild(false);
}

void CadWorkspacePanel::refreshTree()
{
    const juce::ScopedValueSetter<bool> guard(updatingTree, true);
    tree.setRootItem(nullptr);
    rootItem = std::make_unique<GroupItem>("root");
    auto* origin = new GroupItem("Origin  |  XY / XZ / YZ planes");
    auto* bodies = new GroupItem("Bodies");
    auto* sketches = new GroupItem("Sketches");
    auto* features = new GroupItem("Features");
    auto* assembly = new GroupItem("Assembly");
    const auto& model = service->document().mechanicsData();
    for (const auto& instance : model.instances) assembly->addSubItem(new FeatureItem(*this, instance.id, instance.name, instance.grounded ? "grounded" : "instance"));
    for (const auto& joint : model.joints) assembly->addSubItem(new FeatureItem(*this, joint.id, joint.name, joint.type + (model.mappingForJoint(joint.id) != nullptr ? " *" : "")));
    for (const auto& coupling : model.couplings) assembly->addSubItem(new FeatureItem(*this, coupling.id, coupling.name, coupling.type == "rackPinion" ? "rack" : coupling.type));
    for (const auto& ikTask : model.ikTasks) assembly->addSubItem(new FeatureItem(*this, ikTask.id, ikTask.name, "IK task"));
    for (const auto& f : service->document().features())
    {
        auto* item = new FeatureItem(*this, f);
        if (isPrimitiveType(f.type)) bodies->addSubItem(item);
        else if (f.type == "sketch") sketches->addSubItem(item);
        else features->addSubItem(item);
    }
    rootItem->addSubItem(origin);
    for (auto* group : { bodies, sketches, features, assembly }) { rootItem->addSubItem(group); group->setOpen(true); }
    tree.setRootItem(rootItem.get());
    rootItem->setOpen(true);
    syncTreeSelection();
}

void CadWorkspacePanel::syncTreeSelection()
{
    const juce::ScopedValueSetter<bool> guard(updatingTree, true);
    if (rootItem == nullptr) return;
    for (int g = 0; g < rootItem->getNumSubItems(); ++g)
        if (auto* group = rootItem->getSubItem(g))
            for (int i = 0; i < group->getNumSubItems(); ++i)
                if (auto* item = dynamic_cast<FeatureItem*>(group->getSubItem(i)))
                    item->setSelected(item->featureId() == selectedId && !isCommandTask(), false, juce::dontSendNotification);
    tree.repaint();
}

void CadWorkspacePanel::featureClicked(const juce::String& id)
{
    if (isCommandTask() && task != Task::link) { assignRole(id); return; }
    if (selection != nullptr)
    {
        djehuti::cad::CadPick picked;
        if (const auto* instance = service->document().mechanicsData().findInstance(id)) picked = { id, instance->featureId, -1, -1 };
        else if (isSolid(id)) picked = { id, id, -1, -1 };
        selection->setSelected(picked);
    }
    selectFeature(id);
}

void CadWorkspacePanel::bodyPicked(const juce::String& id)
{
    if (task == Task::ik && ikDraft != nullptr && isInstance(id) && juce::isPositiveAndBelow(ikTargetIndex, (int) ikDraft->targets.size()))
    {
        readIkForm();
        ikDraft->targets[(size_t) ikTargetIndex].instanceId = id; ikDirty = true; ikFormStale = true;
        rebuildTaskPanel(false); showIkMarkers();
        setMessage("Endpoint instance set to " + featureName(id) + ".", 0);
        return;
    }
    if (isCommandTask() && task != Task::link) { if (id.isNotEmpty()) assignRole(id); return; }
    selectFeature(id);
}

bool CadWorkspacePanel::isCommandTask() const { return task == Task::extrude || task == Task::revolve || task == Task::cut || task == Task::fillet || task == Task::boolean || task == Task::newInstance || task == Task::newJoint || task == Task::link || task == Task::newCoupling; }
// "face <token> of <body>" for a face-attached sketch.
juce::String CadWorkspacePanel::supportText(const djehuti::cad::CadSketch& sketch) const { return "face " + sketch.supportFace + " of " + featureName(sketch.supportFeature); }
bool CadWorkspacePanel::isIkTask(const juce::String& id) const { return id.isNotEmpty() && service->document().mechanicsData().findIkTask(id) != nullptr; }
bool CadWorkspacePanel::isCoupling(const juce::String& id) const { return id.isNotEmpty() && service->document().mechanicsData().findCoupling(id) != nullptr; }
bool CadWorkspacePanel::isInstance(const juce::String& id) const { return id.isNotEmpty() && service->document().mechanicsData().findInstance(id) != nullptr; }
bool CadWorkspacePanel::isJoint(const juce::String& id) const { return id.isNotEmpty() && service->document().mechanicsData().findJoint(id) != nullptr; }

void CadWorkspacePanel::setSelection(std::shared_ptr<djehuti::cad::CadSelection> shared)
{
    selection = std::move(shared);
    modelView.setSelection(selection);
    if (selection != nullptr) selection->addListener([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->selectionChanged(); });
    updateToolbar();
}

// A face or edge picked in any view opens its inspector; clearing it returns to the model.
void CadWorkspacePanel::selectionChanged()
{
    updateToolbar();
    updateHighlights();
    if (selection == nullptr) return;
    const auto& picked = selection->selected();
    if (picked.valid() && (picked.face >= 0 || picked.edge >= 0))
    {
        if (task == Task::fillet)
        {
            if (!selection->items().empty())
            {
                for (const auto& item : selection->items())
                {
                    if (item.valid() && item.edge >= 0)
                    {
                        if (targetId.isEmpty() || targetId != item.featureId)
                        {
                            targetId = item.featureId;
                            filletEdges.clear();
                        }
                        if (std::find(filletEdges.begin(), filletEdges.end(), item.edge) == filletEdges.end())
                            filletEdges.push_back(item.edge);
                    }
                }
            }
            else if (picked.edge >= 0)
            {
                if (targetId.isEmpty() || targetId != picked.featureId)
                {
                    targetId = picked.featureId;
                    filletEdges.clear();
                }
                auto it = std::find(filletEdges.begin(), filletEdges.end(), picked.edge);
                if (it == filletEdges.end())
                    filletEdges.push_back(picked.edge);
                else if (filletEdges.size() > 1)
                    filletEdges.erase(it);
            }
            rebuildTaskPanel(true);
            return;
        }
        if (editingSketchId.isNotEmpty()) finishSketch();
        task = Task::subshape; commandMessage.clear(); validationMessage.clear();
        rebuildTaskPanel(false);
    }
    else if (task == Task::subshape) { task = Task::none; rebuildTaskPanel(false); }
}

bool CadWorkspacePanel::selectedSupport(djehuti::cad::CadFaceInfo& face, juce::String& reason) const
{
    if (selection == nullptr || !selection->selected().valid() || selection->selected().face < 0) { reason = "Select a planar face in Face mode first."; return false; }
    const auto& picked = selection->selected();
    if (!service->describeFace(picked.featureId, picked.face, face, reason)) return false;
    if (!face.planar) { reason = "This face is curved. Sketches can be attached to planar faces only."; return false; }
    if (face.token.isEmpty()) { reason = "This face has no persistent name in the model history, so a sketch cannot be attached to it."; return false; }
    return true;
}

void CadWorkspacePanel::addSubshapePanel(juce::String& title, juce::String& instruction)
{
    const auto& picked = selection->selected();
    const auto body = featureName(picked.featureId);
    const auto instance = picked.drawId != picked.featureId ? featureName(picked.drawId) : juce::String();
    form->addHeader(picked.face >= 0 ? "Face" : "Edge");
    form->addNote({}, "Body: " + body + (instance.isNotEmpty() ? "\nInstance: " + instance : juce::String()), instance.isNotEmpty() ? 2 : 1);
    juce::String error;
    if (picked.face >= 0)
    {
        djehuti::cad::CadFaceInfo face;
        title = "Face of " + body; instruction = "Hover highlights, click selects. A planar, persistently named face can carry a sketch.";
        if (!service->describeFace(picked.featureId, picked.face, face, error)) { form->addNote({}, error, 2); return; }
        form->addNote({}, "Persistent name: " + (face.token.isNotEmpty() ? face.token : juce::String("none (cannot support a sketch)"))
                          + "\n" + (face.planar ? "Planar" : "Curved") + ", area " + juce::String(face.area, 2) + " mm2"
                          + "\nNormal " + juce::String(face.normal.X(), 3) + ", " + juce::String(face.normal.Y(), 3) + ", " + juce::String(face.normal.Z(), 3), 3);
        juce::String reason;
        djehuti::cad::CadFaceInfo support;
        if (selectedSupport(support, reason))
            form->addButton("faceSketch", "Create Sketch on this face", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->startTask(Task::createSketch); }); },
                            "Start a sketch attached to this face; it follows the face when the body changes.");
        else form->addNote({}, reason, 2);
    }
    else
    {
        juce::String text;
        title = "Edge of " + body; instruction = "Hover highlights, click selects.";
        form->addNote({}, service->describeEdge(picked.featureId, picked.edge, text, error) ? text : error, 2);
        form->addButton("edgeFillet", "Fillet this edge...", [this] {
            postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
                if (safe != nullptr) safe->startTask(Task::fillet);
            });
        }, "Create a constant-radius fillet on this edge.");
    }
    form->addButton("clearPick", "Clear selection", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr && safe->selection != nullptr) safe->selection->clear(); }); }, "Clear the face or edge selection.");
}

void CadWorkspacePanel::setMotion(std::shared_ptr<djehuti::cad::CadMotionRuntime> runtime, djehuti::motion::MotionLink* link)
{
    motionRuntime = std::move(runtime);
    motionLink = link;
    if (motionRuntime != nullptr) { auto source = motionRuntime; modelView.setPoseSource([source] { return source->pose(); }); motionRuntime->onPoseChanged = [this] { mechanismChanged(); }; motionRuntime->onIkChanged = [this]
        {
            if (task != Task::ik) return;
            // A requested solve finished: replace the "Solving" note with its outcome.
            if (ikSolvePending && motionRuntime->hasIkResult())
            {
                ikSolvePending = false;
                const auto& r = motionRuntime->lastIkResult();
                if (r.valid) setMessage("Targets reached (" + djehuti::cad::kin::statusName(r.status) + "). The document is unchanged.", 0);
                else setMessage(djehuti::cad::kin::statusName(r.status) + ": " + r.message + " The last valid pose stays.", r.status == djehuti::cad::kin::Status::invalidInput ? 2 : 1);
            }
            requestRebuild(true);
        }; }
    if (motionLink != nullptr) motionLink->onStatusChanged = [this] { linkChanged(); };
    linkChanged();
}

void CadWorkspacePanel::linkChanged()
{
    if (motionLink == nullptr) return;
    const auto status = motionLink->snapshot();
    const auto mechanism = motionRuntime != nullptr && !service->document().mechanicsData().joints.empty()
                         ? "Mechanism: " + (motionRuntime->showingLastValidPose() ? juce::String("unsolved (last valid pose shown)") : djehuti::cad::kin::statusName(motionRuntime->lastAttempt().status)) + "  |  " : juce::String();
    unitsLabel.setText(mechanism + "Motion link: " + djehuti::motion::stateName(status.state) + "  |  Millimetres  |  Right-handed XYZ", juce::dontSendNotification);
    unitsLabel.setColour(juce::Label::textColourId, status.severity == 2 ? juce::Colour(0xffff8a80) : status.state == djehuti::motion::LinkState::streaming || status.state == djehuti::motion::LinkState::synchronized ? juce::Colour(0xff8fd7c7) : dimTextColour);
    if (status.serial != shownLinkSerial && (task == Task::link || task == Task::joint)) { shownLinkSerial = status.serial; requestRebuild(true); }
}

void CadWorkspacePanel::selectFeature(const juce::String& id)
{
    if (editingSketchId.isNotEmpty() && id != editingSketchId) finishSketch();
    selectedId = id;
    commandMessage.clear(); validationMessage.clear();
    const auto* f = service->document().find(id);
    task = f == nullptr ? Task::none : (f->type == "sketch" ? Task::sketch : Task::body);
    if (isInstance(id)) task = Task::instance;
    else if (isJoint(id)) task = Task::joint;
    else if (isCoupling(id)) task = Task::coupling;
    else if (isIkTask(id)) { task = Task::ik; loadIkDraft(id); }
    else if (f == nullptr) selectedId.clear();
    syncTreeSelection(); updateHighlights(); updateToolbar();
    rebuildTaskPanel(false);
}

void CadWorkspacePanel::selectSketchVertex(const juce::String& pointRef)
{
    const auto entity = pointRef.upToFirstOccurrenceOf(":", false, false);
    selectSketchEntity(entity, false);
    sketchVertexRef = pointRef;
    sketchCanvas->setSelectedPoint(pointRef);
    requestRebuild(false);
}

bool CadWorkspacePanel::moveSketchPoint(const juce::String& pointRef, double u, double v)
{
    juce::String e;
    const bool ok = service->moveSketchPoint(editingSketchId, pointRef, u, v, e);
    refreshFromDocument(true); rebuildTaskPanel(false); sketchCanvas->repaint();
    if (!ok) showError(e);
    else setMessage((pointRef.endsWith(":c1") || pointRef.endsWith(":c2") ? juce::String("Handle moved.") : juce::String("Vertex moved; joined geometry followed.")) + " Ctrl+Z undoes it.", 0);
    return ok;
}

bool CadWorkspacePanel::insertSketchVertex(const juce::String& lineId, double u, double v)
{
    juce::String e, created; juce::StringArray removed;
    const bool ok = service->insertSketchVertex(editingSketchId, lineId, u, v, created, removed, e);
    refreshFromDocument(true);
    if (!ok) { showError(e); return false; }
    selectSketchVertex(lineId + ":p2");   // the new vertex, ready to drag or type
    setMessage("Vertex inserted: " + friendlyEntity(lineId) + " now ends there and " + friendlyEntity(created) + " continues to the old end."
               + (removed.isEmpty() ? juce::String() : " Removed because they described the whole line: " + removed.joinIntoString(", ") + ".") + " Ctrl+Z undoes it.", removed.isEmpty() ? 0 : 1);
    return true;
}

void CadWorkspacePanel::selectSketchEntity(const juce::String& id, bool second)
{
    sketchVertexRef.clear(); sketchCanvas->setSelectedPoint({});
    if (second && sketchEntityId.isNotEmpty() && id != sketchEntityId) sketchSecondEntityId = id;
    else { sketchEntityId = id; sketchSecondEntityId.clear(); }
    sketchCanvas->setSelection(sketchEntityId, sketchSecondEntityId);
    validationMessage.clear(); commandMessage.clear();
    task = sketchEntityId.isEmpty() ? Task::sketchEdit : Task::sketchEntity;
    updateToolbar();
    requestRebuild(second);
}

void CadWorkspacePanel::startTask(Task next, const juce::String& kind)
{
    if (editingSketchId.isNotEmpty() && next != Task::sketchEdit) finishSketch();
    const auto* selected = service->document().find(selectedId);
    task = next; booleanKind = kind;
    if(next==Task::lathe)editingLatheId=selected!=nullptr&&selected->lathe?selectedId:juce::String();
    commandMessage.clear(); validationMessage.clear();
    profileId.clear(); targetId.clear(); toolId.clear(); activeRole = 0; sketchPlane.clear();
    filletEdges.clear();
    if (next == Task::fillet)
    {
        filletRadius = 2.0;
        if (selection != nullptr && selection->selected().valid())
        {
            const auto& picked = selection->selected();
            if (isSolid(picked.featureId))
                targetId = picked.featureId;
            if (picked.edge >= 0)
                filletEdges.push_back(picked.edge);
        }
        else if (isSolid(selectedId))
        {
            targetId = selectedId;
        }
        if (selection != nullptr)
            selection->setMode(djehuti::cad::CadSelection::Mode::edge);
    }
    if (next == Task::createSketch) { djehuti::cad::CadFaceInfo support; juce::String reason; if (selectedSupport(support, reason)) sketchPlane = "FACE"; }
    if (next == Task::extrude || next == Task::revolve || next == Task::cut) { if (selected != nullptr && selected->type == "sketch") profileId = selectedId; }
    if (next == Task::boolean && isSolid(selectedId)) { targetId = selectedId; activeRole = 1; }
    instanceBodyId.clear(); jointParentId.clear(); jointChildId.clear();
    if (next == Task::newInstance && isSolid(selectedId)) instanceBodyId = selectedId;
    if (next == Task::newJoint) { activeRole = 1; if (isInstance(selectedId)) { jointChildId = selectedId; activeRole = 0; } }
    couplingJointA.clear(); couplingJointB.clear();
    if (next == Task::newCoupling) { activeRole = 0; if (isJoint(selectedId)) { couplingJointA = selectedId; activeRole = 1; } }
    syncTreeSelection(); updateHighlights(); updateToolbar();
    rebuildTaskPanel(false);
}

void CadWorkspacePanel::requestRebuild(bool keepValues) { pendingKeepValues = pendingKeepValues && keepValues; triggerAsyncUpdate(); }
void CadWorkspacePanel::handleAsyncUpdate() { const auto keep = pendingKeepValues; pendingKeepValues = true; rebuildTaskPanel(keep); }

void CadWorkspacePanel::addLatheFields()
{
    juce::var data;bool editable=true;double angle=360;bool reverse=false;
    if(editingLatheId.isNotEmpty())
    {
        auto* a=new juce::DynamicObject();a->setProperty("action","get");a->setProperty("id",editingLatheId);data=service->latheCommand(a);
        editable=(bool)data["profileEditable"];angle=(double)data["parameters"]["angleDegrees"];reverse=(bool)data["parameters"]["reverse"];
    }
    if(editable)
    {
        form->addMultiline("latheOuter","Outside profile",editingLatheId.isEmpty()?"0, 20\n30, 20":djehuti::cad::latheStationsText(data["parameters"]["outer"]),"One axial position, outside diameter per line, in mm. Repeat a position for a shoulder.");
        form->addMultiline("latheInner","Bore profile",editingLatheId.isEmpty()?"0, 0\n30, 0":djehuti::cad::latheStationsText(data["parameters"]["inner"]),"One axial position, bore diameter per line, in mm. Zero diameter gives solid material down to the axis.");
    }
    else form->addNote({},"This profile has custom sketch edits or constraints. Edit the source sketch to preserve them; the turning angle remains editable here.",3);
    form->addNumber("latheAngle","Angle",angle,"deg","360 for a complete turned part; partial angles remain supported.");
    form->addToggle("latheReverse","Reverse rotation",reverse,"Reverse the Revolve direction.");
    form->addNote({},"Axial positions run along sketch V (world Z for new profiles); diameters are converted to radial X. Outside and bore ends must match. Station edits and turning are one Undo step.",4);
    if(editingLatheId.isNotEmpty())form->addButton("latheSketch","Edit 2D profile",[this,sketch=data["sketch"].toString()]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),sketch]{if(safe!=nullptr)safe->enterSketch(sketch);});},"Use the normal sketch editor for freeform profile edits.");
}

juce::var CadWorkspacePanel::readLatheForm(juce::String& error) const
{
    auto p=form->control("latheOuter")!=nullptr?djehuti::cad::latheProfileFromText(form->text("latheOuter"),form->text("latheInner"),error):juce::var(new juce::DynamicObject());
    if(!p.isObject())return {};
    if(!form->numberValid("latheAngle")||form->number("latheAngle")<=0||form->number("latheAngle")>360){error="Lathe angle must be greater than 0 and no greater than 360 degrees.";return {};}
    p.getDynamicObject()->setProperty("angleDegrees",form->number("latheAngle"));p.getDynamicObject()->setProperty("reverse",form->toggle("latheReverse"));return p;
}

void CadWorkspacePanel::addRevolveFields(const djehuti::cad::CadRevolveParameters& p, const juce::String& sketchId)
{
    juce::StringArray names; revolveProfileIds.clear();
    for (const auto& f : service->document().features()) if (f.type == "sketch")
    { names.add(f.name); revolveProfileIds.add(f.id); }
    form->addChoice("revolveProfile", "Profile", names, revolveProfileIds.indexOf(sketchId), "Closed sketch profile to revolve.");
    const juce::StringArray types {"sketchU", "sketchV", "worldX", "worldY", "worldZ", "custom"};
    form->addChoice("axisType", "Axis", {"Sketch U", "Sketch V", "World X", "World Y", "World Z", "Custom"}, types.indexOf(p.axisType),
                    "Sketch axes follow the sketch. World axes pass through the world origin. The axis must lie in the sketch plane.");
    form->addNumber("angleDegrees", "Angle", p.angleDegrees, "deg", "Greater than 0, up to 360 degrees for a full revolution.");
    form->addToggle("revolveReverse", "Reverse rotation", p.reverse, "Reverse rotation about the selected axis (right-hand rule).");
    for (int i = 0; i < 3; ++i)
    {
        const auto letter = juce::StringArray{"X", "Y", "Z"}[i];
        form->addNumber("axis" + letter, "Axis origin " + letter, p.origin[(size_t)i], "mm", "Custom axis origin in world coordinates.");
        form->addNumber("axisD" + letter, "Axis direction " + letter, p.direction[(size_t)i], {}, "Custom world direction; any nonzero length is accepted.");
    }
    form->addNote("revolveHelp", "Keep the profile on one side of the axis. An edge may lie on the axis. Nested loops form holes.", 2);
}

djehuti::cad::CadRevolveParameters CadWorkspacePanel::readRevolveForm() const
{
    djehuti::cad::CadRevolveParameters p;
    const juce::StringArray types {"sketchU", "sketchV", "worldX", "worldY", "worldZ", "custom"};
    const int index = form->choice("axisType");
    p.axisType = juce::isPositiveAndBelow(index, types.size()) ? types[index] : juce::String();
    p.angleDegrees = form->number("angleDegrees"); p.reverse = form->toggle("revolveReverse");
    for (int i = 0; i < 3; ++i)
    {
        const auto letter = juce::StringArray{"X", "Y", "Z"}[i];
        p.origin[(size_t)i] = form->number("axis" + letter); p.direction[(size_t)i] = form->number("axisD" + letter);
    }
    return p;
}

juce::String CadWorkspacePanel::revolveFormSketch() const
{
    const int index = form->choice("revolveProfile");
    return juce::isPositiveAndBelow(index, revolveProfileIds.size()) ? revolveProfileIds[index] : juce::String();
}

void CadWorkspacePanel::rebuildTaskPanel(bool keepValues)
{
    cancelPendingUpdate(); pendingKeepValues = true;
    if (task == Task::ik) readIkForm();   // the IK draft holds the edited values; the form is rebuilt from it
    const auto context = juce::String((int) task) + "|" + booleanKind + "|" + selectedId + "|" + editingSketchId + "|" + sketchEntityId;
    juce::NamedValueSet previous;
    int previousScroll = 0;
    if (keepValues && form != nullptr && context == formContext) { previous = form->values(); previousScroll = taskViewport.getViewPositionY(); }
    formContext = context;
    taskViewport.setViewedComponent(nullptr, false);
    form = std::make_unique<TaskForm>();
    form->onChange = [this] { validateTask(); };
    const auto& document = service->document();
    const auto* f = document.find(selectedId);
    if ((task == Task::body || task == Task::sketch) && f == nullptr) task = Task::none;
    if ((task == Task::instance && !isInstance(selectedId)) || (task == Task::joint && !isJoint(selectedId)) || (task == Task::coupling && !isCoupling(selectedId))) task = Task::none;
    juce::String title, instruction, accept = "Apply", cancel = "Cancel";
    bool showAccept = true, showCancel = true;
    const auto slotText = [&](const juce::String& id, const juce::String& empty) { return id.isEmpty() ? empty : featureName(id); };

    // Markers and target dragging exist only while the IK panel is open.
    if (task != Task::ik && ikMarkersShown && motionRuntime != nullptr) { motionRuntime->setIkDisplay(nullptr); ikMarkersShown = false; }
    modelView.setTargetDragEnabled(task == Task::ik && ikDragEnabled);
    if (task == Task::subshape && (selection == nullptr || !selection->selected().valid())) task = Task::none;
    if (task == Task::interference && !interferenceActive) task = Task::none;
    if (task == Task::interference) { addInterferencePanel(title, instruction); showAccept = false; showCancel = false; }
    else if (task == Task::subshape) { addSubshapePanel(title, instruction); showAccept = false; showCancel = false; }
    else if (task == Task::ik) addIkPanel(title, instruction, accept, showAccept, showCancel);
    else if (task == Task::instance || task == Task::joint || task == Task::newInstance || task == Task::newJoint || task == Task::link || task == Task::coupling || task == Task::newCoupling)
        addMotionPanels(title, instruction, accept, showAccept, showCancel);
    else switch (task)
    {
        case Task::none:
            title = "Model"; instruction = "Select a body or sketch in the tree or the model view, or choose a command above.";
            showAccept = showCancel = false;
            form->addNote({}, "Create Sketch starts a sketch on an origin plane. Extrude turns a closed sketch into a body. Subtract removes a Tool body from a Target body.", 4);
            break;
        case Task::body:
        {
            title = f->name; instruction = "Edit the selected body, then press Apply."; cancel = "Revert";
            form->addHeader("Body");
            form->addText("name", "Name", f->name, "Friendly feature name shown in the model tree.");
            if (f->type == "box")
            {
                form->addHeader("Dimensions");
                form->addNumber("width", "Width (X)", f->width, "mm", "Box size along X.");
                form->addNumber("depth", "Depth (Y)", f->depth, "mm", "Box size along Y.");
                form->addNumber("height", "Height (Z)", f->height, "mm", "Box size along Z.");
            }
            else if (f->type == "cylinder")
            {
                form->addHeader("Dimensions");
                form->addNumber("radius", "Radius", f->radius, "mm", "Cylinder radius.");
                form->addNumber("height", "Height (Z)", f->height, "mm", "Cylinder height along Z.");
            }
            else if (f->type == "extrude")
            {
                form->addHeader("Extrude");
                form->addNote({}, "Profile: " + featureName(f->sketchId) + (f->reverse ? "  |  reversed" : ""), 1);
                form->addNumber("height", "Distance", f->height, "mm", "Extrusion distance from the sketch plane.");
            }
            else if (f->type == "cut")
            {
                form->addHeader("Cut");
                form->addNote({}, "Profile: " + featureName(f->sketchId) + "  |  Target: " + featureName(f->targetFeatureId), 2);
                if (f->throughAll) form->addNote({}, "Cuts through all.", 1);
                else form->addNumber("height", "Depth", f->height, "mm", "Cut depth from the sketch plane.");
            }
            else if (f->type == "subtract") { form->addHeader("Subtract"); form->addNote({}, "Target: " + featureName(f->firstOperand) + "\nTool: " + featureName(f->secondOperand), 2); }
            else if (f->type == "union" || f->type == "intersect") { form->addHeader(f->type == "union" ? "Union" : "Intersect"); form->addNote({}, featureName(f->firstOperand) + " and " + featureName(f->secondOperand), 2); }
            else if (f->type == "revolve")
            {
                form->addHeader("Revolve");
                addRevolveFields(f->revolve, f->sketchId);
                if(f->lathe)form->addButton("latheProfile","Lathe profile...",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->startTask(Task::lathe);});},"Edit outside and bore stations.");
                form->addButton("revolveSketch","Edit source sketch",[this,sketch=f->sketchId]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),sketch]{if(safe!=nullptr)safe->enterSketch(sketch);});},"Edit the authoritative 2D profile.");
            }
            else if (f->type == "fillet")
            {
                form->addHeader("Fillet");
                form->addNote({}, "Target: " + featureName(f->targetFeatureId) + "  |  " + juce::String(f->edges.size()) + " edges filleted", 2);
                form->addNumber("radius", "Radius", f->radius, "mm", "Fillet radius.");
            }
            const juce::String placement = f->type == "box" ? "Corner " : f->type == "cylinder" ? "Base centre " : "Offset ";
            form->addHeader("Placement (world)");
            form->addNumber("x", placement + "X", f->x, "mm", "World X position.");
            form->addNumber("y", placement + "Y", f->y, "mm", "World Y position.");
            form->addNumber("z", placement + "Z", f->z, "mm", "World Z position.");
            juce::String materialText = "No engineering material assigned.";
            if (const auto* a = document.materialsData().assignmentFor(selectedId))
                if (const auto* m = document.materialsData().find(a->materialId)) materialText = "Material: " + m->name + " (revision " + juce::String(m->revision) + ")";
            form->addNote({}, materialText, 2);
            form->addButton("materials", "Materials and Sheet Stock...", [this] { showMaterialsManager(); }, "Manage libraries and assign a physical material to this part.");
            form->addHeader("Actions");
            const auto id = selectedId;
            form->addButton("visibility", f->visible ? "Hide" : "Show", [this, id] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id] { if (safe != nullptr) safe->toggleVisibility(id); }); }, "Toggle visibility in the model view.");
            if (isSolid(selectedId)) form->addButton("subtractFrom", "Subtract from this body...", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->startTask(Task::boolean, "subtract"); }); }, "Start Subtract with this body as the Target.");
            form->addButton("delete", "Delete", [this, id] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id] { if (safe != nullptr) safe->deleteFeature(id); }); }, "Delete this feature (Undo restores it).");
            break;
        }
        case Task::sketch:
        {
            const auto* sketch = document.findSketch(selectedId);
            title = f->name; instruction = "Edit the sketch to draw on it, or use it as the profile for Extrude, Revolve or Cut Sketch.";
            accept = "Edit Sketch"; showCancel = false;
            form->addHeader("Sketch");
            form->addText("name", "Name", f->name, "Friendly sketch name.");
            form->addButton("rename", "Rename", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe == nullptr || safe->form == nullptr) return; juce::String e; if (!safe->service->rename(safe->selectedId, safe->form->text("name"), e)) safe->showError(e); else { safe->refreshFromDocument(true); safe->rebuildTaskPanel(false); safe->setMessage("Sketch renamed.", 0); } }); }, "Apply the new sketch name.");
            if (sketch != nullptr) form->addNote({}, (sketch->onFace() ? "On " + supportText(*sketch) : sketch->plane + " plane  |  offset " + juce::String(sketch->offset) + " mm") + "  |  " + juce::String((int) sketch->entities.size()) + " entities", sketch->onFace() ? 2 : 1);
            form->addHeader("Use sketch");
            form->addButton("extrude", "Extrude...", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->startTask(Task::extrude); }); }, "Extrude this sketch's closed profile.");
            form->addButton("revolve", "Revolve...", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->startTask(Task::revolve); }); }, "Revolve this sketch around an axis.");
            form->addButton("cut", "Cut Sketch...", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->startTask(Task::cut); }); }, "Cut this sketch's profile from a body.");
            const auto id = selectedId;
            form->addButton("delete", "Delete", [this, id] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id] { if (safe != nullptr) safe->deleteFeature(id); }); }, "Delete this sketch (Undo restores it).");
            break;
        }
        case Task::createSketch:
        {
            title = "Create Sketch"; instruction = "Choose the selected face or an origin plane to sketch on, then press Create Sketch."; accept = "Create Sketch";
            {
                djehuti::cad::CadFaceInfo support; juce::String reason;
                form->addHeader("Face");
                if (selectedSupport(support, reason))
                    form->addSlot("planeFACE", "Selected face", sketchPlane == "FACE" ? "Selected" : "Choose", targetColour, sketchPlane == "FACE",
                                  [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) { safe->sketchPlane = "FACE"; safe->rebuildTaskPanel(true); } }); },
                                  "Attach the sketch to " + support.token + " of " + featureName(support.featureId) + "; it follows that face when the body changes.");
                else form->addNote({}, reason + " Switch to Face mode and click a planar face to sketch on it.", 2);
            }
            form->addHeader("Plane");
            for (const auto& plane : { juce::String("XY"), juce::String("XZ"), juce::String("YZ") })
            {
                const auto tip = plane == "XY" ? "Sketch on the XY plane (normal along Z)." : plane == "XZ" ? "Sketch on the XZ plane (normal along Y)." : "Sketch on the YZ plane (normal along X).";
                form->addSlot("plane" + plane, plane + " plane", sketchPlane == plane ? "Selected" : "Choose", targetColour, sketchPlane == plane,
                              [this, plane] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), plane] { if (safe != nullptr) { safe->sketchPlane = plane; safe->rebuildTaskPanel(true); } }); }, tip);
            }
            form->addNumber("offset", "Offset", 0.0, "mm", "Distance of the sketch plane from the origin along its normal.");
            break;
        }
        case Task::sketchEdit:
        case Task::sketchEntity:
        {
            const auto* sketch = document.findSketch(editingSketchId);
            const auto plane = sketch != nullptr ? (sketch->onFace() ? juce::String("UV") : sketch->plane) : juce::String("XY");
            const djehuti::cad::CadSketchEntity* entity = nullptr;
            if (sketch != nullptr) for (const auto& e : sketch->entities) if (e.id == sketchEntityId) entity = &e;
            if (entity == nullptr)
            {
                const auto tool = sketchCanvas->tool();
                title = featureName(editingSketchId) + "  |  " + (sketch != nullptr && sketch->onFace() ? "on " + supportText(*sketch) : plane + " plane");
                instruction = tool == "line" ? "Line: drag from start to end. Ends snap to existing line ends (a ring shows the snap). Esc cancels the gesture."
                            : tool == "rectangle" ? "Rectangle: drag from one corner to the opposite corner. Esc cancels the gesture."
                            : tool == "circle" ? "Circle: drag from the centre outwards. Esc cancels the gesture."
                            : tool == "curve" ? "Curve: drag from start to end (ends snap like lines). Then, with Select, drag its square handles to shape it."
                            : "Select: click an entity to edit it; drag a vertex (ring on hover) or a curve handle to move it; double-click a line to insert a vertex. Ctrl+Z undoes, Ctrl+Y redoes.";
                accept = "Finish Sketch"; showCancel = false;
                form->addNote({}, "Choose Line, Rectangle, Circle or Curve above to draw. The tool stays active until you pick another tool or press Esc. Finish Sketch keeps all completed geometry.", 4);
                break;
            }
            // A selected endpoint of this entity: exact coordinates, moved with everything joined to it.
            const auto addVertexSection = [&]
            {
                if (!sketchVertexRef.startsWith(entity->id + ":")) return;
                const auto which = sketchVertexRef.fromFirstOccurrenceOf(":", false, false);
                const auto vu = which == "p1" ? entity->x1 : entity->x2, vv = which == "p1" ? entity->y1 : entity->y2;
                form->addHeader("Selected vertex (" + juce::String(which == "p1" ? "start" : "end") + ")");
                form->addNumber("vertexU", "Vertex " + plane.substring(0, 1), vu, "mm", "Exact position of the selected vertex; joined vertices move with it.");
                form->addNumber("vertexV", "Vertex " + plane.substring(1, 2), vv, "mm", "Exact position of the selected vertex; joined vertices move with it.");
                const auto ref = sketchVertexRef;
                form->addButton("moveVertex", "Move vertex", [this, ref] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), ref] { if (safe != nullptr && safe->form != nullptr) safe->moveSketchPoint(ref, safe->form->number("vertexU"), safe->form->number("vertexV")); }); }, "Move the selected vertex to these coordinates.");
            };
            if (entity->type == "bezier")
            {
                title = friendlyEntity(entity->id) + "  |  " + plane + " plane"; cancel = "Done";
                instruction = "Drag the endpoints (rings) or the square handles in the canvas, or type positions and press Apply.";
                form->addHeader("Endpoints (" + plane + " plane)");
                form->addNumber("p1u", "Start " + plane.substring(0, 1), entity->x1, "mm", "Curve start; joined vertices move with it.");
                form->addNumber("p1v", "Start " + plane.substring(1, 2), entity->y1, "mm", "Curve start; joined vertices move with it.");
                form->addNumber("p2u", "End " + plane.substring(0, 1), entity->x2, "mm", "Curve end; joined vertices move with it.");
                form->addNumber("p2v", "End " + plane.substring(1, 2), entity->y2, "mm", "Curve end; joined vertices move with it.");
                form->addHeader("Handles (control points)");
                form->addNumber("c1u", "Handle 1 " + plane.substring(0, 1), entity->cx1, "mm", "Control point at the start; sets the start tangent.");
                form->addNumber("c1v", "Handle 1 " + plane.substring(1, 2), entity->cy1, "mm", "Control point at the start; sets the start tangent.");
                form->addNumber("c2u", "Handle 2 " + plane.substring(0, 1), entity->cx2, "mm", "Control point at the end; sets the end tangent.");
                form->addNumber("c2v", "Handle 2 " + plane.substring(1, 2), entity->cy2, "mm", "Control point at the end; sets the end tangent.");
                form->addHeader("Continuity");
                const auto id = entity->id, sketchId = editingSketchId;
                for (const auto* end : { "p1", "p2" })
                {
                    const juce::String endName(end);
                    form->addButton(juce::String("smooth") + end, endName == "p1" ? "Smooth join at start" : "Smooth join at end", [this, id, sketchId, endName]
                    {
                        postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id, sketchId, endName]
                        {
                            if (safe == nullptr) return;
                            juce::String e;
                            if (!safe->service->smoothSketchJoint(sketchId, id, endName, e)) { safe->showError(e); return; }
                            safe->refreshFromDocument(true); safe->rebuildTaskPanel(false); safe->sketchCanvas->repaint();
                            safe->setMessage("Handle aligned with the connected segment: the join is tangent.", 0);
                        });
                    }, "Turn this end's handle onto the direction of the segment joined there (tangent join), keeping its length.");
                }
                addVertexSection();
                break;
            }
            const bool circle = entity->type == "circle";
            const bool diameter = circle && circleDiameterDriven(*sketch, entity->id);
            title = friendlyEntity(entity->id) + "  |  " + plane + " plane"; instruction = "Edit the entity's dimension and position, or add a constraint."; cancel = "Done";
            form->addHeader("Dimension");
            if (circle) form->addNumber("dimension", diameter ? "Diameter" : "Radius", diameter ? entity->radius * 2.0 : entity->radius, "mm", diameter ? "This circle is driven by a diameter constraint." : "Circle radius.");
            else form->addNumber("dimension", "Length", std::hypot(entity->x2 - entity->x1, entity->y2 - entity->y1), "mm", "Line length.");
            form->addHeader("Position (" + plane + " plane)");
            const juce::String point = circle ? "Centre " : "Start ";
            form->addNumber("u", point + plane.substring(0, 1), entity->x1, "mm", "Sketch-plane coordinate along " + plane.substring(0, 1) + ".");
            form->addNumber("v", point + plane.substring(1, 2), entity->y1, "mm", "Sketch-plane coordinate along " + plane.substring(1, 2) + ".");
            form->addHeader("Constraint");
            const auto types = juce::StringArray::fromTokens(applicableConstraintTypes(), ",", {});
            auto& typeBox = form->addChoice("constraintType", "Type", types, 0, "Constraint to add to the selected entity.");
            form->addNumber("constraintValue", "Value", 0.0, "mm", "Value for the dimensional constraint.");
            form->addNote("second", "Second entity: " + (sketchSecondEntityId.isEmpty() ? juce::String("none (Shift-click another entity)") : friendlyEntity(sketchSecondEntityId)), 1);
            form->addButton("addConstraint", "Add Constraint", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->addConstraint(); }); }, "Add the chosen constraint.");
            const auto length = std::hypot(entity->x2 - entity->x1, entity->y2 - entity->y1), radius = entity->radius;
            auto syncValue = [this, length, radius, circle]
            {
                if (form == nullptr) return;
                const auto type = form->choiceText("constraintType").toLowerCase();
                const bool dimensional = type == "length" || type == "radius" || type == "diameter";
                form->setRowVisible("constraintValue", dimensional);
                form->setCaption("constraintValue", type == "diameter" ? "Diameter" : type == "radius" ? "Radius" : "Length");
                if (auto* e = dynamic_cast<juce::TextEditor*>(form->control("constraintValue"))) e->setText(juce::String(type == "diameter" ? radius * 2.0 : type == "radius" ? radius : (circle ? radius : length), 3), false);
                resized(); validateTask();
            };
            typeBox.onChange = syncValue;
            syncValue();
            if (!circle)
            {
                form->addHeader("Vertices");
                const auto id = entity->id; const auto mu = (entity->x1 + entity->x2) * 0.5, mv = (entity->y1 + entity->y2) * 0.5;
                form->addButton("insertVertex", "Insert vertex at midpoint", [this, id, mu, mv] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id, mu, mv] { if (safe != nullptr) safe->insertSketchVertex(id, mu, mv); }); },
                                "Split this line into two joined lines at its midpoint (or double-click the line where the vertex should go).");
                addVertexSection();
            }
            break;
        }
        case Task::newBox:
            title = "New Box"; instruction = "Enter the box size, then press Create Box. Set its placement afterwards in its properties."; accept = "Create Box";
            form->addHeader("Dimensions");
            form->addNumber("width", "Width (X)", boxSize[0], "mm", "Box size along X.");
            form->addNumber("depth", "Depth (Y)", boxSize[1], "mm", "Box size along Y.");
            form->addNumber("height", "Height (Z)", boxSize[2], "mm", "Box size along Z.");
            break;
        case Task::newCylinder:
            title = "New Cylinder"; instruction = "Enter the cylinder size, then press Create Cylinder. Set its placement afterwards in its properties."; accept = "Create Cylinder";
            form->addHeader("Dimensions");
            form->addNumber("radius", "Radius", cylinderSize[0], "mm", "Cylinder radius.");
            form->addNumber("height", "Height (Z)", cylinderSize[1], "mm", "Cylinder height along Z.");
            break;
        case Task::extrude:
            title = "Extrude"; instruction = "Choose a closed sketch profile in the model tree and set the distance."; accept = "Extrude";
            form->addHeader("Profile");
            form->addSlot("profile", "Profile", slotText(profileId, "Select a sketch in the tree"), selectedColour, true, [] {}, "The sketch whose closed profile is extruded.");
            form->addHeader("Extent");
            form->addNumber("distance", "Distance", extrudeDistance, "mm", "Extrusion distance from the sketch plane.");
            form->addToggle("reverse", "Reverse direction", false, "Extrude to the opposite side of the sketch plane.");
            break;
        case Task::lathe:
            title = "Virtual Lathe"; instruction = "Enter axial positions and outside/bore diameters in mm. Repeated positions make shoulders and grooves."; accept = editingLatheId.isEmpty()?"Turn part":"Update turned part";
            addLatheFields(); break;
        case Task::revolve:
            title = "Revolve"; instruction = "Choose a closed sketch and an axis in its plane. Set the rotation angle."; accept = "Revolve";
            addRevolveFields(revolveSettings, profileId);
            break;
        case Task::cut:
            title = "Cut Sketch"; instruction = "Choose a sketch profile and the Target body to cut. Pick in the tree or the model view."; accept = "Cut";
            form->addHeader("Inputs");
            form->addSlot("profile", "Profile", slotText(profileId, "Select a sketch in the tree"), selectedColour, profileId.isEmpty(), [] {}, "Closed sketch profile used as the cutter.");
            form->addSlot("target", "Target", slotText(targetId, "Select a body to cut"), targetColour, profileId.isNotEmpty(), [] {}, "Body that is cut (kept).");
            form->addHeader("Extent");
            form->addNumber("depth", "Depth", cutDepth, "mm", "Cut depth from the sketch plane.");
            form->addToggle("throughAll", "Through all", false, "Cut through the whole target.");
            form->addToggle("reverse", "Reverse direction", false, "Cut toward the opposite side of the sketch plane.");
            break;
        case Task::boolean:
        {
            const bool subtract = booleanKind == "subtract";
            title = booleanKind.substring(0, 1).toUpperCase() + booleanKind.substring(1); accept = title;
            instruction = subtract ? "Target is the body to keep; Tool is the body that cuts it. Click a slot, then pick a body in the tree or the model view."
                                   : "Choose the two bodies. Click a slot, then pick a body in the tree or the model view.";
            const auto first = subtract ? "Target" : "First body", second = subtract ? "Tool" : "Second body";
            form->addHeader("Bodies");
            form->addSlot("target", first, slotText(targetId, subtract ? "Select the body to keep" : "Select a body"), targetColour, activeRole == 0,
                          [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) { safe->activeRole = 0; safe->rebuildTaskPanel(true); } }); }, "Click, then pick the body in the tree or the view.");
            form->addSlot("tool", second, slotText(toolId, subtract ? "Select the cutting body" : "Select a body"), toolColour, activeRole == 1,
                          [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) { safe->activeRole = 1; safe->rebuildTaskPanel(true); } }); }, "Click, then pick the body in the tree or the view.");
            form->addButton("swap", "Swap", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) { std::swap(safe->targetId, safe->toolId); safe->updateHighlights(); safe->rebuildTaskPanel(true); } }); }, "Exchange the two roles.");
            form->addNote({}, subtract ? "Both source bodies stay in the feature history and are hidden after the operation. Undo restores them." : "Both source bodies stay in the feature history and are hidden after the operation.", 3);
            break;
        }
        case Task::fillet:
        {
            title = "Fillet";
            instruction = "Select a solid body and one or more of its edges in Edge mode, then enter the radius.";
            accept = "Fillet";
            form->addHeader("Body and Edges");
            form->addSlot("target", "Target body", slotText(targetId, "Select a body in tree or view"), targetColour, targetId.isEmpty(), [this] {
                postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
                    if (safe != nullptr) { safe->activeRole = 0; safe->rebuildTaskPanel(true); }
                });
            }, "Click, then pick the solid body to fillet.");
            juce::String edgeSummary;
            if (filletEdges.empty())
            {
                edgeSummary = "0 edges selected (pick edges in Edge mode)";
            }
            else
            {
                juce::StringArray edgeNums;
                for (int idx : filletEdges) edgeNums.add(juce::String(idx));
                edgeSummary = juce::String((int)filletEdges.size()) + " edge" + (filletEdges.size() == 1 ? "" : "s") + " selected: " + edgeNums.joinIntoString(", ");
            }
            form->addNote("edgeCount", edgeSummary, filletEdges.size() > 4 ? 2 : 1);
            if (!filletEdges.empty())
            {
                form->addButton("clearEdges", "Clear selected edges", [this] {
                    postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
                        if (safe != nullptr) {
                            safe->filletEdges.clear();
                            if (safe->selection != nullptr) safe->selection->clear();
                            safe->rebuildTaskPanel(true);
                        }
                    });
                }, "Clear the currently selected edges.");
            }
            form->addHeader("Parameters");
            form->addNumber("radius", "Radius", filletRadius, "mm", "Constant fillet radius.");
            break;
        }
    }

    if (!previous.isEmpty() && task != Task::ik) form->restore(previous);
    taskTitle.setText(title, juce::dontSendNotification);
    taskInstruction.setText(instruction, juce::dontSendNotification);
    acceptButton.setButtonText(accept); cancelButton.setButtonText(cancel);
    acceptButton.setVisible(showAccept); cancelButton.setVisible(showCancel);
    taskViewport.setViewedComponent(form.get(), false);
    validateTask();
    resized();
    if (previousScroll > 0) taskViewport.setViewPosition(0, previousScroll);   // refreshes keep the reader's place
    repaint();
}

juce::String CadWorkspacePanel::applicableConstraintTypes() const
{
    if (const auto* sketch = service->document().findSketch(editingSketchId))
        for (const auto& e : sketch->entities)
            if (e.id == sketchEntityId) return e.type == "circle" ? "Radius,Diameter,Fixed,Coincident" : "Length,Horizontal,Vertical,Fixed,Coincident";
    return "Length,Horizontal,Vertical,Fixed,Coincident";
}

void CadWorkspacePanel::assignRole(const juce::String& id)
{
    if (task == Task::newCoupling)
    {
        if (!isJoint(id)) { setMessage(featureName(id) + " is not a joint. Pick the two joints to couple in the Assembly group of the tree.", 1); return; }
        if (activeRole == 0) { couplingJointA = id; if (couplingJointB == id) couplingJointB.clear(); if (couplingJointB.isEmpty()) activeRole = 1; }
        else { couplingJointB = id; if (couplingJointA == id) couplingJointA.clear(); }
        commandMessage.clear(); rebuildTaskPanel(true); return;
    }
    if (task == Task::newJoint)
    {
        if (!isInstance(id)) { setMessage(featureName(id) + " is not an assembly instance. Create an instance of the body first (Instance).", 1); return; }
        if (activeRole == 0) { jointParentId = id; if (jointChildId == id) jointChildId.clear(); if (jointChildId.isEmpty()) activeRole = 1; }
        else { jointChildId = id; if (jointParentId == id) jointParentId.clear(); }
        commandMessage.clear(); updateHighlights(); rebuildTaskPanel(true); return;
    }
    if (task == Task::newInstance)
    {
        const auto body = isInstance(id) ? service->document().mechanicsData().findInstance(id)->featureId : id;
        if (!isSolid(body)) { setMessage(featureName(id) + " is not a solid body.", 2); return; }
        instanceBodyId = body; commandMessage.clear(); updateHighlights(); rebuildTaskPanel(true); return;
    }
    // Bodies picked through an assembly instance take part in modeling commands as their source body.
    const auto featureId = isInstance(id) ? service->document().mechanicsData().findInstance(id)->featureId : id;
    if (featureId != id) { assignRole(featureId); return; }
    const auto* f = service->document().find(id);
    if (f == nullptr) return;
    commandMessage.clear();
    if (task == Task::revolve && f->type == "sketch")
    {
        revolveSettings = readRevolveForm(); profileId = id;
        rebuildTaskPanel(false); return;
    }
    if (task == Task::extrude || task == Task::revolve || task == Task::cut)
    {
        if (f->type == "sketch") profileId = id;
        else if (task == Task::cut && isSolid(id)) targetId = id;
        else setMessage(f->name + " is not a sketch. Choose a sketch profile for " + (task == Task::extrude ? juce::String("Extrude.") : juce::String("Cut Sketch.")), 1);
    }
    else if (task == Task::boolean)
    {
        if (!isSolid(id)) { setMessage(f->name + " is not a solid body.", 2); return; }
        if (activeRole == 0) { targetId = id; if (toolId == id) toolId.clear(); if (toolId.isEmpty()) activeRole = 1; }
        else { toolId = id; if (targetId == id) targetId.clear(); if (targetId.isEmpty()) activeRole = 0; }
    }
    else if (task == Task::fillet)
    {
        if (!isSolid(id)) { setMessage(f->name + " is not a solid body.", 2); return; }
        targetId = id;
        filletEdges.clear();
    }
    updateHighlights();
    rebuildTaskPanel(true);
}

void CadWorkspacePanel::validateTask()
{
    if (form == nullptr) return;
    juce::String problem; int severity = 0;
    auto requirePositive = [&](const juce::String& key, const juce::String& label)
    {
        if (problem.isEmpty() && form->rowVisible(key) && (!form->numberValid(key) || form->number(key) <= 0.0)) { problem = label + " must be a number greater than zero."; severity = 2; }
    };
    auto requireNumber = [&](const juce::String& key, const juce::String& label)
    {
        if (problem.isEmpty() && form->rowVisible(key) && !form->numberValid(key)) { problem = label + " must be a number."; severity = 2; }
    };
    switch (task)
    {
        case Task::body:
            if (form->text("name").isEmpty()) { problem = "Name cannot be empty."; severity = 2; }
            for (const auto* key : { "width", "depth", "height", "radius" }) requirePositive(key, form->control(key) != nullptr ? juce::String(key).substring(0, 1).toUpperCase() + juce::String(key).substring(1) : juce::String());
            for (const auto* key : { "x", "y", "z" }) requireNumber(key, juce::String("Position ") + juce::String(key).toUpperCase());
            break;
        case Task::createSketch:
            if (sketchPlane.isEmpty()) problem = "Choose the selected face or the XY, XZ or YZ plane.";
            if (sketchPlane != "FACE") requireNumber("offset", "Offset");
            break;
        case Task::sketchEntity:
            requirePositive("dimension", "The dimension");
            requireNumber("u", "Position"); requireNumber("v", "Position");
            break;
        case Task::newBox: requirePositive("width", "Width"); requirePositive("depth", "Depth"); requirePositive("height", "Height"); break;
        case Task::newCylinder: requirePositive("radius", "Radius"); requirePositive("height", "Height"); break;
        case Task::lathe: { juce::String e; readLatheForm(e); if(e.isNotEmpty()){problem=e;severity=2;} break; }
        case Task::revolve: break;
        case Task::extrude:
            if (profileId.isEmpty()) problem = "Select a sketch with a closed profile in the model tree.";
            requirePositive("distance", "Distance");
            break;
        case Task::cut:
            if (profileId.isEmpty()) problem = "Select the sketch profile in the model tree.";
            else if (targetId.isEmpty()) problem = "Select the Target body to cut in the tree or the model view.";
            if (!form->toggle("throughAll")) requirePositive("depth", "Depth");
            break;
        case Task::boolean:
        {
            const bool subtract = booleanKind == "subtract";
            if (targetId.isEmpty()) problem = subtract ? "Select the Target body (the body to keep)." : "Select the first body.";
            else if (toolId.isEmpty()) problem = subtract ? "Select the Tool body (the body that cuts the Target)." : "Select the second body.";
            else if (targetId == toolId) { problem = "Choose two different bodies."; severity = 2; }
            else if (!isSolid(targetId) || !isSolid(toolId)) { problem = "Both inputs must be solid bodies."; severity = 2; }
            break;
        }
        case Task::fillet:
            if (targetId.isEmpty()) problem = "Select a solid body to fillet.";
            else if (!isSolid(targetId)) problem = featureName(targetId) + " is not a solid body.";
            else if (filletEdges.empty()) problem = "Select at least one edge in Edge mode.";
            requirePositive("radius", "Radius");
            break;
        case Task::none: case Task::sketch: case Task::sketchEdit: break;
        default: validateMotionTask(problem, severity); break;
    }
    if (task == Task::revolve || (task == Task::body && form->control("angleDegrees") != nullptr))
    {
        if (revolveFormSketch().isEmpty()) { problem = "Select a closed sketch profile."; severity = 2; }
        requirePositive("angleDegrees", "Revolve angle");
        if (problem.isEmpty() && form->number("angleDegrees") > 360) { problem = "Revolve angle must be no greater than 360 degrees."; severity = 2; }
        const bool custom = form->choice("axisType") == 5;
        for (const auto* key : {"axisX", "axisY", "axisZ", "axisDX", "axisDY", "axisDZ"})
        { form->setRowVisible(key, custom); if (custom) requireNumber(key, "Custom axis coordinate"); }
        if (custom && problem.isEmpty() && std::hypot(form->number("axisDX"), form->number("axisDY"), form->number("axisDZ")) < 1e-12)
        { problem = "Custom axis direction must be nonzero."; severity = 2; }
    }
    if (task == Task::cut) form->setRowVisible("depth", !form->toggle("throughAll"));
    validationMessage = problem; validationSeverity = severity;
    acceptButton.setEnabled(problem.isEmpty());
    setMessage(commandMessage, commandSeverity);
}

void CadWorkspacePanel::setMessage(const juce::String& text, int severity)
{
    if (&text != &commandMessage) { commandMessage = text; commandSeverity = severity; }
    const bool showValidation = validationMessage.isNotEmpty() && (commandMessage.isEmpty() || validationSeverity >= commandSeverity);
    const auto shown = showValidation ? validationMessage : commandMessage;
    const auto level = showValidation ? validationSeverity : commandSeverity;
    messageLabel.setText(shown, juce::dontSendNotification);
    messageLabel.setColour(juce::Label::textColourId, level == 2 ? juce::Colour(0xffff8a80) : level == 1 ? juce::Colour(0xffffcf70) : juce::Colour(0xff8fd7c7));
    messageLabel.setColour(juce::Label::backgroundColourId, shown.isEmpty() ? juce::Colours::transparentBlack : (level == 2 ? juce::Colour(0x33ff5a50) : level == 1 ? juce::Colour(0x26ffcf70) : juce::Colour(0x1f2a8c99)));
    statusLabel.setText(editingSketchId.isNotEmpty() ? "Editing " + featureName(editingSketchId) + "  |  " + sketchCanvas->tool() + " tool  |  Esc cancels the current gesture" : (shown.isNotEmpty() ? shown : juce::String("Select a body, create a sketch, or choose Subtract.")), juce::dontSendNotification);
    resized();
}

void CadWorkspacePanel::acceptTask()
{
    if (form == nullptr || !acceptButton.isEnabled()) return;
    juce::String e, id;
    const auto& document = service->document();
    switch (task)
    {
        case Task::body:
        {
            const auto* f = document.find(selectedId);
            if (f == nullptr) return;
            const auto name = form->text("name");
            const auto w = form->control("width") ? form->number("width") : f->width, d = form->control("depth") ? form->number("depth") : f->depth;
            const auto h = form->control("height") ? form->number("height") : f->height, r = form->control("radius") ? form->number("radius") : f->radius;
            const auto x = form->number("x"), y = form->number("y"), z = form->number("z");
            bool ok = true;
            if (f->type == "revolve")
            {
                const auto p = readRevolveForm(); const auto sketch = revolveFormSketch();
                if (sketch != f->sketchId || juce::JSON::toString(djehuti::cad::revolveParametersToJson(p),false,17) != juce::JSON::toString(djehuti::cad::revolveParametersToJson(f->revolve),false,17))
                    ok = service->updateRevolve(selectedId, sketch, p, e);
                if (!ok) { showError(e); return; }
                f = document.find(selectedId);
            }
            if (ok && name != f->name) ok = service->rename(selectedId, name, e);
            if (ok && (f = document.find(selectedId)) != nullptr && (w != f->width || d != f->depth || h != f->height || r != f->radius)) ok = service->setDimensions(selectedId, w, d, h, r, e);
            if (ok && (f = document.find(selectedId)) != nullptr && (x != f->x || y != f->y || z != f->z)) ok = service->setPlacement(selectedId, x, y, z, f->rx, f->ry, f->rz, e);
            refreshFromDocument(true);
            rebuildTaskPanel(false);
            if (!ok) showError(e); else setMessage("Changes applied.", 0);
            return;
        }
        case Task::sketch: enterSketch(selectedId); return;
        case Task::createSketch:
            if (sketchPlane == "FACE")
            {
                djehuti::cad::CadFaceInfo support; juce::String reason;
                if (!selectedSupport(support, reason)) { showError(reason); return; }
                if (!service->createFaceSketch(support.featureId, support.token, id, e)) { showError(e); return; }
                if (selection != nullptr) selection->clear();
                refreshFromDocument(true);
                enterSketch(id);
                setMessage(featureName(id) + " created on face " + support.token + " of " + featureName(support.featureId) + ". Choose a drawing tool.", 0);
                return;
            }
            if (!service->createSketch(sketchPlane, form->number("offset"), id, e)) { showError(e); return; }
            refreshFromDocument(true);
            enterSketch(id);
            setMessage(featureName(id) + " created on the " + sketchPlane + " plane. Choose a drawing tool.", 0);
            return;
        case Task::sketchEdit: finishSketch(); return;
        case Task::sketchEntity:
        {
            const auto* sketch = document.findSketch(editingSketchId);
            const djehuti::cad::CadSketchEntity* entity = nullptr;
            if (sketch != nullptr) for (const auto& item : sketch->entities) if (item.id == sketchEntityId) entity = &item;
            if (entity == nullptr) return;
            if (entity->type == "bezier")
            {
                // Each changed point is one move (endpoints carry what is joined to them); usually one changes.
                const auto sketchId = editingSketchId, entityId = sketchEntityId;
                const struct { const char* ref; const char* u; const char* v; double x, y; } points[] {
                    { "p1", "p1u", "p1v", entity->x1, entity->y1 }, { "p2", "p2u", "p2v", entity->x2, entity->y2 },
                    { "c1", "c1u", "c1v", entity->cx1, entity->cy1 }, { "c2", "c2u", "c2v", entity->cx2, entity->cy2 } };
                bool ok = true, changed = false;
                for (const auto& p : points)
                {
                    const auto u = form->number(p.u), v = form->number(p.v);
                    if (std::abs(u - p.x) <= 1.0e-9 && std::abs(v - p.y) <= 1.0e-9) continue;
                    changed = true;
                    if (!(ok = service->moveSketchPoint(sketchId, entityId + ":" + p.ref, u, v, e))) break;
                }
                refreshFromDocument(true); rebuildTaskPanel(false); sketchCanvas->repaint();
                if (!ok) showError(e); else setMessage(changed ? "Curve updated." : "No changes to apply.", 0);
                return;
            }
            const bool circle = entity->type == "circle", diameter = circle && circleDiameterDriven(*sketch, entity->id);
            const auto current = circle ? (diameter ? entity->radius * 2.0 : entity->radius) : std::hypot(entity->x2 - entity->x1, entity->y2 - entity->y1);
            const auto u = form->number("u"), v = form->number("v"), value = form->number("dimension");
            const auto sketchId = editingSketchId, entityId = sketchEntityId;
            const bool moved = std::abs(u - entity->x1) > 1.0e-9 || std::abs(v - entity->y1) > 1.0e-9, resized_ = std::abs(value - current) > 1.0e-9;
            bool ok = true;
            if (moved || resized_) ok = service->editSketchEntity(sketchId, entityId, moved, u, v, resized_, value, e);   // one Apply = one undo step
            refreshFromDocument(true);
            rebuildTaskPanel(false);
            if (!ok) showError(e); else setMessage(moved || resized_ ? "Sketch entity updated." : "No changes to apply.", 0);
            return;
        }
        case Task::newBox:
            boxSize[0] = form->number("width"); boxSize[1] = form->number("depth"); boxSize[2] = form->number("height");
            if (!service->createBox(boxSize[0], boxSize[1], boxSize[2], id, e)) { showError(e); return; }
            refreshFromDocument(true); selectFeature(id); setMessage(featureName(id) + " created at the origin.", 0);
            return;
        case Task::newCylinder:
            cylinderSize[0] = form->number("radius"); cylinderSize[1] = form->number("height");
            if (!service->createCylinder(cylinderSize[0], cylinderSize[1], id, e)) { showError(e); return; }
            refreshFromDocument(true); selectFeature(id); setMessage(featureName(id) + " created at the origin.", 0);
            return;
        case Task::lathe:
        {
            auto parameters=readLatheForm(e);if(e.isNotEmpty()){showError(e);return;}
            auto* args=new juce::DynamicObject();args->setProperty("action",editingLatheId.isEmpty()?"create":"update");args->setProperty("id",editingLatheId);args->setProperty("parameters",parameters);
            const auto reply=service->latheCommand(args);if(!(bool)reply["ok"]){showError(reply["error"].toString());return;}
            id=reply["id"].toString();refreshFromDocument(true);selectFeature(id);setMessage("Turned part updated from its editable profile.",0);return;
        }
        case Task::revolve:
            revolveSettings = readRevolveForm();
            if (!service->createRevolve(revolveFormSketch(), revolveSettings, id, e)) { showError(e); return; }
            refreshFromDocument(true); selectFeature(id); setMessage(featureName(id) + " created.", 0);
            return;
        case Task::extrude:
            extrudeDistance = form->number("distance");
            if (!service->createExtrude(profileId, extrudeDistance, form->toggle("reverse"), id, e)) { showError(e); return; }
            refreshFromDocument(true); selectFeature(id); setMessage(featureName(id) + " created from " + featureName(profileId) + ".", 0);
            return;
        case Task::cut:
        {
            const bool through = form->toggle("throughAll");
            if (!through) cutDepth = form->number("depth");
            const auto target = targetId, profile = profileId;
            if (!service->createCut(profile, target, through ? 0.0 : cutDepth, through, form->toggle("reverse"), id, e)) { showError(e); return; }
            refreshFromDocument(true); selectFeature(id); setMessage(featureName(id) + " cut " + featureName(profile) + " from " + featureName(target) + ". The original body is hidden; Undo restores it.", 0);
            return;
        }
        case Task::boolean:
        {
            const auto target = targetId, tool = toolId, kind = booleanKind;
            const auto* a = document.find(target); const auto* b = document.find(tool);
            const auto targetVolume = a != nullptr ? volumeOf(a->shape) : 0.0, toolVolume = b != nullptr ? volumeOf(b->shape) : 0.0;
            if (!service->booleanOp(kind, target, tool, id, e)) { showError(e); return; }
            const auto* result = document.find(id);
            const auto resultVolume = result != nullptr ? volumeOf(result->shape) : 0.0;
            refreshFromDocument(true); selectFeature(id);
            const auto tolerance = 1.0e-6 * juce::jmax(1.0, targetVolume + toolVolume);
            if (kind == "subtract" && std::abs(resultVolume - targetVolume) <= tolerance)
                setMessage("The Tool does not intersect the Target, so " + featureName(id) + " has the same shape as " + featureName(target) + ". Undo removes it.", 1);
            else if (kind == "union" && std::abs(resultVolume - (targetVolume + toolVolume)) <= tolerance)
                setMessage("The bodies do not overlap, so " + featureName(id) + " keeps them as separate solids.", 1);
            else
                setMessage(featureName(id) + " created. " + featureName(target) + " and " + featureName(tool) + " are hidden in the tree; Undo restores them.", 0);
            return;
        }
        case Task::fillet:
        {
            filletRadius = form->number("radius");
            const auto target = targetId;
            const auto edges = filletEdges;
            if (!service->createFillet(target, edges, filletRadius, id, e)) { showError(e); return; }
            refreshFromDocument(true); selectFeature(id);
            setMessage(featureName(id) + " created. " + featureName(target) + " is hidden in the tree; Undo restores it.", 0);
            return;
        }
        case Task::none: return;
        default: acceptMotionTask(); return;
    }
}

void CadWorkspacePanel::cancelTask()
{
    switch (task)
    {
        case Task::body: rebuildTaskPanel(false); setMessage("Changes reverted.", 0); return;
        case Task::sketchEntity: selectSketchEntity({}, false); return;
        case Task::sketchEdit: case Task::sketch: case Task::none: return;
        case Task::instance: case Task::joint: case Task::coupling: rebuildTaskPanel(false); setMessage("Changes reverted.", 0); return;
        case Task::ik: ikDraft.reset(); ikDirty = false; selectFeature({}); setMessage("IK panel closed. The pose shown stays until you reset the preview.", 0); return;
        default: break;
    }
    selectFeature(selectedId);
    setMessage("Command cancelled; the model is unchanged.", 0);
}

void CadWorkspacePanel::enterSketch(const juce::String& sketchId)
{
    if (service->document().findSketch(sketchId) == nullptr) { showError("Select an existing sketch to edit."); return; }
    editingSketchId = selectedId = sketchId; sketchEntityId.clear(); sketchSecondEntityId.clear();
    task = Task::sketchEdit; commandMessage.clear(); validationMessage.clear();
    modelView.setVisible(false); sketchCanvas->setVisible(true);
    sketchCanvas->setSketch(sketchId); sketchCanvas->setSelection({}, {}); sketchCanvas->setTool("select");
    sketchCanvas->grabKeyboardFocus();
    syncTreeSelection(); updateHighlights(); updateToolbar();
    rebuildTaskPanel(false);
    repaint();
}

void CadWorkspacePanel::finishSketch()
{
    if (editingSketchId.isEmpty()) return;
    const auto sketchId = editingSketchId;
    sketchCanvas->cancelGesture(); sketchCanvas->setTool("select");
    editingSketchId.clear(); sketchEntityId.clear(); sketchSecondEntityId.clear();
    sketchCanvas->setVisible(false); modelView.setVisible(true);
    selectedId = sketchId; task = Task::sketch;
    commandMessage = "Sketch finished. Use Extrude or Cut Sketch with this profile."; commandSeverity = 0; validationMessage.clear();
    syncTreeSelection(); updateHighlights(); updateToolbar();
    rebuildTaskPanel(false);
    repaint();
}

void CadWorkspacePanel::setSketchTool(const juce::String& tool)
{
    sketchCanvas->setTool(tool);
    if (tool != "select") { sketchEntityId.clear(); sketchSecondEntityId.clear(); sketchCanvas->setSelection({}, {}); task = Task::sketchEdit; }
    sketchCanvas->grabKeyboardFocus();
    updateToolbar();
    rebuildTaskPanel(false);
}

void CadWorkspacePanel::chooseConstraint()
{
    if (sketchEntityId.isEmpty()) { setMessage("Select a sketch entity first (Select tool, click the entity), then choose a constraint.", 1); return; }
    juce::PopupMenu menu;
    const auto types = juce::StringArray::fromTokens(applicableConstraintTypes(), ",", {});
    for (int i = 0; i < types.size(); ++i) menu.addItem(i + 1, types[i]);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&constrainButton), [safe = juce::Component::SafePointer<CadWorkspacePanel>(this)](int result)
    {
        if (safe == nullptr || result <= 0 || safe->form == nullptr) return;
        if (auto* box = dynamic_cast<juce::ComboBox*>(safe->form->control("constraintType"))) box->setSelectedItemIndex(result - 1, juce::sendNotificationSync);
        safe->setMessage("Set the value if needed, then press Add Constraint.", 0);
    });
}

void CadWorkspacePanel::addConstraint()
{
    if (form == nullptr || sketchEntityId.isEmpty()) return;
    const auto type = form->choiceText("constraintType").toLowerCase();
    const bool dimensional = type == "length" || type == "radius" || type == "diameter";
    if (type == "coincident" && sketchSecondEntityId.isEmpty()) { showError("Coincident needs a second entity: Shift-click another entity in the sketch."); return; }
    if (dimensional && (!form->numberValid("constraintValue") || form->number("constraintValue") <= 0.0)) { showError("The constraint value must be a number greater than zero."); return; }
    juce::String id, e;
    if (!service->addSketchConstraint(editingSketchId, type == "fixed" ? "fixed" : type, sketchEntityId, sketchSecondEntityId, dimensional ? form->number("constraintValue") : 0.0, id, e)) { showError(e); return; }
    refreshFromDocument(true);
    rebuildTaskPanel(false);
    setMessage(type.substring(0, 1).toUpperCase() + type.substring(1) + " constraint added.", 0);
}

void CadWorkspacePanel::sketchGeometryAdded(const juce::String& what)
{
    refreshFromDocument(true);
    setMessage(what.substring(0, 1).toUpperCase() + what.substring(1) + " added. The " + what + " tool stays active; press Esc or Select to stop.", 0);
}

void CadWorkspacePanel::updateToolbar()
{
    const bool sketching = editingSketchId.isNotEmpty();
    for (auto* b : { &sketchButton, &boxButton, &cylinderButton, &extrudeButton, &revolveButton, &latheButton, &cutButton, &filletButton, &subtractButton, &unionButton, &intersectButton, &fitButton, &resetViewButton, &viewsButton, &instanceButton, &jointButton, &couplingButton, &linkButton, &ikButton, &bodyModeButton, &faceModeButton, &edgeModeButton }) b->setVisible(!sketching);
    {
        const auto mode = selection != nullptr ? selection->mode() : djehuti::cad::CadSelection::Mode::body;
        bodyModeButton.setToggleState(mode == djehuti::cad::CadSelection::Mode::body, juce::dontSendNotification);
        faceModeButton.setToggleState(mode == djehuti::cad::CadSelection::Mode::face, juce::dontSendNotification);
        edgeModeButton.setToggleState(mode == djehuti::cad::CadSelection::Mode::edge, juce::dontSendNotification);
    }
    couplingButton.setToggleState(task == Task::newCoupling, juce::dontSendNotification);
    instanceButton.setToggleState(task == Task::newInstance, juce::dontSendNotification); jointButton.setToggleState(task == Task::newJoint, juce::dontSendNotification); linkButton.setToggleState(task == Task::link, juce::dontSendNotification); ikButton.setToggleState(task == Task::ik, juce::dontSendNotification);
    for (auto* b : { &selectToolButton, &lineButton, &rectangleButton, &circleButton, &curveButton, &constrainButton, &fitSketchButton, &finishSketchButton }) b->setVisible(sketching);
    undoButton.setVisible(true); redoButton.setVisible(true);
    const auto tool = sketchCanvas->tool();
    selectToolButton.setToggleState(tool == "select", juce::dontSendNotification); lineButton.setToggleState(tool == "line", juce::dontSendNotification);
    rectangleButton.setToggleState(tool == "rectangle", juce::dontSendNotification); circleButton.setToggleState(tool == "circle", juce::dontSendNotification); curveButton.setToggleState(tool == "curve", juce::dontSendNotification);
    subtractButton.setToggleState(task == Task::boolean && booleanKind == "subtract", juce::dontSendNotification);
    unionButton.setToggleState(task == Task::boolean && booleanKind == "union", juce::dontSendNotification);
    intersectButton.setToggleState(task == Task::boolean && booleanKind == "intersect", juce::dontSendNotification);
    extrudeButton.setToggleState(task == Task::extrude, juce::dontSendNotification); cutButton.setToggleState(task == Task::cut, juce::dontSendNotification);
    revolveButton.setToggleState(task == Task::revolve, juce::dontSendNotification);
    filletButton.setToggleState(task == Task::fillet, juce::dontSendNotification);
    sketchButton.setToggleState(task == Task::createSketch, juce::dontSendNotification);
    boxButton.setToggleState(task == Task::newBox, juce::dontSendNotification); cylinderButton.setToggleState(task == Task::newCylinder, juce::dontSendNotification);
    if (const auto* sketch = service->document().findSketch(editingSketchId)) modeLabel.setText("Sketch  |  " + featureName(editingSketchId) + "  |  " + (sketch->onFace() ? "on " + supportText(*sketch) : sketch->plane + " plane"), juce::dontSendNotification);
    else modeLabel.setText("Model", juce::dontSendNotification);
    modeLabel.setColour(juce::Label::textColourId, sketching ? juce::Colour(0xff8fe3d6) : textColour);
    for (auto* b : { &sketchButton,&boxButton,&cylinderButton,&extrudeButton, &revolveButton,&latheButton,&cutButton,&filletButton,&subtractButton,&unionButton,&intersectButton,&undoButton,&redoButton,&fitButton,&resetViewButton,&viewsButton,&selectToolButton,&lineButton,&rectangleButton,&circleButton,&curveButton,&constrainButton,&fitSketchButton,&instanceButton,&jointButton,&couplingButton,&linkButton,&ikButton,&bodyModeButton,&faceModeButton,&edgeModeButton }) b->setVisible(false);
    resized(); repaint();
}

void CadWorkspacePanel::updateHighlights()
{
    std::vector<std::pair<juce::String, juce::Colour>> highlights;
    const bool command = isCommandTask();
    // While a face or edge is picked, the body is not highlighted, so the picked subshape stands out.
    const bool subshapePicked = selection != nullptr && selection->selected().valid() && (selection->selected().face >= 0 || selection->selected().edge >= 0);
    if (!command && !subshapePicked && selectedId.isNotEmpty()) highlights.push_back({ selectedId, selectedColour });
    if (!command && isJoint(selectedId)) if (const auto* joint = service->document().mechanicsData().findJoint(selectedId)) { if (joint->parentInstance.isNotEmpty()) highlights.push_back({ joint->parentInstance, targetColour }); highlights.push_back({ joint->childInstance, toolColour }); }
    if (command && targetId.isNotEmpty()) highlights.push_back({ targetId, targetColour });
    if (interferenceActive && !interferenceStale)
        for (const auto& pair : interferenceReport.interferences) { highlights.push_back({ pair.firstId, interferenceColour }); highlights.push_back({ pair.secondId, interferenceColour }); }
    if (command && toolId.isNotEmpty()) highlights.push_back({ toolId, toolColour });
    if (command && instanceBodyId.isNotEmpty()) highlights.push_back({ instanceBodyId, selectedColour });
    if (command && jointParentId.isNotEmpty()) highlights.push_back({ jointParentId, targetColour });
    if (command && jointChildId.isNotEmpty()) highlights.push_back({ jointChildId, toolColour });
    modelView.setHighlights(std::move(highlights));
    tree.repaint();
}

juce::Colour CadWorkspacePanel::colourFor(const juce::String& id) const
{
    if (isCommandTask()) { if (id == targetId || id == jointParentId) return targetColour; if (id == toolId || id == jointChildId) return toolColour; if (id == profileId || id == instanceBodyId) return selectedColour; return {}; }
    return id == selectedId ? selectedColour : juce::Colour();
}

void CadWorkspacePanel::showExportDialog()
{
    showCadExportDialog(service, isSolid(selectedId) || isInstance(selectedId) ? juce::StringArray { selectedId } : juce::StringArray());
}

void CadWorkspacePanel::showFeatureMenu(const juce::String& id)
{
    const auto* f = service->document().find(id);
    if (f == nullptr)
    {
        if (!isInstance(id)) return;
        juce::PopupMenu instanceMenu;
        instanceMenu.addItem(1, "Export...");
        instanceMenu.showMenuAsync(juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id](int result)
        {
            if (safe != nullptr && result == 1) showCadExportDialog(safe->service, { id });
        });
        return;
    }
    juce::PopupMenu menu;
    if (f->type == "sketch") { menu.addItem(1, "Edit Sketch"); menu.addItem(2, "Extrude..."); menu.addItem(12, "Revolve..."); menu.addItem(3, "Cut Sketch..."); }
    if (isSolid(id))
    {
        menu.addItem(4, "Subtract from this body...");
        menu.addItem(10, "Fillet...");
        if (task == Task::boolean) { menu.addItem(5, "Use as Target"); menu.addItem(6, "Use as Tool"); }
        if (task == Task::fillet) { menu.addItem(11, "Use as Fillet Target"); }
    }
    if (f->type != "sketch") menu.addItem(7, f->visible ? "Hide" : "Show");
    if (isSolid(id) && f->visible) menu.addItem(9, "Export...");
    menu.addSeparator(); menu.addItem(8, "Delete");
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id](int result)
    {
        if (safe == nullptr || result == 0) return;
        switch (result)
        {
            case 1: safe->selectFeature(id); safe->enterSketch(id); break;
            case 2: safe->selectFeature(id); safe->startTask(Task::extrude); break;
            case 3: safe->selectFeature(id); safe->startTask(Task::cut); break;
            case 4: safe->selectFeature(id); safe->startTask(Task::boolean, "subtract"); break;
            case 5: safe->activeRole = 0; safe->assignRole(id); break;
            case 6: safe->activeRole = 1; safe->assignRole(id); break;
            case 7: safe->toggleVisibility(id); break;
            case 8: safe->deleteFeature(id); break;
            case 9: showCadExportDialog(safe->service, { id }); break;
            case 12: safe->selectFeature(id); safe->startTask(Task::revolve); break;
            case 10: safe->selectFeature(id); safe->startTask(Task::fillet); break;
            case 11: safe->targetId = id; safe->filletEdges.clear(); safe->updateHighlights(); safe->rebuildTaskPanel(true); break;
            default: break;
        }
    });
}

//==============================================================================
// Motion workflow: assembly instances, joints, preview and the DEWB motion link.
bool CadWorkspacePanel::jointReferenceFrame(const juce::String& jointId, gp_Trsf& world) const
{
    const auto& model = service->document().mechanicsData();
    const auto* joint = model.findJoint(jointId);
    if (joint == nullptr) return false;
    const auto* parentInstance = model.findInstance(joint->parentInstance);
    const auto parent = parentInstance == nullptr ? gp_Trsf() : parentInstance->placement.toTrsf();
    world = parent.Multiplied(joint->parentToJoint.toTrsf());
    return true;
}

void CadWorkspacePanel::addMotionPanels(juce::String& title, juce::String& instruction, juce::String& accept, bool& showAccept, bool& showCancel)
{
    using namespace djehuti;
    const auto& document = service->document();
    const auto& model = document.mechanicsData();
    const auto async = [this](std::function<void(CadWorkspacePanel&)> fn) { return [this, fn] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), fn] { if (safe != nullptr) fn(*safe); }); }; };
    const bool simulationOwns = motionRuntime != nullptr && motionRuntime->driver() == cad::CadMotionRuntime::Driver::simulation;

    if (task == Task::instance)
    {
        const auto* instance = model.findInstance(selectedId);
        title = instance->name; instruction = "An assembly instance of " + featureName(instance->featureId) + ". It moves as one rigid body.";
        form->addHeader("Instance");
        form->addText("name", "Name", instance->name, "Display name; renaming keeps every connection.");
        form->addNote({}, "Body: " + featureName(instance->featureId) + "  |  id " + instance->id, 1);
        form->addToggle("grounded", "Grounded (fixed to the world)", instance->grounded, "A grounded instance stays at its placement; the mechanism is solved relative to it and the world.");
        form->addHeader("Reference placement (world)");
        if (!instance->grounded && model.instanceHasJoints(selectedId)) form->addNote({}, "Positioned by its joints. Ground it, or edit the joint frames, to move it.", 2);
        else
        {
            double rx = 0.0, ry = 0.0, rz = 0.0;
            cad::mech::eulerDegreesFromFrame(instance->placement, rx, ry, rz);
            form->addNumber("x", "Origin X", instance->placement.x, "mm", "Instance frame origin in world millimetres.");
            form->addNumber("y", "Origin Y", instance->placement.y, "mm", "Instance frame origin in world millimetres.");
            form->addNumber("z", "Origin Z", instance->placement.z, "mm", "Instance frame origin in world millimetres.");
            form->addNumber("rx", "Rotate X", rx, "deg", "Orientation: rotation about world X, applied first (extrinsic X, Y, Z).");
            form->addNumber("ry", "Rotate Y", ry, "deg", "Orientation: rotation about world Y, applied second.");
            form->addNumber("rz", "Rotate Z", rz, "deg", "Orientation: rotation about world Z, applied last.");
        }
        addMechanismStatus();
        form->addHeader("Actions");
        const auto id = selectedId;
        form->addButton("addJoint", "Add joint for this instance...", async([id](CadWorkspacePanel& p) { p.selectedId = id; p.startTask(Task::newJoint); }), "Create a joint with this instance as the child.");
        form->addButton("delete", "Delete instance", async([id](CadWorkspacePanel& p) { juce::String e; if (!p.service->removeInstance(id, e)) p.showError(e); else { p.refreshFromDocument(true); p.selectFeature({}); p.setMessage("Instance deleted. Undo restores it.", 0); } }), "Delete this instance (its body stays).");
        accept = "Apply"; showCancel = true;
        return;
    }

    if (task == Task::joint)
    {
        const auto* joint = model.findJoint(selectedId);
        title = joint->name; instruction = "Edit the relationship, preview its motion, or map a DEWB signal to it.";
        gp_Trsf frame; jointReferenceFrame(selectedId, frame);
        const auto origin = frame.TranslationPart(); const auto axis = gp_Dir(0, 0, 1).Transformed(frame);
        form->addHeader("Relationship");
        form->addText("name", "Name", joint->name, "Display name; renaming keeps identity and the binding.");
        form->addChoice("type", "Type", { "fixed", "revolute", "prismatic" }, joint->type == "fixed" ? 0 : joint->type == "revolute" ? 1 : 2, "Fixed, revolute (angle about +Z) or prismatic (travel along +Z).");
        form->addNote({}, "Parent: " + (joint->parentInstance.isEmpty() ? juce::String("World") : featureName(joint->parentInstance)) + "\nChild: " + featureName(joint->childInstance), 2);
        form->addHeader("Joint frame at reference (world)");
        form->addNumber("ox", "Origin X", origin.X(), "mm", "Joint origin in world millimetres at the reference pose.");
        form->addNumber("oy", "Origin Y", origin.Y(), "mm", "Joint origin in world millimetres at the reference pose.");
        form->addNumber("oz", "Origin Z", origin.Z(), "mm", "Joint origin in world millimetres at the reference pose.");
        form->addNumber("ax", "Axis X", axis.X(), "", "Direction of the joint +Z (motion) axis in world coordinates.");
        form->addNumber("ay", "Axis Y", axis.Y(), "", "Direction of the joint +Z (motion) axis in world coordinates.");
        form->addNumber("az", "Axis Z", axis.Z(), "", "Direction of the joint +Z (motion) axis in world coordinates.");
        if (joint->type != "fixed")
        {
            const auto unit = uiUnit(joint->type);
            form->addHeader("Coordinate (" + cad::mech::coordinateQuantity(joint->type) + ")");
            form->addNumber("reference", "Reference", toUi(joint->type, joint->reference), unit, "Coordinate at which the authored placement holds.");
            form->addToggle("limits", "Engineering limits", joint->hasLimits, "Report coordinates outside these limits (values are never clamped).");
            form->addNumber("lower", "Lower", toUi(joint->type, joint->lower), unit, "Lower engineering limit.");
            form->addNumber("upper", "Upper", toUi(joint->type, joint->upper), unit, "Upper engineering limit.");
            form->addToggle("hardBounds", "Enforce limits as hard bounds", joint->enforceLimits, "When on, the solver refuses poses outside the limits; when off, limits only produce warnings.");
            form->addHeader("Mechanism role");
            auto& roleBox = form->addChoice("role", "Role", { "driver", "dependent", "locked" }, joint->role == "driver" ? 0 : joint->role == "dependent" ? 1 : 2,
                                            "Driver: an input you or DEWB prescribe. Dependent: solved from the constraints. Locked: held at the lock value.");
            form->addNumber("lockValue", "Lock value", toUi(joint->type, joint->lockValue), unit, "Coordinate held while the role is locked.");
            roleBox.onChange = [this] { if (form == nullptr) return; form->setRowVisible("lockValue", form->choiceText("role") == "locked"); resized(); validateTask(); };
            form->setRowVisible("lockValue", joint->role == "locked");
            const auto current = motionRuntime != nullptr ? motionRuntime->coordinate(joint->id) : joint->reference;
            if (joint->role == "driver")
            {
                form->addHeader("Preview");
                form->addNumber("preview", "Value", toUi(joint->type, current), unit, "Temporary driver input; dependent joints follow. Never saved and never added to undo.");
                form->addButton("previewApply", simulationOwns ? "Preview (DEWB is driving)" : "Preview", async([](CadWorkspacePanel& p) { p.previewJoint(false); }), "Solve the mechanism with this driver value.");
                form->addButton("previewReset", "Reset all to reference", async([](CadWorkspacePanel& p) { p.previewJoint(true); }), "Return every driver to its reference coordinate.");
            }
            else form->addNote({}, (joint->role == "dependent" ? "Solved value: " : "Locked at: ") + juce::String(toUi(joint->type, current), 4) + " " + unit + ". Drive the mechanism through its driver joints.", 2);
            if (std::abs(current - joint->reference) > 1.0e-12)
                form->addNote({}, "Current coordinate: " + juce::String(toUi(joint->type, current), 3) + " " + unit + (joint->type == "revolute" ? " (" + juce::String(current / (2.0 * juce::MathConstants<double>::pi), 3) + " rev)" : juce::String()), 1);

            // Signal mapping: part of the authored connection profile; DEWB validates it during binding.
            const auto* mapping = model.mappingForJoint(joint->id);
            const auto status = motionLink != nullptr ? motionLink->snapshot() : djehuti::motion::LinkSnapshot {};
            form->addHeader("DEWB signal mapping");
            juce::StringArray quantities { joint->type == "revolute" ? "angle" : "position", joint->type == "revolute" ? "angular_velocity" : "linear_velocity" };
            const auto quantity = mapping != nullptr ? mapping->sourceQuantity : quantities[0];
            if (!status.signals.empty())
            {
                juce::StringArray ids; int selected = 0;
                for (const auto& s : status.signals) { if (mapping != nullptr && s.id == mapping->signalId) selected = ids.size(); ids.add(s.id); }
                if (mapping != nullptr && !ids.contains(mapping->signalId)) { selected = 0; ids.insert(0, mapping->signalId); }
                form->addChoice("signal", "Signal", ids, selected, "Signal advertised by DEWB.");
            }
            else form->addText("signal", "Signal id", mapping != nullptr ? mapping->signalId : juce::String(), "Stable DEWB signal id (connect the link to choose from the advertised list).");
            auto& quantityBox = form->addChoice("quantity", "Quantity", quantities, juce::jmax(0, quantities.indexOf(quantity)), "Physical quantity of the source signal.");
            const auto units = cad::mech::unitsFor(quantity);
            form->addChoice("unit", "Unit", units, juce::jmax(0, units.indexOf(mapping != nullptr ? mapping->sourceUnit : units[0])), "Unit the DEWB signal is expressed in.");
            form->addNumber("scale", "Scale", mapping != nullptr ? mapping->scale : 1.0, "", "Gear ratio: joint coordinate per source unit after unit conversion.");
            form->addNumber("offset", "Offset", toUi(joint->type, mapping != nullptr ? mapping->offset : 0.0), unit, "Added to state sources (angle/position).");
            form->addNumber("initial", "Initial", toUi(joint->type, mapping != nullptr ? mapping->initialCoordinate : joint->reference), unit, "Start coordinate DEWB integrates a velocity source from.");
            form->addButton("saveMapping", "Save mapping", async([](CadWorkspacePanel& p) { p.saveMapping(); }), "Store this mapping in the document's connection profile.");
            if (mapping != nullptr)
            {
                const auto jointId = joint->id;
                form->addButton("removeMapping", "Remove mapping", async([jointId](CadWorkspacePanel& p) { juce::String e; if (!p.service->removeJointMapping(jointId, e)) p.showError(e); else { p.refreshFromDocument(true); p.rebuildTaskPanel(false); p.setMessage("Mapping removed.", 0); } }), "Remove this joint from the connection profile.");
                juce::StringArray problems;
                if (motionLink != nullptr) for (const auto& problem : motionLink->validateProfileAgainstSignals()) if (problem.startsWith(joint->name + ":")) problems.add(problem);
                form->addNote({}, problems.isEmpty() ? (status.signals.empty() ? juce::String("Saved. Validated against DEWB when the link connects.") : juce::String("Matches the advertised DEWB signal.")) : problems.joinIntoString("\n"), problems.isEmpty() ? 1 : juce::jmin(4, problems.size() + 1));
            }
            const auto syncRows = [this]
            {
                if (form == nullptr) return;
                const auto q = form->choiceText("quantity");
                if (auto* unitBox = dynamic_cast<juce::ComboBox*>(form->control("unit"))) { const auto keep = unitBox->getText(); unitBox->clear(juce::dontSendNotification); const auto list = djehuti::cad::mech::unitsFor(q); unitBox->addItemList(list, 1); unitBox->setSelectedItemIndex(juce::jmax(0, list.indexOf(keep)), juce::dontSendNotification); }
                const bool rate = djehuti::cad::mech::isRateQuantity(q);
                form->setRowVisible("initial", rate); form->setRowVisible("offset", !rate);
                resized(); validateTask();
            };
            quantityBox.onChange = syncRows;
            form->setRowVisible("initial", cad::mech::isRateQuantity(quantity)); form->setRowVisible("offset", !cad::mech::isRateQuantity(quantity));
        }
        addMechanismStatus();
        form->addHeader("Actions");
        const auto id = selectedId;
        form->addButton("delete", "Delete joint", async([id](CadWorkspacePanel& p) { juce::String e; if (!p.service->removeJoint(id, e)) p.showError(e); else { p.refreshFromDocument(true); p.selectFeature({}); p.setMessage("Joint deleted. Undo restores it.", 0); } }), "Delete this joint and its mapping.");
        accept = "Apply"; showCancel = true;
        return;
    }

    if (task == Task::newInstance)
    {
        title = "New Instance"; instruction = "Pick the solid body to instance in the tree or the model view, then press Create Instance."; accept = "Create Instance";
        form->addHeader("Body");
        form->addSlot("body", "Body", instanceBodyId.isEmpty() ? juce::String("Select a solid body") : featureName(instanceBodyId), selectedColour, true, [] {}, "Solid body this instance shows.");
        form->addText("name", "Name", instanceBodyId.isEmpty() ? juce::String() : featureName(instanceBodyId) + " instance", "Display name of the new instance.");
        form->addNote({}, "The first instance appears exactly where the body is modelled. Set a different origin afterwards to place further instances of the same body.", 3);
        return;
    }

    if (task == Task::newJoint)
    {
        title = "New Joint"; instruction = "Choose the parent (or World) and the child instance, then the joint frame. The child keeps its current position at the reference coordinate."; accept = "Create Joint";
        gp_Trsf childWorld;
        if (const auto* child = model.findInstance(jointChildId)) childWorld = child->placement.toTrsf();
        const auto childOrigin = childWorld.TranslationPart();
        form->addHeader("Instances");
        form->addSlot("parent", "Parent", jointParentId.isEmpty() ? juce::String("World") : featureName(jointParentId), targetColour, activeRole == 0,
                      async([](CadWorkspacePanel& p) { p.activeRole = 0; p.rebuildTaskPanel(true); }), "Click, then pick the parent instance. Leave as World for a root joint.");
        form->addSlot("child", "Child", jointChildId.isEmpty() ? juce::String("Select the moving instance") : featureName(jointChildId), toolColour, activeRole == 1,
                      async([](CadWorkspacePanel& p) { p.activeRole = 1; p.rebuildTaskPanel(true); }), "Click, then pick the child instance that the joint moves.");
        form->addButton("world", "Use World as parent", async([](CadWorkspacePanel& p) { p.jointParentId.clear(); p.activeRole = 1; p.updateHighlights(); p.rebuildTaskPanel(true); }), "Attach the child to the assembly/world frame.");
        form->addHeader("Relationship");
        form->addText("name", "Name", {}, "Optional display name.");
        auto& typeBox = form->addChoice("type", "Type", { "revolute", "prismatic", "fixed" }, 0, "Revolute rotates about the joint +Z axis; prismatic travels along it; fixed welds the child to the parent.");
        form->addHeader("Joint frame at reference (world)");
        form->addNumber("ox", "Origin X", childOrigin.X(), "mm", "Joint origin in world millimetres.");
        form->addNumber("oy", "Origin Y", childOrigin.Y(), "mm", "Joint origin in world millimetres.");
        form->addNumber("oz", "Origin Z", childOrigin.Z(), "mm", "Joint origin in world millimetres.");
        form->addNumber("ax", "Axis X", 0.0, "", "Joint +Z (motion) axis direction in world coordinates.");
        form->addNumber("ay", "Axis Y", 0.0, "", "Joint +Z (motion) axis direction in world coordinates.");
        form->addNumber("az", "Axis Z", 1.0, "", "Joint +Z (motion) axis direction in world coordinates.");
        form->addHeader("Coordinate");
        form->addNumber("reference", "Reference", 0.0, "", "Coordinate at which the current placement holds.");
        form->addToggle("limits", "Engineering limits", false, "Report coordinates outside these limits.");
        form->addNumber("lower", "Lower", -180.0, "", "Lower engineering limit.");
        form->addNumber("upper", "Upper", 180.0, "", "Upper engineering limit.");
        const auto syncUnits = [this]
        {
            if (form == nullptr) return;
            const auto type = form->choiceText("type");
            const auto unit = type == "fixed" ? juce::String() : uiUnit(type);
            for (const auto* key : { "reference", "lower", "upper" }) { form->setRowVisible(key, type != "fixed"); form->setCaption(key, juce::String(key) == "reference" ? "Reference (" + unit + ")" : juce::String(key) == "lower" ? "Lower (" + unit + ")" : "Upper (" + unit + ")"); }
            form->setRowVisible("limits", type != "fixed");
            resized(); validateTask();
        };
        typeBox.onChange = syncUnits;
        syncUnits();
        return;
    }

    if (task == Task::newCoupling || task == Task::coupling)
    {
        const auto* existing = task == Task::coupling ? model.findCoupling(selectedId) : nullptr;
        title = existing != nullptr ? existing->name : juce::String("New Coupling");
        instruction = "Couple two joint coordinates: (qB - refB) = k (qA - refA) + offset. Pick joints in the Assembly group of the tree.";
        accept = existing != nullptr ? "Apply" : "Create Coupling";
        addCouplingFields(existing);
        addMechanismStatus();
        if (existing != nullptr)
        {
            const auto id = selectedId;
            form->addHeader("Actions");
            form->addButton("delete", "Delete coupling", async([id](CadWorkspacePanel& p) { juce::String e; if (!p.service->removeCoupling(id, e)) p.showError(e); else { p.refreshFromDocument(true); p.selectFeature({}); p.setMessage("Coupling deleted. Undo restores it.", 0); } }), "Delete this coupling.");
        }
        return;
    }

    // Task::link
    title = "Motion Link"; showAccept = false; showCancel = true;
    addMechanismStatus();
    const auto status = motionLink != nullptr ? motionLink->snapshot() : djehuti::motion::LinkSnapshot {};
    instruction = "DEWB connects to this named pipe, advertises its signals, accepts the connection profile, then streams motion.";
    form->addHeader("Status");
    form->addNote({}, motion::stateName(status.state) + (status.message.isNotEmpty() ? "\n" + status.message : juce::String()), 3);
    form->addNote({}, "Endpoint: " + (status.pipePath.isEmpty() ? juce::String("not listening") : status.pipePath), 2);
    if (status.peerName.isNotEmpty() || status.simulationId.isNotEmpty())
        form->addNote({}, "Peer: " + status.peerName + "\nSimulation: " + status.simulationId + "\nAccepted profile: " + (status.acceptedProfileRevision < 0 ? juce::String("none") : juce::String(status.acceptedProfileRevision))
                          + "  |  document profile " + juce::String(model.profileRevision), 3);
    if (status.lastSequence.isNotEmpty())
        form->addNote({}, "Timeline " + juce::String(status.timelineGeneration) + "  |  sequence " + status.lastSequence + "\nSimulation time " + juce::String(status.simTimePs.getLargeIntValue() / 1.0e12, 6) + " s  |  " + status.lifecycle, 2);
    if (!status.problems.isEmpty()) form->addNote({}, status.problems.joinIntoString("\n"), juce::jmin(8, status.problems.size() + 1));
    if (!status.limitWarnings.isEmpty()) form->addNote({}, status.limitWarnings.joinIntoString("\n"), juce::jmin(4, status.limitWarnings.size() + 1));
    // Simulation lifecycle requests: DEWB decides; the panel shows its confirmed state and the request outcome.
    form->addHeader("Simulation");
    const auto confirmed = status.lifecycle.isEmpty() ? juce::String("unknown") : status.lifecycle;
    form->addNote({}, "Confirmed by DEWB: " + confirmed + (status.simTimePs.isNotEmpty() ? "  |  t = " + juce::String(status.simTimePs.getLargeIntValue() / 1.0e12, 6) + " s  |  timeline " + juce::String(status.timelineGeneration) : juce::String()), 2);
    if (!status.lifecycleAvailable) form->addNote({}, status.lifecycleUnavailableReason, 3);
    const bool ready = status.lifecycleAvailable && !status.commandPending;
    const bool running = status.lifecycle == "running", pausedNow = status.lifecycle == "paused", stoppedNow = status.lifecycle == "stopped";
    auto command = [&](const char* key, const juce::String& text, const juce::String& name, const juce::String& tip, bool enabled)
    {
        auto& button = form->addButton(key, text, async([name](CadWorkspacePanel& p) { p.sendLifecycle(name); }), tip);
        button.setEnabled(enabled);
    };
    command("play", pausedNow ? "Resume" : "Start", "play", "Start from a stopped or initial state, or resume a paused simulation.", ready && (pausedNow || stoppedNow));
    command("pause", "Pause", "pause", "Suspend the simulation, keeping its state and time.", ready && running);
    command("stopSim", "Stop Simulation", "stop", "End the simulation run and keep its final state (the link stays connected).", ready && (running || pausedNow));
    command("reset", "Reset", "reset", "Restore the initial simulation state on a new timeline; the pose changes when DEWB synchronizes it.", ready);
    form->addNumber("stepMs", "Step interval", 10.0, "ms", "Simulation time advanced by Single Step (sent as integer picoseconds).");
    command("step", "Single Step", "step", "Advance the simulation by the step interval and remain paused.", ready && (pausedNow || stoppedNow));
    if (status.commandText.isNotEmpty()) form->addNote({}, (status.commandPending ? "Pending: " : juce::String()) + status.commandText, 3);
    if (status.canRetry) form->addButton("retry", "Retry same request", async([](CadWorkspacePanel& p) { juce::String e; if (p.motionLink != nullptr && !p.motionLink->retryLifecycle(e)) p.showError(e); p.rebuildTaskPanel(true); }), "Resend the unconfirmed request with the same request id so DEWB can deduplicate it.");
    form->addHeader("Endpoint");
    const bool listening = motionLink != nullptr && motionLink->isListening();
    form->addButton("listen", listening ? "Stop Listening" : "Start Listening", async([listening](CadWorkspacePanel& p) { if (p.motionLink == nullptr) return; juce::String e; if (listening) p.motionLink->stopListening(); else if (!p.motionLink->startListening(e)) p.showError(e); p.rebuildTaskPanel(false); }), "Open or close the DCAD motion endpoint.");
    if (listening)
    {
        form->addButton("disconnect", "Disconnect DEWB", async([](CadWorkspacePanel& p) { if (p.motionLink != nullptr) p.motionLink->disconnectPeer(); p.rebuildTaskPanel(false); }), "Drop the current DEWB connection; the endpoint keeps listening.");
        const auto path = status.pipePath;
        form->addButton("copy", "Copy pipe path", [path] { juce::SystemClipboard::copyTextToClipboard(path); }, "Copy the pipe path for DEWB.");
    }
    form->addHeader("Connection profile (" + juce::String((int) model.mappings.size()) + " mapped joints)");
    juce::StringArray lines;
    for (const auto& m : model.mappings) lines.add(featureName(m.jointId) + " <- " + m.signalId + " (" + m.sourceQuantity + ", " + m.sourceUnit + ")");
    form->addNote({}, lines.isEmpty() ? juce::String("No joints are mapped yet. Select a joint and set its DEWB signal.") : lines.joinIntoString("\n"), juce::jmin(8, lines.size() + 1));
    if (!status.signals.empty())
    {
        juce::StringArray signalLines;
        for (const auto& s : status.signals) signalLines.add(s.id + "  (" + s.quantity + ", " + s.unit + ", " + s.kind + ")");
        form->addHeader("DEWB signals");
        form->addNote({}, signalLines.joinIntoString("\n"), juce::jmin(10, signalLines.size() + 1));
    }
}

void CadWorkspacePanel::validateMotionTask(juce::String& problem, int& severity)
{
    if (task == Task::ik) { validateIkPanel(problem, severity); return; }
    auto requireNumber = [&](const juce::String& key, const juce::String& label) { if (problem.isEmpty() && form->rowVisible(key) && !form->numberValid(key)) { problem = label + " must be a number."; severity = 2; } };
    if (task == Task::instance)
    {
        if (form->text("name").isEmpty()) { problem = "Name cannot be empty."; severity = 2; }
        for (const auto* key : { "x", "y", "z", "rx", "ry", "rz" }) requireNumber(key, "Placement");
    }
    else if (task == Task::newInstance) { if (instanceBodyId.isEmpty()) problem = "Select the solid body to instance."; }
    else if (task == Task::newCoupling || task == Task::coupling)
    {
        djehuti::cad::CadCoupling c; juce::String error;
        if (task == Task::newCoupling && (couplingJointA.isEmpty() || couplingJointB.isEmpty())) problem = couplingJointA.isEmpty() ? "Select joint A (the input side) in the tree." : "Select joint B (the coupled side) in the tree.";
        else if (!readCoupling(c, error)) { problem = error; severity = 2; }
        else if (!djehuti::cad::mech::validateCoupling(service->document().mechanicsData(), c, error)) { problem = error; severity = 2; }
    }
    else if (task == Task::joint || task == Task::newJoint)
    {
        if (task == Task::joint && form->text("name").isEmpty()) { problem = "Name cannot be empty."; severity = 2; }
        if (task == Task::newJoint && jointChildId.isEmpty()) problem = "Select the child instance that the joint moves.";
        for (const auto* key : { "ox", "oy", "oz", "ax", "ay", "az", "reference", "lower", "upper", "lockValue" }) requireNumber(key, "Joint values");
        if (problem.isEmpty() && form->toggle("hardBounds") && !form->toggle("limits")) { problem = "Hard bounds need engineering limits; switch limits on or bounds off."; severity = 2; }
        if (problem.isEmpty() && std::abs(form->number("ax")) + std::abs(form->number("ay")) + std::abs(form->number("az")) < 1.0e-9) { problem = "The joint axis must be a non-zero direction."; severity = 2; }
        if (problem.isEmpty() && form->toggle("limits") && form->rowVisible("lower") && form->number("lower") > form->number("upper")) { problem = "The lower limit must not exceed the upper limit."; severity = 2; }
    }
}

bool CadWorkspacePanel::acceptMotionTask()
{
    using namespace djehuti::cad;
    if (task == Task::ik) return saveIkDraft();
    juce::String e, id;
    const auto& model = service->document().mechanicsData();
    if (task == Task::newInstance)
    {
        if (!service->createInstance(instanceBodyId, form->text("name"), id, e)) { showError(e); return false; }
        refreshFromDocument(true); selectFeature(id);
        setMessage(featureName(id) + " created. Add a joint to make it move.", 0);
        return true;
    }
    if (task == Task::instance)
    {
        const auto* instance = model.findInstance(selectedId);
        if (instance == nullptr) return false;
        bool ok = true;
        if (form->text("name") != instance->name) ok = service->renameMechanical(selectedId, form->text("name"), e);
        if (ok && form->toggle("grounded") != instance->grounded) ok = service->setInstanceGrounded(selectedId, form->toggle("grounded"), e);
        if (ok && form->control("x") != nullptr && (instance = service->document().mechanicsData().findInstance(selectedId)) != nullptr)
        {
            const auto placement = mech::frameFromEulerDegrees(form->number("x"), form->number("y"), form->number("z"), form->number("rx"), form->number("ry"), form->number("rz"));
            const auto& old = instance->placement;
            const bool changed = std::abs(placement.x - old.x) > 1.0e-9 || std::abs(placement.y - old.y) > 1.0e-9 || std::abs(placement.z - old.z) > 1.0e-9
                              || std::abs(std::abs(placement.qw * old.qw + placement.qx * old.qx + placement.qy * old.qy + placement.qz * old.qz) - 1.0) > 1.0e-12;
            if (changed) ok = service->setInstancePlacement(selectedId, placement, e);
        }
        refreshFromDocument(true); rebuildTaskPanel(false);
        if (!ok) showError(e); else setMessage("Instance updated.", 0);
        return ok;
    }
    if (task == Task::newJoint || task == Task::joint)
    {
        CadJointRequest request;
        request.name = form->text("name"); request.type = form->choiceText("type");
        const auto* existing = task == Task::joint ? model.findJoint(selectedId) : nullptr;
        request.parentInstance = existing != nullptr ? existing->parentInstance : jointParentId;
        request.childInstance = existing != nullptr ? existing->childInstance : jointChildId;
        request.originX = form->number("ox"); request.originY = form->number("oy"); request.originZ = form->number("oz");
        request.axisX = form->number("ax"); request.axisY = form->number("ay"); request.axisZ = form->number("az");
        if (request.type != "fixed")
        {
            request.reference = fromUi(request.type, form->number("reference"));
            request.hasLimits = form->toggle("limits");
            request.lower = fromUi(request.type, form->number("lower")); request.upper = fromUi(request.type, form->number("upper"));
        }
        bool frameChanged = existing == nullptr;
        if (existing != nullptr)
        {
            gp_Trsf frame; jointReferenceFrame(selectedId, frame);
            const auto o = frame.TranslationPart(); const gp_Dir a = gp_Dir(0, 0, 1).Transformed(frame);
            const gp_Vec requested(request.axisX, request.axisY, request.axisZ);
            frameChanged = request.type != existing->type || std::abs(request.originX - o.X()) > 1.0e-9 || std::abs(request.originY - o.Y()) > 1.0e-9 || std::abs(request.originZ - o.Z()) > 1.0e-9
                        || (requested.Magnitude() > 0.0 && gp_Vec(a).Angle(requested) > 1.0e-9) || std::abs(request.reference - existing->reference) > 1.0e-12
                        || request.hasLimits != existing->hasLimits || std::abs(request.lower - existing->lower) > 1.0e-12 || std::abs(request.upper - existing->upper) > 1.0e-12;
        }
        bool ok = true;
        if (existing != nullptr && request.name != existing->name && !frameChanged) ok = service->renameMechanical(selectedId, request.name, e);
        if (ok && frameChanged) ok = existing != nullptr ? service->updateJoint(selectedId, request, e) : service->createJoint(request, id, e);
        if (ok && existing != nullptr && request.type != "fixed" && form->control("role") != nullptr)
        {
            const auto* now = service->document().mechanicsData().findJoint(selectedId);
            const auto role = form->choiceText("role");
            const auto lockValue = form->rowVisible("lockValue") ? fromUi(request.type, form->number("lockValue")) : now->lockValue;
            const auto hard = form->toggle("hardBounds") && request.hasLimits;
            if (role != now->role || std::abs(lockValue - now->lockValue) > 1.0e-12 || hard != now->enforceLimits) ok = service->setJointRole(selectedId, role, lockValue, hard, e);
        }
        if (!ok) { refreshFromDocument(true); rebuildTaskPanel(false); showError(e); return false; }
        refreshFromDocument(true);
        selectFeature(existing != nullptr ? selectedId : id);
        setMessage(existing != nullptr ? "Joint updated." : featureName(id) + " created. Set its mechanism role, preview it, or map a DEWB signal.", 0);
        return true;
    }
    if (task == Task::newCoupling || task == Task::coupling)
    {
        CadCoupling coupling;
        if (!readCoupling(coupling, e)) { showError(e); return false; }
        const bool ok = task == Task::coupling ? service->updateCoupling(selectedId, coupling, e) : service->createCoupling(coupling, id, e);
        if (!ok) { showError(e); return false; }
        refreshFromDocument(true);
        selectFeature(task == Task::coupling ? selectedId : id);
        setMessage("Coupling saved. Make the coupled joint dependent so the solver computes it from the input joint.", 0);
        return true;
    }
    return false;
}

// Coupling fields: the units follow the type (gear phase in degrees; linear, rack and screw offsets in mm).
void CadWorkspacePanel::addCouplingFields(const djehuti::cad::CadCoupling* existing)
{
    using namespace djehuti;
    const auto pi = juce::MathConstants<double>::pi;
    const juce::StringArray types { "gear", "linear", "rackPinion", "screw" };
    const auto type = existing != nullptr ? existing->type : juce::String("gear");
    const auto a = existing != nullptr ? existing->jointA : couplingJointA, b = existing != nullptr ? existing->jointB : couplingJointB;
    form->addHeader("Joints");
    if (existing != nullptr) form->addNote({}, "A (input): " + featureName(a) + "\nB (coupled): " + featureName(b), 2);
    else
    {
        const auto async = [this](std::function<void(CadWorkspacePanel&)> fn) { return [this, fn] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), fn] { if (safe != nullptr) fn(*safe); }); }; };
        form->addSlot("jointA", "Joint A", a.isEmpty() ? juce::String("Select the input joint") : featureName(a), targetColour, activeRole == 0, async([](CadWorkspacePanel& p) { p.activeRole = 0; p.rebuildTaskPanel(true); }), "Input side of the coupling.");
        form->addSlot("jointB", "Joint B", b.isEmpty() ? juce::String("Select the coupled joint") : featureName(b), toolColour, activeRole == 1, async([](CadWorkspacePanel& p) { p.activeRole = 1; p.rebuildTaskPanel(true); }), "Coupled side of the coupling.");
    }
    form->addHeader("Relationship");
    form->addText("name", "Name", existing != nullptr ? existing->name : juce::String(), "Optional display name.");
    auto& typeBox = form->addChoice("couplingType", "Type", { "Gear (rotation : rotation)", "Linear (travel : travel)", "Rack and pinion", "Screw" }, juce::jmax(0, types.indexOf(type)),
                                    "Gear: both revolute. Linear: both prismatic. Rack and pinion and screw: A revolute, B prismatic.");
    form->addNumber("ratio", "Ratio", existing != nullptr ? existing->ratio : -1.0, "", "Signed ratio dB/dA. External gears turn in opposite directions (negative).");
    form->addNumber("pitchRadius", "Pitch radius", (existing != nullptr ? existing->pitchRadius : 0.01) * 1000.0, "mm", "Rack travel per radian of pinion rotation (signed).");
    form->addNumber("lead", "Lead", (existing != nullptr ? existing->lead : 0.001) * 1000.0, "mm/rev", "Screw travel per revolution (signed; negative for left-hand threads).");
    const auto offsetUi = existing == nullptr ? 0.0 : (existing->type == "gear" ? existing->offset * 180.0 / pi : existing->offset * 1000.0);
    form->addNumber("offset", "Offset", offsetUi, "", "Phase (gear, degrees) or offset (linear, rack, screw, millimetres) of B at the reference of A.");
    const auto sync = [this]
    {
        if (form == nullptr) return;
        const auto index = dynamic_cast<juce::ComboBox*>(form->control("couplingType"))->getSelectedItemIndex();
        form->setRowVisible("ratio", index <= 1); form->setRowVisible("pitchRadius", index == 2); form->setRowVisible("lead", index == 3);
        form->setCaption("offset", index == 0 ? "Phase (deg)" : "Offset (mm)");
        resized(); validateTask();
    };
    typeBox.onChange = sync;
    sync();
}

bool CadWorkspacePanel::readCoupling(djehuti::cad::CadCoupling& c, juce::String& error) const
{
    if (form == nullptr) return false;
    const juce::StringArray types { "gear", "linear", "rackPinion", "screw" };
    const auto* box = dynamic_cast<juce::ComboBox*>(form->control("couplingType"));
    if (box == nullptr) return false;
    const auto* existing = task == Task::coupling ? service->document().mechanicsData().findCoupling(selectedId) : nullptr;
    c.type = types[juce::jmax(0, box->getSelectedItemIndex())];
    c.name = form->text("name");
    c.jointA = existing != nullptr ? existing->jointA : couplingJointA;
    c.jointB = existing != nullptr ? existing->jointB : couplingJointB;
    for (const auto* key : { "ratio", "pitchRadius", "lead", "offset" })
        if (form->rowVisible(key) && !form->numberValid(key)) { error = "Coupling values must be numbers."; return false; }
    c.ratio = form->number("ratio"); c.pitchRadius = form->number("pitchRadius") / 1000.0; c.lead = form->number("lead") / 1000.0;
    c.offset = c.type == "gear" ? form->number("offset") * juce::MathConstants<double>::pi / 180.0 : form->number("offset") / 1000.0;
    return true;
}

// Solve status of the whole mechanism, shown wherever mechanism data is edited.
void CadWorkspacePanel::addMechanismStatus()
{
    if (motionRuntime == nullptr || service->document().mechanicsData().joints.empty()) return;
    const auto& r = motionRuntime->lastAttempt();
    form->addHeader("Mechanism");
    juce::String text = (r.valid ? juce::String("Solved: ") : juce::String("Not solved: ")) + djehuti::cad::kin::statusName(r.status) + ". " + r.message;
    if (motionRuntime->showingLastValidPose()) text << " The view shows the last valid pose.";
    text << "\nUnknowns " << r.unknowns << ", equations " << r.equations << ", rank " << r.rank << ", closure " << juce::String(r.positionResidualMm, 8) << " mm / " << juce::String(r.angleResidualRad, 10) << " rad";
    if (r.freeCoordinates > 0) text << "\nFree: " << r.freeJoints.joinIntoString(", ");
    if (r.rigidBodyFreedom > 0) text << "\nUngrounded: " << r.floatingInstances.joinIntoString(", ");
    for (const auto& p : r.problems) text << "\n" << p;
    for (const auto& w : r.warnings) text << "\n" << w;
    form->addNote({}, text, juce::jmin(10, 3 + r.problems.size() + r.warnings.size() + (r.freeCoordinates > 0 ? 1 : 0) + (r.rigidBodyFreedom > 0 ? 1 : 0)));
}

void CadWorkspacePanel::mechanismChanged()
{
    if (motionRuntime == nullptr) return;
    if (interferenceActive && !interferenceStale) if (const auto pose = motionRuntime->pose(); pose != nullptr && pose->serial != interferenceReport.poseSerial) markInterferenceStale();
    wakeFrames();   // a new pose was published (preview, IK, DEWB): draw it without waiting for a slow tick
    linkChanged();
    // Rebuild the panel only when the solve outcome changes, not for every streamed pose.
    const auto summary = motionRuntime->lastAttempt().summary() + (motionRuntime->showingLastValidPose() ? "|stale" : "");
    if (summary != shownMechanismSummary)
    {
        shownMechanismSummary = summary;
        if (task == Task::joint || task == Task::coupling || task == Task::instance || task == Task::newCoupling) requestRebuild(true);
    }
}

void CadWorkspacePanel::saveMapping()
{
    if (form == nullptr || !isJoint(selectedId)) return;
    const auto* joint = service->document().mechanicsData().findJoint(selectedId);
    djehuti::cad::CadSignalMapping mapping;
    mapping.jointId = selectedId;
    mapping.signalId = dynamic_cast<juce::ComboBox*>(form->control("signal")) != nullptr ? form->choiceText("signal") : form->text("signal");
    mapping.sourceQuantity = form->choiceText("quantity"); mapping.sourceUnit = form->choiceText("unit");
    if (!form->numberValid("scale") || !form->numberValid("offset") || !form->numberValid("initial")) { showError("Scale, offset and initial coordinate must be numbers."); return; }
    mapping.scale = form->number("scale");
    mapping.offset = fromUi(joint->type, form->number("offset"));
    mapping.initialCoordinate = fromUi(joint->type, form->number("initial"));
    juce::String e;
    if (!service->setJointMapping(mapping, e)) { showError(e); return; }
    refreshFromDocument(true); rebuildTaskPanel(false);
    setMessage("Mapping saved (profile revision " + juce::String(service->document().mechanicsData().profileRevision) + "). A connected DEWB revalidates it.", 0);
}

void CadWorkspacePanel::sendLifecycle(const juce::String& command)
{
    if (motionLink == nullptr || form == nullptr) return;
    if (command == "step" && (!form->numberValid("stepMs") || form->number("stepMs") <= 0.0)) { showError("The step interval must be a number of milliseconds greater than zero."); return; }
    const auto stepPs = command == "step" ? (std::int64_t) std::llround(form->number("stepMs") * 1.0e9) : (std::int64_t) 0;
    juce::String e;
    if (!motionLink->requestLifecycle(command, stepPs, e)) showError(e);
    rebuildTaskPanel(true);
}

void CadWorkspacePanel::previewJoint(bool reset)
{
    if (motionRuntime == nullptr) return;
    if (reset)
    {
        if (motionRuntime->driver() == djehuti::cad::CadMotionRuntime::Driver::simulation) { showError("DEWB drives the joints while the motion link is bound; disconnect to preview."); return; }
        motionRuntime->resetToReference(); rebuildTaskPanel(false); setMessage("All joints returned to their reference coordinates.", 0); return;
    }
    const auto* joint = service->document().mechanicsData().findJoint(selectedId);
    if (joint == nullptr || form == nullptr || !form->numberValid("preview")) { showError("Enter a preview value."); return; }
    juce::String e;
    if (!motionRuntime->setPreviewCoordinate(selectedId, fromUi(joint->type, form->number("preview")), e)) { showError(e); return; }
    rebuildTaskPanel(true);
    setMessage("Previewing " + joint->name + " at " + juce::String(form->number("preview"), 3) + " " + uiUnit(joint->type) + ". The document is unchanged.", 0);
}

void CadWorkspacePanel::toggleVisibility(const juce::String& id)
{
    const auto* f = service->document().find(id);
    if (f == nullptr) return;
    juce::String e; const auto name = f->name; const bool nowVisible = !f->visible;
    if (!service->setVisible(id, nowVisible, e)) { showError(e); return; }
    refreshFromDocument(true);
    if (task == Task::body && selectedId == id) rebuildTaskPanel(false);
    setMessage(name + (nowVisible ? " is shown." : " is hidden."), 0);
}

void CadWorkspacePanel::deleteFeature(const juce::String& id)
{
    juce::String e; const auto name = featureName(id);
    if (!service->remove(id, e)) { showError(e); return; }
    if (selectedId == id) selectedId.clear();
    refreshFromDocument(true);
    selectFeature(selectedId);
    setMessage(name + " deleted. Undo restores it.", 0);
}

bool CadWorkspacePanel::isSolid(const juce::String& id) const { const auto* f = service->document().find(id); return f != nullptr && isSolidType(f->type) && !f->shape.IsNull(); }
juce::String CadWorkspacePanel::featureName(const juce::String& id) const { if (const auto* f = service->document().find(id)) return f->name; const auto& model = service->document().mechanicsData(); if (const auto* i = model.findInstance(id)) return i->name; if (const auto* j = model.findJoint(id)) return j->name; if (const auto* c = model.findCoupling(id)) return c->name; return id; }

void CadWorkspacePanel::newDocument()
{
    service->newDocument();
    if (editingSketchId.isNotEmpty()) { editingSketchId.clear(); sketchCanvas->setVisible(false); modelView.setVisible(true); }
    selectedId.clear(); targetId.clear(); toolId.clear(); profileId.clear(); sketchEntityId.clear(); sketchSecondEntityId.clear();
    task = Task::none;
    refreshFromDocument(true); selectFeature({});
    setMessage("New CAD document.", 0);
}
void CadWorkspacePanel::openDocument(){fileChooser=std::make_unique<juce::FileChooser>("Open CAD document",juce::File(),"*.djo;*.djc");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& chooser){if(auto file=chooser.getResult();file.existsAsFile()){juce::String e;if(!service->load(file,e))showError(e);else{if(editingSketchId.isNotEmpty()){editingSketchId.clear();sketchCanvas->setVisible(false);modelView.setVisible(true);}refreshFromDocument(true);selectFeature({});modelView.fitAll();}}fileChooser.reset();});}
void CadWorkspacePanel::saveDocument(){auto file=service->document().file();if(file==juce::File())saveDocumentAs();else{juce::String e;if(!service->save(file,e))showError(e);else service->document().markSaved();}}
void CadWorkspacePanel::saveDocumentAs(){fileChooser=std::make_unique<juce::FileChooser>("Save native CAD document",juce::File(),"*.djo");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& chooser){auto file=chooser.getResult().withFileExtension("djo");if(file!=juce::File()){juce::String e;if(!service->save(file,e))showError(e);else{service->document().setFile(file);service->document().markSaved();}}fileChooser.reset();});}


//==============================================================================
// Inverse kinematics task panel. The draft is the edited task; the form is rebuilt from it, and every edit is
// read back into it. Solving and dragging only change the runtime pose; Save stores the definition (undoable).
namespace
{
const double kRadToDegUi = 180.0 / juce::MathConstants<double>::pi;
juce::String formatError(const djehuti::cad::kin::TargetReport& r)
{
    juce::StringArray parts;
    if (r.mode != "orientation") parts.add(juce::String(r.positionErrorMm, 4) + " mm");
    if (r.mode != "position") parts.add(juce::String(r.angleErrorRad * kRadToDegUi, 4) + " deg");
    return parts.joinIntoString(", ");
}
}

void CadWorkspacePanel::loadIkDraft(const juce::String& taskId)
{
    using namespace djehuti::cad;
    const auto& model = service->document().mechanicsData();
    const auto* stored = model.findIkTask(taskId);
    ikDraft = std::make_unique<CadIkTask>(stored != nullptr ? *stored : CadIkTask {});
    ikDocument = service->document().documentInstance();
    ikTargetIndex = 0; ikFormStale = true; ikDirty = stored == nullptr;
    if (stored != nullptr) return;
    // A new task starts with one pose target on the last instance at its current pose, so nothing moves until
    // the desired pose is changed.
    ikDraft->name = "IK task";
    if (!model.instances.empty())
    {
        CadIkTarget target;
        target.id = "draft-" + juce::Uuid().toDashedString().substring(0, 8);
        target.name = "Target 1";
        target.instanceId = model.instances.back().id;
        juce::String ignored;
        if (motionRuntime != nullptr) motionRuntime->captureTarget(target, ignored);
        ikDraft->targets.push_back(target);
    }
}

void CadWorkspacePanel::readIkForm()
{
    using namespace djehuti::cad;
    if (form == nullptr || task != Task::ik || ikDraft == nullptr || ikFormStale || form->control("ikName") == nullptr) return;
    const auto before = juce::JSON::toString(mech::ikTaskToJson(*ikDraft));
    auto& draft = *ikDraft;
    const auto& model = service->document().mechanicsData();
    draft.name = form->text("ikName");
    if (form->numberValid("ikPosTol")) draft.positionToleranceMm = form->number("ikPosTol");
    if (form->numberValid("ikAngTol")) draft.angleToleranceRad = form->number("ikAngTol") / kRadToDegUi;
    for (const auto& joint : model.joints)
    {
        if (form->control("adj:" + joint.id) == nullptr) continue;
        const bool on = form->toggle("adj:" + joint.id);
        if (on && !draft.adjustableJoints.contains(joint.id)) draft.adjustableJoints.add(joint.id);
        if (!on) draft.adjustableJoints.removeString(joint.id);
    }
    if (juce::isPositiveAndBelow(ikTargetIndex, (int) draft.targets.size()) && form->control("tName") != nullptr)
    {
        auto& t = draft.targets[(size_t) ikTargetIndex];
        t.name = form->text("tName");
        t.enabled = form->toggle("tEnabled");
        if (const auto i = form->choice("tInstance"); juce::isPositiveAndBelow(i, (int) model.instances.size())) t.instanceId = model.instances[(size_t) i].id;
        if (const auto i = form->choice("tReference"); i == 0) t.referenceInstance.clear(); else if (juce::isPositiveAndBelow(i - 1, (int) model.instances.size())) t.referenceInstance = model.instances[(size_t) i - 1].id;
        const juce::StringArray modes { "pose", "position", "orientation" };
        if (const auto i = form->choice("tMode"); juce::isPositiveAndBelow(i, 3)) t.mode = modes[i];
        auto readFrame = [&](const char* x, const char* y, const char* z, const char* rx, const char* ry, const char* rz, CadFrame& frame)
        {
            double ox = frame.x, oy = frame.y, oz = frame.z, ax = 0, ay = 0, az = 0;
            mech::eulerDegreesFromFrame(frame, ax, ay, az);
            const bool positionShown = form->rowVisible(x), rotationShown = form->rowVisible(rx);
            for (const auto* key : { x, y, z }) if (positionShown && !form->numberValid(key)) return;
            for (const auto* key : { rx, ry, rz }) if (rotationShown && !form->numberValid(key)) return;
            if (positionShown) { ox = form->number(x); oy = form->number(y); oz = form->number(z); }
            if (rotationShown) { ax = form->number(rx); ay = form->number(ry); az = form->number(rz); }
            if (!positionShown && !rotationShown) return;
            frame = mech::frameFromEulerDegrees(ox, oy, oz, ax, ay, az);
        };
        readFrame("tox", "toy", "toz", "trx", "try", "trz", t.toolFrame);
        // Only rewrite the desired frame when its shown numbers changed, so hidden components keep full precision.
        CadFrame edited = t.desired;
        readFrame("dx", "dy", "dz", "drx", "dry", "drz", edited);
        double ex = 0, ey = 0, ez = 0, cx = 0, cy = 0, cz = 0;
        mech::eulerDegreesFromFrame(edited, ex, ey, ez); mech::eulerDegreesFromFrame(t.desired, cx, cy, cz);
        auto shown = [](double v) { return juce::String(v, 4).trimCharactersAtEnd("0").trimCharactersAtEnd(".").getDoubleValue(); };
        const bool changed = shown(edited.x) != shown(t.desired.x) || shown(edited.y) != shown(t.desired.y) || shown(edited.z) != shown(t.desired.z)
                          || shown(ex) != shown(cx) || shown(ey) != shown(cy) || shown(ez) != shown(cz);
        if (changed) t.desired = edited;
    }
    ikDragEnabled = form->toggle("ikDrag");
    const juce::StringArray planes { "XY", "XZ", "YZ" };
    if (const auto i = form->choice("ikPlane"); juce::isPositiveAndBelow(i, 3)) ikDragPlane = planes[i];
    if (juce::JSON::toString(mech::ikTaskToJson(draft)) != before) ikDirty = true;
    showIkMarkers();
}

// Markers follow the draft; the drag handle is the edited target's desired point.
void CadWorkspacePanel::showIkMarkers()
{
    const bool simulationOwns = motionRuntime != nullptr && motionRuntime->driver() == djehuti::cad::CadMotionRuntime::Driver::simulation;
    modelView.setTargetDragEnabled(task == Task::ik && ikDragEnabled && !simulationOwns);
    if (motionRuntime == nullptr || task != Task::ik || ikDraft == nullptr) return;
    const auto dragTarget = ikDragEnabled && juce::isPositiveAndBelow(ikTargetIndex, (int) ikDraft->targets.size()) ? ikDraft->targets[(size_t) ikTargetIndex].id : juce::String();
    motionRuntime->setIkDisplay(ikDraft.get(), dragTarget, ikDragPlane);
    ikMarkersShown = true;
}

void CadWorkspacePanel::addIkPanel(juce::String& title, juce::String& instruction, juce::String& accept, bool& showAccept, bool& showCancel)
{
    using namespace djehuti;
    if (ikDraft == nullptr) loadIkDraft({});
    auto& draft = *ikDraft;
    const auto& model = service->document().mechanicsData();
    const auto async = [this](std::function<void(CadWorkspacePanel&)> fn) { return [this, fn] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), fn] { if (safe != nullptr) fn(*safe); }); }; };
    const bool simulationOwns = motionRuntime != nullptr && motionRuntime->driver() == cad::CadMotionRuntime::Driver::simulation;
    title = "Inverse Kinematics";
    instruction = "Choose the driver joints the solve may move and the endpoint targets, then Solve. A pose is shown only if the mechanism and every target hold.";
    accept = draft.id.isEmpty() ? "Save Task" : "Save Changes"; showAccept = true; showCancel = true;

    form->addHeader("Task");
    juce::StringArray taskNames { "New task" };
    int selectedTask = 0;
    for (size_t i = 0; i < model.ikTasks.size(); ++i) { taskNames.add(model.ikTasks[i].name); if (model.ikTasks[i].id == draft.id) selectedTask = (int) i + 1; }
    auto& taskBox = form->addChoice("ikTask", "Task", taskNames, selectedTask, "Saved IK tasks of this document, or a new task.");
    // The handlers below rebuild the form, which deletes the drop-down that is calling them, so the rebuild is
    // deferred until the drop-down's callback has returned.
    taskBox.onChange = async([](CadWorkspacePanel& p)
    {
        if (p.form == nullptr || p.task != Task::ik) return;
        const auto index = p.form->choice("ikTask");
        const auto& tasks = p.service->document().mechanicsData().ikTasks;
        p.loadIkDraft(index > 0 && index <= (int) tasks.size() ? tasks[(size_t) index - 1].id : juce::String());
        p.rebuildTaskPanel(false); p.showIkMarkers();
    });
    form->addText("ikName", "Name", draft.name, "Task name shown in the Assembly tree.");
    form->addNote({}, draft.id.isEmpty() ? juce::String("Not saved yet.") : "id " + draft.id + (ikDirty ? "  |  unsaved changes" : "  |  saved"), 1);
    if (simulationOwns)
        form->addNote({}, "DEWB drives the mechanism through the motion link. Its driver coordinates stay prescribed, so local solving and dragging are off; the task can still be edited and saved.", 3);

    form->addHeader("Adjustable joints");
    juce::StringArray others;
    for (const auto& joint : model.joints)
    {
        if (joint.type == "fixed") continue;
        const bool listed = draft.adjustableJoints.contains(joint.id);
        if (joint.role == "driver" || listed)
            form->addToggle("adj:" + joint.id, joint.name + "  (" + joint.type + (joint.role != "driver" ? ", " + joint.role + " - not adjustable" : juce::String()) + (joint.enforceLimits ? ", hard bounds" : juce::String()) + ")", listed,
                            "Let this solve move this driver. Unselected drivers keep their current value; saved roles never change.");
        else others.add(joint.name + " (" + joint.role + ")");
    }
    if (!others.isEmpty()) form->addNote({}, "Follow the mechanism or stay locked: " + others.joinIntoString(", "), 2);

    form->addHeader("Tolerances");
    form->addNumber("ikPosTol", "Position", draft.positionToleranceMm, "mm", "Largest accepted distance between the endpoint and its desired position.");
    form->addNumber("ikAngTol", "Angle", draft.angleToleranceRad * kRadToDegUi, "deg", "Largest accepted rotation between the endpoint frame and its desired orientation.");

    form->addHeader("Targets");
    if (!draft.targets.empty())
    {
        juce::StringArray names;
        for (const auto& t : draft.targets) names.add(t.name + (t.enabled ? juce::String() : juce::String(" (off)")));
        ikTargetIndex = juce::jlimit(0, (int) draft.targets.size() - 1, ikTargetIndex);
        auto& targetBox = form->addChoice("ikTarget", "Edit target", names, ikTargetIndex, "Target whose fields are shown below.");
        targetBox.onChange = async([](CadWorkspacePanel& p)
        {
            if (p.form == nullptr || p.task != Task::ik) return;
            p.readIkForm(); p.ikTargetIndex = p.form->choice("ikTarget"); p.ikFormStale = true; p.rebuildTaskPanel(false); p.showIkMarkers();
        });
    }
    form->addButton("ikAddTarget", "Add target", async([](CadWorkspacePanel& p)
    {
        p.readIkForm();
        cad::CadIkTarget t;
        t.id = "draft-" + juce::Uuid().toDashedString().substring(0, 8);
        t.name = "Target " + juce::String((int) p.ikDraft->targets.size() + 1);
        const auto& instances = p.service->document().mechanicsData().instances;
        if (!instances.empty()) t.instanceId = instances.back().id;
        juce::String ignored;
        if (p.motionRuntime != nullptr) p.motionRuntime->captureTarget(t, ignored);
        p.ikDraft->targets.push_back(t);
        p.ikTargetIndex = (int) p.ikDraft->targets.size() - 1; p.ikDirty = true; p.ikFormStale = true;
        p.rebuildTaskPanel(false); p.showIkMarkers();
    }), "Add an endpoint target at the endpoint's current pose.");
    if (juce::isPositiveAndBelow(ikTargetIndex, (int) draft.targets.size()))
    {
        const auto& t = draft.targets[(size_t) ikTargetIndex];
        juce::StringArray instanceNames, referenceNames { "World" };
        int instanceIndex = -1, referenceIndex = 0;
        for (size_t i = 0; i < model.instances.size(); ++i)
        {
            instanceNames.add(model.instances[i].name); referenceNames.add(model.instances[i].name);
            if (model.instances[i].id == t.instanceId) instanceIndex = (int) i;
            if (model.instances[i].id == t.referenceInstance) referenceIndex = (int) i + 1;
        }
        form->addText("tName", "Name", t.name, "Target name.");
        form->addToggle("tEnabled", "Enabled", t.enabled, "Disabled targets are kept but not solved.");
        auto& instanceBox = form->addChoice("tInstance", "Endpoint on", instanceNames, instanceIndex, "Instance that carries the endpoint (you can also click it in the model view).");
        auto& referenceBox = form->addChoice("tReference", "Reference", referenceNames, referenceIndex, "Frame the desired pose is expressed in: the world or another instance (evaluated in the same configuration).");
        auto& modeBox = form->addChoice("tMode", "Mode", { "Full pose", "Position only", "Orientation only" }, t.mode == "position" ? 1 : t.mode == "orientation" ? 2 : 0, "Which part of the endpoint pose must match.");
        for (auto* box : { &instanceBox, &referenceBox, &modeBox })
            box->onChange = async([](CadWorkspacePanel& p) { if (p.form == nullptr || p.task != Task::ik) return; p.readIkForm(); p.rebuildTaskPanel(false); p.showIkMarkers(); });
        double rx = 0, ry = 0, rz = 0;
        cad::mech::eulerDegreesFromFrame(t.toolFrame, rx, ry, rz);
        form->addHeader("Tool frame on the endpoint instance");
        form->addNumber("tox", "Origin X", t.toolFrame.x, "mm", "Endpoint origin in the instance frame (an engineering frame, not the body centre).");
        form->addNumber("toy", "Origin Y", t.toolFrame.y, "mm", "Endpoint origin in the instance frame.");
        form->addNumber("toz", "Origin Z", t.toolFrame.z, "mm", "Endpoint origin in the instance frame.");
        form->addNumber("trx", "Rotate X", rx, "deg", "Endpoint orientation in the instance frame (extrinsic X, Y, Z).");
        form->addNumber("try", "Rotate Y", ry, "deg", "Endpoint orientation in the instance frame.");
        form->addNumber("trz", "Rotate Z", rz, "deg", "Endpoint orientation in the instance frame.");
        cad::mech::eulerDegreesFromFrame(t.desired, rx, ry, rz);
        form->addHeader("Desired pose in " + referenceNames[referenceIndex]);
        form->addNumber("dx", "X", t.desired.x, "mm", "Desired endpoint position in the reference frame.");
        form->addNumber("dy", "Y", t.desired.y, "mm", "Desired endpoint position in the reference frame.");
        form->addNumber("dz", "Z", t.desired.z, "mm", "Desired endpoint position in the reference frame.");
        form->addNumber("drx", "Rotate X", rx, "deg", "Desired endpoint orientation in the reference frame (extrinsic X, Y, Z).");
        form->addNumber("dry", "Rotate Y", ry, "deg", "Desired endpoint orientation in the reference frame.");
        form->addNumber("drz", "Rotate Z", rz, "deg", "Desired endpoint orientation in the reference frame.");
        for (const auto* key : { "dx", "dy", "dz" }) form->setRowVisible(key, t.mode != "orientation");
        for (const auto* key : { "drx", "dry", "drz" }) form->setRowVisible(key, t.mode != "position");
        form->addButton("ikCapture", "Capture Current Pose", async([](CadWorkspacePanel& p) { p.captureIkTarget(); }), "Set the desired pose to where the endpoint is now. Nothing moves.");
        form->addButton("ikRemoveTarget", "Remove this target", async([](CadWorkspacePanel& p)
        {
            p.readIkForm();
            p.ikDraft->targets.erase(p.ikDraft->targets.begin() + p.ikTargetIndex);
            p.ikTargetIndex = juce::jmax(0, p.ikTargetIndex - 1); p.ikDirty = true; p.ikFormStale = true;
            p.rebuildTaskPanel(false); p.showIkMarkers();
        }), "Remove this target from the task.");
    }

    form->addHeader("Drag target");
    form->addToggle("ikDrag", "Drag the target handle in the model view", ikDragEnabled, "When on, pressing the yellow diamond drags the desired position on the blue plane; elsewhere the view orbits as usual.");
    form->addChoice("ikPlane", "Drag plane", { "XY", "XZ", "YZ" }, ikDragPlane == "XZ" ? 1 : ikDragPlane == "YZ" ? 2 : 0, "Plane of the target's reference frame, through the desired point, that the handle moves in.");

    form->addHeader("Solve");
    form->addButton("ikSolve", simulationOwns ? "Solve (DEWB is driving)" : "Solve", async([](CadWorkspacePanel& p) { p.solveIkDraft(); }), "Solve the selected drivers from the current pose. The document is not changed.");
    form->addButton("ikReset", "Reset Preview", async([](CadWorkspacePanel& p) { p.previewJoint(true); }), "Return every driver to its reference coordinate (runtime only).");
    if (draft.id.isNotEmpty())
    {
        const auto id = draft.id;
        form->addButton("ikDelete", "Delete task", async([id](CadWorkspacePanel& p)
        {
            juce::String e;
            if (!p.service->removeIkTask(id, e)) { p.showError(e); return; }
            p.ikDraft.reset(); p.ikDirty = false;
            p.refreshFromDocument(true); p.selectFeature({});
            p.setMessage("IK task deleted. Undo restores it.", 0);
        }), "Delete this saved task (undoable).");
    }

    // Result: the last solve (accepted, or a rejected candidate) and the errors of the pose actually shown.
    form->addHeader("Result");
    juce::String text;
    int lines = 1;
    if (motionRuntime != nullptr && motionRuntime->hasIkResult() && motionRuntime->lastIkTaskId() == draft.id)
    {
        const auto& r = motionRuntime->lastIkResult();
        text << (r.valid ? "Solved and shown: " : "Not applied: ") << cad::kin::statusName(r.status) << ". " << r.message;
        lines += 3;
        if (!r.valid && !r.targets.empty())
        {
            text << "\n" << (r.candidateConsistent ? "Rejected candidate (closest consistent configuration found):" : "Rejected candidate:");
            for (const auto& t : r.targets) { text << "\n  " << t.name << ": " << formatError(t) << (t.reached ? "  (within tolerance)" : "  (outside tolerance)"); ++lines; }
            ++lines;
        }
        if (!r.limitedJoints.isEmpty()) { text << "\nAt hard bounds: " << r.limitedJoints.joinIntoString(", "); ++lines; }
        if (r.valid && r.freeCoordinates > 0) { text << "\nRemaining freedom: " << r.freeJoints.joinIntoString(", "); ++lines; }
    }
    else text << "Not solved in this session yet.";
    // Evaluate this panel's task, not whichever task the runtime displayed before this rebuild.
    const auto shown = motionRuntime != nullptr ? motionRuntime->shownTargetReports(draft) : std::vector<cad::kin::TargetReport> {};
    if (!shown.empty())
    {
        text << "\nShown pose:";
        for (const auto& t : shown) { text << "\n  " << t.name << ": " << formatError(t) << (t.reached ? "  reached" : "  not reached"); ++lines; }
        ++lines;
    }
    form->addNote("ikResult", text, juce::jlimit(2, 14, lines + 1));
    ikFormStale = false;
    showIkMarkers();
}

void CadWorkspacePanel::validateIkPanel(juce::String& problem, int& severity)
{
    readIkForm();
    if (ikDraft == nullptr) return;
    for (const auto* key : { "ikPosTol", "ikAngTol", "tox", "toy", "toz", "trx", "try", "trz", "dx", "dy", "dz", "drx", "dry", "drz" })
        if (problem.isEmpty() && form->control(key) != nullptr && form->rowVisible(key) && !form->numberValid(key)) { problem = "IK values must be numbers."; severity = 2; }
    if (problem.isEmpty() && ikDraft->name.trim().isEmpty()) { problem = "Name the task."; severity = 2; }
    juce::String error;
    if (problem.isEmpty() && !djehuti::cad::mech::validateIkTask(service->document().mechanicsData(), *ikDraft, error, false)) { problem = error; severity = 2; }
    if (problem.isEmpty() && ikDraft->adjustableJoints.isEmpty()) problem = "Select the driver joints the solve may adjust.";
    if (problem.isEmpty() && ikDraft->targets.empty()) problem = "Add a target.";
}

void CadWorkspacePanel::captureIkTarget()
{
    readIkForm();
    if (ikDraft == nullptr || motionRuntime == nullptr || !juce::isPositiveAndBelow(ikTargetIndex, (int) ikDraft->targets.size())) return;
    juce::String e;
    if (!motionRuntime->captureTarget(ikDraft->targets[(size_t) ikTargetIndex], e)) { showError(e); return; }
    ikDirty = true; ikFormStale = true;
    rebuildTaskPanel(false);
    setMessage("Desired pose set to the current endpoint pose. Nothing moved.", 0);
}

void CadWorkspacePanel::solveIkDraft()
{
    readIkForm();
    if (ikDraft == nullptr || motionRuntime == nullptr) return;
    juce::String error;
    if (!motionRuntime->requestIkAsync(*ikDraft, error, 2000.0)) { showError(error); return; }
    ikSolvePending = true;
    setMessage("Solving the IK task. The document is unchanged; the Result section will show the outcome.", 0);
}

bool CadWorkspacePanel::saveIkDraft()
{
    readIkForm();
    if (ikDraft == nullptr) return false;
    auto toSave = *ikDraft;
    for (auto& t : toSave.targets) if (t.id.startsWith("draft-")) t.id.clear();   // the document assigns stable ids
    juce::String e, id;
    const bool creating = toSave.id.isEmpty();
    if (!(creating ? service->createIkTask(toSave, id, e) : service->updateIkTask(toSave.id, toSave, e))) { showError(e); return false; }
    const auto savedId = creating ? id : toSave.id;
    ikDirty = false;
    refreshFromDocument(true);
    selectedId = savedId;
    loadIkDraft(savedId);
    syncTreeSelection();
    rebuildTaskPanel(false);
    setMessage(creating ? "IK task saved. Undo removes it." : "IK task updated. Undo restores the previous definition.", 0);
    return true;
}

// Dragging the handle moves the edited target's desired position within the drag plane and queues a solve;
// the worker keeps at most one active and one pending request.
void CadWorkspacePanel::ikTargetDragged(const juce::String& targetId, const std::array<float, 3>& point, bool finished)
{
    if (task != Task::ik || ikDraft == nullptr || motionRuntime == nullptr) return;
    for (auto& t : ikDraft->targets)
    {
        if (t.id != targetId) continue;
        gp_Trsf reference;
        if (t.referenceInstance.isNotEmpty())
        {
            // The reference frame of the shown pose: the same transforms the markers were drawn with.
            const auto reports = motionRuntime->shownTargetReports();
            for (const auto& r : reports) if (r.id == t.id) reference = r.desiredWorld.Multiplied(t.desired.toTrsf().Inverted());
        }
        const auto local = gp_Pnt(point[0], point[1], point[2]).Transformed(reference.Inverted());
        t.desired.x = local.X(); t.desired.y = local.Y(); t.desired.z = local.Z();
        ikDirty = true;
        if (form != nullptr)
            for (const auto& [key, value] : { std::pair<const char*, double> { "dx", local.X() }, { "dy", local.Y() }, { "dz", local.Z() } })
                if (auto* editor = dynamic_cast<juce::TextEditor*>(form->control(key))) editor->setText(juce::String(value, 4).trimCharactersAtEnd("0").trimCharactersAtEnd("."), false);
        showIkMarkers();
        juce::String e;
        if (!motionRuntime->requestIkAsync(*ikDraft, e)) showError(e);
        else { ikSolvePending = true; if (finished) setMessage("Target moved to " + juce::String(local.X(), 2) + ", " + juce::String(local.Y(), 2) + ", " + juce::String(local.Z(), 2) + " mm. Solving; the document is unchanged.", 0); }
        return;
    }
}

// Menus, Toolbox and view context actions use the same modeling handlers as before.
bool CadWorkspacePanel::executeUiCommand(const juce::String& command)
{
    if (!uiCommandEnabled(command)) return false;
    if (command == "pointer" || command == "boxSelect" || command == "circleSelect")
    {
        if (selection != nullptr) selection->setTool(command == "pointer" ? djehuti::cad::CadSelection::Tool::pointer : command == "boxSelect" ? djehuti::cad::CadSelection::Tool::box : djehuti::cad::CadSelection::Tool::circle);
        return true;
    }
    if (command == "materials") { showMaterialsManager(); return true; }
    if (command == "export") { showExportDialog(); return true; }
    if (command == "interference") { checkInterference(false); return true; }
    if (command == "clearInterference") { clearInterference(); return true; }
    const std::pair<const char*,juce::TextButton*> commands[] = {
        {"undo",&undoButton},{"redo",&redoButton},{"sketch",&sketchButton},{"box",&boxButton},{"cylinder",&cylinderButton},
        {"lathe",&latheButton},{"revolve",&revolveButton},{"extrude",&extrudeButton},{"cut",&cutButton},{"fillet",&filletButton},{"subtract",&subtractButton},{"union",&unionButton},{"intersect",&intersectButton},
        {"sketchSelect",&selectToolButton},{"line",&lineButton},{"rectangle",&rectangleButton},{"circle",&circleButton},{"curve",&curveButton},{"constrain",&constrainButton},
        {"fitSketch",&fitSketchButton},{"finishSketch",&finishSketchButton},{"instance",&instanceButton},{"joint",&jointButton},{"coupling",&couplingButton},
        {"link",&linkButton},{"ik",&ikButton},{"fit",&fitButton},{"reset",&resetViewButton},{"views",&viewsButton},
        {"bodyMode",&bodyModeButton},{"faceMode",&faceModeButton},{"edgeMode",&edgeModeButton}
    };
    for (const auto& entry : commands) if (command == entry.first) { if (entry.second->onClick) entry.second->onClick(); return true; }
    return false;
}
bool CadWorkspacePanel::uiCommandEnabled(const juce::String& command) const
{
    const bool sketch = editingSketchId.isNotEmpty();
    if (juce::StringArray{"sketchSelect","line","rectangle","circle","curve","constrain","fitSketch","finishSketch"}.contains(command)) return sketch;
    if (juce::StringArray{"bodyMode","faceMode","edgeMode","pointer","boxSelect","circleSelect","interference","materials"}.contains(command)) return !sketch;
    if (command == "clearInterference") return interferenceActive;
    return true;
}
bool CadWorkspacePanel::uiToolActive(const juce::String& command) const
{
    if (editingSketchId.isNotEmpty()) return command == (sketchCanvas->tool() == "select" ? juce::String("sketchSelect") : sketchCanvas->tool());
    if (selection == nullptr) return false;
    using Sel = djehuti::cad::CadSelection;
    if (command == "pointer") return selection->tool() == Sel::Tool::pointer;
    if (command == "boxSelect") return selection->tool() == Sel::Tool::box;
    if (command == "circleSelect") return selection->tool() == Sel::Tool::circle;
    if (command == "bodyMode") return selection->mode() == Sel::Mode::body;
    if (command == "faceMode") return selection->mode() == Sel::Mode::face;
    if (command == "edgeMode") return selection->mode() == Sel::Mode::edge;
    return false;
}

void CadWorkspacePanel::viewContextCommand(const juce::String& command,const djehuti::cad::CadPick& pick)
{
    if (command == "inspectRegion" && !pick.valid()) { selectFeature({}); return; }
    if (pick.valid())
    {
        if (selection != nullptr && command != "inspectRegion") selection->setSelected(pick);
        if (pick.face < 0 && pick.edge < 0) selectFeature(pick.drawId);
    }
    if (command == "inspect" || command == "inspectRegion") return;
    if (command == "hide" && pick.valid()) toggleVisibility(pick.drawId);
    else if (command == "delete" && pick.valid()) deleteFeature(pick.drawId);
    else executeUiCommand(command);
}


juce::var CadWorkspacePanel::automationState()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    refreshFromDocument(); handleUpdateNowIfNeeded();
    const auto& document = service->document();
    auto* state = new juce::DynamicObject();
    state->setProperty("documentRevision", (juce::int64)document.revision());
    state->setProperty("documentInstance", (juce::int64)document.documentInstance());
    state->setProperty("modified", document.modified()); state->setProperty("file", document.file().getFullPathName());
    state->setProperty("showing", isShowing()); state->setProperty("mode", editingSketchId.isEmpty() ? "model" : "sketch");
    state->setProperty("task", taskTitle.getText()); state->setProperty("instruction", taskInstruction.getText());
    state->setProperty("message", messageLabel.getText()); state->setProperty("selected", selectedId);
    state->setProperty("editingSketch", editingSketchId); state->setProperty("selectedEntity", sketchEntityId);
    state->setProperty("profile", profileId); state->setProperty("target", targetId); state->setProperty("tool", toolId);
    state->setProperty("fields", form ? form->describe() : juce::var());
    state->setProperty("applyLabel", acceptButton.getButtonText()); state->setProperty("canApply", acceptButton.isVisible() && acceptButton.isEnabled());
    state->setProperty("canCancel", cancelButton.isVisible() && cancelButton.isEnabled());
    state->setProperty("camera", modelView.cameraState()); state->setProperty("guides", modelView.viewState());
    state->setProperty("renderStatus", modelView.diagnostics()); state->setProperty("gesture", sketchCanvas->gestureState());
    {   // which component has keyboard focus (diagnostics for keyboard routing)
        const auto* focused = juce::Component::getCurrentlyFocusedComponent();
        state->setProperty("keyboardFocus", focused == nullptr ? juce::String("none") : focused == sketchCanvas.get() ? juce::String("sketchCanvas") : focused == this ? juce::String("cadWorkspace")
                                            : isParentOf(focused) ? juce::String("inside cadWorkspace: ") + typeid(*focused).name() : juce::String("outside cadWorkspace: ") + typeid(*focused).name());
    }
    juce::Array<juce::var> commands;
    for (const auto& name : juce::StringArray{"undo","redo","sketch","box","cylinder","extrude","revolve","lathe","cut","subtract","union","intersect","sketchSelect","line","rectangle","circle","curve","constrain","fitSketch","finishSketch","instance","joint","coupling","link","ik","fit","reset","views","pointer","boxSelect","circleSelect","bodyMode","faceMode","edgeMode","interference","clearInterference","materials"})
    { auto* c = new juce::DynamicObject(); c->setProperty("name", name); c->setProperty("enabled", uiCommandEnabled(name)); c->setProperty("active", uiToolActive(name)); commands.add(c); }
    state->setProperty("commands", commands);
    juce::Array<juce::var> features;
    for (const auto& f : document.features())
    {
        auto* o = new juce::DynamicObject(); o->setProperty("id", f.id); o->setProperty("name", f.name); o->setProperty("type", f.type); o->setProperty("visible", f.visible);
        o->setProperty("width", f.width); o->setProperty("depth", f.depth); o->setProperty("height", f.height); o->setProperty("radius", f.radius);
        o->setProperty("x", f.x); o->setProperty("y", f.y); o->setProperty("z", f.z);
        if (f.type == "revolve")
        { auto p = djehuti::cad::revolveParametersToJson(f.revolve); p.getDynamicObject()->setProperty("sketch", f.sketchId); o->setProperty("parameters", p); if(f.lathe)o->setProperty("workflow","lathe"); }
        if (const auto* sketch = document.findSketch(f.id))
        {
            o->setProperty("plane", sketch->plane); o->setProperty("offset", sketch->offset);
            juce::Array<juce::var> entities;
            for (const auto& e : sketch->entities)
            { auto* item = new juce::DynamicObject(); item->setProperty("id", e.id); item->setProperty("type", e.type); item->setProperty("x1",e.x1); item->setProperty("y1",e.y1); item->setProperty("x2",e.x2); item->setProperty("y2",e.y2); item->setProperty("radius",e.radius);
              if (e.type == "bezier") { item->setProperty("cx1",e.cx1); item->setProperty("cy1",e.cy1); item->setProperty("cx2",e.cx2); item->setProperty("cy2",e.cy2); }
              entities.add(item); }
            o->setProperty("entities", entities);
        }
        features.add(o);
    }
    state->setProperty("features", features);
    return state;
}

juce::var CadWorkspacePanel::automation(const juce::var& args)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const juce::ScopedValueSetter<bool> executing(automationInProgress, true);
    refreshFromDocument(); handleUpdateNowIfNeeded();
    juce::String error;
    const auto action = args.getProperty("action", "state").toString();
    if (!args.isObject()) error = "Arguments must be an object.";
    else if (action == "state") {}
    else if (action == "command")
    {
        commandMessage.clear(); commandSeverity = 0;
        if (!executeUiCommand(args.getProperty("command", "").toString())) error = "Unknown or unavailable command.";
    }
    else if (action == "set")
    {
        if (!form) error = "No task form is open.";
        else form->assign(args.getProperty("field", "").toString(), args.getProperty("value", {}), error);
    }
    else if (action == "click")
    {
        const auto key = args.getProperty("button", "").toString();
        juce::Button* button = key == "apply" ? &acceptButton : key == "cancel" ? &cancelButton : form ? dynamic_cast<juce::Button*>(form->control(key)) : nullptr;
        if (!button || !button->isEnabled() || !button->isVisible()) error = "Button is absent, hidden or disabled: " + key;
        else { auto click = button->onClick; commandMessage.clear(); commandSeverity = 0; if (click) click(); else error = "Button has no action."; }
    }
    else if (action == "select")
    {
        commandMessage.clear(); commandSeverity = 0;
        const auto id = args.getProperty("id", "").toString(), entity = args.getProperty("entity", "").toString();
        if (entity.isNotEmpty())
        {
            const auto* sketch = service->document().findSketch(editingSketchId); bool found = false;
            if (sketch) for (const auto& e : sketch->entities) if (e.id == entity) found = true;
            if (!found) error = "Entity is not in the active sketch.";
            else selectSketchEntity(entity, (bool)args.getProperty("second", false));
        }
        else if (id.isNotEmpty() && !service->document().find(id) && !isInstance(id) && !isJoint(id) && !isCoupling(id) && !isIkTask(id)) error = "Unknown model tree id.";
        else featureClicked(id);
    }
    else if (action == "sketch")
    {
        if (editingSketchId.isEmpty()) error = "Open a sketch before drawing.";
        else { commandMessage.clear(); commandSeverity = 0; sketchCanvas->automateGesture(args, error); }
    }
    else if (action == "camera") { commandMessage.clear(); commandSeverity = 0; modelView.controlCamera(args, error); }
    else if (action == "document")
    {
        commandMessage.clear(); commandSeverity = 0;
        const auto op = args.getProperty("operation", "").toString();
        const auto path = args.getProperty("path", "").toString();
        if ((op == "new" || op == "open") && service->document().modified() && !(bool)args.getProperty("discard", false)) error = "Save the modified document first, or explicitly set discard=true.";
        else if (op == "new") newDocument();
        else if (op == "open" || op == "save")
        {
            if (!juce::File::isAbsolutePath(path)) error = "An absolute document path is required.";
            else
            {
                const juce::File file(path);
                if (op == "save")
                {
                    if (!file.hasFileExtension("djo")) error = "Save native documents with a .djo extension.";
                    else if (file.existsAsFile() && !(bool)args.getProperty("overwrite", false)) error = "File exists; explicitly set overwrite=true.";
                    else if (service->save(file,error)) { service->document().setFile(file); service->document().markSaved(); }
                }
                else if (service->load(file,error))
                { editingSketchId.clear(); sketchCanvas->setVisible(false); modelView.setVisible(true); refreshFromDocument(true); selectFeature({}); modelView.fitAll(); }
            }
        }
        else error = "Document operation must be new, open or save.";
    }
    else error = "Unknown UI action.";
    handleUpdateNowIfNeeded();
    if (error.isEmpty() && commandSeverity == 2 && action != "state" && action != "set") error = commandMessage;
    if (error.isNotEmpty()) showError(error);
    auto* reply = new juce::DynamicObject(); reply->setProperty("ok", error.isEmpty()); if (error.isNotEmpty()) reply->setProperty("error", error);
    reply->setProperty("state", automationState()); wakeFrames(); repaint();
    return reply;
}

//==============================================================================
// Interference inspection: one shared operation for the Assembly menu, the toolbar command and automation.
juce::var CadWorkspacePanel::checkInterference(bool referencePose)
{
    if (motionRuntime != nullptr) motionRuntime->synchronize();   // the pose must belong to the current document state
    const auto pose = motionRuntime != nullptr ? motionRuntime->pose() : nullptr;
    const bool usePose = !referencePose && motionRuntime != nullptr;
    auto report = djehuti::cad::interference::check(service->document(), usePose ? &motionRuntime->shownInstanceWorld() : nullptr, pose != nullptr ? pose->serial : 0);
    if (usePose && pose != nullptr && (pose->documentRevision != service->document().revision() || pose->documentInstance != service->document().documentInstance()))
    { report = {}; report.pose = "shown"; report.error = "The mechanism pose does not match the current model yet (it may not be solved). Resolve the mechanism, then check again."; }
    if (!referencePose)
    {
        interferenceReport = report;
        interferenceActive = report.error.isEmpty();
        interferenceStale = false;
        std::vector<djehuti::cad::CadOverlayLine> lines;
        for (const auto& pair : report.interferences)
            for (const auto& s : pair.regionEdges) lines.push_back({ { s[0], s[1], s[2] }, { s[3], s[4], s[5] }, interferenceRegionArgb });
        modelView.setInspectionLines(std::move(lines));
        if (report.error.isNotEmpty()) showError("Interference check: " + report.error);
        else setMessage(!report.completed ? "Interference check incomplete: " + juce::String((int) report.failures.size()) + " pair(s) could not be evaluated."
                        : report.interferences.empty() ? "No interference: " + juce::String(report.components) + " components, no overlapping solid volume."
                        : juce::String((int) report.interferences.size()) + " interfering pair(s) found.", report.error.isNotEmpty() || !report.completed ? 2 : report.interferences.empty() ? 0 : 1);
        if (interferenceActive) { if (editingSketchId.isEmpty()) task = Task::interference; }
        rebuildTaskPanel(false);
        updateHighlights();
        wakeFrames();
    }
    return report.toVar();
}

void CadWorkspacePanel::clearInterference()
{
    interferenceActive = false; interferenceStale = false; interferenceReport = {};
    modelView.setInspectionLines({});
    if (task == Task::interference) task = Task::none;
    rebuildTaskPanel(false);
    updateHighlights();
    wakeFrames();
}

void CadWorkspacePanel::markInterferenceStale()
{
    interferenceStale = true;
    modelView.setInspectionLines({});
    updateHighlights();
    if (task == Task::interference) rebuildTaskPanel(true);
}

void CadWorkspacePanel::addInterferencePanel(juce::String& title, juce::String& instruction)
{
    const auto& r = interferenceReport;
    title = "Interference Check";
    instruction = "Solid overlap between assembly parts in the pose shown. Shared faces (contact) are not interference.";
    if (interferenceStale) form->addNote({}, "Out of date: the pose or the model changed after this check. Press Check again.", 2);
    form->addHeader("Result");
    juce::String summary = juce::String(r.components) + " components, " + juce::String(r.pairs) + " pairs (" + juce::String(r.candidatePairs) + " with overlapping bounds) at pose " + juce::String((juce::int64) r.poseSerial) + ".\n";
    summary += !r.completed ? "Incomplete: some pairs could not be evaluated."
             : r.interferences.empty() ? "No interference found." : juce::String((int) r.interferences.size()) + " interfering pair(s).";
    form->addNote({}, summary, 2);
    int index = 0;
    for (const auto& pair : r.interferences)
    {
        form->addHeader(pair.firstName + "  /  " + pair.secondName);
        form->addNote({}, "Common volume " + juce::String(pair.volumeMm3, 2) + " mm3" + (pair.detail.isNotEmpty() ? "\n" + pair.detail : juce::String()), pair.detail.isNotEmpty() ? 2 : 1);
        for (int side = 0; side < 2; ++side)
        {
            const auto id = side == 0 ? pair.firstId : pair.secondId;
            form->addButton("pair" + juce::String(index) + (side == 0 ? "a" : "b"), "Select " + (side == 0 ? pair.firstName : pair.secondName),
                            [this, id] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id] { if (safe != nullptr) { safe->selectedId = id; safe->updateHighlights(); } }); },
                            "Highlight this part (the check stays open).");
        }
        ++index;
    }
    for (const auto& pair : r.failures) form->addNote({}, pair.firstName + " / " + pair.secondName + ": " + pair.detail, 2);
    for (const auto& s : r.skipped) form->addNote({}, "Not inspected: " + s, 1);
    form->addHeader("Actions");
    form->addButton("recheck", "Check again", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->checkInterference(false); }); }, "Inspect the pose shown now.");
    form->addButton("clearInterference", "Clear", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->clearInterference(); }); }, "Remove the highlights and close the check.");
}

void CadWorkspacePanel::showMaterialsManager()
{
    if (materialsManager != nullptr) { materialsManager->toFront(true); return; }
    const auto* instance = service->document().mechanicsData().findInstance(selectedId);
    materialsManager = showCadMaterialsManager(service, instance ? instance->featureId : selectedId);
}
