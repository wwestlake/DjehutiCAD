#pragma once
#include "CadDocument.h"
#include "WorkspaceMenu.h"
#include "Viewport3D.h"

class CadComponentBrowser final : public juce::Component,private juce::ListBoxModel,private juce::Timer
{
public:
    explicit CadComponentBrowser(std::shared_ptr<djehuti::cad::CadCommandService>);
    ~CadComponentBrowser() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();
    WorkspaceMenu workspaceMenu;
    std::function<juce::String()> selectedPart;
    std::function<void(const juce::String&)> onInserted;
    bool componentCheck(juce::var& report,const juce::File& folder);
private:
    struct Category;
    int getNumRows() override;
    void paintListBoxItem(int,juce::Graphics&,int,int,bool) override;
    void selectedRowsChanged(int) override;
    void timerCallback() override;
    void filter();
    void showRecord();
    void importFile(bool replace=false);
    void insertSelected();
    void publish();
    void edit();
    void attach();
    juce::var command(juce::var);
    const juce::var* selected() const;
    std::shared_ptr<djehuti::cad::CadCommandService> service;
    juce::Array<juce::var> records;
    std::vector<int> shown;
    juce::String categoryPath;
    juce::TreeView categories;
    std::unique_ptr<Category> root;
    juce::ListBox list{"Components",this};
    juce::TextEditor search,details;
    juce::ComboBox manufacturer,origin,geometry,chemistry;
    juce::TextButton importButton{"Import workbook..."},replaceButton{"Replace import conflicts..."},reload{"Reload"},publishButton{"Save designed part..."},editButton{"Edit name / notes..."},attachButton{"Attach CAD..."},insert{"Insert occurrence"};
    juce::Label status,placementLabel;
    juce::OwnedArray<juce::TextEditor> placement;
    juce::OwnedArray<juce::Label> placementNames;
    Viewport3D preview{0};
    std::unique_ptr<djehuti::cad::CadDocument> previewDocument;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastImport;
    std::uint64_t frame=0;
};
bool runCadComponentUiCheck(juce::var&,const juce::File&,bool interactive=false);
