#include "CadComponentBrowser.h"
#include <cmath>
#include <cstdlib>

using namespace djehuti::cad;
using namespace djehuti::cad::components;

struct CadComponentBrowser::Category final : juce::TreeViewItem
{
    CadComponentBrowser& owner;juce::String label,path;
    Category(CadComponentBrowser& o,juce::String name,juce::String p):owner(o),label(std::move(name)),path(std::move(p)){}
    bool mightContainSubItems() override{return getNumSubItems()>0;}
    juce::String getUniqueName() const override{return path.isEmpty()&&label.startsWith("All")?"all":path;}
    void paintItem(juce::Graphics& g,int w,int h)override{if(isSelected()){g.setColour(juce::Colour(0xff3f6f91));g.fillAll();g.setColour(juce::Colour(0xff8fc7ef));g.fillRect(0,0,3,h);}g.setColour(juce::Colours::white);g.setFont(13.0f);g.drawText(label,8,0,w-10,h,juce::Justification::centredLeft,true);}
    // Bounded work only: the list is filtered and the preview is requested; nothing here may wait for geometry.
    void itemSelectionChanged(bool selected)override{if(selected){owner.categoryPath=path;owner.filter();}}
};
// Builds a component preview. It owns its own document and touches no GUI object, so it can run on the preview worker.
static CadComponentBrowser::Preview buildPreview(const juce::var& record)
{
    CadComponentBrowser::Preview p;auto document=std::make_shared<CadDocument>();p.owner=document;juce::String error,instance;   // heap: a document is large and worker threads have small stacks
    p.ready=document->insertComponent(record,{},instance,error);p.error=error;p.snapshot=document->snapshot();p.instance=document->documentInstance();p.label=record["geometry"]["label"].toString();return p;
}
static juce::String previewKey(const juce::var& record){return record["id"].toString()+"#"+record["revision"].toString();}
struct CadComponentBrowser::PreviewJob final : juce::ThreadPoolJob
{
    PreviewJob(juce::Component::SafePointer<CadComponentBrowser> o,std::shared_ptr<std::atomic<std::uint64_t>> l,std::shared_ptr<ResultQueue> q,std::uint64_t g,juce::String k,juce::var r):juce::ThreadPoolJob("component preview"),owner(std::move(o)),latest(std::move(l)),queue(std::move(q)),generation(g),key(std::move(k)),record(std::move(r)){}
    JobStatus runJob() override
    {
        // A later selection makes queued work worthless; skip it before the expensive part starts.
        if(latest->load()!=generation||shouldExit())return jobHasFinished;
        auto result=std::make_shared<Preview>(buildPreview(record));
        {std::lock_guard<std::mutex> guard(queue->lock);queue->items.emplace_back(generation,key,result);}
        juce::MessageManager::callAsync([owner=owner]{if(owner!=nullptr)owner->drainPreviewResults();});
        return jobHasFinished;
    }
    juce::Component::SafePointer<CadComponentBrowser> owner;std::shared_ptr<std::atomic<std::uint64_t>> latest;std::shared_ptr<ResultQueue> queue;std::uint64_t generation;juce::String key;juce::var record;
};
CadComponentBrowser::CadComponentBrowser(std::shared_ptr<CadCommandService> c):service(std::move(c))
{
    for(auto* child:std::vector<juce::Component*>{&workspaceMenu,&categories,&list,&search,&details,&manufacturer,&origin,&geometry,&chemistry,&preview,&importButton,&replaceButton,&reload,&publishButton,&editButton,&attachButton,&insert,&status,&placementLabel})addAndMakeVisible(*child);
    search.setTextToShowWhenEmpty("Search names, manufacturers, models, part numbers...",juce::Colour(0xffa0b0c0));search.onTextChange=[this]{filter();};list.setRowHeight(48);
    for(auto* combo:{&manufacturer,&origin,&geometry,&chemistry})combo->onChange=[this]{filter();};
    origin.addItemList({"All origins","Commercial","Custom"},1);origin.setSelectedId(1,juce::dontSendNotification);
    geometry.addItemList({"All representations","Usable geometry","Catalog only"},1);geometry.setSelectedId(1,juce::dontSendNotification);
    details.setReadOnly(true);details.setMultiLine(true);details.setScrollbarsShown(true);details.setFont(juce::Font(13.0f));
    placementLabel.setText("Insertion placement: mm and degrees (X=width, Y=height, Z=length for envelopes)",juce::dontSendNotification);
    for(const auto* name:{"X","Y","Z","RX","RY","RZ"}){auto* label=placementNames.add(new juce::Label());label->setText(name,juce::dontSendNotification);addAndMakeVisible(label);auto* field=placement.add(new juce::TextEditor());field->setText("0");field->setTooltip(juce::String(name)+(juce::String(name).startsWith("R")?" degrees":" mm"));addAndMakeVisible(field);}
    importButton.onClick=[this]{importFile();};replaceButton.onClick=[this]{importFile(true);};reload.onClick=[this]{auto a=object();put(a,"action","reload");command(a);refresh();};publishButton.onClick=[this]{publish();};editButton.onClick=[this]{edit();};attachButton.onClick=[this]{attach();};insert.onClick=[this]{insertSelected();};replaceButton.setEnabled(false);
    workspaceMenu.sections=[] {return std::vector<WorkspaceMenu::Section>{{"Library",{{1,"Import workbook or portable library..."},{2,"Reload local library"},{3,"Save designed part..."}}}};};workspaceMenu.invoke=[this](int id){if(id==1)importFile();else if(id==2){auto a=object();put(a,"action","reload");command(a);refresh();}else publish();};
    emptySnapshot=std::make_unique<CadDocument>()->snapshot();refresh();startTimerHz(15);
}
CadComponentBrowser::~CadComponentBrowser(){stopTimer();latestGeneration->store(~std::uint64_t(0));previewPool.removeAllJobs(true,3000);categories.setRootItem(nullptr);root.reset();list.setModel(nullptr);}
void CadComponentBrowser::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff131e27));}
void CadComponentBrowser::resized()
{
    auto a=getLocalBounds().reduced(8);workspaceMenu.setBounds(a.removeFromTop(28));auto row=a.removeFromTop(34);importButton.setBounds(row.removeFromLeft(155).reduced(2));replaceButton.setBounds(row.removeFromLeft(210).reduced(2));reload.setBounds(row.removeFromLeft(75).reduced(2));publishButton.setBounds(row.removeFromLeft(165).reduced(2));status.setBounds(a.removeFromBottom(30));
    search.setBounds(a.removeFromTop(32).reduced(2));row=a.removeFromTop(32);for(auto* c:{&manufacturer,&origin,&geometry,&chemistry})c->setBounds(row.removeFromLeft(a.getWidth()/4).reduced(2));
    categories.setBounds(a.removeFromLeft(190).reduced(2));list.setBounds(a.removeFromLeft(juce::jlimit(220,380,a.getWidth()/2)).reduced(2));
    row=a.removeFromBottom(34);editButton.setBounds(row.removeFromLeft(155).reduced(2));attachButton.setBounds(row.removeFromLeft(115).reduced(2));insert.setBounds(row.reduced(2));
    row=a.removeFromBottom(32);for(int i=0;i<6;++i){auto cell=row.removeFromLeft(a.getWidth()/6);placementNames[i]->setBounds(cell.removeFromLeft(26));placement[i]->setBounds(cell.reduced(2));}placementLabel.setBounds(a.removeFromBottom(32));preview.setBounds(a.removeFromTop(a.getHeight()/2).reduced(2));details.setBounds(a.reduced(2));
}
juce::var CadComponentBrowser::command(juce::var args)
{const auto result=service->componentCommand(args);status.setText((bool)result["ok"]?"Library updated. Inserted definitions remain document snapshots.":result["error"].toString(),juce::dontSendNotification);status.setTooltip(status.getText());return result;}
void CadComponentBrowser::refresh()
{
    auto a=object();put(a,"action","list");const auto result=command(a);if(!(bool)result["ok"]){details.setText(result["error"].toString());return;}records=*result["records"].getArray();
    juce::StringArray manufacturers,chemistries;for(const auto& r:records){manufacturers.addIfNotAlreadyThere(r["manufacturer"].toString());if(auto* fields=r["properties"].getArray())for(const auto& field:*fields)if(field["label"].toString()=="Chemistry")chemistries.addIfNotAlreadyThere(field["value"].toString());}manufacturers.removeEmptyStrings();chemistries.removeEmptyStrings();manufacturers.sort(true);chemistries.sort(true);
    const auto oldManufacturer=manufacturer.getText(),oldChemistry=chemistry.getText();manufacturer.clear(juce::dontSendNotification);manufacturer.addItem("All manufacturers",1);manufacturer.addItemList(manufacturers,2);manufacturer.setSelectedId(juce::jmax(1,manufacturers.indexOf(oldManufacturer)+2),juce::dontSendNotification);chemistry.clear(juce::dontSendNotification);chemistry.addItem("All chemistries",1);chemistry.addItemList(chemistries,2);chemistry.setSelectedId(juce::jmax(1,chemistries.indexOf(oldChemistry)+2),juce::dontSendNotification);
    categories.setRootItem(nullptr);root=std::make_unique<Category>(*this,"Library","");root->addSubItem(new Category(*this,"All components ("+juce::String(records.size())+")",""));
    for(const auto& r:records)
    {
        auto* parent=root.get();juce::String path;for(const auto& name:juce::StringArray::fromTokens(r["category"].toString(),"/",{}))
        {if(name.isEmpty())continue;path+=(path.isEmpty()?"":"/")+name;Category* child=nullptr;for(int i=0;i<parent->getNumSubItems();++i){auto* candidate=static_cast<Category*>(parent->getSubItem(i));if(candidate->path==path){child=candidate;break;}}if(!child){child=new Category(*this,name,path);parent->addSubItem(child);}parent=child;}
    }
    categories.setRootItemVisible(false);categories.setRootItem(root.get());root->setOpen(true);for(int i=0;i<root->getNumSubItems();++i)root->getSubItem(i)->setOpen(true);
    previewCache.clear();previewOrder.clear();selectCategoryItem();filter();
}
// Keeps the tree's highlighted row equal to the displayed category (also after the tree is rebuilt). A category that no
// longer exists falls back to "All components".
void CadComponentBrowser::selectCategoryItem()
{
    Category* match=nullptr;std::function<void(Category&)> visit=[&](Category& item){if(match!=nullptr)return;if(&item!=root.get()&&item.path==categoryPath&&(!categoryPath.isEmpty()||item.label.startsWith("All")))match=&item;for(int i=0;i<item.getNumSubItems()&&match==nullptr;++i)visit(*static_cast<Category*>(item.getSubItem(i)));};
    if(root!=nullptr)visit(*root);
    if(match==nullptr){categoryPath.clear();if(root!=nullptr&&root->getNumSubItems()>0)match=static_cast<Category*>(root->getSubItem(0));}
    if(match!=nullptr)match->setSelected(true,true,juce::dontSendNotification);
}
void CadComponentBrowser::filter()
{
    const auto query=search.getText().trim();shown.clear();
    for(int i=0;i<records.size();++i)
    {
        const auto& r=records[i];const auto path=r["category"].toString();if(categoryPath.isNotEmpty()&&path!=categoryPath&&!path.startsWith(categoryPath+"/"))continue;
        juce::String haystack;for(const auto* key:{"name","manufacturer","model","partNumber","recordId","description"})haystack+=" "+r[key].toString();bool match=true;for(const auto& word:juce::StringArray::fromTokens(query," ",{}))match&=haystack.containsIgnoreCase(word);if(!match)continue;
        if(manufacturer.getSelectedId()>1&&r["manufacturer"].toString()!=manufacturer.getText())continue;
        if(origin.getSelectedId()>1&&r["origin"].toString()!=origin.getText().toLowerCase())continue;
        const bool ready=r["geometry"]["kind"].toString()!="none";if((geometry.getSelectedId()==2&&!ready)||(geometry.getSelectedId()==3&&ready))continue;
        if(chemistry.getSelectedId()>1){bool found=false;for(const auto& p:*r["properties"].getArray())found|=p["label"].toString()=="Chemistry"&&p["value"].toString()==chemistry.getText();if(!found)continue;}
        shown.push_back(i);
    }
    list.updateContent();if(shown.empty()){list.deselectAllRows();showRecord();}else {list.selectRow(0);showRecord();}if(pendingKey.isEmpty())status.setText(juce::String(shown.size())+" of "+juce::String(records.size())+" components",juce::dontSendNotification);
}
int CadComponentBrowser::getNumRows(){return (int)shown.size();}
void CadComponentBrowser::paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected)
{
    if(!juce::isPositiveAndBelow(row,(int)shown.size()))return;const auto& r=records[shown[row]];if(selected)g.fillAll(juce::Colour(0xff314d61));g.setColour(juce::Colours::white);g.setFont(14.0f);g.drawText(r["name"].toString(),6,2,w-12,h/2,juce::Justification::centredLeft,true);g.setColour(juce::Colour(0xffaabcc8));g.setFont(12.0f);g.drawText(r["origin"].toString()+" / "+r["geometry"]["level"].toString()+" / "+r["partNumber"].toString(),6,h/2,w-12,h/2,juce::Justification::centredLeft,true);
}
void CadComponentBrowser::selectedRowsChanged(int){showRecord();}
const juce::var* CadComponentBrowser::selected()const{const int row=list.getSelectedRow();return juce::isPositiveAndBelow(row,(int)shown.size())?records.begin()+shown[row]:nullptr;}
void CadComponentBrowser::showRecord()
{
    const auto* record=selected();
    insert.setEnabled(false);editButton.setEnabled(record!=nullptr);attachButton.setEnabled(record!=nullptr);
    if(record==nullptr)
    {
        latestGeneration->store(++previewGeneration);pendingKey.clear();current=Preview();
        details.setText("No matching components.",false);preview.setTitle("No component selected");preview.setSnapshot(emptySnapshot);return;
    }
    // Immediate and bounded: the description is shown now; the geometry preview is prepared on the worker thread.
    const auto key=previewKey(*record);const auto name=(*record)["name"].toString();
    if(const auto hit=previewCache.find(key);hit!=previewCache.end())
    {
        latestGeneration->store(++previewGeneration);pendingKey.clear();applyPreview(hit->second);return;
    }
    details.setText(components::details(*record)+"\nPreview: loading...",false);
    if(key==pendingKey&&pendingKey.isNotEmpty())return;                       // already being prepared; its result will apply
    latestGeneration->store(++previewGeneration);pendingKey=key;
    preview.setTitle("Loading preview: "+name);preview.setSnapshot(emptySnapshot);status.setText("Loading "+name+"...",juce::dontSendNotification);
    previewPool.addJob(new PreviewJob(juce::Component::SafePointer<CadComponentBrowser>(this),latestGeneration,results,previewGeneration,key,*record),true);
}
void CadComponentBrowser::previewReady(std::uint64_t generation,const juce::String& key,std::shared_ptr<Preview> result)
{
    previewCache[key]=*result;previewOrder.push_back(key);while(previewOrder.size()>32){previewCache.erase(previewOrder.front());previewOrder.pop_front();}
    if(generation!=previewGeneration)return;                                    // a later selection is authoritative
    pendingKey.clear();applyPreview(*result);
}
void CadComponentBrowser::applyPreview(const Preview& p)
{
    const auto* record=selected();if(record==nullptr)return;current=p;
    insert.setEnabled(p.ready);details.setText(components::details(*record)+(p.ready?juce::String():"\nGeometry: "+p.error),false);
    preview.setTitle((*record)["geometry"]["label"].toString());preview.setSnapshot(p.snapshot);preview.requestFit(p.instance);preview.prepareFrame(0,++frame);
    status.setText(juce::String(shown.size())+" of "+juce::String(records.size())+" components",juce::dontSendNotification);
}
void CadComponentBrowser::drainPreviewResults()
{
    std::vector<std::tuple<std::uint64_t,juce::String,std::shared_ptr<Preview>>> ready;
    {std::lock_guard<std::mutex> guard(results->lock);ready.swap(results->items);}
    for(auto& item:ready)previewReady(std::get<0>(item),std::get<1>(item),std::get<2>(item));
}
// Used by the synchronous component check, which runs on the GUI thread and cannot wait for posted messages.
bool CadComponentBrowser::waitForPreview(int timeoutMs)
{
    const auto end=juce::Time::getMillisecondCounter()+(juce::uint32)timeoutMs;
    for(;;){drainPreviewResults();if(pendingKey.isEmpty()||juce::Time::getMillisecondCounter()>=end)break;juce::Thread::sleep(10);}
    return pendingKey.isEmpty();
}
void CadComponentBrowser::insertSelected()
{
    const auto* item=selected();if(!item)return;auto p=object();int i=0;
    for(const auto* name:{"x","y","z","rx","ry","rz"}){const auto input=placement[i++]->getText().trim().toStdString();char* end=nullptr;const double value=std::strtod(input.c_str(),&end);if(input.empty()||end!=input.c_str()+input.size()||!std::isfinite(value)||std::abs(value)>1e9){status.setText("Placement needs finite numbers within +/- 1e9 (mm/degrees).",juce::dontSendNotification);return;}put(p,name,value);}
    auto a=object();put(a,"action","insert");put(a,"component",(*item)["id"]);put(a,"placement",p);const auto r=command(a);if((bool)r["ok"]){status.setText("Inserted "+r["id"].toString()+". Repeated insertions share one document definition.",juce::dontSendNotification);if(onInserted)onInserted(r["id"].toString());}
}
void CadComponentBrowser::importFile(bool replace)
{
    const auto apply=[safe=juce::Component::SafePointer<CadComponentBrowser>(this),replace](const juce::File& file)
    {if(safe==nullptr||file==juce::File())return;safe->lastImport=file;auto a=object();put(a,"action","importFile");put(a,"path",file.getFullPathName());put(a,"replaceConflicts",replace);const auto r=safe->command(a);if((bool)r["ok"]){safe->refresh();safe->details.setText(juce::JSON::toString(r));safe->replaceButton.setEnabled(r["import"]["conflicts"].size()>0);safe->status.setText("Import: "+r["import"]["imported"].toString()+" new, "+r["import"]["unchanged"].toString()+" unchanged, "+juce::String(r["import"]["conflicts"].size())+" conflicts retained. See report.",juce::dontSendNotification);}};
    if(replace){if(lastImport==juce::File())return;juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,"Replace catalog conflicts","Replace conflicting library records with this workbook's values? Existing CAD document snapshots remain intact.","Replace conflicts","Cancel",this,juce::ModalCallbackFunction::create([apply,file=lastImport](int result){if(result)apply(file);}));return;}
    chooser=std::make_unique<juce::FileChooser>("Import component workbook or portable library",juce::File(),"*.xlsx;*.json");chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[apply](const juce::FileChooser& fc){apply(fc.getResult());});
}
void CadComponentBrowser::publish()
{
    juce::StringArray ids,names;for(const auto& f:service->document().features())if(!f.shape.IsNull()&&TopExp_Explorer(f.shape,TopAbs_SOLID).More()){ids.add(f.id);names.add(f.name+" ["+f.id+"]");}
    if(ids.isEmpty()){status.setText("Create or open a solid CAD part before publishing.",juce::dontSendNotification);return;}
    auto* dialog=new juce::AlertWindow("Save designed part","Publish an exact evaluated solid snapshot. Source geometry and feature history remain unchanged.",juce::AlertWindow::NoIcon);dialog->addComboBox("part",names,"Solid part");const auto preferred=selectedPart?selectedPart():juce::String();dialog->getComboBoxComponent("part")->setSelectedItemIndex(juce::jmax(0,ids.indexOf(preferred)));dialog->addTextEditor("name","","Library name (blank uses part name)");dialog->addTextEditor("category","Custom/Parts","Category path");dialog->addTextEditor("description","","Description");dialog->addButton("Save component",1);dialog->addButton("Cancel",0);
    dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<CadComponentBrowser>(this),dialog,ids](int result)
    {if(!result||safe==nullptr)return;const int index=dialog->getComboBoxComponent("part")->getSelectedItemIndex();auto a=object();put(a,"action","publishPart");put(a,"feature",ids[index]);const auto name=dialog->getTextEditorContents("name").trim();if(name.isNotEmpty())put(a,"name",name);put(a,"category",dialog->getTextEditorContents("category"));put(a,"description",dialog->getTextEditorContents("description"));const auto r=safe->command(a);if((bool)r["ok"]){safe->categoryPath.clear();safe->search.clear();safe->origin.setSelectedId(3,juce::dontSendNotification);safe->manufacturer.setSelectedId(1,juce::dontSendNotification);safe->chemistry.setSelectedId(1,juce::dontSendNotification);safe->refresh();}}),true);
}
void CadComponentBrowser::edit()
{
    const auto* item=selected();if(!item)return;const auto id=(*item)["id"].toString();auto* dialog=new juce::AlertWindow("Component metadata","Edits are retained on repeated imports; conflicting source changes require an explicit replacement decision.",juce::AlertWindow::NoIcon);for(const auto* key:{"name","category","description"})dialog->addTextEditor(key,(*item)[key].toString(),key);dialog->addButton("Save",1);dialog->addButton("Cancel",0);
    dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<CadComponentBrowser>(this),dialog,id](int result){if(!result||safe==nullptr)return;auto a=object(),d=object();put(a,"action","update");put(a,"component",id);for(const auto* key:{"name","category","description"})put(d,key,dialog->getTextEditorContents(key));put(a,"definition",d);if((bool)safe->command(a)["ok"])safe->refresh();}),true);
}
void CadComponentBrowser::attach()
{
    const auto* item=selected();if(!item)return;const auto id=(*item)["id"].toString();chooser=std::make_unique<juce::FileChooser>("Attach compatible local manufacturer CAD",juce::File(),"*.step;*.stp;*.brep");chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<CadComponentBrowser>(this),id](const juce::FileChooser& fc){if(safe==nullptr||fc.getResult()==juce::File())return;auto a=object();put(a,"action","attachModel");put(a,"component",id);put(a,"path",fc.getResult().getFullPathName());if((bool)safe->command(a)["ok"])safe->refresh();});
}
void CadComponentBrowser::timerCallback(){if(isShowing())preview.prepareFrame(0,++frame);}
bool CadComponentBrowser::componentCheck(juce::var& report,const juce::File& folder)
{
    juce::Array<juce::var> checks;bool passed=true;const auto check=[&](bool ok,const char* name){auto v=object();put(v,"check",name);put(v,"passed",ok);checks.add(v);passed&=ok;};
    check(records.size()>=75,"Browser loads the researched component catalog");search.setText("BLF-1203W");filter();check(shown.size()==1&&selected()&&(*selected())["manufacturer"].toString()=="Bioenno Power","Search resolves a real manufacturer SKU");check(waitForPreview(120000)&&details.getText().contains("0.4 kg")&&details.getText().contains("Source:")&&current.snapshot!=nullptr&&!current.snapshot->meshes.empty(),"Component details preserve specifications/sources and the prepared preview contains geometry");
    insertSelected();placement[0]->setText("200");insertSelected();check(service->document().mechanicsData().instances.size()==2&&service->document().componentData()["definitions"].size()==1,"Browser inserts placed occurrences sharing one OCAF definition");check(service->document().mechanicsData().instances.back().placement.x==200,"Insertion numeric placement reaches the existing assembly model");
    search.clear();geometry.setSelectedId(3,juce::dontSendNotification);filter();check(shown.size()>=7&&!insert.isEnabled(),"Catalog-only records are browsable and cannot fabricate geometry");geometry.setSelectedId(1,juce::dontSendNotification);search.setText("BLF-1203W");filter();
    // TreeView normally lays out asynchronously. Flush its public root binding before the synchronous
    // component snapshot, so the diagnostic captures final category bounds rather than startup bounds.
    categories.setRootItem(nullptr);categories.setRootItem(root.get());check(root->getNumSubItems()==4&&categories.getNumRowsInTree()>=8,"Actual category hierarchy contains Structure, Motion and Electrical, with expandable children");
    // Navigation responsiveness: the selection callback must return at once, show the selection and a loading state, and a
    // later selection must stay authoritative over slower earlier loads.
    search.clear();manufacturer.setSelectedId(1,juce::dontSendNotification);origin.setSelectedId(1,juce::dontSendNotification);geometry.setSelectedId(1,juce::dontSendNotification);chemistry.setSelectedId(1,juce::dontSendNotification);
    juce::Array<juce::var> timings;double worstCallback=0;bool highlightFollows=true;
    const auto selectPath=[&](const juce::String& wanted)->Category*{Category* found=nullptr;for(int row=0;row<categories.getNumRowsInTree();++row)if(auto* item=static_cast<Category*>(categories.getItemOnRow(row)))if(item->path==wanted&&(!wanted.isEmpty()||item->label.startsWith("All"))){found=item;break;}return found;};
    for(int row=0;row<categories.getNumRowsInTree();++row)if(auto* item=static_cast<Category*>(categories.getItemOnRow(row)))
    {
        const auto start=juce::Time::getMillisecondCounterHiRes();item->setSelected(true,true);const auto ms=juce::Time::getMillisecondCounterHiRes()-start;worstCallback=juce::jmax(worstCallback,ms);
        highlightFollows&=item->isSelected()&&categoryPath==item->path;
        auto v=object();put(v,"category",item->path);put(v,"shown",(int)shown.size());put(v,"callbackMs",ms);put(v,"loading",pendingKey.isNotEmpty());timings.add(v);
    }
    check(worstCallback<150.0,"Selecting any category returns within 150 ms (geometry is prepared in the background)");
    check(highlightFollows,"The navigation tree highlights the category that was just selected");
    waitForPreview(300000);
    // Rapid switching: select the slowest-looking categories in a row, then a final one; only the final one may be shown.
    juce::StringArray order{"Motion/Motors","Motion/Actuators","Electrical/Drivers","Motion/Wheels & Hubs","Motion/Gearmotors"};int exercised=0;
    for(const auto& wanted:order)if(auto* item=selectPath(wanted)){item->setSelected(true,true);++exercised;}
    if(exercised>1)
    {
        const auto lastPath=categoryPath;const auto* shownNow=selected();const auto expected=shownNow!=nullptr?(*shownNow)["name"].toString():juce::String();
        waitForPreview(300000);
        const auto* afterWait=selected();check(afterWait!=nullptr&&(*afterWait)["name"].toString()==expected&&categoryPath==lastPath&&details.getText().contains(expected)&&pendingKey.isEmpty(),"Rapid category switching ends on the last selected category with its own preview (stale results are dropped)");
        auto* last=selectPath(lastPath);check(last!=nullptr&&last->isSelected(),"The highlight stays on the last selected category when background work completes");
    }
    
    juce::FileOutputStream stream(folder.getChildFile("component-browser.png"));check(stream.openedOk()&&juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),stream),"Component layout snapshot saved (excludes native OpenGL pixels)");report=object();put(report,"passed",passed);put(report,"rapidSwitchesExercised",exercised);put(report,"checks",checks);put(report,"selectionTimings",timings);put(report,"verification","JUCE controls/command wiring and component layout; not native Windows interaction");put(report,"folder",folder.getFullPathName());return passed;
}
bool runCadComponentUiCheck(juce::var& report,const juce::File& folder,bool interactive)
{
    folder.createDirectory();auto service=std::make_shared<CadCommandService>();service->setComponentLibraryFile(folder.getChildFile("library.json"));auto* browser=new CadComponentBrowser(service);browser->setSize(1380,960);juce::DialogWindow::LaunchOptions options;options.content.setOwned(browser);options.dialogTitle="Component Library check";options.useNativeTitleBar=true;auto* dialog=options.launchAsync();
    if(interactive){report=object();put(report,"folder",folder.getFullPathName());put(report,"verification","Native test window prepared; no programmatic controls invoked.");return true;}
    const auto ok=browser->componentCheck(report,folder);dialog->exitModalState(0);return ok;
}
