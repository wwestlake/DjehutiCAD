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
    void paintItem(juce::Graphics& g,int w,int h)override{if(isSelected()){g.setColour(juce::Colour(0xff314d61));g.fillAll();}g.setColour(juce::Colours::white);g.setFont(13.0f);g.drawText(label,4,0,w-6,h,juce::Justification::centredLeft,true);}
    void itemSelectionChanged(bool selected)override{if(selected){owner.categoryPath=path;owner.filter();}}
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
    refresh();startTimerHz(15);
}
CadComponentBrowser::~CadComponentBrowser(){stopTimer();categories.setRootItem(nullptr);root.reset();list.setModel(nullptr);}
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
    categories.setRootItemVisible(false);categories.setRootItem(root.get());root->setOpen(true);for(int i=0;i<root->getNumSubItems();++i)root->getSubItem(i)->setOpen(true);filter();
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
    list.updateContent();if(shown.empty()){list.deselectAllRows();showRecord();}else {list.selectRow(0);showRecord();}status.setText(juce::String(shown.size())+" of "+juce::String(records.size())+" components",juce::dontSendNotification);
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
    const auto* record=selected();previewDocument=std::make_unique<CadDocument>();juce::String error,instance;
    const bool ready=record&&previewDocument->insertComponent(*record,{},instance,error);insert.setEnabled(ready);editButton.setEnabled(record!=nullptr);attachButton.setEnabled(record!=nullptr);
    details.setText(record?components::details(*record)+(ready?juce::String():"\nGeometry: "+error):juce::String("No matching components."),false);preview.setTitle(record?(*record)["geometry"]["label"].toString():"No component selected");preview.setSnapshot(previewDocument->snapshot());preview.requestFit(previewDocument->documentInstance());preview.prepareFrame(0,++frame);
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
    check(records.size()>=75,"Browser loads the researched component catalog");search.setText("BLF-1203W");filter();check(shown.size()==1&&selected()&&(*selected())["manufacturer"].toString()=="Bioenno Power","Search resolves a real manufacturer SKU");check(details.getText().contains("0.4 kg")&&details.getText().contains("Source:")&&!previewDocument->snapshot()->meshes.empty(),"Component details preserve specifications/sources and create preview geometry");
    insertSelected();placement[0]->setText("200");insertSelected();check(service->document().mechanicsData().instances.size()==2&&service->document().componentData()["definitions"].size()==1,"Browser inserts placed occurrences sharing one OCAF definition");check(service->document().mechanicsData().instances.back().placement.x==200,"Insertion numeric placement reaches the existing assembly model");
    search.clear();geometry.setSelectedId(3,juce::dontSendNotification);filter();check(shown.size()==7&&!insert.isEnabled(),"Catalog-only records are browsable and cannot fabricate geometry");geometry.setSelectedId(1,juce::dontSendNotification);search.setText("BLF-1203W");filter();
    // TreeView normally lays out asynchronously. Flush its public root binding before the synchronous
    // component snapshot, so the diagnostic captures final category bounds rather than startup bounds.
    categories.setRootItem(nullptr);categories.setRootItem(root.get());check(root->getNumSubItems()==4&&categories.getNumRowsInTree()>=8,"Actual category hierarchy contains Structure, Motion and Electrical, with expandable children");
    juce::FileOutputStream stream(folder.getChildFile("component-browser.png"));check(stream.openedOk()&&juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),stream),"Component layout snapshot saved (excludes native OpenGL pixels)");report=object();put(report,"passed",passed);put(report,"checks",checks);put(report,"verification","JUCE controls/command wiring and component layout; not native Windows interaction");put(report,"folder",folder.getFullPathName());return passed;
}
bool runCadComponentUiCheck(juce::var& report,const juce::File& folder,bool interactive)
{
    folder.createDirectory();auto service=std::make_shared<CadCommandService>();service->setComponentLibraryFile(folder.getChildFile("library.json"));auto* browser=new CadComponentBrowser(service);browser->setSize(1380,960);juce::DialogWindow::LaunchOptions options;options.content.setOwned(browser);options.dialogTitle="Component Library check";options.useNativeTitleBar=true;auto* dialog=options.launchAsync();
    if(interactive){report=object();put(report,"folder",folder.getFullPathName());put(report,"verification","Native test window prepared; no programmatic controls invoked.");return true;}
    const auto ok=browser->componentCheck(report,folder);dialog->exitModalState(0);return ok;
}
