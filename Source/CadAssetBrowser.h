#pragma once
#include "CadAssets.h"
#include "CadDocument.h"
#include "WorkspaceMenu.h"
#include <set>
class CadAssetBrowser final : public juce::Component,private juce::TableListBoxModel,private juce::Timer
{
public:
    CadAssetBrowser(djehuti::cad::CadAssetLibrary&,std::shared_ptr<djehuti::cad::CadCommandService>);
    ~CadAssetBrowser() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    void beginSave(const juce::String&);
    juce::var ui(const juce::var&);
    WorkspaceMenu workspaceMenu;
    std::function<juce::String()> selectedPart;
    std::function<void(const juce::String&)> onInserted;
private:
    struct Row{bool category=false;juce::var value;};
    void filter();void sources();void selectedRowsChanged(int) override;
    int getNumRows() override{return (int)rows.size();}
    void paintRowBackground(juce::Graphics&,int,int,int,bool) override;
    void paintCell(juce::Graphics&,int,int,int,int,bool) override;
    void cellClicked(int,int,const juce::MouseEvent&) override;
    void cellDoubleClicked(int,int,const juce::MouseEvent&) override;
    void timerCallback() override;
    juce::var run(juce::var);
    void save(bool);void update();void insert();void category(const juce::String&);void exportSource();
    juce::String chosenSource()const,chosenCategory()const;
    djehuti::cad::CadAssetLibrary& library;std::shared_ptr<djehuti::cad::CadCommandService> service;
    juce::var lastResult,catalog;std::vector<Row> rows;std::set<juce::String> expanded;
    juce::String selectedAsset,selectedCategory;juce::StringArray sourceIds,categoryIds;
    juce::TableListBox table{"Assets",this};juce::Viewport scroll;juce::Component editor;
    juce::TextEditor search,name,description,material,categoryName,x,y,z;
    juce::ComboBox source,categoryBox;
    juce::TextButton expand{"Expand All"},collapse{"Collapse All"},reload{"Reload"},saveButton{"Save new asset"},replace{"Replace selected"},updateButton{"Update metadata"},remove{"Remove asset"},insertButton{"Insert copy"},exportButton{"Export editable copy"},createCategory{"Create category"},renameCategory{"Rename category"},deleteCategory{"Delete empty"};
    juce::Label status;juce::OwnedArray<juce::Label> captions;
    std::unique_ptr<juce::FileChooser> chooser;std::uint64_t revision=0,documentInstance=0;
};
bool runCadAssetUiCheck(juce::var&);
