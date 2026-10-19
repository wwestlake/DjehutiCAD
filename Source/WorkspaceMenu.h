#pragma once
#include "DjehutiMenuBarComponent.h"

// Owned by the workspace content, so its commands and popup parent move with a floated panel.
class WorkspaceMenu final : public juce::Component, private juce::MenuBarModel
{
public:
    struct Entry { int id; juce::String text; bool enabled = true, checked = false; };
    struct Section { juce::String title; std::vector<Entry> entries; };
    std::function<std::vector<Section>()> sections;
    std::function<void(int)> invoke;
    std::function<void(bool)> changeDocking;
    WorkspaceMenu() { addAndMakeVisible(bar); }
    ~WorkspaceMenu() override { bar.setModel(nullptr); }
    void refresh() { menuItemsChanged(); }
    void resized() override { bar.setBounds(getLocalBounds()); }
private:
    std::vector<Section> contents() const
    {
        auto result = sections ? sections() : std::vector<Section>();
        if (changeDocking) result.push_back({ "Workspace", { { 9901, "Float this workspace" }, { 9902, "Dock this workspace" } } });
        return result;
    }
    juce::StringArray getMenuBarNames() override
    {
        juce::StringArray result; for (const auto& s : contents()) result.add(s.title); return result;
    }
    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override
    {
        juce::PopupMenu result; auto all = contents();
        if (juce::isPositiveAndBelow(index, (int) all.size()))
            for (const auto& e : all[(size_t) index].entries)
                if (e.id == 0) result.addSeparator(); else result.addItem(e.id, e.text, e.enabled, e.checked);
        return result;
    }
    void menuItemSelected(int id, int) override
    {
        if (id == 9901 || id == 9902) { if (changeDocking) changeDocking(id == 9901); }
        else if (invoke) invoke(id);
    }
    DjehutiMenuBarComponent bar { this };
};
