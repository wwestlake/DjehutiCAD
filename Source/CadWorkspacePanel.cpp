#include "CadMaterialsManager.h"
#include "CadWorkspacePanel.h"
#include "CadExportDialog.h"
#include "SketchToolPalette.h"

#include <deque>
#include <cstdlib>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>

using djehuti::cad::CadFeature;

namespace
{
const juce::Colour backgroundColour(0xff11161b), panelColour(0xff141a1f), toolbarColour(0xff161d23), borderColour(0xff26323a);
const juce::Colour textColour(0xffdce9ee), dimTextColour(0xff8fa4ad), accentColour(0xff2a8c99);
const juce::Colour selectedColour(0xffffb86b), targetColour(0xff63d48a), toolColour(0xffff7b72);
const juce::Colour interferenceColour(0xffe0303a);           // parts of an interfering pair
constexpr juce::uint32 interferenceRegionArgb = 0xffffe14d;  // outline of the common volume

bool isSolidType(const juce::String& type) { return (djehuti::cad::isModelingType(type)&&!djehuti::cad::isSurfaceType(type)) || type=="surface_solidify" || type=="surface_thicken" || djehuti::cad::isReplicationType(type) || type == "box" || type == "cylinder" || type == "extrude" || type == "revolve" || type == "cut" || type == "union" || type == "subtract" || type == "intersect" || type == "chamfer" || type == "hole" || type == "component" || type == "fillet"; }
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
    juce::TextEditor& addNumber(const juce::String& key, const juce::String& caption, double value, const juce::String& unit, const juce::String& tip, int decimals = 4)
    {
        auto& editor = addText(key, caption, juce::String(value, decimals).trimCharactersAtEnd("0").trimCharactersAtEnd("."), tip);
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
    void setUnit(const juce::String& key, const juce::String& text) { for (auto& row : rows) if (row.key == key && row.unit) row.unit->setText(text, juce::dontSendNotification); }
    juce::Component* control(const juce::String& key) const { for (const auto& row : rows) if (row.key == key) return row.control.get(); return nullptr; }
    juce::String text(const juce::String& key) const { if (auto* e = dynamic_cast<juce::TextEditor*>(control(key))) return e->getText().trim(); return {}; }
    double number(const juce::String& key) const { return text(key).getDoubleValue(); }
    bool numberValid(const juce::String& key) const
    {
        const auto value = text(key).toStdString(); char* end = nullptr;
        const auto number = std::strtod(value.c_str(), &end);
        return !value.empty() && end == value.c_str() + value.size() && std::isfinite(number);
    }
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
                e->setText(juce::String(number, key=="direction"?15:12), false);
                if(key=="direction"&&!e->hasKeyboardFocus(true))e->setCaretPosition(0);
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
    juce::String getUniqueName() const override { return text; }
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
    bool mightContainSubItems() override { return type=="sketch" && getNumSubItems()>0; }
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

class CadWorkspacePanel::SketchEntityItem final : public juce::TreeViewItem
{
public:
    SketchEntityItem(CadWorkspacePanel& p,const juce::String& sketch,const djehuti::cad::CadSketchEntity& entity):panel(p),sketchId(sketch),id(entity.id),label(friendlyEntity(entity.id)),tiny(entity.type=="circle"&&entity.radius<djehuti::cad::minimumSketchCircleRadius){}
    bool mightContainSubItems() override{return false;}
    int getItemHeight() const override{return 25;}
    juce::String getUniqueName() const override{return id;}
    void paintItem(juce::Graphics& g,int w,int h) override
    {
        if(isSelected()){g.setColour(juce::Colour(0xff29534e));g.fillRect(0,0,w,h);}
        g.setColour(tiny?juce::Colour(0xffffcf85):textColour);g.setFont(juce::Font(13));g.drawText(label,6,0,w-12,h,juce::Justification::centredLeft,true);
    }
    juce::String getTooltip() override{return id+(tiny?" | Degenerate legacy circle; select and Delete to remove it.":" | Select to edit; Delete removes geometry; Shift-click selects a second entity.");}
    void itemClicked(const juce::MouseEvent& e) override
    {
        // TreeViewItem receives a row-relative position, but JUCE retains the original event
        // component and mouse-down position. The latter converts correctly through any dock/peer.
        if(e.mods.isPopupMenu())
        {
            const auto screen=e.getNumberOfClicks()==0 ? panel.tree.localPointToGlobal(getItemPosition(true).getCentre()) : e.getMouseDownScreenPosition();
            panel.showSketchEntityMenu(sketchId,id,screen);
        }
        else panel.sketchEntityClicked(sketchId,id,e.mods.isShiftDown());
    }
    void itemSelectionChanged(bool selected) override
    {
        if(!selected||panel.updatingTree)return;
        // JUCE changes tree selection before itemClicked. Defer keyboard/accessibility
        // selection so Shift-click can preserve the existing two-entity selection.
        const auto instance=panel.service->document().documentInstance();
        juce::MessageManager::callAsync([safe=juce::Component::SafePointer<CadWorkspacePanel>(&panel),sketch=sketchId,entity=id,instance]
        {
            if(safe==nullptr||safe->service->document().documentInstance()!=instance)return;
            const auto it=safe->entityTreeItems.find(sketch+"/"+entity);
            if(it!=safe->entityTreeItems.end()&&it->second->isSelected()&&safe->sketchEntityId!=entity&&safe->sketchSecondEntityId!=entity)safe->sketchEntityClicked(sketch,entity,false);
        });
    }
    CadWorkspacePanel& panel;juce::String sketchId,id,label;bool tiny=false;
};

//==============================================================================
// Large sketch drawing surface in plane coordinates (millimetres) with zoom, pan, previews and snapping.
class CadWorkspacePanel::SketchCanvas final : public juce::Component
{
public:
    friend bool runSketchUsabilityCheck(juce::var&);
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
        if (e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown()) { panning = true; panMoved=false; panPress=panStart=e.position; return; }
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
        if (panning) { panMoved=panMoved||e.position.getDistanceFrom(panPress)>=3; const auto d = e.position - panStart; panStart = e.position; centre.x -= d.x / scale; centre.y += d.y / scale; repaint(); return; }
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
        if (panning) { panning = false; if(e.mods.isPopupMenu()&&!panMoved)owner.showSketchEntityMenu(sketchId,entityAt(fromPoint(e.position)),e.getScreenPosition()); return; }
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
        if ((toPoint(end.x, end.y) - toPoint(start.x, start.y)).getDistanceFromOrigin() < 3.0f) { owner.setMessage("Gesture cancelled: drag at least 3 pixels to define geometry.", 1); repaint(); return; }
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
        if (ok) owner.sketchGeometryAdded(mode, entityId); else owner.showError(error);
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
        return owner.handleSketchKey(key);
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
    bool panMoved=false;juce::Point<float> panPress;
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
            { "Modify", { E{6,"Extrude...",!sketching}, E{37,"Revolve...",!sketching}, E{38,"Virtual Lathe...",!sketching}, E{7,"Cut sketch...",!sketching}, E{34,"Fillet...",!sketching}, E{39,"Chamfer...",!sketching}, E{40,"Drill Press / Hole...",!sketching}, E{41,"Mirror...",!sketching}, E{42,"Linear Pattern...",!sketching}, E{43,"Circular Pattern...",!sketching}, E{44,"Sweep...",!sketching}, E{45,"Loft...",!sketching}, E{46,"Virtual Router / Mill...",!sketching}, E{47,"Create Surface...",!sketching}, E{48,"Trim Surface...",!sketching}, E{49,"Extend Surface...",!sketching}, E{50,"Sew Surfaces...",!sketching}, E{51,"Solidify Shell...",!sketching}, E{52,"Thicken Surface...",!sketching}, E{8,"Subtract...",!sketching}, E{9,"Union...",!sketching}, E{10,"Intersect...",!sketching} } },
            { "Sketch", { E{11,"Select",sketching}, E{12,"Line",sketching}, E{13,"Rectangle",sketching}, E{14,"Circle",sketching}, E{35,"Curve",sketching}, E{15,"Constrain...",sketching}, E{16,"Fit sketch",sketching}, E{17,"Finish sketch",sketching}, E{53,"Delete selected entities",sketching&&sketchEntityId.isNotEmpty()} } },
            { "Assembly", { E{18,"Instance...",!sketching}, E{19,"Joint...",!sketching}, E{20,"Coupling...",!sketching}, E{21,"Motion link..."}, E{22,"Inverse kinematics...",!sketching}, E{26,"Check interference",!sketching}, E{27,"Clear interference",interferenceActive} } },
            { "View", { E{23,"Fit all"}, E{24,"Reset camera"}, E{25,"3D Views"}, E{54,"Sketch toolbox",true,sketchPalette->wanted()}, E{55,"Dock sketch toolbox",sketching,sketchPalette->isDocked()}, E{30,"Floor grid",true,modelView.guideEnabled("floor")}, E{31,"World planes",true,modelView.guideEnabled("world")}, E{32,"Selected local planes",true,modelView.guideEnabled("local")}, E{33,"Joint frames and limits",true,modelView.guideEnabled("joints")} } }
        };
    };
    workspaceMenu.invoke = [this](int id)
    {
        const juce::StringArray names { "undo","redo","sketch","box","cylinder","extrude","cut","subtract","union","intersect","sketchSelect","line","rectangle","circle","constrain","fitSketch","finishSketch","instance","joint","coupling","link","ik","fit","reset","views" };
        if (id >= 1 && id <= names.size()) executeUiCommand(names[id-1]);
        else if(id>=53&&id<=55)executeUiCommand(juce::StringArray{"deleteSketchEntities","sketchToolbox","dockSketchToolbox"}[id-53]);
        else if (id == 26) executeUiCommand("interference");
        else if (id == 27) executeUiCommand("clearInterference");
        else if (id >= 44 && id <= 52) executeUiCommand(juce::StringArray{"sweep","loft","mill","surface","surface_trim","surface_extend","surface_sew","surface_solidify","surface_thicken"}[id-44]);
        else if (id >= 41 && id <= 43) executeUiCommand(juce::StringArray{"mirror","linearPattern","circularPattern"}[id-41]);
        else if (id == 40) executeUiCommand("hole");
        else if (id == 39) executeUiCommand("chamfer");
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
    holeButton.onClick = [this] { startTask(Task::hole); };
    chamferButton.onClick = [this] { startTask(Task::chamfer); };
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
    sketchPalette=std::make_unique<SketchToolPalette>();centre.addChildComponent(*sketchPalette);
    sketchPalette->command=[this](const juce::String& name){executeUiCommand(name);};
    sketchPalette->layoutChanged=[this]{sketchPalette->setAvailable(editingSketchId.isNotEmpty());resized();workspaceMenu.refresh();};
    modelView.setTitle("Perspective  |  Model  (drag to orbit, Shift/right-drag to pan, wheel to zoom, click to select)");
    modelView.onContextCommand = [this](const juce::String& command,const djehuti::cad::CadPick& pick) { viewContextCommand(command,pick); };
    modelView.onBodyPicked = [this](const juce::String& id) { bodyPicked(id); };
    modelView.onFrameRequested = [this] { wakeFrames(); };
    modelView.wantsBodyPick = [this] { return isCommandTask(); };
    modelView.wantsSketchPick=[this]{return task==Task::lathe;};
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
    modelView.setBounds(centre.getLocalBounds());
    auto sketchArea=centre.getLocalBounds();sketchPalette->place(sketchArea);
    if(sketchPalette->isVisible()&&sketchPalette->isDocked())sketchArea.removeFromLeft(sketchPalette->getWidth()+6);
    sketchCanvas->setBounds(sketchArea);

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
    auto* focus=juce::Component::getCurrentlyFocusedComponent();
    if(focus!=nullptr&&(focus==this||isParentOf(focus)))return handleSketchKey(key);
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
    if(editingSketchId.isNotEmpty())return handleSketchKey(key);
    if (key != juce::KeyPress::escapeKey) return false;
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
    modelingPreviewActive=false;modelingSourcesActive=false;replicationPreviewActive=false;replicationSourcesActive=false;chamferPreviewActive=false;chamferSourceActive=false;holeSourceActive=false;
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
    const juce::ScopedValueSetter<bool> guard(updatingTree,true);
    auto openness=tree.getOpennessState(false);entityTreeItems.clear();tree.setRootItem(nullptr);
    rootItem=std::make_unique<GroupItem>("root");
    auto* origin=new GroupItem("Origin  |  XY / XZ / YZ planes");
    auto* bodies=new GroupItem("Bodies");auto* sketches=new GroupItem("Sketches");auto* features=new GroupItem("Features");auto* assembly=new GroupItem("Assembly");
    const auto& model=service->document().mechanicsData();
    for(const auto& instance:model.instances)assembly->addSubItem(new FeatureItem(*this,instance.id,instance.name,instance.grounded?"grounded":"instance"));
    for(const auto& joint:model.joints)assembly->addSubItem(new FeatureItem(*this,joint.id,joint.name,joint.type+(model.mappingForJoint(joint.id)!=nullptr?" *":"")));
    for(const auto& coupling:model.couplings)assembly->addSubItem(new FeatureItem(*this,coupling.id,coupling.name,coupling.type=="rackPinion"?"rack":coupling.type));
    for(const auto& ik:model.ikTasks)assembly->addSubItem(new FeatureItem(*this,ik.id,ik.name,"IK task"));
    for(const auto& f:service->document().features())
    {
        auto* item=new FeatureItem(*this,f);
        if(isPrimitiveType(f.type))bodies->addSubItem(item);
        else if(f.type=="sketch")
        {
            sketches->addSubItem(item);
            if(const auto* sketch=service->document().findSketch(f.id))for(const auto& entity:sketch->entities)
            {
                auto* row=new SketchEntityItem(*this,f.id,entity);item->addSubItem(row);entityTreeItems[f.id+"/"+entity.id]=row;
                row->setSelected(f.id==editingSketchId&&(entity.id==sketchEntityId||entity.id==sketchSecondEntityId),false,juce::dontSendNotification);
            }
            item->setOpen(f.id==editingSketchId);
        }
        else features->addSubItem(item);
    }
    rootItem->addSubItem(origin);
    for(auto* group:{bodies,sketches,features,assembly}){rootItem->addSubItem(group);group->setOpen(true);}
    tree.setRootItem(rootItem.get());rootItem->setOpen(true);
    if(openness)tree.restoreOpennessState(*openness,false);
    syncTreeSelection();
}

void CadWorkspacePanel::syncTreeSelection()
{
    const juce::ScopedValueSetter<bool> guard(updatingTree,true);
    if(rootItem==nullptr)return;
    juce::TreeViewItem* primary=nullptr;
    for(int g=0;g<rootItem->getNumSubItems();++g)if(auto* group=rootItem->getSubItem(g))
        for(int i=0;i<group->getNumSubItems();++i)if(auto* item=dynamic_cast<FeatureItem*>(group->getSubItem(i)))
            item->setSelected(item->featureId()==selectedId&&!isCommandTask()&&(editingSketchId.isEmpty()||sketchEntityId.isEmpty()),false,juce::dontSendNotification);
    for(const auto& [key,item]:entityTreeItems)
    {
        const bool selected=key==editingSketchId+"/"+sketchEntityId||key==editingSketchId+"/"+sketchSecondEntityId;
        const bool newlySelected=selected&&!item->isSelected();item->setSelected(selected,false,juce::dontSendNotification);
        if(newlySelected)for(auto* parent=item->getParentItem();parent!=nullptr;parent=parent->getParentItem())parent->setOpen(true);
        if(selected&&key==editingSketchId+"/"+sketchEntityId)primary=item;
    }
    if(primary)tree.scrollToKeepItemVisible(primary);tree.repaint();
}

void CadWorkspacePanel::sketchEntityClicked(const juce::String& sketch,const juce::String& entity,bool second)
{
    if(task==Task::lathe){chooseLatheProfile(sketch,entity);return;}
    const auto* source=service->document().findSketch(sketch);bool found=false;
    if(source)for(const auto& e:source->entities)if(e.id==entity)found=true;
    if(!found)return;
    const bool treeFocused=tree.hasKeyboardFocus(true);
    if(editingSketchId!=sketch)enterSketch(sketch);
    sketchCanvas->setTool("select");selectSketchEntity(entity,second);
    if(treeFocused)tree.grabKeyboardFocus();
}

juce::PopupMenu::Options CadWorkspacePanel::sketchEntityMenuOptions(juce::Point<int> screenPosition)
{
    // The target component supplies lifetime/scaling context; the explicit screen area supplies
    // placement. JUCE then fits the menu to the display's available work area, including floating peers.
    return juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({screenPosition.x,screenPosition.y,1,1});
}

void CadWorkspacePanel::showSketchEntityMenu(const juce::String& sketch,const juce::String& entity,juce::Point<int> screenPosition)
{
    if(entity.isNotEmpty()&&(editingSketchId!=sketch||(sketchEntityId!=entity&&sketchSecondEntityId!=entity)))sketchEntityClicked(sketch,entity,false);
    juce::PopupMenu menu;menu.addItem(1,"Delete selected geometry",sketchEntityId.isNotEmpty());
    menu.addItem(2,"Select tool");
    menu.showMenuAsync(sketchEntityMenuOptions(screenPosition),[safe=juce::Component::SafePointer<CadWorkspacePanel>(this)](int id)
    {if(safe!=nullptr){if(id==1)safe->deleteSelectedSketchEntities();else if(id==2)safe->setSketchTool("select");}});
}

bool CadWorkspacePanel::deleteSelectedSketchEntities()
{
    juce::StringArray ids;if(sketchEntityId.isNotEmpty())ids.add(sketchEntityId);if(sketchSecondEntityId.isNotEmpty())ids.addIfNotAlreadyThere(sketchSecondEntityId);
    sketchCanvas->setTool("select");updateToolbar();juce::String error;
    if(editingSketchId.isEmpty()||!service->deleteSketchEntities(editingSketchId,ids,error)){showError("Cannot delete sketch geometry: "+error);return false;}
    refreshFromDocument(true);selectSketchEntity({},false);rebuildTaskPanel(false);sketchCanvas->repaint();
    setMessage("Deleted "+juce::String(ids.size())+" sketch entit"+(ids.size()==1?"y":"ies")+" and attached constraints. Ctrl+Z restores them.",0);return true;
}

bool CadWorkspacePanel::handleSketchKey(const juce::KeyPress& key)
{
    if(editingSketchId.isEmpty())return false;
    if(key==juce::KeyPress::escapeKey)return sketchCanvas->escape();
    if(key.getKeyCode()!=juce::KeyPress::deleteKey||key.getModifiers().isAnyModifierKeyDown())return false;
    auto* focus=juce::Component::getCurrentlyFocusedComponent();
    if(focus!=nullptr&&(dynamic_cast<juce::TextEditor*>(focus)!=nullptr||focus->findParentComponentOfClass<juce::TextEditor>()!=nullptr))return false;
    if(sketchCanvas->hasActiveGesture()){sketchCanvas->cancelGesture();setMessage("Gesture cancelled; completed geometry is unchanged.",0);return true;}
    return sketchEntityId.isNotEmpty()&&deleteSelectedSketchEntities();
}

juce::String CadWorkspacePanel::viewState() const
{
    auto state=modelView.viewState();state.getDynamicObject()->setProperty("sketchToolbox",sketchPalette->state());
    state.getDynamicObject()->setProperty("sketchLineAngleUnit",lineAnglesInRadians?"radians":"degrees");return juce::JSON::toString(state);
}
void CadWorkspacePanel::restoreViewState(const juce::String& json)
{
    const auto state=juce::JSON::parse(json);modelView.restoreViewState(state);
    if(state.hasProperty("sketchToolbox"))sketchPalette->restore(state["sketchToolbox"]);
    lineAnglesInRadians=state["sketchLineAngleUnit"].toString()=="radians";
}

void CadWorkspacePanel::syncLineProperties(bool polar)
{
    if (form == nullptr) return;
    const auto set = [this](const char* key,double value)
    {
        const bool angle=juce::String(key)=="direction";
        if(!angle&&std::abs(value)<0.5e-12)value=0; // Avoid displaying negative zero at the coordinate field's precision.
        if (auto* editor=dynamic_cast<juce::TextEditor*>(form->control(key)))
        {editor->setText(juce::String(value,angle?15:12),false);if(angle&&!editor->hasKeyboardFocus(true))editor->setCaretPosition(0);}
    };
    const bool startValid=form->numberValid("u")&&form->numberValid("v");
    if (polar)
    {
        if (startValid&&form->numberValid("dimension")&&form->numberValid("direction")&&form->number("dimension")>=1.0e-8)
        {
            const auto angle=form->number("direction")*(lineAnglesInRadians?1.0:juce::MathConstants<double>::pi/180.0);
            set("endU",form->number("u")+form->number("dimension")*std::cos(angle));
            set("endV",form->number("v")+form->number("dimension")*std::sin(angle));
        }
    }
    else if (startValid&&form->numberValid("endU")&&form->numberValid("endV"))
    {
        const auto dx=form->number("endU")-form->number("u"),dy=form->number("endV")-form->number("v");
        set("dimension",std::hypot(dx,dy));
        set("direction",std::atan2(dy,dx)*(lineAnglesInRadians?1.0:180.0/juce::MathConstants<double>::pi));
    }
    validateTask();
}

void CadWorkspacePanel::featureClicked(const juce::String& id)
{
    if(task==Task::lathe){chooseLatheProfile(id);return;}
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
    if(task==Task::lathe){if(id.isNotEmpty()&&service->document().findSketch(id)!=nullptr)chooseLatheProfile(id);return;}
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

bool CadWorkspacePanel::isCommandTask() const { return task == Task::lathe || task == Task::modeling || task == Task::replication || task == Task::extrude || task == Task::revolve || task == Task::cut || task == Task::fillet || task == Task::chamfer || task == Task::hole || task == Task::boolean || task == Task::newInstance || task == Task::newJoint || task == Task::link || task == Task::newCoupling; }
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
    if(task==Task::lathe){if(selection!=nullptr)pickLatheProfile(selection->selected());return;}
    if(modelingPreviewActive){clearModelingView();showModelingSources();return;}
    if(task==Task::modeling){if(selection!=nullptr)pickModeling(selection->selected());return;}
    if(replicationPreviewActive){clearReplicationView();showReplicationSources();return;}
    if(task==Task::replication)
    {
        if(selection!=nullptr){if(replicationPick=="sources"){const auto pickedItems=selection->items();for(const auto& item:pickedItems)pickReplication(item);}else pickReplication(selection->selected());}return;
    }
    if(chamferPreviewActive){clearChamferPreview(true);return;}
    updateToolbar();
    updateHighlights();
    if (selection == nullptr) return;
    const auto& picked = selection->selected();
    if(task==Task::hole){if(picked.valid()&&picked.face>=0)chooseHoleFace(picked);return;}
    if (picked.valid() && (picked.face >= 0 || picked.edge >= 0))
    {
        if (task == Task::fillet || task == Task::chamfer)
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
        if(face.planar&&face.token.isNotEmpty())form->addButton("faceHole","Drill a hole on this face...",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->startTask(Task::hole);});},"Open Drill Press with this planar face selected.");
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
        form->addButton("edgeChamfer","Chamfer this edge...",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->startTask(Task::chamfer);});},"Open Chamfer with this edge selected.");
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
    clearLatheView();clearModelingView();clearReplicationView();
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
    syncTreeSelection();updateToolbar();
    requestRebuild(second);
}

void CadWorkspacePanel::startTask(Task next, const juce::String& kind)
{
    if (editingSketchId.isNotEmpty() && next != Task::sketchEdit) finishSketch();
    clearLatheView();clearModelingView();clearReplicationView();clearHoleSource();clearChamferPreview(false);
    const auto* selected = service->document().find(selectedId);
    task = next; booleanKind = kind;
    if(next==Task::modeling)
    {
        modelingKind=kind;editingModelingId=selected!=nullptr&&selected->type==kind?selectedId:juce::String();modelingSettings=editingModelingId.isEmpty()?djehuti::cad::modelingDefaults(kind):juce::JSON::parse(juce::JSON::toString(selected->modeling,false,17));modelingRevision=service->document().revision();
        modelingPick=kind=="sweep"||kind=="surface"?"profile":kind=="loft"?"sections":kind=="surface_sew"?"sources":"target";
        if(editingModelingId.isEmpty()&&selected!=nullptr)
        {
            if(selected->type=="sketch")
            {if(kind=="sweep"||kind=="surface"){modelingSettings.getDynamicObject()->setProperty("profile",selectedId);modelingPick="path";}else if(kind=="loft"){modelingSettings.getDynamicObject()->setProperty("sections",juce::Array<juce::var>{selectedId});}else {modelingSettings.getDynamicObject()->setProperty("region",selectedId);}}
            else if(kind=="mill"&&isSolid(selectedId)){modelingSettings.getDynamicObject()->setProperty("target",selectedId);defaultMillFace(selectedId);modelingPick="region";}
            else if(djehuti::cad::isSurfaceType(kind)&&selected->type!="sketch")
            {if(kind=="surface_sew")modelingSettings.getDynamicObject()->setProperty("sources",juce::Array<juce::var>{selectedId});else if(kind!="surface")modelingSettings.getDynamicObject()->setProperty("target",selectedId);}
        }
        if(selection!=nullptr)selection->setMode(djehuti::cad::CadSelection::Mode::body);
    }
    if(next==Task::replication)
    {
        replicationKind=kind;editingReplicationId=selected!=nullptr&&selected->type==kind?selectedId:juce::String();
        replicationSettings=editingReplicationId.isEmpty()?djehuti::cad::replicationDefaults(kind):juce::JSON::parse(juce::JSON::toString(selected->replication,false,17));
        replicationRevision=service->document().revision();replicationPick="sources";
        if(editingReplicationId.isEmpty()&&selected!=nullptr&&isSolid(selectedId))
        {
            replicationSettings.getDynamicObject()->setProperty("sources",juce::Array<juce::var>{selectedId});
            if(juce::StringArray{"hole","cut","subtract","union"}.contains(selected->type)){replicationSettings.getDynamicObject()->setProperty("operation","feature");}
        }
        if(selection!=nullptr)selection->setMode(djehuti::cad::CadSelection::Mode::body);
    }
    if(next==Task::lathe){editingLatheId=selected!=nullptr&&selected->lathe?selectedId:juce::String();latheSettings=editingLatheId.isEmpty()?djehuti::cad::CadRevolveParameters():selected->revolve;if(editingLatheId.isEmpty())latheSettings.axisType="worldZ";else if(latheSettings.axisType=="sketchU"||latheSettings.axisType=="sketchV"){const auto* source=service->document().findSketch(selected->sketchId);if(source&&!source->onFace()&&source->offset==0)latheSettings.axisType=latheSettings.axisType=="sketchU"?(source->plane=="YZ"?"worldY":"worldX"):(source->plane=="XY"?"worldY":"worldZ");}lathePreviewWanted=false;}
    commandMessage.clear(); validationMessage.clear();
    profileId.clear(); targetId.clear(); toolId.clear(); activeRole = 0; sketchPlane.clear();
    filletEdges.clear();
    if(next==Task::chamfer)
    {
        chamferSelectionRevision=service->document().revision();editingChamferId=selected!=nullptr&&selected->type=="chamfer"?selectedId:juce::String();chamferDistance=editingChamferId.isEmpty()?1.0:selected->width;
    }
    if (next == Task::fillet || next == Task::chamfer)
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
    if(next==Task::chamfer&&editingChamferId.isNotEmpty())
    {
        auto* a=new juce::DynamicObject();a->setProperty("action","get");a->setProperty("id",editingChamferId);const auto p=service->chamferCommand(a)["parameters"];
        targetId=p["target"].toString();filletEdges.clear();if(const auto* edges=p["edges"].getArray())for(const auto& edge:*edges)filletEdges.push_back((int)edge);
        showChamferSource();
    }
    if(next==Task::hole)
    {
        editingHoleId=selected!=nullptr&&selected->type=="hole"?selectedId:juce::String();holeSettings=editingHoleId.isEmpty()?djehuti::cad::CadHoleParameters():selected->hole;
        holeSelectionRevision=service->document().revision();const auto picked=selection!=nullptr?selection->selected():djehuti::cad::CadPick();
        if(selection!=nullptr)selection->setMode(djehuti::cad::CadSelection::Mode::face);
        if(editingHoleId.isNotEmpty()){targetId=selected->targetFeatureId;showHoleSource();}
        else if(picked.valid()&&picked.face>=0)chooseHoleFace(picked);
    }
    if (next == Task::createSketch) { djehuti::cad::CadFaceInfo support; juce::String reason; if (selectedSupport(support, reason)) sketchPlane = "FACE"; }
    if (next == Task::extrude || next == Task::revolve || next == Task::cut || next==Task::lathe) { if (selected != nullptr && selected->type == "sketch") profileId = selectedId; else if(next==Task::lathe&&selected!=nullptr&&selected->lathe)profileId=selected->sketchId; }
    if (next == Task::boolean && isSolid(selectedId)) { targetId = selectedId; activeRole = 1; }
    instanceBodyId.clear(); jointParentId.clear(); jointChildId.clear();
    if (next == Task::newInstance && isSolid(selectedId)) instanceBodyId = selectedId;
    if (next == Task::newJoint) { activeRole = 1; if (isInstance(selectedId)) { jointChildId = selectedId; activeRole = 0; } }
    couplingJointA.clear(); couplingJointB.clear();
    if (next == Task::newCoupling) { activeRole = 0; if (isJoint(selectedId)) { couplingJointA = selectedId; activeRole = 1; } }
    syncTreeSelection(); updateHighlights(); updateToolbar();
    rebuildTaskPanel(false);
    if(next==Task::lathe)updateLatheView();
    if(next==Task::modeling)showModelingSources();
    if(next==Task::replication)showReplicationSources();
}

void CadWorkspacePanel::requestRebuild(bool keepValues) { pendingKeepValues = pendingKeepValues && keepValues; triggerAsyncUpdate(); }
void CadWorkspacePanel::handleAsyncUpdate() { const auto keep = pendingKeepValues; pendingKeepValues = true; rebuildTaskPanel(keep); }

juce::var CadWorkspacePanel::chamferArguments(const juce::String& action) const
{
    auto* args=new juce::DynamicObject();auto* params=new juce::DynamicObject();args->setProperty("action",action);
    params->setProperty("distance",form->number("chamferDistance"));
    if(task==Task::body)
    {args->setProperty("id",selectedId);}
    else
    {
        if(editingChamferId.isNotEmpty())args->setProperty("id",editingChamferId);
        params->setProperty("target",targetId);juce::Array<juce::var> edges;for(const int edge:filletEdges)edges.add(edge);params->setProperty("edges",edges);
    }
    args->setProperty("parameters",params);return args;
}

void CadWorkspacePanel::showChamferSource()
{
    if(editingChamferId.isEmpty()||targetId.isEmpty())return;
    const auto all=service->document().snapshot(true);auto view=std::make_shared<djehuti::cad::CadRenderSnapshot>(*service->snapshot());
    std::erase_if(view->meshes,[&](const auto& m){return m.featureId==editingChamferId||m.featureId==targetId;});
    std::erase_if(view->edges,[&](const auto& e){return e.featureId==editingChamferId||e.featureId==targetId;});
    for(const auto& m:all->meshes)if(m.featureId==targetId)view->meshes.push_back(m);
    for(const auto& e:all->edges)if(e.featureId==targetId)view->edges.push_back(e);
    view->revision|=(std::uint64_t(1)<<62);modelView.setSnapshot(view);chamferSourceActive=true;wakeFrames();
}

void CadWorkspacePanel::clearChamferPreview(bool keepSource)
{
    if(!chamferPreviewActive&&!chamferSourceActive)return;
    chamferPreviewActive=false;chamferSourceActive=false;holeSourceActive=false;modelView.setSnapshot(service->snapshot());
    if(keepSource&&task==Task::chamfer&&editingChamferId.isNotEmpty())showChamferSource();
    wakeFrames();
}

void CadWorkspacePanel::previewChamfer()
{
    const auto request=chamferArguments("preview");clearChamferPreview(false);juce::var details;juce::String error;
    const auto snapshot=service->document().chamferPreview(request,details,error);
    if(snapshot==nullptr){if(task==Task::chamfer)showChamferSource();showError(error);return;}
    modelView.setSnapshot(snapshot);chamferPreviewActive=true;wakeFrames();
    setMessage("Chamfer preview. Apply commits it; changing inputs or Cancel clears it.",0);validateTask();
}

void CadWorkspacePanel::showHoleSource()
{
    if(editingHoleId.isEmpty()||targetId.isEmpty())return;
    const auto all=service->document().snapshot(true);auto view=std::make_shared<djehuti::cad::CadRenderSnapshot>(*service->snapshot());
    std::erase_if(view->meshes,[&](const auto& m){return m.featureId==editingHoleId||m.featureId==targetId;});
    std::erase_if(view->edges,[&](const auto& e){return e.featureId==editingHoleId||e.featureId==targetId;});
    for(const auto& m:all->meshes)if(m.featureId==targetId)view->meshes.push_back(m);
    for(const auto& e:all->edges)if(e.featureId==targetId)view->edges.push_back(e);
    view->revision|=(std::uint64_t(1)<<62);modelView.setSnapshot(view);holeSourceActive=true;wakeFrames();
}

void CadWorkspacePanel::clearHoleSource()
{
    if(!holeSourceActive)return;holeSourceActive=false;modelView.setSnapshot(service->snapshot());wakeFrames();
}

void CadWorkspacePanel::chooseHoleFace(const djehuti::cad::CadPick& pick)
{
    djehuti::cad::CadFaceInfo face;juce::String error;
    if(!service->describeFace(pick.featureId,pick.face,face,error)){showError(error);return;}
    if(!face.planar||face.token.isEmpty()){showError("Drill Press needs a planar face with a persistent name.");return;}
    auto* a=new juce::DynamicObject();a->setProperty("action","face");a->setProperty("target",pick.featureId);a->setProperty("face",face.token);
    const auto info=service->holeCommand(a);if(!(bool)info["ok"]){showError(info["error"].toString());return;}
    auto p=holeSettings;
    if(form!=nullptr&&form->control("holeDiameter")!=nullptr)
    {
        auto values=readHoleForm(error);
        if(values.isObject()){values.getDynamicObject()->removeProperty("target");values.getDynamicObject()->setProperty("face",face.token);djehuti::cad::readHoleParameters(values,p,error);}
    }
    p.face=face.token;p.u=(double)info["centreUV"][0];p.v=(double)info["centreUV"][1];targetId=pick.featureId;holeSettings=p;
    if(editingHoleId.isNotEmpty())showHoleSource();rebuildTaskPanel(false);
}

void CadWorkspacePanel::clearModelingView()
{
    if(!modelingPreviewActive&&!modelingSourcesActive)return;modelingPreviewActive=false;modelingSourcesActive=false;modelView.setSnapshot(service->snapshot());wakeFrames();
}

void CadWorkspacePanel::showModelingSources()
{
    if(task!=Task::modeling)return;const auto ids=djehuti::cad::modelingDependencies(modelingSettings);const auto all=service->document().snapshot(true);auto view=std::make_shared<djehuti::cad::CadRenderSnapshot>(*service->snapshot());
    std::erase_if(view->meshes,[&](const auto& m){return m.featureId==editingModelingId||ids.contains(m.featureId);});std::erase_if(view->edges,[&](const auto& e){return e.featureId==editingModelingId||ids.contains(e.featureId);});
    for(const auto& m:all->meshes)if(ids.contains(m.featureId))view->meshes.push_back(m);for(const auto& e:all->edges)if(ids.contains(e.featureId))view->edges.push_back(e);view->revision|=(std::uint64_t(1)<<62);modelView.setSnapshot(view);modelingSourcesActive=true;wakeFrames();
}

void CadWorkspacePanel::defaultMillFace(const juce::String& target)
{
    std::vector<djehuti::cad::CadFaceInfo> faces;juce::String error;if(!service->listFaces(target,faces,error))return;const djehuti::cad::CadFaceInfo* top=nullptr;
    for(const auto& face:faces)if(face.planar&&face.token.isNotEmpty()&&face.normal.Z()>.99&&(top==nullptr||face.centre.Z()>top->centre.Z()))top=&face;
    if(top!=nullptr){auto* r=new juce::DynamicObject();r->setProperty("type","face");r->setProperty("feature",target);r->setProperty("face",top->token);modelingSettings.getDynamicObject()->setProperty("reference",r);}
}

juce::var CadWorkspacePanel::readModelingForm(juce::String& error) const
{
    auto p=juce::JSON::parse(juce::JSON::toString(modelingSettings,false,17));if(form==nullptr)return p;
    const auto defaults=djehuti::cad::modelingDefaults(modelingKind);
    for(const auto& prop:defaults.getDynamicObject()->getProperties())
    {
        const auto key=prop.name.toString();if(form->control("adv_"+key)==nullptr)continue;
        if(prop.value.isBool())p.getDynamicObject()->setProperty(key,form->toggle("adv_"+key));
        else if(prop.value.isDouble()||prop.value.isInt()){if(!form->numberValid("adv_"+key)){error=key+" must be finite and numeric.";return {};}p.getDynamicObject()->setProperty(key,form->number("adv_"+key));}
    }
    const auto choice=[&](const char* field,const char* parameter,const juce::StringArray& values){if(form->control(field)!=nullptr)p.getDynamicObject()->setProperty(parameter,values[juce::jlimit(0,values.size()-1,form->choice(field))]);};
    choice("advOperation","operation",{"new","add","subtract"});choice("advOrientation","orientation",{"transport","frenet","fixed"});choice("advTransition","transition",{"round","right","transformed"});choice("advAlignment","alignment",{"auto","manual"});choice("advStrategy","strategy",{"pocket","slot","profileCut"});choice("advCompensation","compensation",{"on","inside","outside"});
    if((modelingKind=="sweep"||modelingKind=="surface")&&form->control("advOrientation")!=nullptr&&form->choice("advOrientation")==2)
    {juce::Array<juce::var> b;for(const auto* component:{"X","Y","Z"}){if(!form->numberValid(juce::String("advBinormal")+component)){error="Binormal components must be numeric.";return {};}b.add(form->number(juce::String("advBinormal")+component));}p.getDynamicObject()->setProperty("binormal",b);}
    if(form->control("advPathReverse")!=nullptr)p["path"].getDynamicObject()->setProperty("reverse",form->toggle("advPathReverse"));
    if(modelingKind=="loft"||(modelingKind=="surface"&&p["method"].toString()=="loft"))
    {
        juce::Array<juce::var> offsets,reversals,seams;for(int i=0;i<p["sections"].size();++i)
        {const auto suffix=juce::String(i);if(!form->numberValid("loftOffset"+suffix)||!form->numberValid("loftSeam"+suffix)){error="Section offsets and seam angles must be numeric.";return {};}offsets.add(form->number("loftOffset"+suffix));reversals.add(form->toggle("loftReverse"+suffix));seams.add(form->number("loftSeam"+suffix));}
        p.getDynamicObject()->setProperty("offsets",offsets);p.getDynamicObject()->setProperty("reverseSections",reversals);p.getDynamicObject()->setProperty("seamAngles",seams);
    }
    if(modelingKind=="mill"&&form->control("advReference")!=nullptr)
    {
        const juce::StringArray types{"XY","XZ","YZ","custom","face"};const auto kind=types[form->choice("advReference")];auto r=juce::var(new juce::DynamicObject());r.getDynamicObject()->setProperty("type",kind);r.getDynamicObject()->setProperty("reverse",form->toggle("advReferenceReverse"));
        if(kind=="face")for(const auto* key:{"feature","face"})if(p["reference"].hasProperty(key))r.getDynamicObject()->setProperty(key,p["reference"][key]);
        if(kind=="custom")
        {juce::Array<juce::var> origin,normal;for(const auto* component:{"X","Y","Z"}){if(!form->numberValid(juce::String("advOrigin")+component)||!form->numberValid(juce::String("advNormal")+component)){error="Reference origin/normal must be numeric.";return {};}origin.add(form->number(juce::String("advOrigin")+component));normal.add(form->number(juce::String("advNormal")+component));}r.getDynamicObject()->setProperty("origin",origin);r.getDynamicObject()->setProperty("vector",normal);}
        p.getDynamicObject()->setProperty("reference",r);
    }
    if(djehuti::cad::isSurfaceType(modelingKind))readSurfaceForm(p,error);
    if(error.isEmpty())djehuti::cad::validateModeling(modelingKind,p,error);return p;
}

void CadWorkspacePanel::addModelingFields()
{
    const auto p=modelingSettings;const auto beginPick=[this](const juce::String& role,djehuti::cad::CadSelection::Mode mode)
    {juce::String ignored;const auto current=readModelingForm(ignored);if(current.isObject())modelingSettings=current;clearModelingView();modelingPick=role;if(selection!=nullptr)selection->setMode(mode);showModelingSources();setMessage(role=="path"?"Choose a path sketch in the tree, or click connected edges in a model view.":"Choose the "+role+" in the tree or model view.",0);};
    const auto slot=[&](const char* key,const char* label,const juce::String& id,const char* role)
    {form->addSlot(key,label,id.isEmpty()?juce::String("Choose ")+label:featureName(id),accentColour,id.isEmpty(),[beginPick,role]{beginPick(role,juce::String(role)=="path"?djehuti::cad::CadSelection::Mode::edge:djehuti::cad::CadSelection::Mode::body);},"Choose the referenced source geometry.");};
    const bool surface=djehuti::cad::isSurfaceType(modelingKind);const auto geometryKind=modelingKind=="surface"?p["method"].toString():modelingKind;
    if(surface){addSurfaceFields();if(geometryKind!="sweep"&&geometryKind!="loft")return;}
    if(!surface)slot("advTarget","Target body",p["target"].toString(),"target");
    if(!surface&&modelingKind!="mill")form->addChoice("advOperation","Operation",{"New body","Add to target","Subtract from target"},juce::StringArray{"new","add","subtract"}.indexOf(p["operation"].toString()),"Create a separate solid or apply the generated solid to the target.");
    if(geometryKind=="sweep")slot("advProfile","Profile sketch",p["profile"].toString(),"profile");
    if(geometryKind=="sweep"||modelingKind=="mill")
    {
        const auto path=p["path"];const auto pathId=path["sketch"].toString().isNotEmpty()?path["sketch"].toString():path["feature"].toString();slot("advPath","Path sketch / edges",pathId,"path");
        form->addNote({},path["edges"].size()>0?juce::String(path["edges"].size())+" persistent path edges":"A path sketch may contain lines, circles and Bezier curves.",1);
        form->addButton("advClearPath","Clear path",[this]{juce::String ignored;const auto current=readModelingForm(ignored);if(current.isObject())modelingSettings=current;modelingSettings.getDynamicObject()->setProperty("path",djehuti::cad::modelingDefaults(modelingKind)["path"]);rebuildTaskPanel(false);},"Replace the path selection.");
        form->addToggle("advPathReverse","Reverse path",(bool)path.getProperty("reverse",false),"Reverse connected segment traversal.");
    }
    if(geometryKind=="sweep")
    {
        form->addChoice("advOrientation","Orientation",{"Transport","Frenet","Fixed binormal"},juce::StringArray{"transport","frenet","fixed"}.indexOf(p["orientation"].toString()),"Transport uses a corrected frame; Frenet follows curvature; fixed uses a constant binormal.");
        form->addChoice("advTransition","Path corners",{"Round","Right corner","Transformed"},juce::StringArray{"round","right","transformed"}.indexOf(p["transition"].toString()),"Corner treatment where the path tangent changes.");
        form->addToggle("adv_alignProfile","Align profile to path",(bool)p["alignProfile"],"Map the sketch origin/plane to the starting point and tangent. Disable only for an already aligned profile.");
        form->addNumber("adv_rollDegrees","Initial roll",(double)p["rollDegrees"],"deg","Rotate the starting profile about the path tangent.",12);form->addNumber("adv_twistDegrees","Total twist",(double)p["twistDegrees"],"deg","Controlled twist is supported on straight paths in Transport mode.",12);
        for(int i=0;i<3;++i){const auto letter=juce::String::charToString("XYZ"[i]);form->addNumber("advBinormal"+letter,"Binormal "+letter,(double)p["binormal"][i],{},"Fixed binormal direction; choose a valid direction for the entire path.",12);}
    }
    if(geometryKind=="loft")
    {
        form->addSlot("advSections","Ordered sections",juce::String(p["sections"].size())+" selected",accentColour,p["sections"].size()<2,[beginPick]{beginPick("sections",djehuti::cad::CadSelection::Mode::body);},"Click sketches in the desired section order.");
        form->addToggle("adv_ruled","Ruled transitions",(bool)p["ruled"],"Use ruled segments; disable for smooth OCCT transitions.");form->addChoice("advAlignment","Correspondence",{"Automatic","Manual"},p["alignment"].toString()=="auto"?0:1,"Manual requires matching edge counts and honors per-section edge/seam choices.");
        for(int i=0;i<p["sections"].size();++i)
        {
            const auto suffix=juce::String(i);form->addHeader("Section "+juce::String(i+1)+": "+featureName(p["sections"][i].toString()));
            form->addButton("loftUp"+suffix,"Move section up",[this,i]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),i]{if(safe!=nullptr)safe->editLoftSection(i,-1);});},"Move this section earlier in the loft.");form->addButton("loftDown"+suffix,"Move section down",[this,i]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),i]{if(safe!=nullptr)safe->editLoftSection(i,1);});},"Move this section later in the loft.");form->addButton("loftRemove"+suffix,"Remove section",[this,i]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),i]{if(safe!=nullptr)safe->editLoftSection(i,0);});},"Remove this source section.");
            form->addNumber("loftOffset"+suffix,"Starting edge",p["offsets"].size()>i?(double)p["offsets"][i]:0,{},"Zero-based cyclic starting edge for polygon correspondence.",12);form->addToggle("loftReverse"+suffix,"Reverse section",p["reverseSections"].size()>i&&(bool)p["reverseSections"][i],"Reverse the section's wire traversal.");form->addNumber("loftSeam"+suffix,"Circular seam angle",p["seamAngles"].size()>i?(double)p["seamAngles"][i]:0,"deg","Nonzero seam angles require a circular section; use Manual to retain explicit correspondence.",12);
        }
    }
    if(modelingKind=="mill")
    {
        slot("advRegion","Pocket-region sketch",p["region"].toString(),"region");form->addChoice("advStrategy","Machining operation",{"Pocket","Slot","Profile cut"},juce::StringArray{"pocket","slot","profileCut"}.indexOf(p["strategy"].toString()),"Flat-ended cutter removal with a region or centreline path.");
        const auto r=p["reference"];form->addChoice("advReference","Entry reference",{"XY","XZ","YZ","custom","face"},juce::StringArray{"XY","XZ","YZ","custom","face"}.indexOf(r["type"].toString()),"The normal points outward toward the approaching cutter.");
        form->addButton("advReferencePick","Pick entry face...",[beginPick]{beginPick("reference",djehuti::cad::CadSelection::Mode::face);},"Choose an accessible named planar face.");form->addNote({},r["face"].toString(),1);
        const auto o=r.getProperty("origin",juce::Array<juce::var>{0,0,0}),n=r.getProperty("vector",juce::Array<juce::var>{0,0,1});for(int i=0;i<3;++i){const auto letter=juce::String::charToString("XYZ"[i]);form->addNumber("advOrigin"+letter,"Plane origin "+letter,(double)o[i],"mm","Custom entry plane origin.",12);form->addNumber("advNormal"+letter,"Plane normal "+letter,(double)n[i],{},"Outward approach normal.",12);}
        form->addToggle("advReferenceReverse","Reverse approach",(bool)r.getProperty("reverse",false),"Reverse the normal and its local V direction.");form->addToggle("adv_alignToReference","Align sketch to reference",(bool)p["alignToReference"],"Map sketch-local coordinates to the reference U/V frame. Disable to retain world placement, which must be coplanar.");
        form->addNumber("adv_toolDiameter","Tool diameter",(double)p["toolDiameter"],"mm","Flat-ended axial cutter diameter; pocket corners retain this radius.",12);form->addNumber("adv_depth","Blind depth",(double)p["depth"],"mm","Depth measured inward from the entry plane.",12);form->addToggle("adv_throughAll","Through cut",(bool)p["throughAll"],"Recompute the full target extent in the inward direction.");form->addChoice("advCompensation","Profile cutter side",{"On path","Inside","Outside"},juce::StringArray{"on","inside","outside"}.indexOf(p["compensation"].toString()),"Closed profile only; select which side retains the intended boundary.");
    }
    form->addButton("modelingPreview","Preview",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->previewModeling();});},"Evaluate and display an isolated draft without live document or Undo changes.");form->addButton("modelingClearPreview","Clear preview",[this]{clearModelingView();showModelingSources();validateTask();},"Restore the source geometry for picking.");
}

void CadWorkspacePanel::readSurfaceForm(juce::var& p,juce::String& error) const
{
    const auto choice=[&](const char* field,const char* key,const juce::StringArray& values)
    {if(form->control(field)!=nullptr)p.getDynamicObject()->setProperty(key,values[juce::jlimit(0,values.size()-1,form->choice(field))]);};
    choice("surfMethod","method",{"planar","extrude","revolve","sweep","loft"});choice("surfDirection","direction",{"positive","negative","symmetric"});
    const auto vector=[&](const juce::String& prefix,const char* key)
    {
        if(form->control(prefix+"X")==nullptr)return;juce::Array<juce::var> values;
        for(const auto* letter:{"X","Y","Z"}){if(!form->numberValid(prefix+letter)){error="Surface coordinates and vectors must be finite numbers.";return;}values.add(form->number(prefix+letter));}
        p.getDynamicObject()->setProperty(key,values);
    };
    if(modelingKind=="surface")
    {
        vector("surfExtrude","direction");
        if(form->control("surfAxis")!=nullptr)
        {
            auto axis=juce::var(new juce::DynamicObject());const juce::StringArray types{"sketchU","sketchV","worldX","worldY","worldZ","custom"};axis.getDynamicObject()->setProperty("type",types[form->choice("surfAxis")]);
            for(const auto* key:{"origin","direction"}){juce::Array<juce::var> values;for(const auto* letter:{"X","Y","Z"}){const auto field=juce::String("surfAxis")+key+letter;if(!form->numberValid(field)){error="Axis origin and direction must be numeric.";return;}values.add(form->number(field));}axis.getDynamicObject()->setProperty(key,values);}p.getDynamicObject()->setProperty("axis",axis);
        }
    }
    if(modelingKind=="surface_trim")vector("surfSeed","seed");
    if(modelingKind=="surface_extend")
    {juce::Array<juce::var> sides;for(int i=0;i<4;++i)sides.add(form->toggle("surfSide"+juce::String(i)));p.getDynamicObject()->setProperty("sides",sides);}
}

void CadWorkspacePanel::addSurfaceFields()
{
    const auto p=modelingSettings;const auto begin=[this](const juce::String& role,djehuti::cad::CadSelection::Mode mode)
    {juce::String ignored;const auto current=readModelingForm(ignored);if(current.isObject())modelingSettings=current;clearModelingView();modelingPick=role;if(selection!=nullptr)selection->setMode(mode);showModelingSources();setMessage(role=="seed"?"Click inside the trim region, then choose Keep/Remove and Preview.":"Choose the "+role+" in the tree or model view.",0);};
    const auto slot=[&](const char* key,const char* label,const juce::String& text,const char* role,djehuti::cad::CadSelection::Mode mode=djehuti::cad::CadSelection::Mode::body)
    {form->addSlot(key,label,text.isEmpty()?juce::String("Choose ")+label:text,accentColour,text.isEmpty(),[begin,role,mode]{begin(role,mode);},"Choose a managed source or a named face. Tree selection chooses the entire feature.");};
    const auto number=[&](const char* key,const char* caption,const char* unit,const char* tip){form->addNumber(juce::String("adv_")+key,caption,(double)p[key],unit,tip,12);};
    const auto vector=[&](const char* prefix,const char* label,const juce::var& value,const char* unit)
    {for(int i=0;i<3;++i){const auto letter=juce::String::charToString("XYZ"[i]);form->addNumber(juce::String(prefix)+letter,juce::String(label)+" "+letter,(double)value[i],unit,"World coordinates; vectors are normalized by the geometry command.",12);}};
    if(modelingKind=="surface")
    {
        auto& method=form->addChoice("surfMethod","Surface method",{"Planar boundary","Extruded surface","Revolved surface","Swept surface","Lofted surface"},juce::StringArray{"planar","extrude","revolve","sweep","loft"}.indexOf(p["method"].toString()),"Create a face or open shell; caps and solid conversion are separate operations.");
        method.onChange=[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe==nullptr)return;juce::String ignored;auto values=safe->readModelingForm(ignored);if(values.isObject())safe->modelingSettings=values;safe->clearModelingView();safe->rebuildTaskPanel(false);safe->showModelingSources();});};
        form->addToggle("adv_flipNormals","Flip surface normals",(bool)p["flipNormals"],"Reverse the source-oriented surface normals. Positive and negative thickening follow these normals.");
        const auto kind=p["method"].toString();
        if(kind!="sweep"&&kind!="loft")slot("advProfile","Source profile",p["profile"].toString().isEmpty()?juce::String():featureName(p["profile"].toString()),"profile");
        if(kind=="extrude"){number("distance","Signed distance","mm","Distance along the normalized world direction; the profile may be open or closed.");vector("surfExtrude","Direction",p["direction"],"");}
        if(kind=="revolve")
        {
            const auto axis=p["axis"];form->addChoice("surfAxis","Axis",{"Sketch U","Sketch V","World X","World Y","World Z","Custom"},juce::StringArray{"sketchU","sketchV","worldX","worldY","worldZ","custom"}.indexOf(axis["type"].toString()),"The axis must lie in the sketch plane.");
            for(const auto* key:{"origin","direction"})for(int i=0;i<3;++i){const auto letter=juce::String::charToString("XYZ"[i]);form->addNumber(juce::String("surfAxis")+key+letter,juce::String("Axis ")+key+" "+letter,(double)axis[key][i],juce::String(key)=="origin"?"mm":"","Used by Custom axis.",12);}
            number("angleDegrees","Revolution angle","deg","Partial or full revolution, greater than zero and at most 360 degrees.");form->addToggle("adv_reverse","Reverse revolution",(bool)p["reverse"],"Reverse the axis direction.");
        }
        if(kind=="sweep"||kind=="loft")return; // The shared solid-tool form supplies the identical path/section controls.
    }
    else if(modelingKind=="surface_sew")
    {
        slot("surfSources","Surfaces / faces",juce::String(p["sources"].size())+" selected","sources",djehuti::cad::CadSelection::Mode::face);
        for(int i=0;i<p["sources"].size();++i)
        {
            const auto ref=p["sources"][i];const auto id=ref.isObject()?ref["feature"].toString():ref.toString();form->addNote({},featureName(id)+(ref.isObject()?" — "+ref["face"].toString():juce::String()),2);
            form->addButton("surfRemove"+juce::String(i),"Remove source",[this,i]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),i]{if(safe==nullptr)return;juce::String ignored;auto values=safe->readModelingForm(ignored);if(!values.isObject())return;values["sources"].getArray()->remove(i);safe->modelingSettings=values;safe->clearModelingView();safe->rebuildTaskPanel(false);safe->showModelingSources();});},"Remove this reference from the sewing operation.");
        }
        number("tolerance","Sew tolerance","mm","Engineering tolerance: 0.0000001 to 0.1 mm. Sewing preserves an open shell and never automatically solidifies it.");
    }
    else
    {
        const auto id=p["target"].toString();slot("advTarget","Surface / shell",id.isEmpty()?juce::String():featureName(id)+(p["face"].toString().isEmpty()?juce::String():" — "+p["face"].toString()),"target",modelingKind=="surface_solidify"?djehuti::cad::CadSelection::Mode::body:djehuti::cad::CadSelection::Mode::face);
        if(modelingKind=="surface_trim")
        {
            const auto tool=p["tool"];slot("surfTool","Trim boundary / tool",tool["sketch"].toString().isNotEmpty()?featureName(tool["sketch"].toString()):tool["feature"].toString().isNotEmpty()?featureName(tool["feature"].toString()):juce::String(),"tool");
            form->addButton("surfPickRegion","Pick trim region...",[begin]{begin("seed",djehuti::cad::CadSelection::Mode::face);},"Click the region interior; the exact surface point is stored in world coordinates.");
            vector("surfSeed","Region point",p["seed"],"mm");form->addToggle("adv_keep","Keep selected region",(bool)p["keep"],"Enabled keeps the region containing the point; disabled removes it.");
        }
        if(modelingKind=="surface_extend")
        {
            number("distance","Extension distance","mm","Natural analytic continuation or C1 bounded-surface continuation. Only rectangular U/V boundaries are eligible.");
            for(int i=0;i<4;++i)form->addToggle("surfSide"+juce::String(i),juce::StringArray{"U minimum","U maximum","V minimum","V maximum"}[i],(bool)p["sides"][i],"Extend this surface-parameter boundary by the specified distance.");
        }
        if(modelingKind=="surface_thicken")
        {number("thickness","Thickness","mm","Positive magnitude, measured along oriented surface normals. Invalid/self-intersecting offsets fail atomically.");form->addChoice("surfDirection","Thickness direction",{"Positive normal","Negative normal","Symmetric"},juce::StringArray{"positive","negative","symmetric"}.indexOf(p["direction"].toString()),"Symmetric places half the thickness on each side.");}
    }
    form->addButton("modelingPreview","Preview",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->previewModeling();});},"Show an isolated surface/solid draft using the shared command.");
    form->addButton("modelingClearPreview","Clear preview",[this]{clearModelingView();showModelingSources();validateTask();},"Restore the live model for source selection.");
}

bool CadWorkspacePanel::pickSurface(const djehuti::cad::CadPick& pick)
{
    if(modelingPick!="target"&&modelingPick!="sources"&&modelingPick!="tool"&&modelingPick!="seed")return false;
    juce::String ignored;const auto current=readModelingForm(ignored);if(current.isObject())modelingSettings=current;auto* o=modelingSettings.getDynamicObject();
    const auto* f=service->document().find(pick.featureId);if(f==nullptr)return true;
    djehuti::cad::CadFaceInfo info;juce::String error;const bool face=pick.face>=0&&service->document().describeFace(pick.featureId,pick.face,info,error)&&info.token.isNotEmpty();
    if(modelingPick=="seed")
    {
        if(pick.featureId!=modelingSettings["target"].toString()||!face||!pick.hasPoint){showError("Click a point on the target surface in a model view, or enter its world coordinates.");return true;}
        int i=0;TopoDS_Face target;for(TopExp_Explorer e(f->shape,TopAbs_FACE);e.More();e.Next(),++i)if(i==pick.face)target=TopoDS::Face(e.Current());
        BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(gp_Pnt(pick.point[0],pick.point[1],pick.point[2])).Vertex(),target);distance.Perform();
        if(!distance.IsDone()||distance.NbSolution()<1){showError("The picked region point could not be projected onto the surface.");return true;}const auto point=distance.PointOnShape2(1);o->setProperty("seed",juce::Array<juce::var>{point.X(),point.Y(),point.Z()});
    }
    else if(modelingPick=="tool")
    {
        auto tool=juce::var(new juce::DynamicObject());tool.getDynamicObject()->setProperty(f->type=="sketch"?"sketch":"feature",f->id);if(face)tool.getDynamicObject()->setProperty("face",info.token);o->setProperty("tool",tool);
    }
    else
    {
        if(f->type=="sketch"||(!face&&!TopExp_Explorer(f->shape,TopAbs_FACE).More())){showError("Choose a surface, shell or named face.");return true;}
        if(modelingPick=="sources")
        {
            juce::var ref=f->id;if(face){auto* value=new juce::DynamicObject();value->setProperty("feature",f->id);value->setProperty("face",info.token);ref=value;}
            auto list=modelingSettings["sources"];for(const auto& value:*list.getArray())if(juce::JSON::toString(value,true)==juce::JSON::toString(ref,true))return true;list.append(ref);o->setProperty("sources",list);
        }
        else {o->setProperty("target",f->id);if(modelingSettings.hasProperty("face"))o->setProperty("face",face?info.token:juce::String());}
    }
    clearModelingView();rebuildTaskPanel(false);showModelingSources();return true;
}

void CadWorkspacePanel::editLoftSection(int index,int action)
{
    juce::String ignored;auto p=readModelingForm(ignored);if(!p.isObject()||index<0||index>=p["sections"].size())return;const int next=index+action;if(action!=0&&(next<0||next>=p["sections"].size()))return;
    for(const auto* key:{"sections","offsets","reverseSections","seamAngles"})if(auto* values=p[key].getArray()){if(action==0)values->remove(index);else values->swap(index,next);}modelingSettings=p;clearModelingView();rebuildTaskPanel(false);showModelingSources();
}

void CadWorkspacePanel::pickModeling(const djehuti::cad::CadPick& pick)
{
    if(!pick.valid()||pick.featureId==editingModelingId)return;if(djehuti::cad::isSurfaceType(modelingKind)&&pickSurface(pick))return;juce::String ignored;const auto current=readModelingForm(ignored);if(current.isObject())modelingSettings=current;auto* o=modelingSettings.getDynamicObject();const auto* sketch=service->document().findSketch(pick.featureId);
    if(modelingPick=="profile"||modelingPick=="region"||modelingPick=="sections")
    {
        if(sketch==nullptr){showError("Choose a sketch in the feature tree.");return;}
        if(modelingPick=="sections"){auto list=modelingSettings["sections"];for(const auto& id:*list.getArray())if(id.toString()==pick.featureId)return;list.append(pick.featureId);o->setProperty("sections",list);for(const auto* key:{"offsets","reverseSections","seamAngles"}){auto values=modelingSettings[key];values.append(juce::String(key)=="reverseSections"?juce::var(false):juce::var(0));o->setProperty(key,values);}}
        else o->setProperty(modelingPick,pick.featureId);
    }
    else if(modelingPick=="target")
    {if(!isSolid(pick.featureId)){showError("Choose an existing solid target.");return;}o->setProperty("target",pick.featureId);if(modelingKind=="mill")defaultMillFace(pick.featureId);}
    else if(modelingPick=="reference")
    {djehuti::cad::CadFaceInfo face;juce::String error;if(pick.face<0||!service->document().describeFace(pick.featureId,pick.face,face,error)||!face.planar||face.token.isEmpty()){showError("Choose a persistently named planar entry face.");return;}auto* r=new juce::DynamicObject();r->setProperty("type","face");r->setProperty("feature",pick.featureId);r->setProperty("face",face.token);o->setProperty("reference",r);if(modelingSettings["target"].toString().isEmpty())o->setProperty("target",pick.featureId);}
    else if(modelingPick=="path")
    {
        if(sketch!=nullptr){auto* path=new juce::DynamicObject();path->setProperty("sketch",pick.featureId);path->setProperty("reverse",false);o->setProperty("path",path);}
        else if(pick.edge>=0)
        {auto* args=new juce::DynamicObject();args->setProperty("action","edges");args->setProperty("target",pick.featureId);const auto list=service->chamferCommand(args);if(!(bool)list["ok"]||pick.edge>=list["edges"].size())return;auto path=modelingSettings["path"];if(path["feature"].toString()!=pick.featureId){path=juce::var(new juce::DynamicObject());path.getDynamicObject()->setProperty("feature",pick.featureId);path.getDynamicObject()->setProperty("edges",juce::Array<juce::var>());path.getDynamicObject()->setProperty("reverse",false);}auto edges=path["edges"];const auto token=list["edges"][pick.edge]["reference"];bool present=false;for(const auto& v:*edges.getArray())if(v==token)present=true;if(!present)edges.append(token);path.getDynamicObject()->setProperty("edges",edges);o->setProperty("path",path);}
        else {showError("Choose a path sketch or a model edge.");return;}
    }
    clearModelingView();rebuildTaskPanel(false);showModelingSources();
}

void CadWorkspacePanel::previewModeling()
{
    juce::String error;const auto p=readModelingForm(error);if(error.isNotEmpty()){showError(error);return;}auto* args=new juce::DynamicObject();args->setProperty("action","preview");args->setProperty("id",editingModelingId);args->setProperty("parameters",p);juce::var details;const auto view=service->document().modelingPreview(modelingKind,args,details,error);if(view==nullptr){showError(error);return;}modelView.setSnapshot(view);modelingPreviewActive=true;wakeFrames();setMessage(djehuti::cad::isSurfaceType(modelingKind)?"Preview: "+details["topology"]["faces"].toString()+" faces, "+details["topology"]["unmatchedEdges"].toString()+" unmatched boundaries. Apply commits; Cancel restores the model.":"Preview. Apply commits; changing inputs or Cancel restores the model.",0);
}

void CadWorkspacePanel::clearReplicationView()
{
    if(!replicationPreviewActive&&!replicationSourcesActive)return;
    replicationPreviewActive=false;replicationSourcesActive=false;modelView.setSnapshot(service->snapshot());wakeFrames();
}

void CadWorkspacePanel::showReplicationSources()
{
    if(task!=Task::replication)return;
    const auto ids=djehuti::cad::replicationDependencies(replicationSettings);
    const auto all=service->document().snapshot(true);auto view=std::make_shared<djehuti::cad::CadRenderSnapshot>(*service->snapshot());
    std::erase_if(view->meshes,[&](const auto& m){return m.featureId==editingReplicationId||ids.contains(m.featureId);});
    std::erase_if(view->edges,[&](const auto& e){return e.featureId==editingReplicationId||ids.contains(e.featureId);});
    for(const auto& m:all->meshes)if(ids.contains(m.featureId))view->meshes.push_back(m);
    for(const auto& e:all->edges)if(ids.contains(e.featureId))view->edges.push_back(e);
    view->revision|=(std::uint64_t(1)<<62);modelView.setSnapshot(view);replicationSourcesActive=true;wakeFrames();
}

juce::var CadWorkspacePanel::readReplicationForm(juce::String& error) const
{
    auto p=juce::JSON::parse(juce::JSON::toString(replicationSettings,false,17));if(form==nullptr||form->control("repOperation")==nullptr)return p;
    const juce::StringArray modes{"bodies","feature","add","remove"};const int index=form->choice("repOperation");
    p.getDynamicObject()->setProperty("operation",modes[juce::jlimit(0,3,index)]);p.getDynamicObject()->setProperty("includeSource",form->toggle("repInclude"));
    const auto defaults=djehuti::cad::replicationDefaults(replicationKind);
    for(const auto& prop:defaults.getDynamicObject()->getProperties())
    {
        const auto key=prop.name.toString();if(form->control("rep_"+key)==nullptr)continue;
        if(prop.value.isBool())p.getDynamicObject()->setProperty(key,form->toggle("rep_"+key));
        else if(prop.value.isInt()||prop.value.isDouble())
        {if(!form->numberValid("rep_"+key)){error=key+" must be numeric.";return {};}p.getDynamicObject()->setProperty(key,form->number("rep_"+key));}
    }
    for(const auto& key:juce::StringArray{"plane","direction1","direction2","axis"})if(form->control(key+"Type")!=nullptr)
    {
        const juce::StringArray kinds=key=="plane"?juce::StringArray{"XY","XZ","YZ","custom","face"}:juce::StringArray{"X","Y","Z","custom","edge","face"};
        const auto kind=kinds[form->choice(key+"Type")];auto old=p.getProperty(key,juce::var());auto r=juce::var(new juce::DynamicObject());r.getDynamicObject()->setProperty("type",kind);
        if(kind=="custom")
        {
            juce::Array<juce::var> origin,vector;
            for(const auto* component:{"X","Y","Z"})
            {if(!form->numberValid(key+"O"+component)||!form->numberValid(key+"D"+component)){error="Reference coordinates must be finite numbers.";return {};}origin.add(form->number(key+"O"+component));vector.add(form->number(key+"D"+component));}
            r.getDynamicObject()->setProperty("origin",origin);r.getDynamicObject()->setProperty("vector",vector);
        }
        if(kind=="edge"||kind=="face")for(const auto* field:{"feature","face","reference"})if(old.hasProperty(field))r.getDynamicObject()->setProperty(field,old[field]);
        p.getDynamicObject()->setProperty(key,r);
    }
    djehuti::cad::validateReplication(replicationKind,p,error);return p;
}

void CadWorkspacePanel::addReplicationFields()
{
    const auto p=replicationSettings;
    const auto beginPick=[this](const juce::String& role,djehuti::cad::CadSelection::Mode mode)
    {
        juce::String ignored;const auto current=readReplicationForm(ignored);if(current.isObject())replicationSettings=current;
        clearReplicationView();replicationPick=role;if(selection!=nullptr)selection->setMode(mode);showReplicationSources();
        setMessage(role=="sources"?"Click source bodies in the tree or view to add them.":"Pick the "+role+" reference in the view.",0);
    };
    juce::StringArray names;if(const auto* sources=p["sources"].getArray())for(const auto& id:*sources)names.add(featureName(id.toString()));
    form->addSlot("repSources","Sources",names.isEmpty()?"Pick source bodies":names.joinIntoString(", "),accentColour,names.isEmpty(),[beginPick]{beginPick("sources",djehuti::cad::CadSelection::Mode::body);},"Click to pick and add source bodies.");
    form->addButton("repClearSources","Clear sources",[this]{juce::String ignored;const auto current=readReplicationForm(ignored);if(current.isObject())replicationSettings=current;replicationSettings.getDynamicObject()->setProperty("sources",juce::Array<juce::var>());replicationPick="sources";rebuildTaskPanel(false);},"Replace the source selection.");
    const juce::StringArray modes{"bodies","feature","add","remove"};form->addChoice("repOperation","Operation",{"Bodies","Modeling feature","Add solids to target","Cut solids from target"},modes.indexOf(p["operation"].toString()),"Feature repeats Hole, Cut, Subtract or Union on its base target; add/remove uses source solids as tools.");
    form->addSlot("repTarget","Target",p["target"].toString().isEmpty()?"Pick target (feature mode defaults to base)":featureName(p["target"].toString()),targetColour,false,[beginPick]{beginPick("target",djehuti::cad::CadSelection::Mode::body);},"Choose the target for feature, add or cut mode.");
    form->addButton("repAutoTarget","Follow source target",[this]{juce::String ignored;const auto current=readReplicationForm(ignored);if(current.isObject())replicationSettings=current;replicationSettings.getDynamicObject()->setProperty("target","");rebuildTaskPanel(false);},"In Modeling feature mode, follow the source operation's target, including later target changes.");
    form->addToggle("repInclude",replicationKind=="mirror"?"Retain original":"Include source position",(bool)p["includeSource"],"Position zero counts toward the requested count. Disabling it excludes that position.");
    const auto defaults=djehuti::cad::replicationDefaults(replicationKind);
    for(const auto& key:juce::StringArray{"plane","direction1","direction2","axis"})if(defaults.hasProperty(key))
    {
        form->addHeader(key=="plane"?"Mirror plane":key=="axis"?"Rotation axis":key=="direction1"?"First direction":"Second direction");
        const auto r=p.getProperty(key,juce::var());const juce::StringArray kinds=key=="plane"?juce::StringArray{"XY","XZ","YZ","custom","face"}:juce::StringArray{"X","Y","Z","custom","edge","face"};
        form->addChoice(key+"Type","Reference",kinds,kinds.indexOf(r["type"].toString()),"Principal datum, numeric custom reference, or a picked persistent reference.");
        form->addButton(key+"Pick","Pick face / edge...",[this,key,beginPick]{const int selected=form->choice(key+"Type");const bool edge=key!="plane"&&selected==4;beginPick(key,edge?djehuti::cad::CadSelection::Mode::edge:djehuti::cad::CadSelection::Mode::face);},"Choose face or edge in Reference first, then pick it in the view.");
        form->addNote(key+"Picked",r["face"].toString().isNotEmpty()?r["face"].toString():r["reference"].toString().isNotEmpty()?featureName(r["feature"].toString())+" edge":"No picked reference",1);
        const auto o=r.getProperty("origin",juce::Array<juce::var>{0,0,0}),d=r.getProperty("vector",juce::Array<juce::var>{0,0,1});
        for(int i=0;i<3;++i){const auto letter=juce::String::charToString("XYZ"[i]);form->addNumber(key+"O"+letter,"Origin "+letter,(double)o[i],"mm","Custom plane/axis origin in world coordinates.",12);form->addNumber(key+"D"+letter,"Vector "+letter,(double)d[i],{},"Custom plane normal or direction; any nonzero length.",12);}
    }
    for(const auto& field:std::vector<std::pair<juce::String,juce::String>>{{"count1","First count"},{"count2","Second count"},{"spacing1","First spacing"},{"spacing2","Second spacing"},{"count","Instance count"},{"angleDegrees","Total angle"}})
    {const auto& key=field.first;if(defaults.hasProperty(key))form->addNumber("rep_"+key,field.second,(double)p.getProperty(key,juce::var()),key.startsWith("spacing")?"mm":key=="angleDegrees"?"deg":"","Counts include position zero; spacing is per step. Full circles omit a duplicated endpoint.",12);}
    for(const auto& field:std::vector<std::pair<juce::String,juce::String>>{{"reverse1","Reverse first direction"},{"reverse2","Reverse second direction"},{"reverse","Reverse rotation"}})
    {const auto& key=field.first;if(defaults.hasProperty(key))form->addToggle("rep_"+key,field.second,(bool)p.getProperty(key,juce::var()),"Reverse this distribution direction.");}
    form->addButton("repPreview","Preview",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->previewReplication();});},"Validate and preview without changing the live document or Undo history.");
    form->addButton("repClearPreview","Clear preview",[this]{clearReplicationView();showReplicationSources();validateTask();},"Restore sources for picking.");
}

void CadWorkspacePanel::pickReplication(const djehuti::cad::CadPick& pick)
{
    if(!pick.valid()||pick.featureId==editingReplicationId)return;
    juce::String ignored;const auto current=readReplicationForm(ignored);if(current.isObject())replicationSettings=current;auto* o=replicationSettings.getDynamicObject();
    if(replicationPick=="sources")
    {if(!isSolid(pick.featureId)){showError("Choose a solid source.");return;}auto sources=replicationSettings["sources"];bool present=false;for(const auto& id:*sources.getArray())if(id.toString()==pick.featureId)present=true;if(!present)sources.append(pick.featureId);o->setProperty("sources",sources);}
    else if(replicationPick=="target"){if(!isSolid(pick.featureId)){showError("Choose a solid target.");return;}o->setProperty("target",pick.featureId);}
    else
    {
        auto r=juce::var(new juce::DynamicObject());r.getDynamicObject()->setProperty("feature",pick.featureId);
        if(pick.face>=0)
        {djehuti::cad::CadFaceInfo f;juce::String error;if(!service->document().describeFace(pick.featureId,pick.face,f,error)||!f.planar||f.token.isEmpty()){showError("Choose a persistently named planar face.");return;}r.getDynamicObject()->setProperty("type","face");r.getDynamicObject()->setProperty("face",f.token);}
        else if(pick.edge>=0&&replicationPick!="plane")
        {auto* a=new juce::DynamicObject();a->setProperty("action","edges");a->setProperty("target",pick.featureId);const auto list=service->chamferCommand(a);if(!(bool)list["ok"]||pick.edge>=list["edges"].size())return;r.getDynamicObject()->setProperty("type","edge");r.getDynamicObject()->setProperty("reference",list["edges"][pick.edge]["reference"]);}
        else return;o->setProperty(replicationPick,r);
    }
    clearReplicationView();rebuildTaskPanel(false);showReplicationSources();
}

void CadWorkspacePanel::previewReplication()
{
    juce::String error;const auto p=readReplicationForm(error);if(error.isNotEmpty()){showError(error);return;}
    auto* a=new juce::DynamicObject();a->setProperty("action","preview");a->setProperty("id",editingReplicationId);a->setProperty("parameters",p);juce::var details;
    const auto view=service->document().replicationPreview(replicationKind,a,details,error);if(view==nullptr){showError(error);return;}
    modelView.setSnapshot(view);replicationPreviewActive=true;wakeFrames();setMessage(djehuti::cad::isSurfaceType(modelingKind)?"Preview: "+details["topology"]["faces"].toString()+" faces, "+details["topology"]["unmatchedEdges"].toString()+" unmatched boundaries. Apply commits; Cancel restores the model.":"Preview. Apply commits; changing inputs or Cancel restores the model.",0);
}

void CadWorkspacePanel::addHoleFields(const djehuti::cad::CadHoleParameters& p,const juce::String& target)
{
    form->addNote({},target.isEmpty()?"Choose a planar entry face in Face mode.":"Entry face on "+featureName(target)+". Position is measured in the face U/V frame; drilling goes into the body.",3);
    form->addNumber("holeU","Position U",p.u,"mm","Coordinate along the support face's local U direction.",12);
    form->addNumber("holeV","Position V",p.v,"mm","Coordinate along the support face's local V direction.",12);
    const juce::StringArray styles{"plain","counterbore","countersink"};form->addChoice("holeStyle","Hole type",{"Plain","Counterbore","Countersink"},styles.indexOf(p.style),"Plain hole, cylindrical counterbore, or conical countersink.");
    form->addNumber("holeDiameter","Diameter",p.diameter,"mm","Shaft diameter.",12);
    form->addToggle("holeThroughAll","Through all",p.throughAll,"Follow the target's full extent in the drilling direction after parameter edits.");
    form->addNumber("holeDepth","Blind depth",p.depth,"mm","Finite flat-bottom depth measured from the entry face.",12);
    form->addNumber("holeHeadDiameter","Head diameter",p.headDiameter,"mm","Counterbore or countersink entry diameter; larger than the shaft.",12);
    form->addNumber("holeHeadDepth","Counterbore depth",p.headDepth,"mm","Depth of the larger cylindrical recess.",12);
    form->addNumber("holeAngle","Included angle",p.countersinkAngleDegrees,"deg","Countersink included angle, greater than 0 and less than 180 degrees.",12);
    form->addNote({},"The centre must lie in face material. Use U/V to move away from edges or existing openings. Blind holes have a flat bottom; countersink depth follows its diameters and angle.",4);
}

juce::var CadWorkspacePanel::readHoleForm(juce::String& error) const
{
    const auto* feature=service->document().find(selectedId);const bool body=task==Task::body&&feature!=nullptr&&feature->type=="hole";
    auto p=body?feature->hole:holeSettings;
    const juce::StringArray styles{"plain","counterbore","countersink"};const int style=form->choice("holeStyle");
    if(!juce::isPositiveAndBelow(style,styles.size())){error="Choose a hole type.";return {};}
    p.style=styles[style];p.throughAll=form->toggle("holeThroughAll");
    for(const auto& item:std::vector<std::pair<const char*,double*>>{{"holeU",&p.u},{"holeV",&p.v},{"holeDiameter",&p.diameter},{"holeDepth",&p.depth},{"holeHeadDiameter",&p.headDiameter},{"holeHeadDepth",&p.headDepth},{"holeAngle",&p.countersinkAngleDegrees}})
    {
        if(form->rowVisible(item.first)&&!form->numberValid(item.first)){error="Hole coordinates and dimensions must be finite numbers.";return {};}
        if(form->numberValid(item.first))*item.second=form->number(item.first);
    }
    auto result=djehuti::cad::holeParametersToJson(p);result.getDynamicObject()->setProperty("target",body?feature->targetFeatureId:targetId);return result;
}

void CadWorkspacePanel::clearLatheView()
{
    if(!latheViewActive)return;latheViewActive=false;lathePreviewWanted=false;modelView.setSnapshot(service->snapshot());wakeFrames();
}

void CadWorkspacePanel::chooseLatheProfile(const juce::String& sketch,const juce::String& entity)
{
    if(service->document().findSketch(sketch)==nullptr)return;
    juce::String ignored;const auto draft=readLatheForm(ignored);if(draft.isObject())djehuti::cad::readRevolveParameters(draft,latheSettings,ignored);
    if(profileId!=sketch){profileId=sketch;latheSettings.entities.clear();}
    const auto contours=service->document().latheContours(sketch);
    if(entity.isNotEmpty())
    {
        int found=-1,matches=0;for(int i=0;i<contours.size();++i)for(const auto& id:*contours[i]["entities"].getArray())if(id.toString()==entity){found=i;++matches;break;}
        if(matches>0)latheSettings.entities.clear();if(matches==1)for(const auto& member:*contours[found]["entities"].getArray())latheSettings.entities.add(member.toString());
    }
    rebuildTaskPanel(false);updateLatheView();
}

void CadWorkspacePanel::pickLatheProfile(const djehuti::cad::CadPick& pick)
{
    const auto* sketch=service->document().findSketch(pick.featureId);if(sketch==nullptr)return;juce::String nearest;
    if(pick.hasPoint)
    {
        djehuti::cad::CadSketchFrame frame;if(service->document().sketchFrame(sketch->id,frame))
        {
            const gp_Vec d(frame.origin,gp_Pnt(pick.point[0],pick.point[1],pick.point[2]));const auto u=d.Dot(gp_Vec(frame.u)),v=d.Dot(gp_Vec(frame.v));double best=1e100;
            for(const auto& e:sketch->entities)
            {
                double distance=1e100;
                if(e.type=="circle")distance=std::abs(std::hypot(u-e.x1,v-e.y1)-e.radius);
                else
                {
                    auto x=e.x1,y=e.y1;const int steps=e.type=="bezier"?64:1;
                    for(int i=1;i<=steps;++i){const auto t=(double)i/steps,a=1-t;const auto nx=e.type=="bezier"?a*a*a*e.x1+3*a*a*t*e.cx1+3*a*t*t*e.cx2+t*t*t*e.x2:e.x2,ny=e.type=="bezier"?a*a*a*e.y1+3*a*a*t*e.cy1+3*a*t*t*e.cy2+t*t*t*e.y2:e.y2;const auto dx=nx-x,dy=ny-y,den=dx*dx+dy*dy,q=den>0?juce::jlimit(0.0,1.0,((u-x)*dx+(v-y)*dy)/den):0;distance=std::min(distance,std::hypot(u-x-q*dx,v-y-q*dy));x=nx;y=ny;}
                }
                if(distance<best){best=distance;nearest=e.id;}
            }
        }
    }
    chooseLatheProfile(sketch->id,nearest);
}

void CadWorkspacePanel::addLatheFields()
{
    juce::StringArray names;latheSketchIds.clear();for(const auto& f:service->document().features())if(f.type=="sketch"){latheSketchIds.add(f.id);names.add(f.name+" ["+f.id+"]");}
    auto& profile=form->addChoice("latheProfile","Sketch",names,latheSketchIds.indexOf(profileId),"Choose an existing sketch, or pick its contour in the model tree or viewport.");
    profile.onChange=[this]{const auto i=form->choice("latheProfile");if(juce::isPositiveAndBelow(i,latheSketchIds.size())){const auto id=latheSketchIds[i];postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),id]{if(safe!=nullptr)safe->chooseLatheProfile(id);});}};
    latheContourChoices=service->document().latheContours(profileId);juce::StringArray labels;int selected=-1;
    for(int i=0;i<latheContourChoices.size();++i)
    {
        const auto c=latheContourChoices[i];labels.add(c["label"].toString());juce::StringArray ids;for(const auto& id:*c["entities"].getArray())ids.add(id.toString());
        auto stored=latheSettings.entities;stored.sort(false);if(stored==ids)selected=i;
    }
    if(selected<0&&latheContourChoices.size()==1)selected=0;
    auto& contour=form->addChoice("latheContour","Closed contour",labels,selected,"Only the selected contour is revolved; other sketch entities remain unchanged.");
    contour.onChange=[this]{if(form!=nullptr){juce::String e;const auto p=readLatheForm(e);if(p.isObject())djehuti::cad::readRevolveParameters(p,latheSettings,e);validateTask();updateLatheView();}};
    auto& axis=form->addChoice("latheAxis","Rotation axis",{"X-axis","Y-axis","Z-axis","Sketch line"},juce::StringArray{"worldX","worldY","worldZ","sketchLine"}.indexOf(latheSettings.axisType),"World axes pass through the document origin; a line axis follows its referenced sketch geometry.");
    juce::StringArray lines;latheAxisSketchIds.clear();latheAxisEntityIds.clear();int chosen=-1;
    for(const auto& f:service->document().features())if(const auto* sketch=service->document().findSketch(f.id))for(const auto& e:sketch->entities)if(e.type=="line")
    {if(f.id==latheSettings.axisSketch&&e.id==latheSettings.axisEntity)chosen=lines.size();lines.add(f.name+" / "+e.id);latheAxisSketchIds.add(f.id);latheAxisEntityIds.add(e.id);}
    auto& line=form->addChoice("latheAxisLine","Axis line",lines,chosen,"A nonzero straight sketch/construction line, coplanar with the profile.");
    const auto changed=[this]{if(form==nullptr)return;form->setRowVisible("latheAxisLine",form->choice("latheAxis")==3);validateTask();updateLatheView();resized();};axis.onChange=changed;line.onChange=changed;
    form->setRowVisible("latheAxisLine",latheSettings.axisType=="sketchLine");
    form->addNumber("latheAngle","Angle",latheSettings.angleDegrees,"deg","Greater than 0, up to 360 degrees. A partial turn includes its end faces.",12);
    form->addToggle("latheReverse","Reverse rotation",latheSettings.reverse,"Reverse the right-hand rotation about the selected axis.");
    form->addButton("lathePreview","Preview",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr){safe->lathePreviewWanted=true;safe->updateLatheView();}});},"Evaluate the shared command in an isolated document. Axis/angle changes update an active preview.");
    form->addButton("latheClearPreview","Clear preview",[this]{lathePreviewWanted=false;updateLatheView();},"Show the source sketch and axis without the candidate solid.");
    form->addNote("latheAxisStatus","The displayed axis is in document coordinates. The source sketch remains editable.",2);
    if(editingLatheId.isNotEmpty())form->addButton("latheSketch","Edit source sketch",[this,sketch=profileId]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),sketch]{if(safe!=nullptr){safe->clearLatheView();safe->enterSketch(sketch);}});},"Edit the authoritative source profile; the lathe regenerates through its dependencies.");
}

juce::var CadWorkspacePanel::readLatheForm(juce::String& error) const
{
    if(form==nullptr||profileId.isEmpty()){error="Select an existing sketch profile.";return {};}
    auto p=latheSettings;const auto contour=form->choice("latheContour");
    if(!juce::isPositiveAndBelow(contour,latheContourChoices.size())){error="Select one closed contour in the sketch.";return {};}
    p.entities.clear();for(const auto& id:*latheContourChoices[contour]["entities"].getArray())p.entities.add(id.toString());
    if(!form->numberValid("latheAngle")||form->number("latheAngle")<=0||form->number("latheAngle")>360){error="Lathe angle must be greater than 0 and no greater than 360 degrees.";return {};}
    p.angleDegrees=form->number("latheAngle");p.reverse=form->toggle("latheReverse");
    const auto axis=form->choice("latheAxis");if(!juce::isPositiveAndBelow(axis,4)){error="Choose a document axis or sketch line.";return {};}
    p.axisType=juce::StringArray{"worldX","worldY","worldZ","sketchLine"}[axis];
    if(axis==3){const auto i=form->choice("latheAxisLine");if(!juce::isPositiveAndBelow(i,latheAxisEntityIds.size())){error="Choose a straight sketch line as the axis.";return {};}p.axisSketch=latheAxisSketchIds[i];p.axisEntity=latheAxisEntityIds[i];}
    gp_Pnt origin;gp_Dir direction;if(!service->document().revolveAxis(profileId,p,origin,direction,error))return {};
    auto result=djehuti::cad::revolveParametersToJson(p);result.getDynamicObject()->setProperty("sketch",profileId);return result;
}

void CadWorkspacePanel::updateLatheView()
{
    if(task!=Task::lathe)return;juce::String error;const auto p=readLatheForm(error);
    auto view=std::make_shared<djehuti::cad::CadRenderSnapshot>(*service->snapshot());
    std::erase_if(view->meshes,[&](const auto& m){return m.featureId==editingLatheId;});std::erase_if(view->edges,[&](const auto& e){return e.featureId==editingLatheId;});
    if(p.isObject())
    {
        djehuti::cad::readRevolveParameters(p,latheSettings,error);
        if(lathePreviewWanted)
        {
            auto* args=new juce::DynamicObject();args->setProperty("action","preview");args->setProperty("id",editingLatheId);args->setProperty("parameters",p);juce::var details;
            if(const auto preview=service->document().lathePreview(args,details,error))view=std::make_shared<djehuti::cad::CadRenderSnapshot>(*preview);
        }
        gp_Pnt origin;gp_Dir direction;if(service->document().revolveAxis(profileId,latheSettings,origin,direction,error))
        {
            double span=1;if(const auto* sketch=service->document().findSketch(profileId))for(const auto& e:sketch->entities)span=std::max({span,std::abs(e.x1),std::abs(e.y1),std::abs(e.x2),std::abs(e.y2),e.radius});
            gp_Pnt centre=origin;djehuti::cad::CadSketchFrame frame;if(service->document().sketchFrame(profileId,frame))
            {double u=0,v=0,count=0;if(const auto* sketch=service->document().findSketch(profileId))for(const auto& e:sketch->entities)if(latheSettings.entities.contains(e.id)){u+=e.x1+e.x2;v+=e.y1+e.y2;count+=2;}if(count>0){const auto profileCentre=frame.origin.Translated(gp_Vec(frame.u)*(u/count)+gp_Vec(frame.v)*(v/count));centre=origin.Translated(gp_Vec(direction)*gp_Vec(origin,profileCentre).Dot(gp_Vec(direction)));}}
            const auto a=centre.Translated(gp_Vec(direction)*(-1.2*span)),b=centre.Translated(gp_Vec(direction)*(1.2*span));djehuti::cad::CadPolyline axis;axis.featureId="__lathe_axis";axis.points={(float)a.X(),(float)a.Y(),(float)a.Z(),(float)b.X(),(float)b.Y(),(float)b.Z()};view->sketchOutlines.push_back(std::move(axis));
        }
    }
    const auto all=service->document().snapshot(true);for(const auto& outline:all->sketchOutlines)if(std::none_of(view->sketchOutlines.begin(),view->sketchOutlines.end(),[&](const auto& e){return e.featureId==outline.featureId;}))view->sketchOutlines.push_back(outline);
    view->documentInstance=service->document().documentInstance();view->revision=(std::uint64_t(1)<<61)|++latheViewSerial;modelView.setSnapshot(view);latheViewActive=true;wakeFrames();
    if(error.isNotEmpty()&&lathePreviewWanted)showError(error);
    else if(lathePreviewWanted)setMessage("Preview updated. Turn part commits; Cancel leaves the document unchanged.",0);
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
    form->onChange = [this] { if(task==Task::lathe){validateTask();updateLatheView();return;} if(modelingPreviewActive){clearModelingView();showModelingSources();} if(replicationPreviewActive){clearReplicationView();showReplicationSources();} clearChamferPreview(true); validateTask(); };
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
                if(!f->lathe)addRevolveFields(f->revolve, f->sketchId);
                if(f->lathe)form->addButton("latheProfile","Lathe profile...",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->startTask(Task::lathe);});},"Edit the source contour, rotation axis and angle; preview before applying.");
                form->addButton("revolveSketch","Edit source sketch",[this,sketch=f->sketchId]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),sketch]{if(safe!=nullptr)safe->enterSketch(sketch);});},"Edit the authoritative 2D profile.");
            }
            else if(djehuti::cad::isModelingType(f->type))
            {
                form->addHeader(djehuti::cad::modelingLabel(f->type));
                if(djehuti::cad::isSurfaceType(f->type)){const auto topology=djehuti::cad::surfaceTopology(f->shape);form->addNote({},topology["faces"].toString()+" faces | "+topology["unmatchedEdges"].toString()+" unmatched boundaries | "+(bool(topology["closed"])?"Closed":"Open"),2);}
                form->addButton("modelingEdit","Edit sources and parameters...",[this,type=f->type]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),type]{if(safe!=nullptr)safe->startTask(Task::modeling,type);});},"Edit the parametric sources, settings and operation; preview before applying.");
            }
            else if(djehuti::cad::isReplicationType(f->type))
            {
                form->addHeader(f->type=="mirror"?"Mirror":f->type=="linearPattern"?"Linear Pattern":"Circular Pattern");form->addNote({},"Parametric sources, references and distribution",1);
                form->addButton("repEdit","Edit sources and parameters...",[this,type=f->type]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this),type]{if(safe!=nullptr)safe->startTask(Task::replication,type);});},"Edit the parametric definition, preview and apply.");
            }
            else if(f->type=="hole")
            {
                form->addHeader("Hole / Drill Press");addHoleFields(f->hole,f->targetFeatureId);
                form->addButton("holeEditFace","Edit support face...",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->startTask(Task::hole);});},"Show the source body and select another planar support face.");
            }
            else if(f->type=="chamfer")
            {
                form->addHeader("Chamfer");form->addNumber("chamferDistance","Distance",f->width,"mm","Equal distance on both adjacent faces.",12);
                form->addNote({},juce::String(f->edges.size())+" persistent edge references",1);
                form->addButton("chamferEditEdges","Edit edges...",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->startTask(Task::chamfer);});},"Show the source body and reselect edges.");
                form->addButton("chamferPreview","Preview",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->previewChamfer();});},"Preview without changing the document.");
                form->addButton("chamferClearPreview","Clear preview",[this]{clearChamferPreview(true);validateTask();},"Restore the authored geometry.");
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
            form->addButton("deleteSketchEntities","Delete selected geometry",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->deleteSelectedSketchEntities();});},"Delete selected entities and their constraints; Ctrl+Z restores them.");
            form->addButton("selectTool","Return to Select (Esc)",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->setSketchTool("select");});},"Stop drawing while keeping completed geometry.");
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
            if (circle) form->addNumber("dimension", diameter ? "Diameter" : "Radius", diameter ? entity->radius * 2.0 : entity->radius, "mm", diameter ? "This circle is driven by a diameter constraint." : "Circle radius.", 9);
            else form->addNumber("dimension", "Length", std::hypot(entity->x2 - entity->x1, entity->y2 - entity->y1), "mm", "Change length while keeping the start and direction.",12);
            form->addHeader("Position (" + plane + " plane)");
            const juce::String point = circle ? "Centre " : "Start ";
            form->addNumber("u", point + plane.substring(0, 1), entity->x1, "mm", "Sketch-plane coordinate along " + plane.substring(0, 1) + ".", circle?9:12);
            form->addNumber("v", point + plane.substring(1, 2), entity->y1, "mm", "Sketch-plane coordinate along " + plane.substring(1, 2) + ".", circle?9:12);
            if (!circle)
            {
                instruction="Edit start/end coordinates or direction/length, then Apply. Start-coordinate edits keep the endpoint. Existing constraints remain active.";
                form->addNumber("endU","End "+plane.substring(0,1),entity->x2,"mm","Endpoint along the sketch's positive horizontal axis.",12);
                form->addNumber("endV","End "+plane.substring(1,2),entity->y2,"mm","Endpoint along the sketch's positive vertical axis.",12);
                form->addHeader("Direction and length");
                const auto angle=std::atan2(entity->y2-entity->y1,entity->x2-entity->x1);
                auto& directionEditor=form->addNumber("direction","Direction",angle*(lineAnglesInRadians?1.0:180.0/juce::MathConstants<double>::pi),lineAnglesInRadians?"rad":"deg","Counterclockwise from the positive horizontal sketch axis. Keeps the start and length.",15);
                directionEditor.setCaretPosition(0);
                directionEditor.onFocusLost=[safe=juce::Component::SafePointer<juce::TextEditor>(&directionEditor)]{if(safe!=nullptr)safe->setCaretPosition(0);};
                auto& unit=form->addChoice("angleUnit","Angle units",{"Degrees","Radians"},lineAnglesInRadians?1:0,"Display/edit direction in degrees or radians; remembered as a workspace preference.");
                unit.onChange=[this]
                {
                    const bool radians=form->choice("angleUnit")==1;
                    if(radians!=lineAnglesInRadians)
                    {
                        if(form->numberValid("direction"))
                            if(auto* editor=dynamic_cast<juce::TextEditor*>(form->control("direction")))
                            {editor->setText(juce::String(form->number("direction")*(radians?juce::MathConstants<double>::pi/180.0:180.0/juce::MathConstants<double>::pi),15),false);if(!editor->hasKeyboardFocus(true))editor->setCaretPosition(0);}
                        lineAnglesInRadians=radians;form->setUnit("direction",radians?"rad":"deg");
                    }
                    validateTask();
                };
                for(const auto* key:{"u","v","endU","endV"})
                    if(auto* editor=dynamic_cast<juce::TextEditor*>(form->control(key))) editor->onTextChange=[this]{syncLineProperties(false);};
                for(const auto* key:{"direction","dimension"})
                    if(auto* editor=dynamic_cast<juce::TextEditor*>(form->control(key))) editor->onTextChange=[this]{syncLineProperties(true);};
            }
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
        case Task::modeling:
            title=djehuti::cad::modelingLabel(modelingKind);instruction="Select the source geometry and parameters. Preview validates the result before Apply.";accept=editingModelingId.isEmpty()?"Create":"Update";addModelingFields();break;
        case Task::replication:
            title=replicationKind=="mirror"?"Mirror":replicationKind=="linearPattern"?"Linear Pattern":"Circular Pattern";instruction="Choose source bodies or a modeling feature, then references and distribution. Preview before applying.";accept=editingReplicationId.isEmpty()?"Create":"Update";addReplicationFields();break;
        case Task::hole:
            title="Drill Press / Hole";instruction="Pick a planar face, then set its U/V position, diameter and drilling depth.";accept=editingHoleId.isEmpty()?"Drill hole":"Update hole";
            form->addSlot("holeTarget","Support face",targetId.isEmpty()?juce::String("Pick a planar face"):featureName(targetId),targetColour,true,[this]{if(selection!=nullptr)selection->setMode(djehuti::cad::CadSelection::Mode::face);setMessage("Pick a planar face on the target body.",0);},"Select the planar entry face in Face mode.");
            addHoleFields(holeSettings,targetId);break;
        case Task::lathe:
            title = "Virtual Lathe"; instruction = "Select a sketch and closed contour, choose a document axis, then Preview and Turn part."; accept = editingLatheId.isEmpty()?"Turn part":"Update turned part";
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
        case Task::chamfer:
        case Task::fillet:
        {
            const bool chamfer=task==Task::chamfer;
            title = chamfer?"Chamfer":"Fillet";
            instruction = chamfer?"Click each edge on one solid body in Edge mode to add it, then enter the distance. Clear selected edges to start again.":"Select a solid body and one or more of its edges in Edge mode, then enter the radius.";
            accept = chamfer?(editingChamferId.isEmpty()?"Chamfer":"Update Chamfer"):"Fillet";
            form->addHeader("Body and Edges");
            form->addSlot("target", "Target body", slotText(targetId, "Select a body in tree or view"), targetColour, targetId.isEmpty(), [this] {
                postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
                    if (safe != nullptr) { safe->activeRole = 0; safe->rebuildTaskPanel(true); }
                });
            }, "Click, then pick the solid target body.");
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
            if(chamfer)
            {
                form->addNumber("chamferDistance","Distance",chamferDistance,"mm","Equal distance on both faces adjoining each edge.",12);
                form->addButton("chamferPreview","Preview",[this]{postUiAction([safe=juce::Component::SafePointer<CadWorkspacePanel>(this)]{if(safe!=nullptr)safe->previewChamfer();});},"Validate and display the candidate without committing.");
                form->addButton("chamferClearPreview","Clear preview",[this]{clearChamferPreview(true);validateTask();},"Restore the source body for edge picking.");
            }
            else form->addNumber("radius", "Radius", filletRadius, "mm", "Constant fillet radius.");
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
    if(task==Task::modeling){pickModeling({id,id,-1,-1});return;}
    if(task==Task::replication){pickReplication({id,id,-1,-1});return;}
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
    else if (task == Task::fillet || task == Task::chamfer)
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
            if(form->control("endU")!=nullptr)
            {
                for(const auto* key:{"endU","endV","direction"})requireNumber(key,key);
                if(form->number("dimension")<1.0e-8||std::hypot(form->number("endU")-form->number("u"),form->number("endV")-form->number("v"))<1.0e-8)
                    problem="Line endpoints must be distinct (length at least 0.00000001 mm).";
            }
            break;
        case Task::newBox: requirePositive("width", "Width"); requirePositive("depth", "Depth"); requirePositive("height", "Height"); break;
        case Task::newCylinder: requirePositive("radius", "Radius"); requirePositive("height", "Height"); break;
        case Task::modeling: {juce::String e;readModelingForm(e);if(e.isNotEmpty())problem=e;if(modelingRevision!=service->document().revision())problem="The model changed. Reopen this tool to refresh its sources.";break;}
        case Task::replication: { juce::String e;readReplicationForm(e);if(e.isNotEmpty())problem=e;if(replicationRevision!=service->document().revision())problem="The model changed. Reopen this tool to refresh its references.";break; }
        case Task::hole: break;
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
        case Task::chamfer:
        case Task::fillet:
            if (targetId.isEmpty()) problem = "Select a solid target body.";
            else if (!isSolid(targetId)) problem = featureName(targetId) + " is not a solid body.";
            else if (filletEdges.empty()) problem = "Select at least one edge in Edge mode.";
            if(task==Task::chamfer)requirePositive("chamferDistance","Chamfer distance");else requirePositive("radius", "Radius");
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
    if(task==Task::chamfer&&chamferSelectionRevision!=service->document().revision()){problem="The model changed. Reopen Chamfer and reselect its edges.";severity=2;}
    if(form->control("holeDiameter")!=nullptr)
    {
        const int style=form->choice("holeStyle");form->setRowVisible("holeDepth",!form->toggle("holeThroughAll"));form->setRowVisible("holeHeadDiameter",style>0);form->setRowVisible("holeHeadDepth",style==1);form->setRowVisible("holeAngle",style==2);
        juce::String e;auto parameters=readHoleForm(e);djehuti::cad::CadHoleParameters p;
        if(parameters.isObject()){const auto target=parameters["target"].toString();parameters.getDynamicObject()->removeProperty("target");if(!isSolid(target))e="Choose a solid target and a planar face.";else if(e.isEmpty())djehuti::cad::readHoleParameters(parameters,p,e);}
        if(e.isNotEmpty()){problem=e;severity=2;}
        if(task==Task::hole&&holeSelectionRevision!=service->document().revision()){problem="The model changed. Reopen Drill Press and choose its face again.";severity=2;}
    }
    if(task==Task::modeling)
    {
        if(auto* button=form->control("modelingPreview"))button->setEnabled(problem.isEmpty());
        if(!djehuti::cad::isSurfaceType(modelingKind)&&modelingKind!="mill")form->setRowVisible("advTarget",form->choice("advOperation")!=0);
        if(modelingKind=="sweep")for(const auto* component:{"X","Y","Z"})form->setRowVisible(juce::String("advBinormal")+component,form->choice("advOrientation")==2);
        if(modelingKind=="mill")
        {
            const int strategy=form->choice("advStrategy"),reference=form->choice("advReference");form->setRowVisible("advRegion",strategy==0);form->setRowVisible("advPath",strategy!=0);form->setRowVisible("advClearPath",strategy!=0);form->setRowVisible("advCompensation",strategy==2);form->setRowVisible("adv_depth",!form->toggle("adv_throughAll"));
            for(const auto* component:{"X","Y","Z"}){form->setRowVisible(juce::String("advOrigin")+component,reference==3);form->setRowVisible(juce::String("advNormal")+component,reference==3);}form->setRowVisible("advReferencePick",reference==4);
        }
    }
    if(task==Task::replication)
    {
        for(const auto& key:juce::StringArray{"plane","direction1","direction2","axis"})if(form->control(key+"Type")!=nullptr)
        {
            const auto index=form->choice(key+"Type");const bool custom=index==3;for(const auto* letter:{"X","Y","Z"}){form->setRowVisible(key+"O"+letter,custom);form->setRowVisible(key+"D"+letter,custom);}
            form->setRowVisible(key+"Pick",index>=4);form->setRowVisible(key+"Picked",index>=4);
        }
        if(auto* button=form->control("repPreview"))button->setEnabled(problem.isEmpty());
    }
    if(form->control("chamferDistance")!=nullptr)requirePositive("chamferDistance","Chamfer distance");
    if(auto* button=form->control("chamferPreview"))button->setEnabled(problem.isEmpty());
    if(auto* button=form->control("chamferClearPreview"))button->setEnabled(chamferPreviewActive);
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
            if (f->type == "revolve" && !f->lathe)
            {
                const auto p = readRevolveForm(); const auto sketch = revolveFormSketch();
                if (sketch != f->sketchId || juce::JSON::toString(djehuti::cad::revolveParametersToJson(p),false,17) != juce::JSON::toString(djehuti::cad::revolveParametersToJson(f->revolve),false,17))
                    ok = service->updateRevolve(selectedId, sketch, p, e);
                if (!ok) { showError(e); return; }
                f = document.find(selectedId);
            }
            if(f->type=="hole")
            {
                auto parameters=readHoleForm(e);if(e.isNotEmpty()){showError(e);return;}auto* args=new juce::DynamicObject();args->setProperty("action","update");args->setProperty("id",selectedId);args->setProperty("parameters",parameters);const auto reply=service->holeCommand(args);
                if(!(bool)reply["ok"]){showError(reply["error"].toString());return;}f=document.find(selectedId);
            }
            if(f->type=="chamfer")
            {
                const auto request=chamferArguments("update");clearChamferPreview(false);const auto reply=service->chamferCommand(request);
                if(!(bool)reply["ok"]){showError(reply["error"].toString());return;}f=document.find(selectedId);
            }
            if (ok && name != f->name) ok = service->rename(selectedId, name, e);
            if (ok && (f = document.find(selectedId)) != nullptr && (f->type!="chamfer" && f->type!="hole" && (w != f->width || d != f->depth || h != f->height || r != f->radius))) ok = service->setDimensions(selectedId, w, d, h, r, e);
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
            if(entity->type=="line")
            {
                const auto u=form->number("u"),v=form->number("v"),endU=form->number("endU"),endV=form->number("endV");
                const auto sketchId=editingSketchId,entityId=sketchEntityId;
                const bool changed=std::abs(u-entity->x1)>1.0e-12||std::abs(v-entity->y1)>1.0e-12||std::abs(endU-entity->x2)>1.0e-12||std::abs(endV-entity->y2)>1.0e-12;
                const bool ok=!changed||service->setSketchLineGeometry(sketchId,entityId,u,v,endU,endV,e);
                refreshFromDocument(true);rebuildTaskPanel(false);sketchCanvas->repaint();
                if(!ok)showError(e);else setMessage(changed?"Line updated.":"No changes to apply.",0);
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
        case Task::modeling:
        {
            const auto p=readModelingForm(e);if(e.isNotEmpty()){showError(e);return;}auto* a=new juce::DynamicObject();a->setProperty("action",editingModelingId.isEmpty()?"create":"update");a->setProperty("id",editingModelingId);a->setProperty("parameters",p);clearModelingView();const auto result=service->modelingCommand(modelingKind,a);
            if(!(bool)result["ok"]){showModelingSources();showError(result["error"].toString());return;}id=result["id"].toString();refreshFromDocument(true);selectFeature(id);setMessage("Modeling feature applied.",0);return;
        }
        case Task::replication:
        {
            auto p=readReplicationForm(e);if(e.isNotEmpty()){showError(e);return;}auto* a=new juce::DynamicObject();a->setProperty("action",editingReplicationId.isEmpty()?"create":"update");a->setProperty("id",editingReplicationId);a->setProperty("parameters",p);clearReplicationView();const auto r=service->replicationCommand(replicationKind,a);
            if(!(bool)r["ok"]){showReplicationSources();showError(r["error"].toString());return;}id=r["id"].toString();refreshFromDocument(true);selectFeature(id);setMessage("Parametric "+replicationKind+" applied.",0);return;
        }
        case Task::hole:
        {
            auto parameters=readHoleForm(e);if(e.isNotEmpty()){showError(e);return;}auto* args=new juce::DynamicObject();args->setProperty("action",editingHoleId.isEmpty()?"create":"update");args->setProperty("id",editingHoleId);args->setProperty("parameters",parameters);clearHoleSource();
            const auto reply=service->holeCommand(args);if(!(bool)reply["ok"]){if(editingHoleId.isNotEmpty())showHoleSource();showError(reply["error"].toString());return;}
            id=reply["id"].toString();refreshFromDocument(true);selectFeature(id);setMessage("Hole drilled.",0);return;
        }
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
        case Task::chamfer:
        {
            const auto request=chamferArguments(editingChamferId.isEmpty()?"create":"update");chamferDistance=form->number("chamferDistance");clearChamferPreview(false);
            const auto reply=service->chamferCommand(request);if(!(bool)reply["ok"]){showError(reply["error"].toString());return;}
            id=reply["id"].toString();refreshFromDocument(true);selectFeature(id);setMessage("Chamfer applied.",0);return;
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
    clearLatheView();clearModelingView();clearReplicationView();clearHoleSource();clearChamferPreview(false);
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
    rebuildTaskPanel(false);resized();
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
    rebuildTaskPanel(false);resized();
    repaint();
}

void CadWorkspacePanel::setSketchTool(const juce::String& tool)
{
    sketchCanvas->setTool(tool);
    if (tool != "select") { sketchEntityId.clear(); sketchSecondEntityId.clear(); sketchCanvas->setSelection({}, {}); task = Task::sketchEdit; }
    sketchCanvas->grabKeyboardFocus();
    syncTreeSelection();updateToolbar();
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

void CadWorkspacePanel::sketchGeometryAdded(const juce::String& what,const juce::String& entityOrGroup)
{
    refreshFromDocument(true);
    if(const auto* sketch=service->document().findSketch(editingSketchId))
        for(const auto& e:sketch->entities)if(e.id==entityOrGroup||e.groupId==entityOrGroup){selectSketchEntity(e.id,false);break;}
    setMessage(what.substring(0, 1).toUpperCase() + what.substring(1) + " added. The " + what + " tool stays active; press Esc or Select to stop.", 0);
}

void CadWorkspacePanel::updateToolbar()
{
    const bool sketching = editingSketchId.isNotEmpty();
    sketchPalette->setAvailable(sketching);
    sketchPalette->refresh([this](const auto& c){return uiCommandEnabled(c);},[this](const auto& c){return uiToolActive(c);});
    for (auto* b : { &sketchButton, &boxButton, &cylinderButton, &extrudeButton, &revolveButton, &latheButton, &cutButton, &filletButton, &chamferButton, &holeButton, &subtractButton, &unionButton, &intersectButton, &fitButton, &resetViewButton, &viewsButton, &instanceButton, &jointButton, &couplingButton, &linkButton, &ikButton, &bodyModeButton, &faceModeButton, &edgeModeButton }) b->setVisible(!sketching);
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
    for (auto* b : { &sketchButton,&boxButton,&cylinderButton,&extrudeButton, &revolveButton,&latheButton,&cutButton,&filletButton,&chamferButton,&holeButton,&subtractButton,&unionButton,&intersectButton,&undoButton,&redoButton,&fitButton,&resetViewButton,&viewsButton,&selectToolButton,&lineButton,&rectangleButton,&circleButton,&curveButton,&constrainButton,&fitSketchButton,&instanceButton,&jointButton,&couplingButton,&linkButton,&ikButton,&bodyModeButton,&faceModeButton,&edgeModeButton }) b->setVisible(false);
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

bool CadWorkspacePanel::isSolid(const juce::String& id) const { const auto* f = service->document().find(id); return f != nullptr && isSolidType(f->type) && !f->shape.IsNull() && TopExp_Explorer(f->shape,TopAbs_SOLID).More(); }
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
    if(command=="deleteSketchEntities")return deleteSelectedSketchEntities();
    if(command=="sketchToolbox"){sketchPalette->toggleVisible();return true;}
    if(command=="dockSketchToolbox"){sketchPalette->toggleDocked();return true;}
    if (command == "pointer" || command == "boxSelect" || command == "circleSelect")
    {
        if (selection != nullptr) selection->setTool(command == "pointer" ? djehuti::cad::CadSelection::Tool::pointer : command == "boxSelect" ? djehuti::cad::CadSelection::Tool::box : djehuti::cad::CadSelection::Tool::circle);
        return true;
    }
    if(djehuti::cad::isModelingType(command)){startTask(Task::modeling,command);return true;}
    if(djehuti::cad::isReplicationType(command)){startTask(Task::replication,command);return true;}
    if (command == "materials") { showMaterialsManager(); return true; }
    if (command == "export") { showExportDialog(); return true; }
    if (command == "interference") { checkInterference(false); return true; }
    if (command == "clearInterference") { clearInterference(); return true; }
    const std::pair<const char*,juce::TextButton*> commands[] = {
        {"undo",&undoButton},{"redo",&redoButton},{"sketch",&sketchButton},{"box",&boxButton},{"cylinder",&cylinderButton},
        {"lathe",&latheButton},{"revolve",&revolveButton},{"extrude",&extrudeButton},{"cut",&cutButton},{"hole",&holeButton},{"chamfer",&chamferButton},{"fillet",&filletButton},{"subtract",&subtractButton},{"union",&unionButton},{"intersect",&intersectButton},
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
    if(command=="deleteSketchEntities")return sketch&&sketchEntityId.isNotEmpty();
    if(command=="dockSketchToolbox")return sketch;
    if(djehuti::cad::isModelingType(command)||djehuti::cad::isReplicationType(command))return !sketch;
    if (juce::StringArray{"sketchSelect","line","rectangle","circle","curve","constrain","fitSketch","finishSketch"}.contains(command)) return sketch;
    if (juce::StringArray{"bodyMode","faceMode","edgeMode","pointer","boxSelect","circleSelect","interference","materials"}.contains(command)) return !sketch;
    if (command == "clearInterference") return interferenceActive;
    return true;
}
bool CadWorkspacePanel::uiToolActive(const juce::String& command) const
{
    if(djehuti::cad::isModelingType(command))return task==Task::modeling&&modelingKind==command;
    if(djehuti::cad::isReplicationType(command))return task==Task::replication&&replicationKind==command;
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
    if(task==Task::modeling&&(command=="inspect"||command=="inspectRegion")){pickModeling(pick);return;}
    if(task==Task::replication&&(command=="inspect"||command=="inspectRegion")){pickReplication(pick);return;}
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
    state->setProperty("modelingPreview",modelingPreviewActive);
    state->setProperty("replicationPreview",replicationPreviewActive);
    state->setProperty("chamferPreview",chamferPreviewActive);
    state->setProperty("documentRevision", (juce::int64)document.revision());
    state->setProperty("documentInstance", (juce::int64)document.documentInstance());
    state->setProperty("modified", document.modified()); state->setProperty("file", document.file().getFullPathName());
    state->setProperty("showing", isShowing()); state->setProperty("mode", editingSketchId.isEmpty() ? "model" : "sketch");
    state->setProperty("task", taskTitle.getText()); state->setProperty("instruction", taskInstruction.getText());
    state->setProperty("message", messageLabel.getText()); state->setProperty("selected", selectedId);
    state->setProperty("editingSketch", editingSketchId); state->setProperty("selectedEntity", sketchEntityId);state->setProperty("secondEntity",sketchSecondEntityId);state->setProperty("sketchToolbox",sketchPalette->state());
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
    for (const auto& name : juce::StringArray{"undo","redo","sketch","box","cylinder","extrude","revolve","lathe","chamfer","hole","mirror","linearPattern","circularPattern","sweep","loft","mill","surface","surface_trim","surface_extend","surface_sew","surface_solidify","surface_thicken","cut","subtract","union","intersect","sketchSelect","line","rectangle","circle","curve","constrain","fitSketch","finishSketch","instance","joint","coupling","link","ik","fit","reset","views","pointer","boxSelect","circleSelect","bodyMode","faceMode","edgeMode","interference","clearInterference","materials","deleteSketchEntities","sketchToolbox","dockSketchToolbox"})
    { auto* c = new juce::DynamicObject(); c->setProperty("name", name); c->setProperty("enabled", uiCommandEnabled(name)); c->setProperty("active", uiToolActive(name)); commands.add(c); }
    state->setProperty("commands", commands);
    juce::Array<juce::var> features;
    for (const auto& f : document.features())
    {
        auto* o = new juce::DynamicObject(); o->setProperty("id", f.id); o->setProperty("name", f.name); o->setProperty("type", f.type); o->setProperty("visible", f.visible);
        o->setProperty("width", f.width); o->setProperty("depth", f.depth); o->setProperty("height", f.height); o->setProperty("radius", f.radius);
        o->setProperty("x", f.x); o->setProperty("y", f.y); o->setProperty("z", f.z);
        if(djehuti::cad::isModelingType(f.type))o->setProperty("parameters",f.modeling);
        if(djehuti::cad::isReplicationType(f.type))o->setProperty("parameters",f.replication);
        if(f.type=="hole"){auto p=djehuti::cad::holeParametersToJson(f.hole);p.getDynamicObject()->setProperty("target",f.targetFeatureId);o->setProperty("parameters",p);}
        if(f.type=="chamfer"){auto* a=new juce::DynamicObject();a->setProperty("action","get");a->setProperty("id",f.id);o->setProperty("parameters",service->chamferCommand(a)["parameters"]);}
        if (f.type == "revolve")
        { auto p = djehuti::cad::revolveParametersToJson(f.revolve); p.getDynamicObject()->setProperty("sketch", f.sketchId); o->setProperty("parameters", p); if(f.lathe)o->setProperty("workflow","lathe"); }
        if (const auto* sketch = document.findSketch(f.id))
        {
            o->setProperty("plane", sketch->plane); o->setProperty("offset", sketch->offset);
            juce::Array<juce::var> entities;
            for (const auto& e : sketch->entities)
            { auto* item = new juce::DynamicObject(); item->setProperty("id", e.id); item->setProperty("type", e.type); item->setProperty("x1",e.x1); item->setProperty("y1",e.y1); item->setProperty("x2",e.x2); item->setProperty("y2",e.y2); item->setProperty("radius",e.radius);
              const auto row=entityTreeItems.find(f.id+"/"+e.id);item->setProperty("treeSelected",row!=entityTreeItems.end()&&row->second->isSelected());item->setProperty("treeName",friendlyEntity(e.id));
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
    if(action=="sketchToolbox"){commandMessage.clear();commandSeverity=0;}
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
            const auto* sketch = service->document().findSketch(id.isNotEmpty()?id:editingSketchId); bool found = false;
            if (sketch) for (const auto& e : sketch->entities) if (e.id == entity) found = true;
            if (!found) error = "Entity is not in the active sketch.";
            else if(id.isNotEmpty())sketchEntityClicked(id,entity,(bool)args.getProperty("second",false));
            else selectSketchEntity(entity, (bool)args.getProperty("second", false));
        }
        else if (id.isNotEmpty() && !service->document().find(id) && !isInstance(id) && !isJoint(id) && !isCoupling(id) && !isIkTask(id)) error = "Unknown model tree id.";
        else featureClicked(id);
    }
    else if(action=="sketchToolbox"&&args["operation"].toString()=="capture")
    {
        const auto path=args["path"].toString();
        if(editingSketchId.isEmpty()||!juce::File::isAbsolutePath(path))error="Open a sketch and provide an absolute PNG path.";
        else
        {
            const juce::File file(path);
            if(!file.hasFileExtension("png")||!file.getParentDirectory().isDirectory())error="Capture requires a PNG in an existing directory.";
            else if(file.existsAsFile()&&!(bool)args.getProperty("overwrite",false))error="Capture exists; explicitly set overwrite=true.";
            else if(auto stream=file.createOutputStream())
            {handleUpdateNowIfNeeded();stream->setPosition(0);stream->truncate();if(!juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),*stream))error="Unable to write sketch component capture.";}
            else error="Unable to open sketch capture file.";
        }
    }
    else if(action=="sketchToolbox")
    {
        auto value=args.getProperty("value",{});
        if(value.isObject())
        {
            auto next=sketchPalette->state();
            for(const auto* key:{"visible","docked"})if(value.hasProperty(key)){if(!value[key].isBool())error="Toolbox visibility/docking must be boolean.";else next.getDynamicObject()->setProperty(key,value[key]);}
            for(const auto* key:{"x","y"})if(value.hasProperty(key)){auto v=value[key];if(!(v.isDouble()||v.isInt()||v.isInt64())||!std::isfinite((double)v)||(double)v<0||(double)v>100000)error="Toolbox position must be finite and between 0 and 100000 pixels.";else next.getDynamicObject()->setProperty(key,(int)(double)v);}
            if(error.isEmpty())sketchPalette->restore(next);
        }
        else if(!value.isVoid())error="Toolbox value must be an object.";
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

// Focused component/service checks. Mouse/key objects below invoke real callbacks;
// they are not native pointer interaction.
bool runSketchUsabilityCheck(juce::var& report)
{
    using namespace djehuti::cad;juce::Array<juce::var> checks;int failures=0;juce::String error;
    const auto check=[&](bool ok,const juce::String& name){auto* item=new juce::DynamicObject();item->setProperty("name",name);item->setProperty("ok",ok);checks.add(item);if(!ok)++failures;};
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/sketch-usability-check");folder.createDirectory();
    const auto json=[&](const std::shared_ptr<CadCommandService>& source){const auto file=folder.getChildFile("state.djc");source->document().exportJson(file,error);return file.loadFileAsString();};
    auto service=std::make_shared<CadCommandService>();CadWorkspacePanel panel(service);panel.setSize(1400,850);
    const auto call=[&](const juce::String& json){return panel.automation(juce::JSON::parse(json));};
    const auto draw=[&](double radius){call(R"({"action":"command","command":"circle"})");call(R"({"action":"sketch","phase":"begin","x":0,"y":0})");return call("{\"action\":\"sketch\",\"phase\":\"commit\",\"x\":"+juce::String(radius,9)+",\"y\":0}");};
    juce::String sketch;check(service->createSketch("XY",0,sketch,error),"Create XY sketch through the existing command");panel.refreshFromDocument(true);panel.enterSketch(sketch);
    check(panel.sketchPalette->isVisible()&&panel.sketchPalette->isDocked(),"Sketch toolbox appears automatically and docks within CAD");
    auto result=draw(50);auto state=result["state"];const auto first=state["selectedEntity"].toString();
    check((bool)result["ok"]&&first.isNotEmpty()&&state["task"].toString().startsWith("Circle"),"50 mm circle is automatically selected with immediate properties");
    check(panel.entityTreeItems.size()==1&&panel.entityTreeItems.at(sketch+"/"+first)->isSelected(),"Creation synchronizes the actual entity tree row");
    check(panel.form!=nullptr&&std::abs(panel.form->number("dimension")-50)<1e-8,"Radius and coordinates are exposed in the existing properties form");
    auto second=draw(30)["state"]["selectedEntity"].toString();auto third=draw(10)["state"]["selectedEntity"].toString();
    check(service->document().findSketch(sketch)->entities.size()==3&&panel.entityTreeItems.size()==3,"All three concentric circles exist in the expandable sketch tree");
    auto* sketchRow=panel.entityTreeItems.at(sketch+"/"+first)->getParentItem();sketchRow->setOpen(false);panel.refreshFromDocument(true);
    check(!panel.entityTreeItems.at(sketch+"/"+first)->getParentItem()->isOpen(),"Tree collapse survives a document refresh");
    const auto event=[&](juce::Component& component,juce::Point<float> position,int mods,juce::Point<float> down=juce::Point<float>())
    {return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),position,juce::ModifierKeys(mods),1,0,0,0,0,&component,&component,juce::Time::getCurrentTime(),down,juce::Time::getCurrentTime(),1,true);};
    panel.entityTreeItems.at(sketch+"/"+first)->itemClicked(event(panel.tree,{},juce::ModifierKeys::leftButtonModifier));panel.handleUpdateNowIfNeeded();
    check(panel.sketchEntityId==first&&panel.sketchCanvas->tool()=="select"&&panel.entityTreeItems.at(sketch+"/"+first)->getParentItem()->isOpen(),"Tree click selects the entity, expands it and opens the shared properties");
    auto pixel=panel.sketchCanvas->toPoint(0,30);panel.sketchCanvas->mouseDown(event(*panel.sketchCanvas,pixel,juce::ModifierKeys::leftButtonModifier));panel.sketchCanvas->mouseUp(event(*panel.sketchCanvas,pixel,0));panel.handleUpdateNowIfNeeded();
    check(panel.sketchEntityId==second&&panel.entityTreeItems.at(sketch+"/"+second)->isSelected(),"Viewport click highlights the same entity row and properties");
    panel.entityTreeItems.at(sketch+"/"+first)->itemClicked(event(panel.tree,{},juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier));panel.handleUpdateNowIfNeeded();
    check(panel.sketchEntityId==second&&panel.sketchSecondEntityId==first&&panel.entityTreeItems.at(sketch+"/"+first)->isSelected()&&panel.entityTreeItems.at(sketch+"/"+second)->isSelected(),"Tree preserves the existing two-entity Shift selection");
    panel.selectSketchEntity(first,false);call(R"({"action":"set","field":"dimension","value":55})");result=call(R"({"action":"click","button":"apply"})");
    check((bool)result["ok"]&&std::abs(service->document().findSketch(sketch)->entities.front().radius-55)<1e-8,"Circle radius edits through properties as one transaction");
    const auto before=service->document().revision();panel.handleSketchKey(juce::KeyPress(juce::KeyPress::deleteKey));
    check(service->document().revision()==before+1&&panel.entityTreeItems.size()==2&&panel.sketchEntityId.isEmpty(),"Delete key removes tree/canvas geometry and clears the properties selection");
    call(R"({"action":"command","command":"undo"})");check(panel.entityTreeItems.count(sketch+"/"+first)==1&&service->document().findSketch(sketch)->constraints.size()==1,"Undo restores entity identity and its radius constraint");
    call(R"({"action":"command","command":"redo"})");check(panel.entityTreeItems.size()==2&&service->document().findSketch(sketch)->constraints.empty(),"Redo removes the entity and associated constraints again");
    const auto serialized=json(service);const auto revision=service->document().revision();result=draw(0);
    check(!(bool)result["ok"]&&service->document().revision()==revision&&json(service)==serialized,"Zero-radius gesture is rejected without a document edit or allocated identifier");
    result=draw(minimumSketchCircleRadius*.1);check(!(bool)result["ok"],"Near-zero radius is rejected at the documented geometric tolerance");
    check((bool)call(R"({"action":"sketchToolbox"})")["ok"],"Toolbox presentation can recover normally after a rejected gesture");
    juce::String rejected;check(!service->addSketchCircle(sketch,std::numeric_limits<double>::infinity(),0,1,rejected,error),"Nonfinite circle centres fail through the same model command");
    draw(5);const auto fourth=panel.sketchEntityId;
    check(fourth.fromLastOccurrenceOf("-",false,false).getIntValue()>third.fromLastOccurrenceOf("-",false,false).getIntValue()+1,"Constraint allocation/deletion explains identifier gaps without hidden entities");
    panel.setSketchTool("circle");call(R"({"action":"sketch","phase":"begin","x":5,"y":5})");const auto count=service->document().findSketch(sketch)->entities.size();
    check(panel.handleSketchKey(juce::KeyPress(juce::KeyPress::escapeKey))&&!panel.sketchCanvas->hasActiveGesture(),"First Escape cancels an unfinished gesture");
    check(panel.handleSketchKey(juce::KeyPress(juce::KeyPress::escapeKey))&&panel.sketchCanvas->tool()=="select","Next Escape returns a drawing tool to Select");
    panel.selectSketchEntity(fourth,false);check(panel.handleSketchKey(juce::KeyPress(juce::KeyPress::escapeKey))&&panel.sketchEntityId.isEmpty()&&service->document().findSketch(sketch)->entities.size()==count,"Escape in Select clears selection without deleting completed geometry");
    panel.setSketchTool("circle");pixel=panel.sketchCanvas->toPoint(0,0);panel.sketchCanvas->mouseDown(event(*panel.sketchCanvas,pixel,juce::ModifierKeys::leftButtonModifier));panel.sketchCanvas->mouseUp(event(*panel.sketchCanvas,pixel,0));
    check(service->document().findSketch(sketch)->entities.size()==count&&panel.commandMessage.contains("3 pixels"),"Accidental pointer click cancels with visible feedback rather than creating geometry");
    auto paletteState=juce::JSON::parse(R"({"visible":true,"docked":false,"x":130,"y":45})");panel.sketchPalette->restore(paletteState);
    check(panel.sketchPalette->getX()==130&&panel.sketchCanvas->getX()==0,"Floating toolbox overlays the drawing area");
    panel.sketchPalette->mouseDown(event(*panel.sketchPalette,{18,10},juce::ModifierKeys::leftButtonModifier,{18,10}));panel.sketchPalette->mouseDrag(event(*panel.sketchPalette,{48,40},juce::ModifierKeys::leftButtonModifier,{18,10}));panel.sketchPalette->mouseUp(event(*panel.sketchPalette,{48,40},0,{18,10}));
    check(!panel.sketchPalette->isDocked()&&panel.sketchPalette->getX()>130,"Toolbox header drag invokes movable overlay behavior");
    panel.executeUiCommand("dockSketchToolbox");check(panel.sketchPalette->isDocked()&&panel.sketchCanvas->getX()>0,"Dock command reserves sketch space");
    panel.executeUiCommand("sketchToolbox");check(!panel.sketchPalette->isVisible()&&panel.sketchCanvas->getX()==0,"View command hides the toolbox without reserving canvas space");
    const auto preferences=panel.viewState();{CadWorkspacePanel restored(service);restored.setSize(1400,850);restored.restoreViewState(preferences);restored.enterSketch(sketch);check(!restored.sketchPalette->isVisible()&&restored.sketchPalette->isDocked(),"Toolbox visibility/docking/position persist in existing workspace preferences");}
    panel.executeUiCommand("sketchToolbox");panel.executeUiCommand("dockSketchToolbox");panel.setSketchTool("select");panel.selectSketchEntity(second,false);panel.handleUpdateNowIfNeeded();
    {auto snapshot=panel.createComponentSnapshot(panel.getLocalBounds());if(auto stream=folder.getChildFile("sketch-workflow.png").createOutputStream()){stream->setPosition(0);stream->truncate();juce::PNGImageFormat().writeImageToStream(snapshot,*stream);}}
    for(const auto* extension:{"djo","djc"})
    {const auto path=folder.getChildFile(juce::String("circles.")+extension);CadCommandService reopened;check(service->save(path,error)&&reopened.load(path,error)&&reopened.document().findSketch(sketch)->entities.size()==count,juce::String("Geometry and stable ids survive save/reopen: ")+extension);}
    // Legacy degenerate circles are deliberately still loadable and discoverable.
    auto legacy=juce::JSON::parse(json(service));auto& legacySketch=legacy["sketches"].getArray()->getReference(0);const auto legacyId=legacySketch["entities"][0]["id"].toString();legacySketch["entities"].getArray()->getReference(0).getDynamicObject()->setProperty("radius",0);
    auto legacyService=std::make_shared<CadCommandService>();const auto legacyFile=folder.getChildFile("legacy.djc");legacyFile.replaceWithText(juce::JSON::toString(legacy));check(legacyService->load(legacyFile,error),"Legacy zero-radius entity remains loadable");
    {CadWorkspacePanel legacyPanel(legacyService);legacyPanel.setSize(1400,850);legacyPanel.enterSketch(sketch);check(legacyPanel.entityTreeItems.count(sketch+"/"+legacyId)==1,"Legacy invisible entity appears in the actual tree");legacyPanel.sketchEntityClicked(sketch,legacyId,false);check(legacyPanel.deleteSelectedSketchEntities(),"Legacy invisible entity can be selected in the tree and deleted");}
    juce::String rect,group;service->createSketch("XY",0,rect,error);service->addSketchRectangle(rect,0,0,10,10,group,error);const auto edge=service->document().findSketch(rect)->entities.front().id;
    check(service->deleteSketchEntities(rect,{edge},error)&&service->document().findSketch(rect)->entities.size()==3,"Deleting one rectangle edge removes endpoint constraints and dissolves its rectangle group");
    const auto prior=json(service);check(!service->deleteSketchEntities(rect,{"missing",edge},error)&&json(service)==prior,"Invalid multi-delete fails atomically");
    juce::String dependency,entity,solid;service->createSketch("XY",0,dependency,error);service->addSketchCircle(dependency,0,0,2,entity,error);service->createExtrude(dependency,5,false,solid,error);const auto dependentState=json(service);
    check(!service->deleteSketchEntities(dependency,{entity},error)&&json(service)==dependentState,"Deletion that invalidates an existing solid rolls back through the existing dependency rebuild");
    auto* summary=new juce::DynamicObject();summary->setProperty("ok",failures==0);summary->setProperty("passed",checks.size()-failures);summary->setProperty("failed",failures);summary->setProperty("checks",checks);report=summary;return failures==0;
}

// Focused integration check: real property widgets/callbacks and native JUCE popup peers.
// Synthetic mouse events exercise the production callbacks; this is not a pointer-driven test.
bool runSketchLineRepairCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks; int failures=0; juce::String error;
    const auto check=[&](bool ok,const juce::String& name)
    {auto* row=new juce::DynamicObject();row->setProperty("name",name);row->setProperty("ok",ok);if(!ok)row->setProperty("detail",error);checks.add(row);if(!ok)++failures;};
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/sketch-line-repair-check");folder.createDirectory();
    auto service=std::make_shared<CadCommandService>();CadWorkspacePanel panel(service);panel.setSize(1400,900);
    const auto call=[&](const juce::String& json){return panel.automation(juce::JSON::parse(json));};
    const auto set=[&](const char* key,const juce::var& value)
    {juce::DynamicObject::Ptr args=new juce::DynamicObject();args->setProperty("action","set");args->setProperty("field",key);args->setProperty("value",value);return panel.automation(juce::var(args.get()));};
    const auto line=[&](const juce::String& sketch,const juce::String& id)->CadSketchEntity
    {if(const auto* s=service->document().findSketch(sketch))for(const auto& e:s->entities)if(e.id==id)return e;return {};};
    const auto approximately=[](double a,double b){return std::abs(a-b)<1e-7;};
    juce::String firstSketch,firstLine;
    for(const auto* plane:{"XY","XZ","YZ"})
    {
        juce::String sketch,id;check(service->createSketch(plane,0,sketch,error)&&service->addSketchLine(sketch,1,2,11,2,id,error),juce::String(plane)+": create line");
        if(firstSketch.isEmpty()){firstSketch=sketch;firstLine=id;}
        panel.refreshFromDocument(true);panel.sketchEntityClicked(sketch,id,false);panel.handleUpdateNowIfNeeded();
        check(panel.form->control("endU")&&panel.form->control("endV")&&panel.form->control("direction"),juce::String(plane)+": both numeric methods available");
        set("angleUnit","Degrees");set("u",0);set("v",0);set("endU",50);set("endV",0);
        check(approximately(panel.form->number("dimension"),50)&&approximately(panel.form->number("direction"),0),juce::String(plane)+": endpoints derive 50 mm / 0 degrees");
        auto revision=service->document().revision();call(R"({"action":"click","button":"apply"})");auto e=line(sketch,id);
        check(approximately(e.x1,0)&&approximately(e.y1,0)&&approximately(e.x2,50)&&approximately(e.y2,0)&&service->document().revision()==revision+1,juce::String(plane)+": endpoint Apply is one transaction");
        set("direction",90);check(approximately(panel.form->number("endU"),0)&&approximately(panel.form->number("endV"),50),juce::String(plane)+": 90 degrees computes endpoint before Apply");
        call(R"({"action":"click","button":"apply"})");set("dimension",25);call(R"({"action":"click","button":"apply"})");e=line(sketch,id);
        check(approximately(e.x1,0)&&approximately(e.y1,0)&&approximately(e.x2,0)&&approximately(e.y2,25),juce::String(plane)+": length edit preserves start and direction");
        set("angleUnit","Radians");check(approximately(panel.form->number("direction"),juce::MathConstants<double>::halfPi),juce::String(plane)+": degree to radian conversion");
        set("direction",juce::MathConstants<double>::pi);call(R"({"action":"click","button":"apply"})");e=line(sketch,id);
        check(approximately(e.x2,-25)&&approximately(e.y2,0),juce::String(plane)+": radians edit uses the sketch horizontal axis");
        set("angleUnit","Degrees");check(approximately(std::abs(panel.form->number("direction")),180),juce::String(plane)+": radian to degree conversion");
        call(R"({"action":"command","command":"undo"})");e=line(sketch,id);check(approximately(e.y2,25),juce::String(plane)+": Undo restores geometry and properties");
        call(R"({"action":"command","command":"redo"})");e=line(sketch,id);check(approximately(e.x2,-25)&&approximately(panel.form->number("endU"),-25),juce::String(plane)+": Redo synchronizes properties");
        set("endU",0);set("endV",0);check(!panel.acceptButton.isEnabled(),juce::String(plane)+": degenerate endpoints disable Apply");
        panel.rebuildTaskPanel(false);auto* editor=dynamic_cast<juce::TextEditor*>(panel.form->control("endU"));editor->setText("1..2",false);editor->onTextChange();
        check(!panel.acceptButton.isEnabled(),juce::String(plane)+": malformed native numeric input disables Apply");panel.rebuildTaskPanel(false);
    }
    const auto snapshot=[&]{const auto file=folder.getChildFile("state.djc");service->document().exportJson(file,error);return file.loadFileAsString();};
    const auto before=snapshot();const auto revision=service->document().revision();
    check(!service->setSketchLineGeometry(firstSketch,firstLine,0,0,0,0,error)&&snapshot()==before&&service->document().revision()==revision,"Degenerate command rolls back document and revision");
    check(!service->setSketchLineGeometry(firstSketch,firstLine,0,0,std::numeric_limits<double>::infinity(),1,error)&&snapshot()==before,"Nonfinite command leaves document unchanged");
    panel.sketchEntityClicked(firstSketch,firstLine,false);panel.handleUpdateNowIfNeeded();
    check(!(bool)set("endU","NaN")["ok"]&&!(bool)set("direction","1..2")["ok"],"Automation rejects nonfinite and malformed values");
    juce::String s,a,b,c;service->createSketch("XY",0,s,error);service->addSketchLine(s,0,0,50,0,a,error);service->addSketchLine(s,50,0,50,20,b,error,a+":p2");
    service->addSketchConstraint(s,"horizontal",a,{},0,c,error);service->setSketchEntityDimension(s,a,50,error);
    check(service->setSketchLineGeometry(s,a,0,0,25,0,error)&&approximately(line(s,b).x1,25),"Length driving dimension is updated and joined endpoints follow atomically");
    const auto constrained=snapshot();const auto rev=service->document().revision();
    check(!service->setSketchLineGeometry(s,a,0,0,0,25,error)&&error.contains("constraints")&&snapshot()==constrained&&service->document().revision()==rev,"Incompatible horizontal direction is rejected without losing constraints");
    panel.refreshFromDocument(true);panel.sketchEntityClicked(s,a,false);panel.handleUpdateNowIfNeeded();set("angleUnit","Degrees");set("direction",90);
    const auto conflict=call(R"({"action":"click","button":"apply"})");
    check(!(bool)conflict["ok"]&&snapshot()==constrained&&approximately(panel.form->number("endU"),25),"GUI Apply reports a constraint conflict and restores synchronized document values");
    service->addSketchConstraint(s,"fixed",a,{},0,c,error);
    check(!service->setSketchLineGeometry(s,a,5,5,30,5,error),"Fixed start rejects translation");
    check(service->undo(error)&&service->undo(error)&&approximately(line(s,b).x1,50),"Undo restores joined endpoints and the old driving dimension");
    check(service->redo(error)&&approximately(line(s,b).x1,25),"Redo restores joined edit");
    juce::String profile,group,body;service->createSketch("XY",0,profile,error);service->addSketchRectangle(profile,0,0,30,20,group,error);service->createExtrude(profile,5,false,body,error);
    const auto edge=service->document().findSketch(profile)->entities.front().id;
    const auto volume=[&]{GProp_GProps props;BRepGProp::VolumeProperties(service->document().find(body)->shape,props);return props.Mass();};
    check(service->setSketchLineGeometry(profile,edge,0,0,40,0,error)&&approximately(volume(),4000),"Line edit rebuilds dependent solid using existing document dependencies");
    check(service->undo(error)&&approximately(volume(),3000)&&service->redo(error)&&approximately(volume(),4000),"Undo/Redo rebuilds dependent solid with its source line");
    for(const auto* extension:{"djo","djc"})
    {
        const auto path=folder.getChildFile(juce::String("lines.")+extension);CadCommandService reopened;
        bool ok=service->save(path,error)&&reopened.load(path,error);
        for(const auto& sketch:service->document().sketchData())
        {
            const auto* loaded=reopened.document().findSketch(sketch.id);
            const bool sizes=loaded&&loaded->entities.size()==sketch.entities.size()&&loaded->constraints.size()==sketch.constraints.size();ok=ok&&sizes;
            if(sizes)
            {
                for(size_t i=0;i<sketch.entities.size();++i){const auto& p=sketch.entities[i];const auto& q=loaded->entities[i];ok=ok&&p.id==q.id&&approximately(p.x1,q.x1)&&approximately(p.y1,q.y1)&&approximately(p.x2,q.x2)&&approximately(p.y2,q.y2);}
                for(size_t i=0;i<sketch.constraints.size();++i){const auto& p=sketch.constraints[i];const auto& q=loaded->constraints[i];ok=ok&&p.id==q.id&&p.type==q.type&&p.firstEntity==q.firstEntity&&p.secondEntity==q.secondEntity&&approximately(p.value,q.value);}
            }
        }
        check(ok,juce::String("All planes, endpoints, constraints and stable ids save/reopen: ")+extension);
    }
    panel.sketchEntityClicked(firstSketch,firstLine,false);panel.handleUpdateNowIfNeeded();set("angleUnit","Radians");
    auto* angleEditor=dynamic_cast<juce::TextEditor*>(panel.form->control("direction"));angleEditor->setCaretPosition(angleEditor->getText().length());angleEditor->onFocusLost();
    check(angleEditor->getCaretPosition()==0,"Long radian values show their leading digits after leaving the field");
    {CadWorkspacePanel restored(service);restored.restoreViewState(panel.viewState());check(restored.lineAnglesInRadians,"Angle unit preference persists in workspace settings");}
    // Use the actual TreeView content component, reproducing JUCE's row-relative event transformation.
    juce::Component host;host.setSize(1600,1000);host.addAndMakeVisible(panel);
    juce::DocumentWindow window("Sketch line repair check",juce::Colours::darkgrey,0);window.setContentNonOwned(&host,false);window.setBounds(80,80,1600,1000);window.setVisible(true);
    const auto popupCheck=[&](juce::Point<int> requested,const juce::String& label,bool treeEvent)
    {
        juce::PopupMenu::dismissAllActiveMenus();
        if(treeEvent)
        {
            auto* item=panel.entityTreeItems.at(firstSketch+"/"+firstLine);auto* content=panel.tree.getViewport()->getViewedComponent();
            const auto original=content->getLocalPoint(nullptr,requested).toFloat();const auto row=item->getItemPosition(false).getPosition().toFloat();
            const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),original-row,juce::ModifierKeys::rightButtonModifier,1,0,0,0,0,content,content,juce::Time::getCurrentTime(),original,juce::Time::getCurrentTime(),1,false);
            check(event.getMouseDownScreenPosition()==requested&&event.getScreenPosition()!=requested,label+": tree row translation retains original click");item->itemClicked(event);
        }
        else panel.showSketchEntityMenu(firstSketch,firstLine,requested);
        check(panel.sketchEntityMenuOptions(requested).getTargetScreenArea().getPosition()==requested,label+": explicit screen anchor");
        bool visible=false,contained=false,nearPointer=false;
        auto& desktop=juce::Desktop::getInstance();
        for(int i=0;i<desktop.getNumComponents();++i)if(auto* menu=desktop.getComponent(i);menu->getName()=="menu"&&menu->isVisible())
        {visible=true;const auto bounds=menu->getScreenBounds();const auto* display=desktop.getDisplays().getDisplayForPoint(requested);contained=display&&display->userArea.contains(bounds);nearPointer=bounds.expanded(12).contains(requested);}
        check(visible&&contained&&nearPointer,label+": real popup peer is near click and within screen");juce::PopupMenu::dismissAllActiveMenus();
    };
    panel.setBounds(160,90,1350,850);popupCheck(panel.tree.localPointToGlobal(juce::Point<int>(110,120)),"Docked / offset parent",true);
    panel.setBounds(30,25,1000,700);popupCheck(panel.tree.localPointToGlobal(juce::Point<int>(100,140)),"Resized panel",true);
    window.clearContentComponent();host.removeChildComponent(&panel);window.setContentNonOwned(&panel,false);window.setBounds(220,140,1200,800);
    popupCheck(panel.tree.localPointToGlobal(juce::Point<int>(100,160)),"Floating native peer",true);
    if(const auto* display=juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())popupCheck(display->userArea.getBottomRight()-juce::Point<int>(3,3),"Screen boundary",false);
    window.clearContentComponent();window.setVisible(false);
    auto image=panel.createComponentSnapshot(panel.getLocalBounds());if(auto stream=folder.getChildFile("line-properties.png").createOutputStream())juce::PNGImageFormat().writeImageToStream(image,*stream);
    auto* result=new juce::DynamicObject();result->setProperty("ok",failures==0);result->setProperty("failures",failures);result->setProperty("checks",checks);result->setProperty("pointerDriven",false);report=result;return failures==0;
}
