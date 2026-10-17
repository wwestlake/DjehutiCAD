#include "CadWorkspacePanel.h"

#include <deque>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

using djehuti::cad::CadFeature;

namespace
{
const juce::Colour backgroundColour(0xff11161b), panelColour(0xff141a1f), toolbarColour(0xff161d23), borderColour(0xff26323a);
const juce::Colour textColour(0xffdce9ee), dimTextColour(0xff8fa4ad), accentColour(0xff2a8c99);
const juce::Colour selectedColour(0xffffb86b), targetColour(0xff63d48a), toolColour(0xffff7b72);

bool isSolidType(const juce::String& type) { return type == "box" || type == "cylinder" || type == "extrude" || type == "cut" || type == "union" || type == "subtract" || type == "intersect"; }
bool isPrimitiveType(const juce::String& type) { return type == "box" || type == "cylinder"; }
juce::String friendlyEntity(const juce::String& id) { const auto type = id.upToFirstOccurrenceOf("-", false, false); return type.substring(0, 1).toUpperCase() + type.substring(1) + " " + id.fromLastOccurrenceOf("-", false, false); }
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
        if (gestureActive) { cancelGesture(); owner.setMessage("Gesture cancelled; committed sketch geometry is unchanged.", 0); return true; }
        if (mode != "select") { owner.setSketchTool("select"); return true; }
        if (selectedEntity.isNotEmpty()) { owner.selectSketchEntity({}, false); return true; }
        return false;
    }
    void cancelGesture() { gestureActive = false; panning = false; snapTarget.clear(); repaint(); }
    void fitToContent()
    {
        needsFit = false;
        double minX = 0, minY = 0, maxX = 0, maxY = 0; bool found = false;
        auto include = [&](double x, double y) { if (!found) { minX = maxX = x; minY = maxY = y; found = true; } else { minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y); } };
        if (const auto* sketch = owner.service->document().findSketch(sketchId))
            for (const auto& e : sketch->entities)
            {
                if (e.type == "circle") { include(e.x1 - e.radius, e.y1 - e.radius); include(e.x1 + e.radius, e.y1 + e.radius); }
                else { include(e.x1, e.y1); include(e.x2, e.y2); }
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
        const auto plane = sketch != nullptr ? sketch->plane : juce::String("XY");
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
            for (const auto& entity : sketch->entities)
            {
                g.setColour(entity.id == selectedEntity ? selectedColour : entity.id == secondEntity ? juce::Colour(0xffc39bff) : juce::Colour(0xff78dcca));
                const auto thickness = entity.id == selectedEntity || entity.id == secondEntity ? 3.0f : 2.0f;
                if (entity.type == "circle") { const auto c = toPoint(entity.x1, entity.y1); const auto r = (float) (entity.radius * scale); g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, thickness); }
                else { const auto a = toPoint(entity.x1, entity.y1), b = toPoint(entity.x2, entity.y2); g.drawLine(a.x, a.y, b.x, b.y, thickness); }
            }

        if (gestureActive && mode != "select")
        {
            const auto a = toPoint(start.x, start.y), b = toPoint(current.x, current.y);
            g.setColour(juce::Colour(0xffffdf7e));
            juce::String readout;
            if (mode == "rectangle") { g.drawRect(juce::Rectangle<float>(a, b), 1.5f); readout = juce::String(std::abs(current.x - start.x), 2) + " x " + juce::String(std::abs(current.y - start.y), 2) + " mm"; }
            else if (mode == "circle") { const auto r = (float) (std::hypot(current.x - start.x, current.y - start.y) * scale); g.drawEllipse(a.x - r, a.y - r, r * 2.0f, r * 2.0f, 1.5f); g.drawLine(a.x, a.y, b.x, b.y, 1.0f); readout = "R " + juce::String(std::hypot(current.x - start.x, current.y - start.y), 2) + " mm"; }
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
        if (mode == "select") return;
        gestureActive = true; start = current = fromPoint(e.position); startConnection.clear(); snapTarget.clear();
        if (mode == "line") { const auto snap = nearestSnap(start); start = current = snap.point; startConnection = snap.target; snapTarget = snap.target; snapPoint = snap.point; }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        updateCursor(e.position);
        if (panning) { const auto d = e.position - panStart; panStart = e.position; centre.x -= d.x / scale; centre.y += d.y / scale; repaint(); return; }
        if (!gestureActive) return;
        const auto raw = fromPoint(e.position); const auto snap = mode == "line" ? nearestSnap(raw) : Snap { raw, {} };
        current = snap.point; snapTarget = snap.target; snapPoint = snap.point; repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (panning) { panning = false; return; }
        if (mode == "select")
        {
            const auto p = fromPoint(e.position); juce::String hit; double best = 8.0 / scale;
            if (const auto* sketch = owner.service->document().findSketch(sketchId))
                for (const auto& entity : sketch->entities)
                {
                    const auto d = entity.type == "circle" ? std::abs(std::hypot(p.x - entity.x1, p.y - entity.y1) - entity.radius) : distanceToSegment(p.x, p.y, entity.x1, entity.y1, entity.x2, entity.y2);
                    if (d < best) { best = d; hit = entity.id; }
                }
            owner.selectSketchEntity(hit, e.mods.isShiftDown() && hit.isNotEmpty());
            return;
        }
        if (!gestureActive) return;
        gestureActive = false;
        const auto endSnap = mode == "line" ? nearestSnap(fromPoint(e.position)) : Snap { fromPoint(e.position), {} };
        const auto end = endSnap.point; snapTarget.clear();
        if ((toPoint(end.x, end.y) - toPoint(start.x, start.y)).getDistanceFromOrigin() < 3.0f) { repaint(); return; }
        juce::String entityId, error; bool ok = false;
        if (mode == "rectangle") ok = owner.service->addSketchRectangle(sketchId, std::min(start.x, end.x), std::min(start.y, end.y), std::abs(end.x - start.x), std::abs(end.y - start.y), entityId, error);
        else if (mode == "circle") ok = owner.service->addSketchCircle(sketchId, start.x, start.y, std::hypot(end.x - start.x, end.y - start.y), entityId, error);
        else if (mode == "line") ok = owner.service->addSketchLine(sketchId, start.x, start.y, end.x, end.y, entityId, error, startConnection, endSnap.target);
        if (ok) owner.sketchGeometryAdded(mode); else owner.showError(error);
        repaint();
    }
    void mouseMove(const juce::MouseEvent& e) override { updateCursor(e.position); }
    void mouseExit(const juce::MouseEvent&) override { hasCursor = false; repaint(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto anchor = fromPoint(e.position);
        scale = juce::jlimit(0.05, 400.0, scale * std::exp(w.deltaY * 1.2));
        const auto moved = fromPoint(e.position);
        centre.x += anchor.x - moved.x; centre.y += anchor.y - moved.y; repaint();
    }
    bool keyPressed(const juce::KeyPress& key) override { return key == juce::KeyPress::escapeKey && escape(); }

private:
    struct Point { double x = 0, y = 0; };
    struct Snap { Point point; juce::String target; };
    double gridStep() const { for (const auto s : { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0 }) if (s * scale >= 16.0) return s; return 1000.0; }
    juce::Point<float> toPoint(double x, double y) const { return { (float) (getWidth() * 0.5 + (x - centre.x) * scale), (float) (getHeight() * 0.5 - (y - centre.y) * scale) }; }
    Point fromPoint(juce::Point<float> p) const { return { centre.x + (p.x - getWidth() * 0.5) / scale, centre.y + (getHeight() * 0.5 - p.y) / scale }; }
    void updateCursor(juce::Point<float> p) { cursor = fromPoint(p); hasCursor = true; repaint(); }
    Snap nearestSnap(Point candidate) const
    {
        Snap result { candidate, {} }; double bestPixels = 8.0;
        if (const auto* sketch = owner.service->document().findSketch(sketchId))
            for (const auto& entity : sketch->entities)
                if (entity.type == "line")
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
    Point start, current, snapPoint, centre, cursor;
    juce::Point<float> panStart;
    double scale = 4.0;
    bool gestureActive = false, panning = false, needsFit = true, hasCursor = false;
};

//==============================================================================
CadWorkspacePanel::~CadWorkspacePanel() { stopTimer(); tree.setRootItem(nullptr); }

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
    addAndMakeVisible(modeLabel); addAndMakeVisible(statusLabel); addAndMakeVisible(unitsLabel);

    struct ToolSpec { juce::TextButton* button; const char* tip; };
    for (const auto& spec : { ToolSpec { &sketchButton, "Create a sketch on an origin plane (XY, XZ or YZ)." }, ToolSpec { &boxButton, "Create a box body from width, depth and height." },
                              ToolSpec { &cylinderButton, "Create a cylinder body from radius and height." }, ToolSpec { &extrudeButton, "Extrude a closed sketch profile into a solid body." },
                              ToolSpec { &cutButton, "Cut a closed sketch profile from a target body." }, ToolSpec { &subtractButton, "Subtract a Tool body from a Target body." },
                              ToolSpec { &unionButton, "Combine two bodies into one." }, ToolSpec { &intersectButton, "Keep only the volume shared by two bodies." },
                              ToolSpec { &undoButton, "Undo the last modeling command." }, ToolSpec { &redoButton, "Redo the last undone modeling command." },
                              ToolSpec { &fitButton, "Frame all visible bodies in the model view." }, ToolSpec { &resetViewButton, "Return the model view to the default orientation." },
                              ToolSpec { &viewsButton, "Open the multi-view 3D Views workspace." }, ToolSpec { &selectToolButton, "Select sketch entities. Shift-click selects a second entity." },
                              ToolSpec { &lineButton, "Draw lines: drag from start to end. Endpoints snap to existing line ends." }, ToolSpec { &rectangleButton, "Draw rectangles: drag from corner to corner." },
                              ToolSpec { &circleButton, "Draw circles: drag from the centre outwards." }, ToolSpec { &constrainButton, "Add a constraint to the selected sketch entity." },
                              ToolSpec { &fitSketchButton, "Frame the whole sketch in the canvas." } })
    {
        styleButton(*spec.button, spec.tip);
        addChildComponent(*spec.button);
    }
    styleButton(finishSketchButton, "Leave sketch editing and keep all completed geometry.", true); addChildComponent(finishSketchButton);
    for (auto* b : { &selectToolButton, &lineButton, &rectangleButton, &circleButton }) b->setClickingTogglesState(false);

    sketchButton.onClick = [this] { startTask(Task::createSketch); };
    boxButton.onClick = [this] { startTask(Task::newBox); };
    cylinderButton.onClick = [this] { startTask(Task::newCylinder); };
    extrudeButton.onClick = [this] { startTask(Task::extrude); };
    cutButton.onClick = [this] { startTask(Task::cut); };
    subtractButton.onClick = [this] { startTask(Task::boolean, "subtract"); };
    unionButton.onClick = [this] { startTask(Task::boolean, "union"); };
    intersectButton.onClick = [this] { startTask(Task::boolean, "intersect"); };
    undoButton.onClick = [this] { juce::String e; if (!service->undo(e)) showError(e); else { refreshFromDocument(true); setMessage("Undone.", 0); } };
    redoButton.onClick = [this] { juce::String e; if (!service->redo(e)) showError(e); else { refreshFromDocument(true); setMessage("Redone.", 0); } };
    fitButton.onClick = [this] { modelView.fitAll(); };
    resetViewButton.onClick = [this] { modelView.resetCamera(); };
    viewsButton.onClick = [this] { if (onOpenViews) onOpenViews(); };
    selectToolButton.onClick = [this] { setSketchTool("select"); };
    lineButton.onClick = [this] { setSketchTool("line"); };
    rectangleButton.onClick = [this] { setSketchTool("rectangle"); };
    circleButton.onClick = [this] { setSketchTool("circle"); };
    constrainButton.onClick = [this] { chooseConstraint(); };
    fitSketchButton.onClick = [this] { sketchCanvas->fitToContent(); };
    finishSketchButton.onClick = [this] { finishSketch(); };

    tree.setRootItemVisible(false); tree.setIndentSize(14);
    tree.setColour(juce::TreeView::backgroundColourId, panelColour);
    side.setOpaque(false); centre.setOpaque(false);
    addAndMakeVisible(treeHeading); addAndMakeVisible(tree); addAndMakeVisible(leftBar); addAndMakeVisible(centre); addAndMakeVisible(rightBar); addAndMakeVisible(side);
    centre.addAndMakeVisible(modelView); centre.addChildComponent(*sketchCanvas);
    modelView.setTitle("Perspective  |  Model  (drag to orbit, Shift/right-drag to pan, wheel to zoom, click to select)");
    modelView.onBodyPicked = [this](const juce::String& id) { bodyPicked(id); };

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
    g.setColour(toolbarColour); g.fillRect(0, 0, getWidth(), 92);
    g.setColour(borderColour); g.drawHorizontalLine(46, 0.0f, (float) getWidth()); g.drawHorizontalLine(92, 0.0f, (float) getWidth());
    g.setColour(panelColour); g.fillRect(tree.getX(), 92, tree.getWidth(), getHeight() - 92 - 28); g.fillRect(side.getX(), 92, side.getWidth(), getHeight() - 92 - 28);
    g.setColour(borderColour); g.drawHorizontalLine(getHeight() - 28, 0.0f, (float) getWidth());
    if (editingSketchId.isNotEmpty()) { g.setColour(accentColour.withAlpha(0.35f)); g.fillRect(0, 0, 6, 92); }
}

void CadWorkspacePanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(46).reduced(12, 7);
    auto toolbar = area.removeFromTop(46).reduced(12, 7);
    auto status = area.removeFromBottom(28).reduced(12, 2);
    unitsLabel.setBounds(status.removeFromRight(260)); statusLabel.setBounds(status);

    auto place = [](juce::Rectangle<int>& row, juce::TextButton& b, bool fromRight = false) { const auto w = juce::jmax(56, juce::roundToInt(juce::Font(15.0f).getStringWidthFloat(b.getButtonText())) + 26); b.setBounds(fromRight ? row.removeFromRight(w) : row.removeFromLeft(w)); if (fromRight) row.removeFromRight(6); else row.removeFromLeft(6); };
    auto headerRight = header;
    place(headerRight, redoButton, true); place(headerRight, undoButton, true);
    if (editingSketchId.isEmpty()) { headerRight.removeFromRight(10); place(headerRight, viewsButton, true); place(headerRight, resetViewButton, true); place(headerRight, fitButton, true); }
    else { headerRight.removeFromRight(10); place(headerRight, fitSketchButton, true); }
    modeLabel.setBounds(headerRight);

    auto row = toolbar;
    if (editingSketchId.isEmpty())
    {
        place(row, sketchButton); place(row, boxButton); place(row, cylinderButton); row.removeFromLeft(14);
        place(row, extrudeButton); place(row, cutButton); row.removeFromLeft(14);
        place(row, subtractButton); place(row, unionButton); place(row, intersectButton);
    }
    else
    {
        place(row, selectToolButton); place(row, lineButton); place(row, rectangleButton); place(row, circleButton); row.removeFromLeft(14);
        place(row, constrainButton);
        finishSketchButton.setBounds(row.removeFromRight(juce::jmax(140, row.getWidth() / 5)));
    }

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

bool CadWorkspacePanel::keyPressed(const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey) return false;
    if (editingSketchId.isNotEmpty()) return sketchCanvas->escape();
    if (task == Task::createSketch || task == Task::newBox || task == Task::newCylinder || task == Task::extrude || task == Task::cut || task == Task::boolean) { cancelTask(); return true; }
    return false;
}

void CadWorkspacePanel::timerCallback()
{
    refreshFromDocument();
    if (modelView.isShowing()) modelView.prepareFrame(0.0f, ++frameSerial);
}

void CadWorkspacePanel::refreshFromDocument(bool force)
{
    const auto& document = service->document();
    if (!force && document.revision() == shownRevision) return;
    shownRevision = document.revision();
    modelView.setSnapshot(service->snapshot());
    auto missing = [&](const juce::String& id) { return id.isNotEmpty() && document.find(id) == nullptr; };
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
        sketchCanvas->setSelection(sketchEntityId, sketchSecondEntityId);
    }
    if (task == Task::body && selectedId.isEmpty()) task = Task::none;
    if (task == Task::sketch && selectedId.isEmpty()) task = Task::none;
    if (task == Task::sketchEntity && sketchEntityId.isEmpty()) task = Task::sketchEdit;
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
    for (const auto& f : service->document().features())
    {
        auto* item = new FeatureItem(*this, f);
        if (isPrimitiveType(f.type)) bodies->addSubItem(item);
        else if (f.type == "sketch") sketches->addSubItem(item);
        else features->addSubItem(item);
    }
    rootItem->addSubItem(origin);
    for (auto* group : { bodies, sketches, features }) { rootItem->addSubItem(group); group->setOpen(true); }
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
                    item->setSelected(item->featureId() == selectedId && task != Task::boolean && task != Task::cut && task != Task::extrude, false, juce::dontSendNotification);
    tree.repaint();
}

void CadWorkspacePanel::featureClicked(const juce::String& id)
{
    if (task == Task::extrude || task == Task::cut || task == Task::boolean) { assignRole(id); return; }
    selectFeature(id);
}

void CadWorkspacePanel::bodyPicked(const juce::String& id)
{
    if (task == Task::extrude || task == Task::cut || task == Task::boolean) { if (id.isNotEmpty()) assignRole(id); return; }
    selectFeature(id);
}

void CadWorkspacePanel::selectFeature(const juce::String& id)
{
    if (editingSketchId.isNotEmpty() && id != editingSketchId) finishSketch();
    selectedId = id;
    commandMessage.clear(); validationMessage.clear();
    const auto* f = service->document().find(id);
    task = f == nullptr ? Task::none : (f->type == "sketch" ? Task::sketch : Task::body);
    if (f == nullptr) selectedId.clear();
    syncTreeSelection(); updateHighlights(); updateToolbar();
    rebuildTaskPanel(false);
}

void CadWorkspacePanel::selectSketchEntity(const juce::String& id, bool second)
{
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
    commandMessage.clear(); validationMessage.clear();
    profileId.clear(); targetId.clear(); toolId.clear(); activeRole = 0; sketchPlane.clear();
    if (next == Task::extrude || next == Task::cut) { if (selected != nullptr && selected->type == "sketch") profileId = selectedId; }
    if (next == Task::boolean && isSolid(selectedId)) { targetId = selectedId; activeRole = 1; }
    syncTreeSelection(); updateHighlights(); updateToolbar();
    rebuildTaskPanel(false);
}

void CadWorkspacePanel::requestRebuild(bool keepValues) { pendingKeepValues = pendingKeepValues && keepValues; triggerAsyncUpdate(); }
void CadWorkspacePanel::handleAsyncUpdate() { const auto keep = pendingKeepValues; pendingKeepValues = true; rebuildTaskPanel(keep); }

void CadWorkspacePanel::rebuildTaskPanel(bool keepValues)
{
    cancelPendingUpdate(); pendingKeepValues = true;
    const auto context = juce::String((int) task) + "|" + booleanKind + "|" + selectedId + "|" + editingSketchId + "|" + sketchEntityId;
    juce::NamedValueSet previous;
    if (keepValues && form != nullptr && context == formContext) previous = form->values();
    formContext = context;
    taskViewport.setViewedComponent(nullptr, false);
    form = std::make_unique<TaskForm>();
    form->onChange = [this] { validateTask(); };
    const auto& document = service->document();
    const auto* f = document.find(selectedId);
    if ((task == Task::body || task == Task::sketch) && f == nullptr) task = Task::none;
    juce::String title, instruction, accept = "Apply", cancel = "Cancel";
    bool showAccept = true, showCancel = true;
    const auto slotText = [&](const juce::String& id, const juce::String& empty) { return id.isEmpty() ? empty : featureName(id); };

    switch (task)
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
            const juce::String placement = f->type == "box" ? "Corner " : f->type == "cylinder" ? "Base centre " : "Offset ";
            form->addHeader("Placement (world)");
            form->addNumber("x", placement + "X", f->x, "mm", "World X position.");
            form->addNumber("y", placement + "Y", f->y, "mm", "World Y position.");
            form->addNumber("z", placement + "Z", f->z, "mm", "World Z position.");
            form->addHeader("Actions");
            const auto id = selectedId;
            form->addButton("visibility", f->visible ? "Hide" : "Show", [this, id] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id] { if (safe != nullptr) safe->toggleVisibility(id); }); }, "Toggle visibility in the model view.");
            if (isSolid(selectedId)) form->addButton("subtractFrom", "Subtract from this body...", [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->startTask(Task::boolean, "subtract"); }); }, "Start Subtract with this body as the Target.");
            form->addButton("delete", "Delete", [this, id] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id] { if (safe != nullptr) safe->deleteFeature(id); }); }, "Delete this feature (Undo restores it).");
            break;
        }
        case Task::sketch:
        {
            const auto* sketch = document.findSketch(selectedId);
            title = f->name; instruction = "Edit the sketch to draw on it, or use it as the profile for Extrude or Cut Sketch.";
            accept = "Edit Sketch"; showCancel = false;
            form->addHeader("Sketch");
            form->addText("name", "Name", f->name, "Friendly sketch name.");
            form->addButton("rename", "Rename", [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe == nullptr || safe->form == nullptr) return; juce::String e; if (!safe->service->rename(safe->selectedId, safe->form->text("name"), e)) safe->showError(e); else { safe->refreshFromDocument(true); safe->rebuildTaskPanel(false); safe->setMessage("Sketch renamed.", 0); } }); }, "Apply the new sketch name.");
            if (sketch != nullptr) form->addNote({}, sketch->plane + " plane  |  offset " + juce::String(sketch->offset) + " mm  |  " + juce::String((int) sketch->entities.size()) + " entities", 1);
            form->addHeader("Use sketch");
            form->addButton("extrude", "Extrude...", [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->startTask(Task::extrude); }); }, "Extrude this sketch's closed profile.");
            form->addButton("cut", "Cut Sketch...", [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->startTask(Task::cut); }); }, "Cut this sketch's profile from a body.");
            const auto id = selectedId;
            form->addButton("delete", "Delete", [this, id] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id] { if (safe != nullptr) safe->deleteFeature(id); }); }, "Delete this sketch (Undo restores it).");
            break;
        }
        case Task::createSketch:
        {
            title = "Create Sketch"; instruction = "Choose the origin plane to sketch on, then press Create Sketch."; accept = "Create Sketch";
            form->addHeader("Plane");
            for (const auto& plane : { juce::String("XY"), juce::String("XZ"), juce::String("YZ") })
            {
                const auto tip = plane == "XY" ? "Sketch on the XY plane (normal along Z)." : plane == "XZ" ? "Sketch on the XZ plane (normal along Y)." : "Sketch on the YZ plane (normal along X).";
                form->addSlot("plane" + plane, plane + " plane", sketchPlane == plane ? "Selected" : "Choose", targetColour, sketchPlane == plane,
                              [this, plane] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), plane] { if (safe != nullptr) { safe->sketchPlane = plane; safe->rebuildTaskPanel(true); } }); }, tip);
            }
            form->addNumber("offset", "Offset", 0.0, "mm", "Distance of the sketch plane from the origin along its normal.");
            break;
        }
        case Task::sketchEdit:
        case Task::sketchEntity:
        {
            const auto* sketch = document.findSketch(editingSketchId);
            const auto plane = sketch != nullptr ? sketch->plane : juce::String("XY");
            const djehuti::cad::CadSketchEntity* entity = nullptr;
            if (sketch != nullptr) for (const auto& e : sketch->entities) if (e.id == sketchEntityId) entity = &e;
            if (entity == nullptr)
            {
                const auto tool = sketchCanvas->tool();
                title = featureName(editingSketchId) + "  |  " + plane + " plane";
                instruction = tool == "line" ? "Line: drag from start to end. Ends snap to existing line ends. Esc cancels the gesture."
                            : tool == "rectangle" ? "Rectangle: drag from one corner to the opposite corner. Esc cancels the gesture."
                            : tool == "circle" ? "Circle: drag from the centre outwards. Esc cancels the gesture."
                            : "Select: click an entity to edit it. Shift-click a second entity for two-entity constraints.";
                accept = "Finish Sketch"; showCancel = false;
                form->addNote({}, "Choose Line, Rectangle or Circle above to draw. The tool stays active until you pick another tool or press Esc. Finish Sketch keeps all completed geometry.", 4);
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
            form->addButton("addConstraint", "Add Constraint", [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->addConstraint(); }); }, "Add the chosen constraint.");
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
                          [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) { safe->activeRole = 0; safe->rebuildTaskPanel(true); } }); }, "Click, then pick the body in the tree or the view.");
            form->addSlot("tool", second, slotText(toolId, subtract ? "Select the cutting body" : "Select a body"), toolColour, activeRole == 1,
                          [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) { safe->activeRole = 1; safe->rebuildTaskPanel(true); } }); }, "Click, then pick the body in the tree or the view.");
            form->addButton("swap", "Swap", [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) { std::swap(safe->targetId, safe->toolId); safe->updateHighlights(); safe->rebuildTaskPanel(true); } }); }, "Exchange the two roles.");
            form->addNote({}, subtract ? "Both source bodies stay in the feature history and are hidden after the operation. Undo restores them." : "Both source bodies stay in the feature history and are hidden after the operation.", 3);
            break;
        }
    }

    if (!previous.isEmpty()) form->restore(previous);
    taskTitle.setText(title, juce::dontSendNotification);
    taskInstruction.setText(instruction, juce::dontSendNotification);
    acceptButton.setButtonText(accept); cancelButton.setButtonText(cancel);
    acceptButton.setVisible(showAccept); cancelButton.setVisible(showCancel);
    taskViewport.setViewedComponent(form.get(), false);
    validateTask();
    resized();
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
    const auto* f = service->document().find(id);
    if (f == nullptr) return;
    commandMessage.clear();
    if (task == Task::extrude || task == Task::cut)
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
            if (sketchPlane.isEmpty()) problem = "Choose the XY, XZ or YZ plane.";
            requireNumber("offset", "Offset");
            break;
        case Task::sketchEntity:
            requirePositive("dimension", "The dimension");
            requireNumber("u", "Position"); requireNumber("v", "Position");
            break;
        case Task::newBox: requirePositive("width", "Width"); requirePositive("depth", "Depth"); requirePositive("height", "Height"); break;
        case Task::newCylinder: requirePositive("radius", "Radius"); requirePositive("height", "Height"); break;
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
        case Task::none: case Task::sketch: case Task::sketchEdit: break;
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
            const bool circle = entity->type == "circle", diameter = circle && circleDiameterDriven(*sketch, entity->id);
            const auto current = circle ? (diameter ? entity->radius * 2.0 : entity->radius) : std::hypot(entity->x2 - entity->x1, entity->y2 - entity->y1);
            const auto u = form->number("u"), v = form->number("v"), value = form->number("dimension");
            const auto sketchId = editingSketchId, entityId = sketchEntityId;
            const bool moved = std::abs(u - entity->x1) > 1.0e-9 || std::abs(v - entity->y1) > 1.0e-9, resized_ = std::abs(value - current) > 1.0e-9;
            bool ok = true;
            if (moved) ok = service->setSketchEntityPosition(sketchId, entityId, u, v, e);
            if (ok && resized_) ok = service->setSketchEntityDimension(sketchId, entityId, value, e);
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
        case Task::none: return;
    }
}

void CadWorkspacePanel::cancelTask()
{
    switch (task)
    {
        case Task::body: rebuildTaskPanel(false); setMessage("Changes reverted.", 0); return;
        case Task::sketchEntity: selectSketchEntity({}, false); return;
        case Task::sketchEdit: case Task::sketch: case Task::none: return;
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
    for (auto* b : { &sketchButton, &boxButton, &cylinderButton, &extrudeButton, &cutButton, &subtractButton, &unionButton, &intersectButton, &fitButton, &resetViewButton, &viewsButton }) b->setVisible(!sketching);
    for (auto* b : { &selectToolButton, &lineButton, &rectangleButton, &circleButton, &constrainButton, &fitSketchButton, &finishSketchButton }) b->setVisible(sketching);
    undoButton.setVisible(true); redoButton.setVisible(true);
    const auto tool = sketchCanvas->tool();
    selectToolButton.setToggleState(tool == "select", juce::dontSendNotification); lineButton.setToggleState(tool == "line", juce::dontSendNotification);
    rectangleButton.setToggleState(tool == "rectangle", juce::dontSendNotification); circleButton.setToggleState(tool == "circle", juce::dontSendNotification);
    subtractButton.setToggleState(task == Task::boolean && booleanKind == "subtract", juce::dontSendNotification);
    unionButton.setToggleState(task == Task::boolean && booleanKind == "union", juce::dontSendNotification);
    intersectButton.setToggleState(task == Task::boolean && booleanKind == "intersect", juce::dontSendNotification);
    extrudeButton.setToggleState(task == Task::extrude, juce::dontSendNotification); cutButton.setToggleState(task == Task::cut, juce::dontSendNotification);
    sketchButton.setToggleState(task == Task::createSketch, juce::dontSendNotification);
    boxButton.setToggleState(task == Task::newBox, juce::dontSendNotification); cylinderButton.setToggleState(task == Task::newCylinder, juce::dontSendNotification);
    if (const auto* sketch = service->document().findSketch(editingSketchId)) modeLabel.setText("Sketch  |  " + featureName(editingSketchId) + "  |  " + sketch->plane + " plane", juce::dontSendNotification);
    else modeLabel.setText("Model", juce::dontSendNotification);
    modeLabel.setColour(juce::Label::textColourId, sketching ? juce::Colour(0xff8fe3d6) : textColour);
    resized(); repaint();
}

void CadWorkspacePanel::updateHighlights()
{
    std::vector<std::pair<juce::String, juce::Colour>> highlights;
    const bool command = task == Task::boolean || task == Task::cut || task == Task::extrude;
    if (!command && selectedId.isNotEmpty()) highlights.push_back({ selectedId, selectedColour });
    if (command && targetId.isNotEmpty()) highlights.push_back({ targetId, targetColour });
    if (command && toolId.isNotEmpty()) highlights.push_back({ toolId, toolColour });
    modelView.setHighlights(std::move(highlights));
    tree.repaint();
}

juce::Colour CadWorkspacePanel::colourFor(const juce::String& id) const
{
    const bool command = task == Task::boolean || task == Task::cut || task == Task::extrude;
    if (command) { if (id == targetId) return targetColour; if (id == toolId) return toolColour; if (id == profileId) return selectedColour; return {}; }
    return id == selectedId ? selectedColour : juce::Colour();
}

void CadWorkspacePanel::showFeatureMenu(const juce::String& id)
{
    const auto* f = service->document().find(id);
    if (f == nullptr) return;
    juce::PopupMenu menu;
    if (f->type == "sketch") { menu.addItem(1, "Edit Sketch"); menu.addItem(2, "Extrude..."); menu.addItem(3, "Cut Sketch..."); }
    if (isSolid(id)) { menu.addItem(4, "Subtract from this body..."); if (task == Task::boolean) { menu.addItem(5, "Use as Target"); menu.addItem(6, "Use as Tool"); } }
    if (f->type != "sketch") menu.addItem(7, f->visible ? "Hide" : "Show");
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
            default: break;
        }
    });
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
juce::String CadWorkspacePanel::featureName(const juce::String& id) const { if (const auto* f = service->document().find(id)) return f->name; return id; }

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
