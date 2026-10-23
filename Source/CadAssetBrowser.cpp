#include "CadAssetBrowser.h"
using namespace djehuti::cad;using namespace djehuti::cad::components;
CadAssetBrowser::CadAssetBrowser(CadAssetLibrary& l,std::shared_ptr<CadCommandService> s):library(l),service(std::move(s))
{
    for(auto* c:std::vector<juce::Component*>{&workspaceMenu,&table,&search,&expand,&collapse,&reload,&scroll,&status})addAndMakeVisible(c);
    scroll.setViewedComponent(&editor,false);scroll.setScrollBarsShown(true,false);
    for(auto* c:std::vector<juce::Component*>{&source,&name,&categoryBox,&description,&material,&x,&y,&z,&saveButton,&replace,&updateButton,&remove,&insertButton,&exportButton,&categoryName,&createCategory,&renameCategory,&deleteCategory})editor.addAndMakeVisible(c);
    search.setTextToShowWhenEmpty("Search assets, descriptions, materials...",juce::Colours::grey);search.onTextChange=[this]{filter();};
    description.setMultiLine(true);description.setReturnKeyStartsNewLine(true);for(auto* e:{&x,&y,&z}){e->setText("0");e->setInputRestrictions(20,"0123456789.-");}
    const auto flags=juce::TableHeaderComponent::visible|juce::TableHeaderComponent::resizable|juce::TableHeaderComponent::draggable;
    int id=0;for(const auto* title:{"Category / Asset","Type","Material","Dimensions (mm)","Modified"})table.getHeader().addColumn(title,++id,id==1?230:150,70,1500,flags);
    table.setRowHeight(28);table.setColour(juce::ListBox::backgroundColourId,juce::Colour(0xff111b23));
    expand.onClick=[this]{if(!catalog["categories"].isArray())return;for(const auto& c:*catalog["categories"].getArray())expanded.insert(c["id"].toString());filter();};collapse.onClick=[this]{expanded.clear();filter();};
    reload.onClick=[this]{auto a=object();put(a,"action","reload");run(a);};
    saveButton.onClick=[this]{save(false);};replace.onClick=[this]{save(true);};updateButton.onClick=[this]{update();};insertButton.onClick=[this]{insert();};exportButton.onClick=[this]{exportSource();};
    createCategory.onClick=[this]{category("createCategory");};renameCategory.onClick=[this]{category("renameCategory");};deleteCategory.onClick=[this]{category("deleteCategory");};
    remove.onClick=[this]{if(selectedAsset.isEmpty())return;juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,"Remove library asset","Remove this library entry? Previously inserted copies remain intact.","Remove","Cancel",this,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<CadAssetBrowser>(this)](int yes){if(yes&&safe!=nullptr){auto a=object();put(a,"action","delete");put(a,"asset",safe->selectedAsset);safe->run(a);}}));};
    workspaceMenu.sections=[this]{return std::vector<WorkspaceMenu::Section>{{"Assets",{{1,"Save selected design"},{2,"Expand All"},{3,"Collapse All"},{4,"Reload"}}}};};
    workspaceMenu.invoke=[this](int id){if(id==1)beginSave(selectedPart?selectedPart():juce::String());if(id==2)expand.onClick();if(id==3)collapse.onClick();if(id==4)reload.onClick();};
    refresh();sources();startTimerHz(2);
}
CadAssetBrowser::~CadAssetBrowser(){stopTimer();scroll.setViewedComponent(nullptr,false);table.setModel(nullptr);}
void CadAssetBrowser::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff141e27));}
void CadAssetBrowser::resized()
{
    auto area=getLocalBounds().reduced(8);workspaceMenu.setBounds(area.removeFromTop(28));auto bar=area.removeFromTop(34);search.setBounds(bar.removeFromLeft(juce::jmax(100,bar.getWidth()-320)).reduced(2));expand.setBounds(bar.removeFromLeft(105).reduced(2));collapse.setBounds(bar.removeFromLeft(115).reduced(2));reload.setBounds(bar.reduced(2));
    status.setBounds(area.removeFromBottom(26));scroll.setBounds(area.removeFromRight(juce::jmin(350,juce::jmax(220,area.getWidth()/3))));area.removeFromRight(8);table.setBounds(area);
    editor.setSize(juce::jmax(200,scroll.getWidth()-scroll.getScrollBarThickness()),630);auto form=editor.getLocalBounds().reduced(6);captions.clear();
    const auto field=[&](const char* label,juce::Component& c,int h=25){auto* caption=captions.add(new juce::Label({},label));caption->setColour(juce::Label::textColourId,juce::Colours::lightgrey);editor.addAndMakeVisible(caption);caption->setBounds(form.removeFromTop(19));c.setBounds(form.removeFromTop(h));form.removeFromTop(5);};
    field("Save source (part or entire assembly)",source);field("Asset name",name);field("Category",categoryBox);field("Description",description,48);field("Material / material note",material);
    auto* caption=captions.add(new juce::Label({},"Insert position X / Y / Z (mm)"));caption->setColour(juce::Label::textColourId,juce::Colours::lightgrey);editor.addAndMakeVisible(caption);caption->setBounds(form.removeFromTop(20));auto xyz=form.removeFromTop(25);const int w=xyz.getWidth()/3;x.setBounds(xyz.removeFromLeft(w).reduced(1));y.setBounds(xyz.removeFromLeft(w).reduced(1));z.setBounds(xyz.reduced(1));form.removeFromTop(8);
    const auto buttons=[&](juce::Button& a,juce::Button& b){auto row=form.removeFromTop(31);a.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));b.setBounds(row.reduced(1));};
    buttons(saveButton,replace);buttons(updateButton,remove);buttons(insertButton,exportButton);form.removeFromTop(10);field("Category management name",categoryName);buttons(createCategory,renameCategory);deleteCategory.setBounds(form.removeFromTop(28));
}
void CadAssetBrowser::sources()
{
    const auto old=chosenSource();source.clear();sourceIds.clear();
    for(const auto& f:service->document().features())if(!f.shape.IsNull()&&TopExp_Explorer(f.shape,TopAbs_SOLID).More()){sourceIds.add(f.id);source.addItem(f.name,sourceIds.size());}
    if(!service->document().mechanicsData().instances.empty()){sourceIds.add("assembly");source.addItem("Entire document assembly",sourceIds.size());}
    int index=sourceIds.indexOf(old);if(index<0&&selectedPart)index=sourceIds.indexOf(selectedPart());source.setSelectedId(index>=0?index+1:sourceIds.size()>0?1:0,juce::dontSendNotification);
    revision=service->document().revision();documentInstance=service->document().documentInstance();
}
void CadAssetBrowser::timerCallback(){if(isShowing()&&(revision!=service->document().revision()||documentInstance!=service->document().documentInstance()))sources();}
juce::String CadAssetBrowser::chosenSource()const{return sourceIds[source.getSelectedId()-1];}
juce::String CadAssetBrowser::chosenCategory()const{return categoryIds[categoryBox.getSelectedId()-1];}
void CadAssetBrowser::refresh(){auto a=object();put(a,"action","list");const auto r=library.command(a,service->document());if(!(bool)r["ok"]){status.setText(r["error"].toString(),juce::dontSendNotification);return;}catalog=r;const auto old=chosenCategory();categoryBox.clear();categoryIds.clear();for(const auto& c:*catalog["categories"].getArray()){categoryIds.add(c["id"].toString());categoryBox.addItem(c["name"].toString(),categoryIds.size());}const int index=categoryIds.indexOf(old);categoryBox.setSelectedId(index>=0?index+1:categoryIds.size()>0?1:0,juce::dontSendNotification);filter();}
void CadAssetBrowser::filter()
{
    rows.clear();const auto query=search.getText().trim();
    if(!catalog["categories"].isArray())return;
    for(const auto& c:*catalog["categories"].getArray())
    {
        std::vector<juce::var> assets;for(const auto& a:*catalog["assets"].getArray())if(a["category"]==c["id"])
        {const auto haystack=c["name"].toString()+" "+a["name"].toString()+" "+a["description"].toString()+" "+a["materialLabel"].toString();if(query.isEmpty()||haystack.containsIgnoreCase(query))assets.push_back(a);}
        if(query.isNotEmpty()&&assets.empty())continue;rows.push_back({true,c});if(expanded.count(c["id"].toString())||query.isNotEmpty())for(const auto& a:assets)rows.push_back({false,a});
    }
    table.updateContent();table.deselectAllRows();for(int i=0;i<(int)rows.size();++i)if(!rows[i].category&&rows[i].value["id"].toString()==selectedAsset){table.selectRow(i);break;}table.repaint();status.setText(catalog["assets"].size()==0?"Your Asset Library is empty. Select a solid or assembly, then Save new asset.":juce::String(catalog["assets"].size())+" assets. Click a category to expand it; inserted copies are independent.",juce::dontSendNotification);
}
void CadAssetBrowser::paintRowBackground(juce::Graphics& g,int row,int w,int h,bool selected){if(selected)g.fillAll(juce::Colour(0xff315164));else if(juce::isPositiveAndBelow(row,(int)rows.size())&&rows[row].category)g.fillAll(juce::Colour(0xff23303b));g.setColour(juce::Colour(0xff30414d));g.drawHorizontalLine(h-1,0,(float)w);}
void CadAssetBrowser::paintCell(juce::Graphics& g,int row,int column,int w,int h,bool)
{
    if(!juce::isPositiveAndBelow(row,(int)rows.size()))return;const auto& r=rows[row];juce::String text;
    if(r.category){if(column==1)text=(expanded.count(r.value["id"].toString())||search.getText().isNotEmpty()?"v  ":">  ")+r.value["name"].toString();}
    else if(column==1)text="    "+r.value["name"].toString();else if(column==2)text=r.value["type"].toString();else if(column==3)text=r.value["materialLabel"].toString();else if(column==5)text=r.value["modified"].toString();else if(column==4&&r.value["dimensions"].size()==3)text=juce::String((double)r.value["dimensions"][0],3)+" x "+juce::String((double)r.value["dimensions"][1],3)+" x "+juce::String((double)r.value["dimensions"][2],3);
    g.setColour(juce::Colours::whitesmoke);g.setFont(juce::Font(13,r.category?juce::Font::bold:juce::Font::plain));g.drawText(text,5,0,w-10,h,juce::Justification::centredLeft,true);
}
void CadAssetBrowser::selectedRowsChanged(int row)
{
    if(!juce::isPositiveAndBelow(row,(int)rows.size()))return;const auto& r=rows[row];selectedCategory=r.category?r.value["id"].toString():r.value["category"].toString();categoryBox.setSelectedId(categoryIds.indexOf(selectedCategory)+1,juce::dontSendNotification);
    for(const auto& c:*catalog["categories"].getArray())if(c["id"].toString()==selectedCategory)categoryName.setText(c["name"].toString());
    selectedAsset=r.category?juce::String():r.value["id"].toString();if(!r.category){name.setText(r.value["name"].toString());description.setText(r.value["description"].toString());material.setText(r.value["materialLabel"].toString());}
}
void CadAssetBrowser::cellClicked(int row,int column,const juce::MouseEvent&){if(juce::isPositiveAndBelow(row,(int)rows.size())&&rows[row].category&&column==1){const auto id=rows[row].value["id"].toString();if(expanded.count(id))expanded.erase(id);else expanded.insert(id);filter();selectedCategory=id;}}
void CadAssetBrowser::cellDoubleClicked(int row,int,const juce::MouseEvent&){if(juce::isPositiveAndBelow(row,(int)rows.size())&&!rows[row].category)insert();}
juce::var CadAssetBrowser::run(juce::var args){auto r=library.command(args,service->document());lastResult=r;if((bool)r["ok"])refresh();status.setText((bool)r["ok"]?"Asset operation completed.":r["error"].toString(),juce::dontSendNotification);return r;}
void CadAssetBrowser::beginSave(const juce::String& selected)
{
    refresh();sources();auto id=selected;if(const auto* i=service->document().mechanicsData().findInstance(id))id=i->featureId;source.setSelectedId(sourceIds.indexOf(id)+1,juce::dontSendNotification);
    if(source.getSelectedId()==0&&sourceIds.size())source.setSelectedId(1,juce::dontSendNotification);if(const auto* f=service->document().find(chosenSource()))name.setText(f->name);else name.setText("My assembly");selectedAsset.clear();description.clear();material.clear();
}
void CadAssetBrowser::save(bool replacing){auto a=object();put(a,"action","save");put(a,"source",chosenSource());put(a,"name",name.getText());put(a,"category",chosenCategory());put(a,"description",description.getText());put(a,"materialLabel",material.getText());if(replacing){if(selectedAsset.isEmpty()){status.setText("Select the asset to replace first.",juce::dontSendNotification);return;}put(a,"replace",selectedAsset);}auto r=run(a);if((bool)r["ok"]){selectedAsset=r["id"].toString();expanded.insert(chosenCategory());filter();}}
void CadAssetBrowser::update(){auto a=object(),d=object();for(const auto& p:std::vector<std::pair<const char*,juce::String>>{{"name",name.getText()},{"category",chosenCategory()},{"description",description.getText()},{"materialLabel",material.getText()}})put(d,p.first,p.second);put(a,"action","update");put(a,"asset",selectedAsset);put(a,"definition",d);run(a);}
void CadAssetBrowser::category(const juce::String& action){auto a=object();put(a,"action",action);put(a,"category",chosenCategory());put(a,"name",categoryName.getText());run(a);}
void CadAssetBrowser::insert()
{
    auto a=object(),p=object();for(const auto& field:std::vector<std::pair<const char*,juce::TextEditor*>>{{"x",&x},{"y",&y},{"z",&z}}){const auto text=field.second->getText().trim().toStdString();char* end=nullptr;const double n=std::strtod(text.c_str(),&end);if(text.empty()||end!=text.c_str()+text.size()||!std::isfinite(n)){status.setText("Enter finite X/Y/Z insertion coordinates.",juce::dontSendNotification);return;}put(p,field.first,n);}put(a,"action","insert");put(a,"asset",selectedAsset);put(a,"placement",p);const auto r=run(a);if((bool)r["ok"]&&onInserted)onInserted(r["id"].toString());
}
void CadAssetBrowser::exportSource(){if(selectedAsset.isEmpty())return;chooser=std::make_unique<juce::FileChooser>("Export editable asset copy",juce::File(),"*.djc");chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<CadAssetBrowser>(this),id=selectedAsset](const juce::FileChooser& fc){if(safe!=nullptr&&fc.getResult()!=juce::File()){auto a=object();put(a,"action","exportSource");put(a,"asset",id);put(a,"path",fc.getResult().withFileExtension("djc").getFullPathName());safe->run(a);}});}
juce::var CadAssetBrowser::ui(const juce::var& args)
{
    juce::String error;const auto op=args.getProperty("operation","state").toString();
    if(op=="expandAll")expand.onClick();else if(op=="collapseAll")collapse.onClick();
    else if(op=="beginSave")beginSave(args["source"].toString());
    else if(op=="select"){bool found=false;for(int i=0;i<(int)rows.size();++i)if(rows[i].value["id"]==args["id"]){table.selectRow(i);found=true;break;}if(!found)error="Expand the category or search before selecting an asset row.";}
    else if(op=="set")
    {
        const auto key=args["field"].toString(),value=args["value"].toString();juce::TextEditor* input=key=="name"?&name:key=="description"?&description:key=="material"?&material:key=="categoryName"?&categoryName:key=="search"?&search:key=="x"?&x:key=="y"?&y:key=="z"?&z:nullptr;
        if(input)input->setText(value,true);else if(key=="source"&&sourceIds.contains(value))source.setSelectedId(sourceIds.indexOf(value)+1,juce::dontSendNotification);else if(key=="category"&&categoryIds.contains(value))categoryBox.setSelectedId(categoryIds.indexOf(value)+1,juce::dontSendNotification);else error="Unknown field or choice.";
    }
    else if(op=="click")
    {
        const auto key=args["button"].toString();juce::Button* button=key=="save"?&saveButton:key=="replace"?&replace:key=="update"?&updateButton:key=="insert"?&insertButton:key=="createCategory"?&createCategory:key=="renameCategory"?&renameCategory:key=="deleteCategory"?&deleteCategory:nullptr;if(button&&button->onClick){lastResult=juce::var();button->onClick();if(lastResult.hasProperty("ok")&&!(bool)lastResult["ok"])error=lastResult["error"].toString();}else error="Unknown button.";
    }
    else if(op=="capture")
    {const auto path=args["path"].toString();if(!juce::File::isAbsolutePath(path)||!juce::File(path).hasFileExtension("png")||!juce::File(path).getParentDirectory().isDirectory())error="Choose an absolute PNG path in an existing directory.";else if(auto stream=juce::File(path).createOutputStream()){stream->setPosition(0);stream->truncate();juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),*stream);}else error="Cannot write component render.";}
    else if(op!="state")error="Unknown asset UI operation.";
    auto r=object();put(r,"ok",error.isEmpty());if(error.isNotEmpty())put(r,"error",error);juce::Array<juce::var> visible;for(const auto& row:rows){auto value=clone(row.value);put(value,"categoryRow",row.category);visible.add(value);}put(r,"rows",visible);put(r,"expandedCount",(int)expanded.size());put(r,"selected",selectedAsset);put(r,"message",status.getText());put(r,"source",chosenSource());return r;
}
