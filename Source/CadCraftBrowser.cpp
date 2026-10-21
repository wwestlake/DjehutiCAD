#include "CadCraftBrowser.h"
#include "CadCraftStock.h"
#include <cmath>
#include <cstdlib>

using namespace djehuti::cad;
using namespace djehuti::cad::components;
namespace
{
bool numeric(const juce::String& text, double& value)
{
    const auto raw=text.trim().toStdString(); char* end=nullptr;
    value=std::strtod(raw.c_str(), &end);
    return !raw.empty() && end==raw.c_str()+raw.size() && std::isfinite(value);
}
}
CadCraftBrowser::CadCraftBrowser(std::shared_ptr<CadCommandService> shared) : service(std::move(shared))
{
    addAndMakeVisible(workspaceMenu); addAndMakeVisible(scroll); scroll.setViewedComponent(&content,false); scroll.setScrollBarsShown(true,false);
    for(auto* c:std::vector<juce::Component*>{&heading,&category,&search,&list,&mode,&details,&materialLabel,&material,&placementLabel,&status,&editPiece,&templatesButton,&insert,&apply,&assign,&move,&linked,&saveDefaults,&duplicate,&materialsButton,&reloadButton})content.addAndMakeVisible(c);
    heading.setText("Craft Stock",juce::dontSendNotification); heading.setFont(juce::Font(22.0f));
    category.addItemList({"All categories","Wood","Plastic","Cardboard","Foam board"},1);category.setSelectedId(1);
    category.onChange=[this]{filter();};search.setTextToShowWhenEmpty("Search craft templates...",juce::Colours::grey);search.onTextChange=[this]{filter();};
    list.setRowHeight(36);details.setMultiLine(true);details.setReadOnly(true);details.setScrollbarsShown(true);
    materialLabel.setText("Material / approximate density",juce::dontSendNotification);
    placementLabel.setText("Placement: mm / rotation: degrees",juce::dontSendNotification);
    status.setMinimumHorizontalScale(1.0f);status.setJustificationType(juce::Justification::topLeft);
    for(int i=0;i<3;++i){auto* label=dimensionLabels.add(new juce::Label());auto* value=dimensions.add(new juce::TextEditor());content.addAndMakeVisible(label);content.addAndMakeVisible(value);value->setName("Stock dimension "+juce::String(i+1));}
    for(const auto* name:{"X","Y","Z","RX","RY","RZ"})
    {auto* label=placementLabels.add(new juce::Label());label->setText(name,juce::dontSendNotification);content.addAndMakeVisible(label);auto* value=placements.add(new juce::TextEditor());value->setText("0");value->setName(juce::String("Placement ")+name);content.addAndMakeVisible(value);}
    templatesButton.onClick=[this]{loadTemplate();};editPiece.onClick=[this]{editSelected();};insert.onClick=[this]{insertPiece();};
    apply.onClick=[this]{applyDimensions();};assign.onClick=[this]{applyMaterial();};move.onClick=[this]{applyPlacement();};linked.onClick=[this]{linkedCopy();};
    saveDefaults.onClick=[this]{saveTemplate(false);};duplicate.onClick=[this]{saveTemplate(true);};materialsButton.onClick=[this]{if(onMaterials)onMaterials();};
    reloadButton.onClick=[this]{auto a=object();put(a,"action","reload");service->componentCommand(a);reload();};
    reload();startTimerHz(3);
}
CadCraftBrowser::~CadCraftBrowser(){stopTimer();scroll.setViewedComponent(nullptr,false);}
void CadCraftBrowser::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff1d222b));}
void CadCraftBrowser::resized()
{
    auto a=getLocalBounds();workspaceMenu.setBounds(a.removeFromTop(28));scroll.setBounds(a);
    content.setSize(juce::jmax(350,scroll.getMaximumVisibleWidth()), 850);
    auto r=content.getLocalBounds().reduced(8);heading.setBounds(r.removeFromTop(32));category.setBounds(r.removeFromTop(30).reduced(0,2));search.setBounds(r.removeFromTop(30).reduced(0,2));
    list.setBounds(r.removeFromTop(145));auto row=r.removeFromTop(32);editPiece.setBounds(row.removeFromLeft(row.getWidth()*2/3).reduced(1));templatesButton.setBounds(row.reduced(1));
    mode.setBounds(r.removeFromTop(30));details.setBounds(r.removeFromTop(105));
    for(int i=0;i<3;++i){row=r.removeFromTop(30);dimensionLabels[i]->setBounds(row.removeFromLeft(160));dimensions[i]->setBounds(row.reduced(1));}
    materialLabel.setBounds(r.removeFromTop(24));material.setBounds(r.removeFromTop(30).reduced(1));
    placementLabel.setBounds(r.removeFromTop(24));for(int line=0;line<2;++line){row=r.removeFromTop(30);const int width=row.getWidth()/3;for(int col=0;col<3;++col){const int i=line*3+col;auto cell=row.removeFromLeft(width);placementLabels[i]->setBounds(cell.removeFromLeft(25));placements[i]->setBounds(cell.reduced(1));}}
    insert.setBounds(r.removeFromTop(32).reduced(1));
    row=r.removeFromTop(32);apply.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));assign.setBounds(row.reduced(1));
    row=r.removeFromTop(32);move.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));linked.setBounds(row.reduced(1));
    row=r.removeFromTop(32);saveDefaults.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));duplicate.setBounds(row.reduced(1));
    row=r.removeFromTop(32);materialsButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));reloadButton.setBounds(row.reduced(1));status.setBounds(r);
}
void CadCraftBrowser::message(const juce::String& text,bool error){status.setColour(juce::Label::textColourId,error?juce::Colours::salmon:juce::Colours::lightgrey);status.setText(text,juce::dontSendNotification);}
int CadCraftBrowser::getNumRows(){return (int)shown.size();}
void CadCraftBrowser::paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected)
{if(row<0||row>=(int)shown.size())return;if(selected)g.fillAll(juce::Colour(0xff385572));g.setColour(juce::Colours::white);g.drawText(records[shown[(size_t)row]]["name"].toString(),5,0,w-10,h,juce::Justification::centredLeft);}
void CadCraftBrowser::selectedRowsChanged(int){loadTemplate();}
void CadCraftBrowser::reload(const juce::String& select)
{
    auto a=object();put(a,"action","list");const auto result=service->componentCommand(a);
    if(!(bool)result["ok"]){message(result["error"].toString(),true);return;}
    records.clear();if(const auto* entries=result["records"].getArray())for(const auto& entry:*entries)if(craft::isStock(entry))records.add(clone(entry));
    filter();if(select.isNotEmpty())for(int i=0;i<(int)shown.size();++i)if(records[shown[(size_t)i]]["id"].toString()==select){list.selectRow(i);loadTemplate();break;}
    documentInstance=service->document().documentInstance();materialRevision=service->document().revision();
}
void CadCraftBrowser::filter()
{
    shown.clear();const auto terms=juce::StringArray::fromTokens(search.getText().toLowerCase()," ","");
    for(int i=0;i<records.size();++i)
    {
        const auto& entry=records[i];if(category.getSelectedId()>1&&!entry["category"].toString().endsWith("/"+category.getText()))continue;
        const auto haystack=(entry["name"].toString()+" "+entry["category"].toString()+" "+entry["geometry"]["profile"].toString()).toLowerCase();bool matches=true;
        for(const auto& term:terms)if(!haystack.contains(term))matches=false;if(matches)shown.push_back(i);
    }
    list.updateContent();list.deselectAllRows();if(!shown.empty())list.selectRow(0);loadTemplate();
}
void CadCraftBrowser::loadTemplate()
{
    editingFeature.clear();editingInstance.clear();const int row=list.getSelectedRow();
    draft=row>=0&&row<(int)shown.size()?clone(records[shown[(size_t)row]]):juce::var();fill(draft);
}
void CadCraftBrowser::fill(const juce::var& record,const juce::String& materialId)
{
    const auto keys=craft::dimensionKeys(record["geometry"]["profile"].toString());
    for(int i=0;i<3;++i){const bool used=i<keys.size();dimensionLabels[i]->setVisible(used);dimensions[i]->setVisible(used);if(used){dimensionLabels[i]->setText(craft::dimensionLabel(keys[i]),juce::dontSendNotification);dimensions[i]->setText(record["geometry"][juce::Identifier(keys[i])].toString());}}
    const bool editing=editingFeature.isNotEmpty();mode.setText(editing?"Editing piece: "+editingInstance:"Template: "+record["name"].toString(),juce::dontSendNotification);
    auto text=record["description"].toString()+"\nProfile: "+record["geometry"]["profile"].toString()+"; length along +X.\nTemplate: "+record.getProperty("templateId",record["id"]).toString()+" / revision "+record.getProperty("templateRevision",record["revision"]).toString();
    if(editing){int count=0;for(const auto& instance:service->document().mechanicsData().instances)if(instance.featureId==editingFeature)++count;text+="\nLinked occurrences of this definition: "+juce::String(count)+". Dimension/material edits affect them all.";}
    details.setText(text,false);loadMaterials(materialId);
    insert.setEnabled(!editing&&record.isObject());saveDefaults.setEnabled(!editing&&record.isObject());duplicate.setEnabled(!editing&&record.isObject());apply.setEnabled(editing);assign.setEnabled(editing);move.setEnabled(editing);
    message(editing?"Edit dimensions, material or placement, then apply that change.":"Illustrative dimensions and densities. Enter your actual stock measurements.");
}
void CadCraftBrowser::loadMaterials(const juce::String& materialId)
{
    material.clear(juce::dontSendNotification);materialRecords.clear();
    auto a=object();put(a,"action","list");const auto result=service->catalogCommand(a);
    if(const auto* entries=result["catalog"]["materials"].getArray())for(const auto& entry:*entries)materialRecords.add(clone(entry));
    for(const auto& entry:service->document().materialsData().materials)materialRecords.add(matl::materialToJson(entry));
    const auto desired=materialId.isEmpty()?draft.getProperty("material",{})["id"].toString():materialId;
    int chosen=0;for(int i=0;i<materialRecords.size();++i)
    {
        const auto& entry=materialRecords[i];const auto density=entry["properties"]["density"]["value"].toString();
        material.addItem(entry["name"].toString()+" — "+density+" kg/m^3",i+1);
        if(entry["id"].toString()==desired|| (desired.isEmpty()&&entry["id"]==draft["materialCatalogId"]))chosen=i+1;
    }
    // A customized template may hold a material snapshot not yet in this document/catalog.
    if(!chosen&&draft["material"].isObject()){materialRecords.add(clone(draft["material"]));chosen=materialRecords.size();material.addItem(draft["material"]["name"].toString(),chosen);}
    material.setSelectedId(chosen,juce::dontSendNotification);material.setTextWhenNothingSelected("Choose an existing material");
}
juce::var CadCraftBrowser::selectedMaterial()const
{const int index=material.getSelectedItemIndex();return index>=0&&index<materialRecords.size()?clone(materialRecords[index]):juce::var();}
bool CadCraftBrowser::readGeometry(juce::var& geometry)
{
    geometry=clone(draft["geometry"]);const auto keys=craft::dimensionKeys(geometry["profile"].toString());
    for(int i=0;i<keys.size();++i){double number=0;if(!numeric(dimensions[i]->getText(),number)){message("Enter valid numeric dimensions in mm.",true);return false;}put(geometry,keys[i],number);}
    juce::String error;if(!craft::validateGeometry(geometry,error)){message(error,true);return false;}return true;
}
bool CadCraftBrowser::readPlacement(CadFrame& frame)
{
    double values[6];for(int i=0;i<6;++i)if(!numeric(placements[i]->getText(),values[i])||std::abs(values[i])>1e9){message("Placement requires finite mm/degree values within +/- 1e9.",true);return false;}
    frame=mech::frameFromEulerDegrees(values[0],values[1],values[2],values[3],values[4],values[5]);return true;
}
void CadCraftBrowser::insertPiece()
{
    juce::var geometry;CadFrame placement;if(!readGeometry(geometry)||!readPlacement(placement))return;
    if(!selectedMaterial().isObject()){message("Choose a material; use Materials... to create one.",true);return;}
    auto a=object(),p=object();int i=0;for(const auto* key:{"x","y","z","rx","ry","rz"})put(p,key,placements[i++]->getText().getDoubleValue());
    put(a,"action","insert");put(a,"component",draft["id"]);put(a,"geometry",geometry);put(a,"material",selectedMaterial());put(a,"placement",p);
    const auto result=service->componentCommand(a);if(!(bool)result["ok"]){message(result["error"].toString(),true);return;}
    if(onInserted)onInserted(result["id"].toString());message("Inserted independent piece "+result["id"].toString()+". Select Edit selected piece to change it.");
}
void CadCraftBrowser::editSelected()
{
    const auto id=selectedOccurrence?selectedOccurrence():juce::String();const auto& doc=service->document();const auto* instance=doc.mechanicsData().findInstance(id);
    const auto feature=instance?instance->featureId:id;const auto* record=doc.componentFor(feature);
    if(!record||!craft::isStock(*record)){message("Select a craft stock occurrence in the model tree or viewport.",true);return;}
    editingFeature=feature;editingInstance=instance?id:juce::String();draft=clone(*record);
    juce::String materialId;if(const auto* assignment=doc.materialsData().assignmentFor(feature))materialId=assignment->materialId;
    fill(draft,materialId);
    if(instance){const auto p=instance->placement;double rx=0,ry=0,rz=0;mech::eulerDegreesFromFrame(p,rx,ry,rz);const double values[]{p.x,p.y,p.z,rx,ry,rz};for(int i=0;i<6;++i)placements[i]->setText(juce::String(values[i],8));}
    move.setEnabled(instance!=nullptr);
}
void CadCraftBrowser::applyDimensions()
{
    juce::var geometry;if(!readGeometry(geometry))return;auto a=object();put(a,"action","editStock");put(a,"feature",editingFeature);put(a,"geometry",geometry);
    const auto result=service->componentCommand(a);if(!(bool)result["ok"])message(result["error"].toString(),true);else{put(draft,"geometry",geometry);message("Dimensions updated through OCAF; linked occurrences share this definition.");}
}
void CadCraftBrowser::applyMaterial()
{
    CadMaterial m;juce::String error,id;const auto record=selectedMaterial();
    if(!record.isObject()||!matl::materialFromJson(record,m,error)||!service->document().importMaterial(m,editingFeature,id,error)){message(error.isEmpty()?"Select a material.":error,true);return;}
    message("Material assigned. Density remains approximate where indicated; no missing properties are inferred.");
}
void CadCraftBrowser::applyPlacement()
{CadFrame p;if(!readPlacement(p))return;juce::String error;if(!service->setInstancePlacement(editingInstance,p,error))message(error,true);else message("Occurrence placement updated.");}
void CadCraftBrowser::linkedCopy()
{
    const auto selected=selectedOccurrence?selectedOccurrence():juce::String();const auto* instance=service->document().mechanicsData().findInstance(selected);
    if(!instance||!craft::isStock(service->document().componentFor(instance->featureId)?*service->document().componentFor(instance->featureId):juce::var())){message("Select a craft occurrence to make a linked copy.",true);return;}
    juce::String id,error;if(!service->createInstance(instance->featureId,"Linked craft piece",id,error)){message(error,true);return;}
    if(onInserted)onInserted(id);editSelected();message("Linked copy created. Move it with placement controls; dimensions and material are shared.");
}
void CadCraftBrowser::saveTemplate(bool copy)
{
    juce::var geometry;if(!readGeometry(geometry)||!selectedMaterial().isObject())return;
    auto a=object();put(a,"action",copy?"duplicateStock":"updateStock");put(a,"component",draft["id"]);put(a,"geometry",geometry);put(a,"material",selectedMaterial());
    const auto result=service->componentCommand(a);if(!(bool)result["ok"])message(result["error"].toString(),true);else{reload(result["id"].toString());message("Template saved locally. Existing document pieces retain their own snapshots.");}
}
void CadCraftBrowser::timerCallback()
{
    if(documentInstance!=service->document().documentInstance()){reload();return;}
    if(materialRevision!=service->document().revision())
    {
        materialRevision=service->document().revision();const auto previous=selectedMaterial()["id"].toString();loadMaterials(previous);
        if(editingFeature.isNotEmpty())
        {
            const auto* current=service->document().componentFor(editingFeature);
            if(!current){loadTemplate();return;}
            if(juce::JSON::toString((*current)["geometry"])!=juce::JSON::toString(draft["geometry"]))editSelected();
        }
    }
}
bool CadCraftBrowser::craftCheck(juce::var& report, const juce::File& folder)
{
    juce::Array<juce::var> checks; bool passed = true;
    const auto check = [&](bool ok, const char* name, const juce::String& detail = {})
    {
        auto v = object(); put(v, "check", name); put(v, "passed", ok);
        if (detail.isNotEmpty()) put(v, "detail", detail);
        checks.add(v); passed &= ok;
    };

    check(records.size() >= 8, "Craft browser loads bundled craft stock templates");

    category.setSelectedId(3); // Plastic
    filter();
    check(shown.size() == 2, "Filtering by Plastic category shows plastic templates");

    search.setText("tube");
    filter();
    check(shown.size() == 1, "Search filters to plastic tube");

    check(dimensions.size() >= 3 && dimensions[0]->getText() == "100" && dimensions[1]->getText() == "8" && dimensions[2]->getText() == "6",
          "Tube dimensions populated with default values");

    dimensions[0]->setText("150");
    placements[0]->setText("25"); // X placement

    juce::String currentSelected;
    selectedOccurrence = [&currentSelected] { return currentSelected; };
    onInserted = [&currentSelected](const juce::String& id) { currentSelected = id; };

    insertPiece();
    const auto& instances = service->document().mechanicsData().instances;
    check(!instances.empty() && instances.back().placement.x == 25,
          "Insert independent piece sets placement in document mechanics");

    const auto insertedId = instances.back().id;
    const auto insertedFeature = instances.back().featureId;
    currentSelected = insertedId;

    editSelected();
    check(editingFeature == insertedFeature && mode.getText().contains(insertedId),
          "Edit selected piece loads occurrence into editor");

    dimensions[0]->setText("180");
    applyDimensions();
    const auto* editedDef = service->document().componentFor(insertedFeature);
    check(editedDef != nullptr && (double)(*editedDef)["geometry"]["lengthMm"] == 180,
          "Apply dimensions updates component definition length in document");

    linkedCopy();
    check(service->document().mechanicsData().instances.size() == 2,
          "Linked copy creates second instance sharing definition");

    juce::FileOutputStream stream(folder.getChildFile("craft-browser.png"));
    check(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()), stream),
          "Craft stock browser snapshot saved");

    report = object();
    put(report, "passed", passed);
    put(report, "checks", checks);
    put(report, "verification", "JUCE craft stock browser UI controls, dimension editing, insertion, linked copy, and snapshot");
    put(report, "folder", folder.getFullPathName());
    return passed;
}
namespace djehuti::cad
{
bool runCadCraftUiCheck(juce::var& report, const juce::File& folder, bool interactive)
{
    folder.createDirectory();
    auto service = std::make_shared<CadCommandService>();
    service->setComponentLibraryFile(folder.getChildFile("library.json"));
    service->setCatalogFile(folder.getChildFile("materials.json"));
    auto* browser = new CadCraftBrowser(service);
    browser->setSize(400, 850);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(browser);
    options.dialogTitle = "Craft Stock Browser check";
    options.useNativeTitleBar = true;
    auto* dialog = options.launchAsync();
    if (interactive)
    {
        report = object();
        put(report, "folder", folder.getFullPathName());
        put(report, "verification", "Native test window prepared; no programmatic controls invoked.");
        return true;
    }
    const auto ok = browser->craftCheck(report, folder);
    dialog->exitModalState(0);
    return ok;
}
}
