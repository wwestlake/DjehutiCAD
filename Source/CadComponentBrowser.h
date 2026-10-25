#pragma once
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "CadDocument.h"
#include "WorkspaceMenu.h"
#include "Viewport3D.h"

class CadComponentBrowser final : public juce::Component,private juce::ListBoxModel,private juce::Timer
{
public:
    // A prepared preview: everything the GUI needs, built off the GUI thread and delivered as an immutable result.
    struct Preview{std::shared_ptr<const djehuti::cad::CadRenderSnapshot> snapshot;std::shared_ptr<djehuti::cad::CadDocument> owner;std::uint64_t instance=0;bool ready=false;juce::String error,label;};
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
    struct PreviewJob;
    void previewReady(std::uint64_t generation,const juce::String& key,std::shared_ptr<Preview> result);
    void applyPreview(const Preview&);
    void selectCategoryItem();
    bool waitForPreview(int timeoutMs);
    void drainPreviewResults();
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
    Preview current;                                        // the preview shown now
    std::shared_ptr<const djehuti::cad::CadRenderSnapshot> emptySnapshot;
    std::map<juce::String,Preview> previewCache;            // GUI thread only: prepared previews by record id and revision
    std::deque<juce::String> previewOrder;
    // Prepared previews are queued here by the worker (thread-safe) and applied on the GUI thread, either by the posted
    // message or, in the synchronous component check, by waitForPreview().
    struct ResultQueue{std::mutex lock;std::vector<std::tuple<std::uint64_t,juce::String,std::shared_ptr<Preview>>> items;};
    std::shared_ptr<ResultQueue> results=std::make_shared<ResultQueue>();
    juce::String pendingKey;                                // record whose preview is being prepared (empty when idle)
    std::uint64_t previewGeneration=0;                      // GUI thread: the latest selection; later selections win
    std::shared_ptr<std::atomic<std::uint64_t>> latestGeneration=std::make_shared<std::atomic<std::uint64_t>>(0);
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastImport;
    std::uint64_t frame=0;
    juce::ThreadPool previewPool{juce::ThreadPoolOptions().withThreadName("component preview").withNumberOfThreads(1).withThreadStackSizeBytes(64u*1024u*1024u)};                        // declared last: destroyed first, so no job outlives the members it posts to
};
bool runCadComponentUiCheck(juce::var&,const juce::File&,bool interactive=false);
