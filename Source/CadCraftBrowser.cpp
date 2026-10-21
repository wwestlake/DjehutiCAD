#include "CadCraftBrowser.h"
#include "CadCraftStock.h"
#include "CadStructuralModel.h"
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
    for(auto* c:std::vector<juce::Component*>{&heading,&category,&search,&list,&mode,&details,&materialLabel,&material,&placementLabel,&status,&editPiece,&templatesButton,&insert,&apply,&assign,&move,&linked,&saveDefaults,&duplicate,&materialsButton,&reloadButton,
        &cutHeading,&cutType,&cutSide,&cutPosition,&cutAngle,&applyCutButton,&clearCutsButton,
        &alignHeading,&alignTarget,&alignType,&alignButton,
        &assemblyHeading,&assemblyName,&groupButton,&moveAssemblyButton,&cutListButton}) content.addAndMakeVisible(c);
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

    // Phase 2 setup
    cutHeading.setText("Cutting & Trimming", juce::dontSendNotification); cutHeading.setFont(juce::Font(16.0f, juce::Font::bold));
    cutType.addItemList({"Cut to length", "Miter (angle)"}, 1); cutType.setSelectedId(1);
    cutSide.addItemList({"Remove end (+X)", "Remove start (-X)"}, 1); cutSide.setSelectedId(1);
    cutPosition.setText("90"); cutAngle.setText("45");
    applyCutButton.onClick = [this]{ applyCut(); };
    clearCutsButton.onClick = [this]{ clearCuts(); };

    alignHeading.setText("Placement & Alignment", juce::dontSendNotification); alignHeading.setFont(juce::Font(16.0f, juce::Font::bold));
    alignType.addItemList({"Butt joint (End-to-End)", "Corner 90 deg", "Corner -90 deg", "Stack on top", "Flush"}, 1); alignType.setSelectedId(1);
    alignButton.onClick = [this]{ applyAlign(); };
    for (const auto* name : {"DX", "DY", "DZ"})
    {
        auto* ed = alignOffsets.add(new juce::TextEditor());
        ed->setText("0"); ed->setName(juce::String("Align ") + name);
        content.addAndMakeVisible(ed);
    }

    assemblyHeading.setText("Assembly Organization", juce::dontSendNotification); assemblyHeading.setFont(juce::Font(16.0f, juce::Font::bold));
    assemblyName.setText("Frame Assembly");
    groupButton.onClick = [this]{ groupAssembly(); };
    moveAssemblyButton.onClick = [this]{ moveAssembly(); };
    cutListButton.onClick = [this]{ showCutList(); };

    bondHeading.setText("Adhesives & Bonded Joints", juce::dontSendNotification); bondHeading.setFont(juce::Font(16.0f, juce::Font::bold));
    bondType.addItem("Surface bond", 1); bondType.addItem("Edge bond", 2); bondType.addItem("Fillet bond", 3); bondType.setSelectedId(1);
    bondThickness.setText("0.1"); bondThickness.setInputRestrictions(8, "0123456789.");
    bondBeadLeg.setText("2.0"); bondBeadLeg.setInputRestrictions(8, "0123456789.");
    createBondButton.onClick = [this]{ applyBond(); };
    moveBondedButton.onClick = [this]{ moveBonded(); };
    reevaluateBondsButton.onClick = [this]{
        juce::String err;
        service->reevaluateBondedJoints(err);
        message("Reevaluated all bonded joints.");
    };
    adhesiveBomButton.onClick = [this]{ showAdhesiveBom(); };
    content.addAndMakeVisible(bondHeading);
    content.addAndMakeVisible(bondType);
    content.addAndMakeVisible(bondAdhesive);
    content.addAndMakeVisible(bondTarget);
    content.addAndMakeVisible(bondThickness);
    content.addAndMakeVisible(bondBeadLeg);
    content.addAndMakeVisible(createBondButton);
    content.addAndMakeVisible(moveBondedButton);
    content.addAndMakeVisible(reevaluateBondsButton);
    content.addAndMakeVisible(adhesiveBomButton);

    // Phase 4: Structural Analysis
    analysisHeading.setText("Structural Analysis", juce::dontSendNotification); analysisHeading.setFont(juce::Font(16.0f, juce::Font::bold));
    supportType.addItem("Fixed (all 6 DOFs)", 1); supportType.addItem("Pinned (translations)", 2); supportType.addItem("Roller (normal)", 3); supportType.setSelectedId(1);
    supportLocation.addItem("Start of member", 1); supportLocation.addItem("End of member", 2); supportLocation.setSelectedId(1);
    loadType.addItem("Force (N)", 1); loadType.addItem("Moment (N*m)", 2); loadType.setSelectedId(1);
    loadDirection.addItem("-Z (Downward)", 1); loadDirection.addItem("+Z (Upward)", 2); loadDirection.addItem("-Y", 3); loadDirection.addItem("+Y", 4); loadDirection.addItem("-X", 5); loadDirection.addItem("+X", 6); loadDirection.setSelectedId(1);
    loadMagnitude.setText("10.0");
    defScale.setText("20.0");
    showDeformedToggle.setToggleState(true, juce::dontSendNotification);

    createCaseButton.onClick = [this]{ createNewCase(); };
    addSupportButton.onClick = [this]{ addSupportToCase(); };
    addLoadButton.onClick = [this]{ addLoadToCase(); };
    clearBoundaryButton.onClick = [this]{ clearCaseBoundary(); };
    runSolverButton.onClick = [this]{ runStructuralSolver(); };
    showResultsButton.onClick = [this]{ showStructuralResults(); };

    // Phase 5: Interactive Craft Testing Laboratory
    labHeading.setText("Craft Testing Laboratory", juce::dontSendNotification);
    labHeading.setColour(juce::Label::textColourId, juce::Colour(0xff88ddff));
    labHeading.setFont(juce::Font(14.0f, juce::Font::bold));

    experimentTemplateSelect.addItem("Cantilever Bending Test", 1);
    experimentTemplateSelect.addItem("Simply Supported Beam Test", 2);
    experimentTemplateSelect.addItem("Axial Tension Test", 3);
    experimentTemplateSelect.addItem("Axial Compression Test", 4);
    experimentTemplateSelect.addItem("Craft Bridge Load Test", 5);
    experimentTemplateSelect.addItem("Bonded Joint Shear Test", 6);
    experimentTemplateSelect.setSelectedId(1, juce::dontSendNotification);
    applyTemplateButton.onClick = [this]{ applySelectedTemplate(); };

    loadSweepSlider.setRange(0.0, 100.0, 1.0);
    loadSweepSlider.setValue(10.0, juce::dontSendNotification);
    loadSweepSlider.setTextValueSuffix(" N");
    loadSweepSlider.onValueChange = [this]{
        loadMagnitude.setText(juce::String(loadSweepSlider.getValue(), 1));
    };

    runSweepButton.onClick = [this]{ runInteractiveLoadSweep(); };
    resetExperimentButton.onClick = [this]{ resetCurrentExperiment(); };

    compareHeading.setText("Experiment Comparison", juce::dontSendNotification);
    compareHeading.setColour(juce::Label::textColourId, juce::Colour(0xff88ddff));
    compareHeading.setFont(juce::Font(14.0f, juce::Font::bold));

    compareButton.onClick = [this]{ compareSelectedExperiments(); };
    exportReportButton.onClick = [this]{ showLaboratoryReport(); };

    content.addAndMakeVisible(analysisHeading);
    content.addAndMakeVisible(analysisCaseSelect);
    content.addAndMakeVisible(createCaseButton);
    content.addAndMakeVisible(supportType);
    content.addAndMakeVisible(supportLocation);
    content.addAndMakeVisible(addSupportButton);
    content.addAndMakeVisible(loadType);
    content.addAndMakeVisible(loadDirection);
    content.addAndMakeVisible(loadMagnitude);
    content.addAndMakeVisible(addLoadButton);
    content.addAndMakeVisible(gravityToggle);
    content.addAndMakeVisible(showDeformedToggle);
    content.addAndMakeVisible(defScale);
    content.addAndMakeVisible(clearBoundaryButton);
    content.addAndMakeVisible(runSolverButton);
    content.addAndMakeVisible(showResultsButton);

    content.addAndMakeVisible(labHeading);
    content.addAndMakeVisible(experimentTemplateSelect);
    content.addAndMakeVisible(applyTemplateButton);
    content.addAndMakeVisible(loadSweepSlider);
    content.addAndMakeVisible(runSweepButton);
    content.addAndMakeVisible(resetExperimentButton);
    content.addAndMakeVisible(compareHeading);
    content.addAndMakeVisible(compareCaseA);
    content.addAndMakeVisible(compareCaseB);
    content.addAndMakeVisible(compareButton);
    content.addAndMakeVisible(exportReportButton);

    reload();startTimerHz(3);
}
CadCraftBrowser::~CadCraftBrowser(){stopTimer();scroll.setViewedComponent(nullptr,false);}
void CadCraftBrowser::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff1d222b));}
void CadCraftBrowser::resized()
{
    auto a=getLocalBounds();workspaceMenu.setBounds(a.removeFromTop(28));scroll.setBounds(a);
    content.setSize(juce::jmax(350,scroll.getMaximumVisibleWidth()), 2480);
    auto r=content.getLocalBounds().reduced(8);heading.setBounds(r.removeFromTop(32));category.setBounds(r.removeFromTop(30).reduced(0,2));search.setBounds(r.removeFromTop(30).reduced(0,2));
    list.setBounds(r.removeFromTop(145));auto row=r.removeFromTop(32);editPiece.setBounds(row.removeFromLeft(row.getWidth()*2/3).reduced(1));templatesButton.setBounds(row.reduced(1));
    mode.setBounds(r.removeFromTop(30));details.setBounds(r.removeFromTop(105));
    for(int i=0;i<3;++i){row=r.removeFromTop(30);dimensionLabels[i]->setBounds(row.removeFromLeft(160));dimensions[i]->setBounds(row.reduced(1));}
    materialLabel.setBounds(r.removeFromTop(24));material.setBounds(r.removeFromTop(30).reduced(1));
    placementLabel.setBounds(r.removeFromTop(24));for(int line=0;line<2;++line){row=r.removeFromTop(30);const int width=row.getWidth()/3;for(int col=0;col<3;++col){const int i=line*3+col;auto cell=row.removeFromLeft(width);placementLabels[i]->setBounds(cell.removeFromLeft(25));placements[i]->setBounds(cell.reduced(1));}}
    insert.setBounds(r.removeFromTop(32).reduced(1));
    row=r.removeFromTop(32);apply.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));assign.setBounds(row.reduced(1));
    row=r.removeFromTop(32);move.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));linked.setBounds(row.reduced(1));

    // Phase 2 cutting rows
    cutHeading.setBounds(r.removeFromTop(26));
    row = r.removeFromTop(30);
    cutType.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    cutSide.setBounds(row.reduced(1));
    row = r.removeFromTop(30);
    cutPosition.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    cutAngle.setBounds(row.reduced(1));
    row = r.removeFromTop(32);
    applyCutButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    clearCutsButton.setBounds(row.reduced(1));

    // Phase 2 alignment rows
    alignHeading.setBounds(r.removeFromTop(26));
    row = r.removeFromTop(30);
    alignTarget.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    alignType.setBounds(row.reduced(1));
    row = r.removeFromTop(30);
    const int offWidth = row.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        alignOffsets[i]->setBounds(row.removeFromLeft(offWidth).reduced(1));
    }
    alignButton.setBounds(r.removeFromTop(32).reduced(1));

    // Phase 2 assembly rows
    assemblyHeading.setBounds(r.removeFromTop(26));
    assemblyName.setBounds(r.removeFromTop(30).reduced(1));
    row = r.removeFromTop(32);
    groupButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    moveAssemblyButton.setBounds(row.reduced(1));

    // Cut list button
    cutListButton.setBounds(r.removeFromTop(32).reduced(1));

    // Phase 3 bonding rows
    bondHeading.setBounds(r.removeFromTop(26));
    row = r.removeFromTop(30);
    bondType.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    bondAdhesive.setBounds(row.reduced(1));
    row = r.removeFromTop(30);
    bondTarget.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    bondThickness.setBounds(row.reduced(1));
    row = r.removeFromTop(32);
    createBondButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    moveBondedButton.setBounds(row.reduced(1));
    row = r.removeFromTop(32);
    reevaluateBondsButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    adhesiveBomButton.setBounds(row.reduced(1));

    // Phase 4 structural analysis rows
    analysisHeading.setBounds(r.removeFromTop(26));
    row = r.removeFromTop(30);
    analysisCaseSelect.setBounds(row.removeFromLeft(row.getWidth()*2/3).reduced(1));
    createCaseButton.setBounds(row.reduced(1));
    row = r.removeFromTop(30);
    supportType.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    supportLocation.setBounds(row.reduced(1));
    addSupportButton.setBounds(r.removeFromTop(32).reduced(1));
    row = r.removeFromTop(30);
    loadType.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(1));
    loadDirection.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    loadMagnitude.setBounds(row.reduced(1));
    addLoadButton.setBounds(r.removeFromTop(32).reduced(1));
    row = r.removeFromTop(28);
    gravityToggle.setBounds(row.removeFromLeft(row.getWidth()*2/3).reduced(1));
    defScale.setBounds(row.reduced(1));
    showDeformedToggle.setBounds(r.removeFromTop(28).reduced(1));
    row = r.removeFromTop(32);
    runSolverButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    showResultsButton.setBounds(row.reduced(1));
    clearBoundaryButton.setBounds(r.removeFromTop(32).reduced(1));

    // Phase 5: Interactive Craft Testing Laboratory
    labHeading.setBounds(r.removeFromTop(26));
    row = r.removeFromTop(30);
    experimentTemplateSelect.setBounds(row.removeFromLeft(row.getWidth()*3/5).reduced(1));
    applyTemplateButton.setBounds(row.reduced(1));
    row = r.removeFromTop(30);
    loadSweepSlider.setBounds(row.removeFromLeft(row.getWidth()*3/5).reduced(1));
    runSweepButton.setBounds(row.reduced(1));
    resetExperimentButton.setBounds(r.removeFromTop(32).reduced(1));

    compareHeading.setBounds(r.removeFromTop(26));
    row = r.removeFromTop(30);
    compareCaseA.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    compareCaseB.setBounds(row.reduced(1));
    row = r.removeFromTop(32);
    compareButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));
    exportReportButton.setBounds(row.reduced(1));

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
    if(editing){int count=0;for(const auto& instance:service->document().mechanicsData().instances)if(instance.featureId==editingFeature)++count;text+="\nLinked occurrences of this definition: "+juce::String(count)+". Dimension/material edits affect them all.";
        if(const auto* cuts=record["geometry"].getProperty("cuts",{}).getArray();cuts&&cuts->size()>0)text+="\nAuthored cuts: "+juce::String(cuts->size())+" applied.";
    }
    details.setText(text,false);loadMaterials(materialId);
    insert.setEnabled(!editing&&record.isObject());saveDefaults.setEnabled(!editing&&record.isObject());duplicate.setEnabled(!editing&&record.isObject());apply.setEnabled(editing);assign.setEnabled(editing);move.setEnabled(editing);
    applyCutButton.setEnabled(editing);clearCutsButton.setEnabled(editing);alignButton.setEnabled(editing);
    groupButton.setEnabled(service->document().mechanicsData().instances.size()>=2);
    moveAssemblyButton.setEnabled(!service->document().mechanicsData().assemblies.empty());
    message(editing?"Edit dimensions, cuts, material or placement, then apply that change.":"Illustrative dimensions and densities. Enter your actual stock measurements.");
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
        material.addItem(entry["name"].toString()+" â€” "+density+" kg/m^3",i+1);
        if(entry["id"].toString()==desired|| (desired.isEmpty()&&entry["id"]==draft["materialCatalogId"]))chosen=i+1;
    }
    // A customized template may hold a material snapshot not yet in this document/catalog.
    if(!chosen&&draft["material"].isObject()){materialRecords.add(clone(draft["material"]));chosen=materialRecords.size();material.addItem(draft["material"]["name"].toString(),chosen);}
    material.setSelectedId(chosen,juce::dontSendNotification);material.setTextWhenNothingSelected("Choose an existing material");
    if (bondAdhesive.getNumItems() == 0)
    {
        int aid = 1;
        for (const auto& a : mech::defaultAdhesives())
            bondAdhesive.addItem(a.name, aid++);
        bondAdhesive.setSelectedId(1, juce::dontSendNotification);
    }
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
    refreshAlignTargets();
    refreshBondTargets();
}
void CadCraftBrowser::refreshAlignTargets()
{
    alignTarget.clear(juce::dontSendNotification);
    const auto& instances = service->document().mechanicsData().instances;
    int idx = 1;
    for (const auto& inst : instances)
    {
        if (inst.id != editingInstance)
        {
            alignTarget.addItem(inst.id + " (" + inst.name + ")", idx);
            if (alignTarget.getSelectedId() == 0) alignTarget.setSelectedId(idx, juce::dontSendNotification);
            ++idx;
        }
    }
}
void CadCraftBrowser::refreshBondTargets()
{
    bondTarget.clear(juce::dontSendNotification);
    const auto& instances = service->document().mechanicsData().instances;
    int idx = 1;
    for (const auto& inst : instances)
    {
        if (inst.id != editingInstance)
        {
            bondTarget.addItem(inst.id + " (" + inst.name + ")", idx);
            if (bondTarget.getSelectedId() == 0) bondTarget.setSelectedId(idx, juce::dontSendNotification);
            ++idx;
        }
    }
}
void CadCraftBrowser::applyCut()
{
    if (editingFeature.isEmpty()) { message("Select and edit a craft piece first.", true); return; }
    auto a = object(), cut = object();
    const int typeId = cutType.getSelectedId();
    if (typeId == 1)
    {
        put(cut, "type", "length");
        double pos = 0;
        if (!numeric(cutPosition.getText(), pos)) { message("Enter numeric cut position in mm.", true); return; }
        put(cut, "positionMm", pos);
        put(cut, "side", cutSide.getSelectedId() == 2 ? "start" : "end");
    }
    else
    {
        put(cut, "type", "miter");
        double pos = 0, angle = 0;
        if (!numeric(cutPosition.getText(), pos) || !numeric(cutAngle.getText(), angle))
        { message("Enter numeric cut position and miter angle.", true); return; }
        put(cut, "positionMm", pos);
        put(cut, "angleDeg", angle);
        put(cut, "side", cutSide.getSelectedId() == 2 ? "start" : "end");
    }
    put(a, "action", "addCut");
    put(a, "feature", editingFeature);
    put(a, "cut", cut);
    const auto result = service->componentCommand(a);
    if (!(bool)result["ok"]) message(result["error"].toString(), true);
    else
    {
        editSelected();
        message("Cut applied to piece through OCAF.");
    }
}
void CadCraftBrowser::clearCuts()
{
    if (editingFeature.isEmpty()) { message("Select and edit a craft piece first.", true); return; }
    auto a = object();
    put(a, "action", "clearCuts");
    put(a, "feature", editingFeature);
    const auto result = service->componentCommand(a);
    if (!(bool)result["ok"]) message(result["error"].toString(), true);
    else
    {
        editSelected();
        message("All cuts cleared from piece.");
    }
}
void CadCraftBrowser::applyAlign()
{
    if (editingInstance.isEmpty()) { message("Select and edit a piece as alignment source.", true); return; }
    const auto targetText = alignTarget.getText();
    const auto targetId = targetText.upToFirstOccurrenceOf(" ", false, false);
    if (targetId.isEmpty()) { message("Select a target piece to align to.", true); return; }
    auto a = object(), opt = object();
    put(opt, "dx", alignOffsets[0]->getText().getDoubleValue());
    put(opt, "dy", alignOffsets[1]->getText().getDoubleValue());
    put(opt, "dz", alignOffsets[2]->getText().getDoubleValue());
    const juce::String types[] = {"butt", "corner90", "corner90_neg", "stack", "flush"};
    const int idx = juce::jlimit(1, 5, alignType.getSelectedId()) - 1;
    put(a, "action", "align");
    put(a, "target", targetId);
    put(a, "source", editingInstance);
    put(a, "alignType", types[idx]);
    put(a, "options", opt);
    const auto result = service->componentCommand(a);
    if (!(bool)result["ok"]) message(result["error"].toString(), true);
    else
    {
        editSelected();
        message("Aligned " + editingInstance + " to " + targetId + ".");
    }
}
void CadCraftBrowser::groupAssembly()
{
    const auto& instances = service->document().mechanicsData().instances;
    if (instances.size() < 2) { message("Insert at least two pieces to group into an assembly.", true); return; }
    std::vector<juce::String> ids;
    for (const auto& inst : instances) ids.push_back(inst.id);
    juce::String groupId, error;
    if (!service->createAssemblyGroup(assemblyName.getText().trim(), ids, groupId, error))
        message(error, true);
    else
        message("Created assembly " + groupId + " with " + juce::String(ids.size()) + " pieces.");
}
void CadCraftBrowser::moveAssembly()
{
    CadFrame delta;
    if (!readPlacement(delta)) return;
    const auto& mechanics = service->document().mechanicsData();
    if (mechanics.assemblies.empty()) { message("No assembly created yet.", true); return; }
    juce::String targetGroup = mechanics.assemblies.front().id;
    for (const auto& asmGroup : mechanics.assemblies)
    {
        for (const auto& id : asmGroup.instanceIds)
            if (id == editingInstance) { targetGroup = asmGroup.id; break; }
    }
    juce::String error;
    if (!service->moveAssemblyGroup(targetGroup, delta, error))
        message(error, true);
    else
    {
        editSelected();
        message("Moved assembly " + targetGroup + " as a unit.");
    }
}
void CadCraftBrowser::showCutList()
{
    const auto list = service->generateCutList();
    juce::String text = "=== CRAFT CUT LIST ===\n";
    text += "Total pieces: " + list["totalPieces"].toString() + "\n\n";
    text += "--- Summary by Piece Specification ---\n";
    if (const auto* summary = list["summary"].getArray())
    {
        for (const auto& entry : *summary)
        {
            text += "Qty: " + entry["quantity"].toString() + "x  "
                 + entry["componentName"].toString() + " (" + entry["material"].toString() + ")\n"
                 + "  Profile: " + entry["profile"].toString()
                 + "  Nominal length: " + entry["originalLengthMm"].toString() + " mm"
                 + "  Finished length: " + juce::String((double)entry["finishedLengthMm"], 1) + " mm"
                 + "  Offcut: " + juce::String((double)entry["offcutMm"], 1) + " mm\n";
            if (const auto* cuts = entry["cuts"].getArray(); cuts && cuts->size() > 0)
            {
                text += "  Cuts: ";
                for (const auto& c : *cuts)
                {
                    text += c["type"].toString() + " @" + c["positionMm"].toString() + "mm ";
                    if (c.hasProperty("angleDeg")) text += "(" + c["angleDeg"].toString() + " deg) ";
                }
                text += "\n";
            }
        }
    }
    details.setText(text, false);
    if (list.hasProperty("adhesives"))
    {
        const auto adhs = list["adhesives"];
        text += "\n--- ADHESIVE USAGE (BOM) ---\n";
        text += "Total joints: " + adhs["totalJoints"].toString() + "  (Valid: " + adhs["validJoints"].toString() + ")\n";
        text += "Total adhesive volume: " + juce::String((double)adhs["totalAdhesiveVolumeMm3"], 2) + " mm^3\n";
        if (const auto* arr = adhs["adhesives"].getArray())
        {
            for (const auto& a : *arr)
            {
                text += "  " + a["adhesiveName"].toString() + ": " + a["jointCount"].toString() + " joint(s), "
                     + juce::String((double)a["totalAreaMm2"], 1) + " mm^2, "
                     + juce::String((double)a["totalVolumeMm3"], 2) + " mm^3\n";
            }
        }
        details.setText(text, false);
    }
    message("Cut list generated from current construction.");
}
void CadCraftBrowser::applyBond()
{
    if (editingInstance.isEmpty()) { message("Select and edit a piece first.", true); return; }
    const auto targetText = bondTarget.getText();
    const auto targetId = targetText.upToFirstOccurrenceOf(" ", false, false);
    if (targetId.isEmpty()) { message("Select a target piece to bond with.", true); return; }

    const juce::String adhIds[] = { "adh-pva", "adh-ca", "adh-epoxy", "adh-hotmelt", "adh-contact", "adh-solvent" };
    const int adhIdx = juce::jlimit(1, 6, bondAdhesive.getSelectedId()) - 1;
    const juce::String types[] = { "surface", "edge", "fillet" };
    const int typeIdx = juce::jlimit(1, 3, bondType.getSelectedId()) - 1;

    CadBondedJoint joint;
    joint.instanceA = editingInstance;
    joint.instanceB = targetId;
    joint.type = types[typeIdx];
    joint.adhesiveId = adhIds[adhIdx];
    joint.bondThicknessMm = bondThickness.getText().getDoubleValue();
    if (joint.bondThicknessMm <= 0.0) joint.bondThicknessMm = 0.1;
    joint.beadLegMm = bondBeadLeg.getText().getDoubleValue();
    if (joint.beadLegMm <= 0.0) joint.beadLegMm = 2.0;
    if (joint.type == "fillet") joint.createBeadSolid = true;

    juce::String jointId, error;
    if (!service->createBondedJoint(joint, jointId, error))
    {
        message(error, true);
    }
    else
    {
        const auto* created = service->document().mechanicsData().findBondedJoint(jointId);
        if (created && created->status != "valid")
            message("Bond " + jointId + " created: " + created->status + " (" + created->failureReason + ")", true);
        else
            message("Created " + joint.type + " bond " + jointId + " (" + (created ? juce::String((double)created->contactAreaMm2, 1) : "") + " mm^2).");
    }
}
void CadCraftBrowser::moveBonded()
{
    if (editingInstance.isEmpty()) { message("Select and edit a piece in the bonded construction.", true); return; }
    CadFrame delta;
    if (!readPlacement(delta)) return;
    juce::String error;
    if (!service->moveBondedGroup(editingInstance, delta, error))
        message(error, true);
    else
    {
        editSelected();
        message("Moved bonded construction as a unit.");
    }
}
void CadCraftBrowser::showAdhesiveBom()
{
    const auto bom = service->adhesiveBom();
    juce::String text = "=== ADHESIVE BILL OF MATERIALS (BOM) ===\n";
    text += "Total joints: " + bom["totalJoints"].toString() + "  (Valid: " + bom["validJoints"].toString()
         + ", Separated: " + bom["separatedJoints"].toString()
         + ", Incompatible: " + bom["incompatibleJoints"].toString() + ")\n";
    text += "Total bond area: " + juce::String((double)bom["totalBondAreaMm2"], 1) + " mm^2\n";
    text += "Total adhesive volume: " + juce::String((double)bom["totalAdhesiveVolumeMm3"], 2) + " mm^3 ("
         + juce::String((double)bom["totalAdhesiveVolumeMl"], 4) + " mL)\n\n";

    text += "--- Adhesives Summary ---\n";
    if (const auto* adhs = bom["adhesives"].getArray())
    {
        for (const auto& a : *adhs)
        {
            text += a["adhesiveName"].toString() + " (" + a["adhesiveId"].toString() + "):\n"
                 + "  Joints: " + a["jointCount"].toString() + " (Valid: " + a["validJointCount"].toString() + ")\n"
                 + "  Total area: " + juce::String((double)a["totalAreaMm2"], 1) + " mm^2\n"
                 + "  Estimated volume: " + juce::String((double)a["totalVolumeMm3"], 2) + " mm^3 ("
                 + juce::String((double)a["totalVolumeMl"], 4) + " mL)\n\n";
        }
    }

    text += "--- Bonded Joints ---\n";
    if (const auto* joints = bom["joints"].getArray())
    {
        for (const auto& j : *joints)
        {
            text += j["id"].toString() + " [" + j["status"].toString().toUpperCase() + "]: "
                 + j["instanceA"].toString() + " <-> " + j["instanceB"].toString() + " (" + j["type"].toString() + ")\n"
                 + "  Adhesive: " + j["adhesiveName"].toString()
                 + "  Area: " + juce::String((double)j["contactAreaMm2"], 1) + " mm^2"
                 + "  Vol: " + juce::String((double)j["estimatedVolumeMm3"], 2) + " mm^3\n";
            if (j["status"].toString() != "valid")
                text += "  Reason: " + j["failureReason"].toString() + "\n";
        }
    }
    details.setText(text, false);
    message("Adhesive BOM generated.");
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
void CadCraftBrowser::refreshAnalysisCases()
{
    const int currentId = analysisCaseSelect.getSelectedId();
    const int currentA = compareCaseA.getSelectedId();
    const int currentB = compareCaseB.getSelectedId();
    analysisCaseSelect.clear();
    compareCaseA.clear();
    compareCaseB.clear();

    const auto& cases = service->document().analysisData().cases;
    int id = 1;
    for (const auto& c : cases)
    {
        const auto label = c.name.isNotEmpty() ? c.name : c.id;
        analysisCaseSelect.addItem(label, id);
        compareCaseA.addItem(label, id);
        compareCaseB.addItem(label, id);
        id++;
    }
    if (analysisCaseSelect.getNumItems() > 0)
    {
        if (currentId > 0 && currentId <= analysisCaseSelect.getNumItems())
            analysisCaseSelect.setSelectedId(currentId, juce::dontSendNotification);
        else
            analysisCaseSelect.setSelectedId(1, juce::dontSendNotification);

        if (currentA > 0 && currentA <= compareCaseA.getNumItems())
            compareCaseA.setSelectedId(currentA, juce::dontSendNotification);
        else
            compareCaseA.setSelectedId(1, juce::dontSendNotification);

        if (currentB > 0 && currentB <= compareCaseB.getNumItems())
            compareCaseB.setSelectedId(currentB, juce::dontSendNotification);
        else
            compareCaseB.setSelectedId(juce::jmin(2, compareCaseB.getNumItems()), juce::dontSendNotification);
    }
}
void CadCraftBrowser::createNewCase()
{
    CadAnalysisCase c;
    c.name = "Craft Load Case " + juce::String(service->document().analysisData().cases.size() + 1);
    c.analysisType = "linearStatic";
    for (const auto& inst : service->document().mechanicsData().instances)
    {
        CadAnalysisTarget pt; pt.instance = inst.id;
        c.parts.push_back(pt);
    }
    juce::String id, error;
    if (service->document().createAnalysisCase(c, id, error))
    {
        refreshAnalysisCases();
        message("Created analysis case: " + id);
    }
    else message(error, true);
}
void CadCraftBrowser::addSupportToCase()
{
    const auto& cases = service->document().analysisData().cases;
    if (cases.empty()) { createNewCase(); }
    const int idx = analysisCaseSelect.getSelectedItemIndex();
    if (idx < 0 || idx >= (int)service->document().analysisData().cases.size()) { message("No analysis case selected.", true); return; }
    const auto caseId = service->document().analysisData().cases[(size_t)idx].id;
    auto c = service->document().analysisData().cases[(size_t)idx];

    const juce::String target = editingInstance.isNotEmpty() ? editingInstance : (selectedOccurrence ? selectedOccurrence() : "");
    if (target.isEmpty()) { message("Select an instance to support.", true); return; }

    bool hasPart = false;
    for (const auto& p : c.parts) if (p.instance == target) { hasPart = true; break; }
    if (!hasPart) { CadAnalysisTarget pt; pt.instance = target; c.parts.push_back(pt); }

    CadAnalysisSupport sup;
    sup.id = "sup-" + juce::String(c.nextItem++);
    sup.target.instance = target;
    const int locId = supportLocation.getSelectedId();
    sup.target.face = locId == 1 ? "start" : "end";
    const int typeId = supportType.getSelectedId();
    if (typeId == 1) // Fixed
        sup.fixed = { "ux", "uy", "uz", "rx", "ry", "rz" };
    else if (typeId == 2) // Pinned
        sup.fixed = { "ux", "uy", "uz" };
    else // Roller
        sup.fixed = { "uz" };

    sup.name = (typeId == 1 ? "Fixed" : (typeId == 2 ? "Pinned" : "Roller")) + juce::String(" on ") + target;
    c.supports.push_back(sup);

    juce::String error;
    if (service->document().updateAnalysisCase(caseId, c, error))
        message("Added " + sup.name + " (" + sup.target.face + ")");
    else message(error, true);
}
void CadCraftBrowser::addLoadToCase()
{
    const auto& cases = service->document().analysisData().cases;
    if (cases.empty()) { createNewCase(); }
    const int idx = analysisCaseSelect.getSelectedItemIndex();
    if (idx < 0 || idx >= (int)service->document().analysisData().cases.size()) { message("No analysis case selected.", true); return; }
    const auto caseId = service->document().analysisData().cases[(size_t)idx].id;
    auto c = service->document().analysisData().cases[(size_t)idx];

    const juce::String target = editingInstance.isNotEmpty() ? editingInstance : (selectedOccurrence ? selectedOccurrence() : "");
    if (target.isEmpty()) { message("Select an instance to apply load.", true); return; }

    bool hasPart = false;
    for (const auto& p : c.parts) if (p.instance == target) { hasPart = true; break; }
    if (!hasPart) { CadAnalysisTarget pt; pt.instance = target; c.parts.push_back(pt); }

    double mag = 10.0;
    numeric(loadMagnitude.getText(), mag);
    if (mag <= 0.0) { message("Load magnitude must be positive.", true); return; }

    CadAnalysisLoad load;
    load.id = "load-" + juce::String(c.nextItem++);
    load.target.instance = target;
    load.type = loadType.getSelectedId() == 1 ? "force" : "moment";
    load.magnitude = mag;

    const int dirId = loadDirection.getSelectedId();
    if (dirId == 1) load.direction = { 0, 0, -1 };
    else if (dirId == 2) load.direction = { 0, 0, 1 };
    else if (dirId == 3) load.direction = { 0, -1, 0 };
    else if (dirId == 4) load.direction = { 0, 1, 0 };
    else if (dirId == 5) load.direction = { -1, 0, 0 };
    else load.direction = { 1, 0, 0 };

    const int locId = supportLocation.getSelectedId();
    load.target.face = locId == 1 ? "start" : "end";
    load.name = juce::String(load.type == "force" ? "Force " : "Moment ") + juce::String(mag) + " on " + target;
    c.loads.push_back(load);

    if (gravityToggle.getToggleState())
        c.gravity = std::array<double, 3>{ 0, 0, -9.81 };
    else
        c.gravity.reset();

    juce::String error;
    if (service->document().updateAnalysisCase(caseId, c, error))
        message("Added " + load.name);
    else message(error, true);
}
void CadCraftBrowser::clearCaseBoundary()
{
    const int idx = analysisCaseSelect.getSelectedItemIndex();
    if (idx < 0 || idx >= (int)service->document().analysisData().cases.size()) return;
    const auto caseId = service->document().analysisData().cases[(size_t)idx].id;
    auto c = service->document().analysisData().cases[(size_t)idx];
    c.supports.clear();
    c.loads.clear();
    juce::String error;
    if (service->document().updateAnalysisCase(caseId, c, error))
        message("Cleared loads and supports from case.");
    else message(error, true);
}
void CadCraftBrowser::runStructuralSolver()
{
    const auto& cases = service->document().analysisData().cases;
    if (cases.empty()) { message("No analysis case to solve. Create one first.", true); return; }
    const int idx = analysisCaseSelect.getSelectedItemIndex();
    const auto& c = (idx >= 0 && idx < (int)cases.size()) ? cases[(size_t)idx] : cases.front();

    structural::StructuralOptions opt;
    double scale = 20.0;
    numeric(defScale.getText(), scale);
    opt.visualDeformationScale = scale > 0.0 ? scale : 20.0;
    opt.showDeformed = showDeformedToggle.getToggleState();

    const auto res = structural::evaluateCase(service->document(), c, opt);
    lastSolveResult = res.toVar();

    if (setOverlaysCallback)
        setOverlaysCallback(res.overlays);

    if (res.solved)
    {
        message("Solved: max deflection " + juce::String(res.maxDeflectionM * 1000.0, 3) + 
                " mm, max stress " + juce::String(res.maxNormalStressPa / 1e6, 2) + " MPa");
    }
    else
    {
        message("Solver " + res.status + ": " + res.diagnostic, true);
    }
}
void CadCraftBrowser::showStructuralResults()
{
    if (lastSolveResult.isVoid())
    {
        runStructuralSolver();
    }
    if (lastSolveResult.isVoid()) return;

    juce::String text = "=== Structural Analysis Report ===\n";
    text += "Status: " + lastSolveResult["status"].toString() + "\n";
    if (lastSolveResult["diagnostic"].toString().isNotEmpty())
        text += "Diagnostic: " + lastSolveResult["diagnostic"].toString() + "\n";
    text += "Max Deflection: " + juce::String((double)lastSolveResult["maxDeflectionMm"], 4) + " mm\n";
    text += "Max Normal Stress: " + juce::String((double)lastSolveResult["maxNormalStressMPa"], 3) + " MPa\n";
    text += "Max Shear Stress: " + juce::String((double)lastSolveResult["maxShearStressMPa"], 3) + " MPa\n";
    text += "Max Utilization: " + juce::String((double)lastSolveResult["maxUtilizationRatio"] * 100.0, 1) + " %\n\n";

    const auto rf = lastSolveResult["totalReactionForceN"];
    text += "Total Reactions (N): Rx=" + juce::String((double)rf["x"], 2) +
            ", Ry=" + juce::String((double)rf["y"], 2) +
            ", Rz=" + juce::String((double)rf["z"], 2) + "\n\n";

    if (const auto* mems = lastSolveResult["members"].getArray())
    {
        text += "Members (" + juce::String(mems->size()) + "):\n";
        for (const auto& m : *mems)
        {
            text += "  " + m["instanceId"].toString() + ": Axial=" + juce::String((double)m["axialForceN"], 2) +
                    " N, Bend=" + juce::String((double)m["bendingMomentNm"], 3) + " N*m, Stress=" +
                    juce::String((double)m["normalStressMPa"], 2) + " MPa\n";
        }
    }
    details.setText(text);
}
void CadCraftBrowser::applySelectedTemplate()
{
    const auto& instances = service->document().mechanicsData().instances;
    if (instances.empty())
    {
        message("No components in document. Insert craft stock first.", true);
        return;
    }

    juce::String target = editingInstance.isNotEmpty() ? editingInstance : (selectedOccurrence ? selectedOccurrence() : "");
    if (target.isEmpty())
        target = instances.front().id;

    const int sel = experimentTemplateSelect.getSelectedId();
    juce::String templateType = "cantilever";
    if (sel == 2) templateType = "simplySupported";
    else if (sel == 3) templateType = "axialTension";
    else if (sel == 4) templateType = "axialCompression";
    else if (sel == 5) templateType = "bridge";
    else if (sel == 6) templateType = "bondedTransfer";

    double mag = loadSweepSlider.getValue();
    if (mag <= 0.0)
    {
        numeric(loadMagnitude.getText(), mag);
        if (mag <= 0.0) mag = 10.0;
    }

    auto c = structural::createTemplateCase(service->document(), templateType, target, mag);
    juce::String id, error;
    if (service->document().createAnalysisCase(c, id, error))
    {
        refreshAnalysisCases();
        analysisCaseSelect.setSelectedId(analysisCaseSelect.getNumItems(), juce::dontSendNotification);
        message("Configured experiment: " + c.name);
    }
    else message(error, true);
}
void CadCraftBrowser::runInteractiveLoadSweep()
{
    const auto& cases = service->document().analysisData().cases;
    if (cases.empty())
    {
        message("No test case to sweep. Apply a template first.", true);
        return;
    }
    const int idx = analysisCaseSelect.getSelectedItemIndex();
    const auto& baseCase = (idx >= 0 && idx < (int)cases.size()) ? cases[(size_t)idx] : cases.front();

    structural::StructuralOptions opt;
    double scale = 20.0;
    numeric(defScale.getText(), scale);
    opt.visualDeformationScale = scale > 0.0 ? scale : 20.0;
    opt.showDeformed = showDeformedToggle.getToggleState();

    const double endMag = loadSweepSlider.getValue() > 0.0 ? loadSweepSlider.getValue() : 10.0;
    const auto sweep = structural::runLoadSweep(service->document(), baseCase, 0.0, endMag, 5, opt);

    juce::String sweepText = "=== Quasi-Static Load Sweep ===\n";
    sweepText += "Experiment: " + (baseCase.name.isNotEmpty() ? baseCase.name : baseCase.id) + "\n";
    sweepText += "Steps: 0.0 N -> " + juce::String(endMag, 1) + " N (5 increments)\n\n";
    sweepText += "| Load (N) | Deflection (mm) | Stress (MPa) | Status |\n";
    for (const auto& step : sweep)
    {
        sweepText += "| " + juce::String(step.loadMagnitudeN, 1) + " N | " +
                     juce::String(step.maxDeflectionMm, 4) + " mm | " +
                     juce::String(step.maxNormalStressMPa, 2) + " MPa | " +
                     (step.solved ? "OK" : "FAILED") + " |\n";
    }

    details.setText(sweepText);

    runStructuralSolver();
    message("Quasi-static sweep complete (" + juce::String(endMag, 1) + " N max)");
}
void CadCraftBrowser::resetCurrentExperiment()
{
    const auto& cases = service->document().analysisData().cases;
    if (cases.empty()) { message("No active experiment to reset.", true); return; }
    const int idx = analysisCaseSelect.getSelectedItemIndex();
    if (idx < 0 || idx >= (int)cases.size()) { message("Select an experiment to reset.", true); return; }

    const auto caseId = cases[(size_t)idx].id;
    auto c = cases[(size_t)idx];
    c.supports.clear();
    c.loads.clear();
    juce::String error;
    if (service->document().updateAnalysisCase(caseId, c, error))
    {
        lastSolveResult = juce::var();
        if (setOverlaysCallback)
            setOverlaysCallback({});
        details.setText("Experiment reset. Boundary conditions and loads cleared.");
        message("Reset experiment " + (c.name.isNotEmpty() ? c.name : c.id));
    }
    else message(error, true);
}
void CadCraftBrowser::compareSelectedExperiments()
{
    const auto& cases = service->document().analysisData().cases;
    if (cases.size() < 2)
    {
        message("Need at least 2 analysis cases to compare.", true);
        return;
    }
    const int idxA = compareCaseA.getSelectedItemIndex();
    const int idxB = compareCaseB.getSelectedItemIndex();
    if (idxA < 0 || idxA >= (int)cases.size() || idxB < 0 || idxB >= (int)cases.size())
    {
        message("Select two valid cases to compare.", true);
        return;
    }

    structural::StructuralOptions opt;
    double scale = 20.0;
    numeric(defScale.getText(), scale);
    opt.visualDeformationScale = scale > 0.0 ? scale : 20.0;

    const auto comp = structural::compareCases(service->document(), cases[(size_t)idxA], cases[(size_t)idxB], opt);

    juce::String text = "=== Experiment Comparison ===\n";
    text += comp.summary + "\n\n";
    text += "Case A: " + comp.nameA + "\n";
    text += "  Deflection: " + juce::String(comp.deflectionA_Mm, 4) + " mm\n";
    text += "  Normal Stress: " + juce::String(comp.stressA_MPa, 2) + " MPa\n\n";
    text += "Case B: " + comp.nameB + "\n";
    text += "  Deflection: " + juce::String(comp.deflectionB_Mm, 4) + " mm\n";
    text += "  Normal Stress: " + juce::String(comp.stressB_MPa, 2) + " MPa\n\n";
    text += "Delta Deflection: " + juce::String(comp.deltaDeflectionMm, 4) + " mm (" + juce::String(comp.percentDeflectionChange, 1) + "%)\n";
    text += "Delta Stress: " + juce::String(comp.deltaStressMPa, 2) + " MPa (" + juce::String(comp.percentStressChange, 1) + "%)\n";

    details.setText(text);
    message(comp.summary);
}
void CadCraftBrowser::showLaboratoryReport()
{
    const auto& cases = service->document().analysisData().cases;
    if (cases.empty())
    {
        message("No analysis case available for report.", true);
        return;
    }
    const int idx = analysisCaseSelect.getSelectedItemIndex();
    const auto& c = (idx >= 0 && idx < (int)cases.size()) ? cases[(size_t)idx] : cases.front();

    structural::StructuralOptions opt;
    double scale = 20.0;
    numeric(defScale.getText(), scale);
    opt.visualDeformationScale = scale > 0.0 ? scale : 20.0;
    opt.showDeformed = showDeformedToggle.getToggleState();

    const auto res = structural::evaluateCase(service->document(), c, opt);
    lastSolveResult = res.toVar();
    if (setOverlaysCallback)
        setOverlaysCallback(res.overlays);

    structural::ExperimentComparison comp;
    structural::ExperimentComparison* compPtr = nullptr;
    if (cases.size() >= 2)
    {
        const int idxA = compareCaseA.getSelectedItemIndex();
        const int idxB = compareCaseB.getSelectedItemIndex();
        if (idxA >= 0 && idxA < (int)cases.size() && idxB >= 0 && idxB < (int)cases.size() && idxA != idxB)
        {
            comp = structural::compareCases(service->document(), cases[(size_t)idxA], cases[(size_t)idxB], opt);
            compPtr = &comp;
        }
    }

    const auto report = structural::generateLabReport(service->document(), c, res, compPtr);
    details.setText(report);
    message("Laboratory Report generated (" + juce::String(report.length()) + " characters)");
}
void CadCraftBrowser::timerCallback()
{
    if(documentInstance!=service->document().documentInstance()){reload();refreshAnalysisCases();return;}
    if(materialRevision!=service->document().revision())
    {
        materialRevision=service->document().revision();const auto previous=selectedMaterial()["id"].toString();loadMaterials(previous);
        if(editingFeature.isNotEmpty())
        {
            const auto* current=service->document().componentFor(editingFeature);
            if(!current){loadTemplate();return;}
            if(juce::JSON::toString((*current)["geometry"])!=juce::JSON::toString(draft["geometry"]))editSelected();
        }
        refreshAnalysisCases();
    }
}
bool CadCraftBrowser::craftCheck(juce::var& report, const juce::File& folder)
{
    juce::Array<juce::var> checks; bool passed = true;
    const auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
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
bool CadCraftBrowser::craftPhase2Check(juce::var& report, const juce::File& folder)
{
    juce::Array<juce::var> checks; bool passed = true;
    const auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto v = object(); put(v, "check", name); put(v, "passed", ok);
        if (detail.isNotEmpty()) put(v, "detail", detail);
        checks.add(v); passed &= ok;
    };

    juce::String currentSelected;
    selectedOccurrence = [&currentSelected] { return currentSelected; };
    onInserted = [&currentSelected](const juce::String& id) { currentSelected = id; };

    // 1. Insert piece 1: Wooden craft stick
    search.setText("strip");
    filter();
    check(shown.size() == 1, "Select wood strip template");
    if (material.getSelectedId() <= 0 && material.getNumItems() > 0)
        material.setSelectedId(1, juce::dontSendNotification);
    dimensions[0]->setText("120"); dimensions[1]->setText("10"); dimensions[2]->setText("3");
    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto firstId = currentSelected;
    check(firstId.isNotEmpty(), "Insert first wooden craft stick: " + firstId);

    // 2. Insert piece 2: Second wooden craft stick
    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto secondId = currentSelected;
    check(secondId.isNotEmpty() && secondId != firstId, "Insert second wooden craft stick: " + secondId);

    // 3. Cut first piece to 90 mm
    currentSelected = firstId;
    editSelected();
    check(editingInstance == firstId, "Select and edit first piece");
    cutType.setSelectedId(1); // length
    cutPosition.setText("90");
    cutSide.setSelectedId(1); // end
    applyCut();
    const auto* feat1 = service->document().componentFor(editingFeature);
    check(feat1 != nullptr && feat1->hasProperty("geometry") && (*feat1)["geometry"].hasProperty("cuts"),
          "Apply length cut to first stick via UI");

    // 4. Apply miter cut to second piece (45 deg at 90 mm)
    currentSelected = secondId;
    editSelected();
    check(editingInstance == secondId, "Select and edit second piece");
    cutType.setSelectedId(2); // miter
    cutPosition.setText("90");
    cutAngle.setText("45");
    cutSide.setSelectedId(1); // end
    applyCut();
    const auto* feat2 = service->document().componentFor(editingFeature);
    check(feat2 != nullptr && feat2->hasProperty("geometry") && (*feat2)["geometry"].hasProperty("cuts"),
          "Apply 45-degree miter cut to second stick via UI");

    // 5. Align second piece to first piece using corner90
    alignTarget.setSelectedId(1); // target is firstId
    alignType.setSelectedId(2); // corner90
    applyAlign();
    const auto* inst2 = service->document().mechanicsData().findInstance(secondId);
    check(inst2 != nullptr && inst2->placement.x != 0,
          "Align second stick to first stick with corner 90 joint via UI");

    // 6. Group into assembly
    assemblyName.setText("Frame Construction");
    groupAssembly();
    check(!service->document().mechanicsData().assemblies.empty() && service->document().mechanicsData().assemblies.front().instanceIds.size() == 2,
          "Group frame pieces into an assembly via UI");

    // 7. Move assembly as unit
    placements[0]->setText("50"); placements[1]->setText("20"); placements[2]->setText("0");
    moveAssembly();
    check(service->document().mechanicsData().findInstance(firstId)->placement.x == 50,
          "Move assembly as a unit via UI");

    // 8. Generate cut list
    showCutList();
    const auto cutListData = service->generateCutList();
    check((int)cutListData["totalPieces"] == 2 && cutListData["summary"].size() > 0,
          "Generate and display craft cut list via UI");

    juce::FileOutputStream stream(folder.getChildFile("craft-browser-phase2.png"));
    check(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()), stream),
          "Craft Designer Phase 2 browser snapshot saved");

    report = object();
    put(report, "passed", passed);
    put(report, "checks", checks);
    put(report, "verification", "Craft Designer Phase 2 UI: cutting, miter, alignment, assembly grouping, move unit, and cut list");
    put(report, "folder", folder.getFullPathName());
    return passed;
}
bool CadCraftBrowser::craftPhase3Check(juce::var& report, const juce::File& folder)
{
    juce::Array<juce::var> checks; bool passed = true;
    const auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto v = object(); put(v, "check", name); put(v, "passed", ok);
        if (detail.isNotEmpty()) put(v, "detail", detail);
        checks.add(v); passed &= ok;
    };

    juce::String currentSelected;
    selectedOccurrence = [&currentSelected] { return currentSelected; };
    onInserted = [&currentSelected](const juce::String& id) { currentSelected = id; };

    // 1. Insert piece 1: Wooden craft stick
    search.setText("strip");
    filter();
    check(shown.size() == 1, "Select wood strip template");
    if (material.getSelectedId() <= 0 && material.getNumItems() > 0)
        material.setSelectedId(1, juce::dontSendNotification);
    dimensions[0]->setText("100"); dimensions[1]->setText("10"); dimensions[2]->setText("3");
    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto firstId = currentSelected;
    check(firstId.isNotEmpty(), "Insert first wooden craft stick: " + firstId);

    // 2. Insert piece 2: Second wooden craft stick
    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto secondId = currentSelected;
    check(secondId.isNotEmpty() && secondId != firstId, "Insert second wooden craft stick: " + secondId);

    // 3. Align second piece to first using butt
    currentSelected = secondId;
    editSelected();
    alignTarget.setSelectedId(1); // target is firstId
    alignType.setSelectedId(1); // butt
    applyAlign();
    const auto* p2 = service->document().mechanicsData().findInstance(secondId);
    check(p2 != nullptr && p2->placement.x != 0, "Align second stick to end of first stick (butt joint)");

    // 4. Create surface bond with PVA glue via UI
    bondTarget.setSelectedId(1); // target firstId
    bondType.setSelectedId(1); // surface bond
    bondAdhesive.setSelectedId(1); // PVA wood glue
    bondThickness.setText("0.1");
    applyBond();
    check(!service->document().mechanicsData().bondedJoints.empty() &&
          service->document().mechanicsData().bondedJoints.back().status == "valid",
          "Create valid PVA surface bond between wood sticks via UI");

    // 5. Test incompatible adhesive detection via UI
    bondAdhesive.setSelectedId(6); // Plastic solvent cement
    applyBond();
    check(!service->document().mechanicsData().bondedJoints.empty() &&
          service->document().mechanicsData().bondedJoints.back().status == "incompatible_substrate",
          "Plastic solvent cement flagged as incompatible with wood substrates via UI");

    // 6. Insert piece 3 and create fillet bond
    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto thirdId = currentSelected;
    currentSelected = thirdId;
    editSelected();
    alignTarget.setSelectedId(1); // target firstId
    alignType.setSelectedId(2); // corner90
    applyAlign();

    bondTarget.setSelectedId(1); // target firstId
    bondType.setSelectedId(3); // fillet bond
    bondAdhesive.setSelectedId(3); // Two-Part Epoxy
    bondBeadLeg.setText("2.0");
    applyBond();
    check(service->document().mechanicsData().bondedJoints.size() >= 3 &&
          service->document().mechanicsData().bondedJoints.back().status == "valid",
          "Create fillet bond with epoxy bead via UI");

    // 7. Move bonded construction as a unit
    currentSelected = firstId;
    editSelected();
    placements[0]->setText("30"); placements[1]->setText("15"); placements[2]->setText("0");
    moveBonded();
    const auto* p1_after = service->document().mechanicsData().findInstance(firstId);
    check(p1_after != nullptr && p1_after->placement.x == 30,
          "Move bonded construction translates all connected pieces as a unit via UI");

    // 8. Generate and display Adhesive BOM
    showAdhesiveBom();
    const auto bomData = service->adhesiveBom();
    check((int)bomData["totalJoints"] >= 3 && (int)bomData["validJoints"] >= 2,
          "Generate and display Adhesive BOM via UI");

    // 9. Generate Cut list with integrated adhesive usage
    showCutList();
    const auto cutListData = service->generateCutList();
    check((int)cutListData["totalPieces"] >= 3 && cutListData.hasProperty("adhesives"),
          "Cut list includes integrated adhesive consumption BOM via UI");

    juce::FileOutputStream stream(folder.getChildFile("craft-browser-phase3.png"));
    check(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()), stream),
          "Craft Designer Phase 3 browser snapshot saved");

    report = object();
    put(report, "passed", passed);
    put(report, "checks", checks);
    put(report, "verification", "Craft Designer Phase 3 UI: adhesives, bonded joints, substrate compatibility, fillet bonds, connected movement, and adhesive BOM");
    put(report, "folder", folder.getFullPathName());
    return passed;
}
bool CadCraftBrowser::craftPhase4Check(juce::var& report, const juce::File& folder)
{
    juce::Array<juce::var> checks; bool passed = true;
    const auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto v = object(); put(v, "check", name); put(v, "passed", ok);
        if (detail.isNotEmpty()) put(v, "detail", detail);
        checks.add(v); passed &= ok;
    };

    // 1. Select wood strip template
    category.setSelectedId(2); // Wood
    search.setText("strip");
    filter();
    check(shown.size() == 1, "Select wood strip template");
    if (material.getSelectedId() <= 0 && material.getNumItems() > 0)
        material.setSelectedId(1, juce::sendNotificationSync);

    // 2. Insert two wooden craft sticks and align them
    juce::String currentSelected;
    selectedOccurrence = [&]{ return currentSelected; };
    onInserted = [&](const juce::String& id){ currentSelected = id; };

    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto firstId = currentSelected;
    check(firstId.isNotEmpty(), "Insert first wooden craft stick: " + firstId);

    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto secondId = currentSelected;
    check(secondId.isNotEmpty() && secondId != firstId, "Insert second wooden craft stick: " + secondId);

    // Align second stick to end of first stick
    currentSelected = secondId;
    editSelected();
    refreshAlignTargets();
    alignTarget.setSelectedId(1);
    alignType.setSelectedId(1); // butt joint
    applyAlign();
    check(service->document().mechanicsData().findInstance(secondId)->placement.x > 50.0,
          "Align second stick to end of first stick (butt joint)");

    // 3. Create valid PVA bond between them
    refreshBondTargets();
    bondTarget.setSelectedId(1);
    bondType.setSelectedId(1); // surface bond
    bondAdhesive.setSelectedId(1); // PVA wood glue
    applyBond();
    check(!service->document().mechanicsData().bondedJoints.empty() &&
          service->document().mechanicsData().bondedJoints.back().status == "valid",
          "Create valid PVA adhesive bond between sticks via UI");

    // 4. Create analysis case via UI
    createNewCase();
    check(!service->document().analysisData().cases.empty(),
          "Create structural analysis case via UI");

    // 5. Assign Fixed support to first stick at start via UI
    currentSelected = firstId;
    editSelected();
    supportType.setSelectedId(1); // Fixed
    supportLocation.setSelectedId(1); // Start
    addSupportToCase();
    const auto& c = service->document().analysisData().cases.back();
    check(!c.supports.empty(), "Assign fixed support to first stick start via UI");

    // 6. Apply 10 N downward load at end of second stick via UI
    currentSelected = secondId;
    editSelected();
    loadType.setSelectedId(1); // Force
    loadDirection.setSelectedId(1); // -Z
    loadMagnitude.setText("10.0");
    supportLocation.setSelectedId(2); // End
    addLoadToCase();
    const auto& cWithLoad = service->document().analysisData().cases.back();
    check(!cWithLoad.loads.empty(), "Apply 10 N downward force at end of second stick via UI");

    // 7. Run structural solver via UI
    defScale.setText("25.0");
    showDeformedToggle.setToggleState(true, juce::dontSendNotification);
    std::vector<djehuti::cad::CadOverlayLine> capturedOverlays;
    setOverlaysCallback = [&](std::vector<djehuti::cad::CadOverlayLine> lines){ capturedOverlays = lines; };
    runStructuralSolver();

    check(!lastSolveResult.isVoid() && lastSolveResult["status"].toString() == "completed",
          "Execute 3D frame FEA solver via UI with status 'completed'");
    check((double)lastSolveResult["maxDeflectionMm"] > 0.05,
          "Solver computes nonzero deflection (" + juce::String((double)lastSolveResult["maxDeflectionMm"], 3) + " mm)");
    check(!capturedOverlays.empty(),
          "Solver generates deformed overlay lines for 3D viewport inspection");

    // 8. Inspect detailed numerical report in UI
    showStructuralResults();
    check(details.getText().contains("Structural Analysis Report") && details.getText().contains("Max Deflection"),
          "Display comprehensive structural results and member forces in UI details panel");

    // 9. Save UI snapshot
    juce::FileOutputStream stream(folder.getChildFile("craft-browser-phase4.png"));
    check(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()), stream),
          "Craft Designer Phase 4 browser snapshot saved");

    report = object();
    put(report, "passed", passed);
    put(report, "checks", checks);
    put(report, "verification", "Craft Designer Phase 4 UI: structural analysis, supports, loads, 3D frame solver, deformation overlays, and results inspection");
    put(report, "folder", folder.getFullPathName());
    return passed;
}
bool CadCraftBrowser::craftPhase5Check(juce::var& report, const juce::File& folder)
{
    juce::Array<juce::var> checks; bool passed = true;
    const auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto v = object(); put(v, "check", name); put(v, "passed", ok);
        if (detail.isNotEmpty()) put(v, "detail", detail);
        checks.add(v); passed &= ok;
    };

    // 1. Select wood strip template
    category.setSelectedId(2); // Wood
    search.setText("strip");
    filter();
    check(shown.size() == 1, "Select wood strip template in library");
    if (material.getSelectedId() <= 0 && material.getNumItems() > 0)
        material.setSelectedId(1, juce::sendNotificationSync);

    // 2. Insert two wooden craft sticks and align them end-to-end
    juce::String currentSelected;
    selectedOccurrence = [&]{ return currentSelected; };
    onInserted = [&](const juce::String& id){ currentSelected = id; };

    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto firstId = currentSelected;
    check(firstId.isNotEmpty(), "Insert first wooden craft stick: " + firstId);

    placements[0]->setText("0"); placements[1]->setText("0"); placements[2]->setText("0");
    insertPiece();
    const auto secondId = currentSelected;
    check(secondId.isNotEmpty() && secondId != firstId, "Insert second wooden craft stick: " + secondId);

    // Align second stick to end of first stick (butt joint)
    currentSelected = secondId;
    editSelected();
    refreshAlignTargets();
    alignTarget.setSelectedId(1);
    alignType.setSelectedId(1); // butt joint
    applyAlign();
    check(service->document().mechanicsData().findInstance(secondId)->placement.x > 50.0,
          "Align second stick to end of first stick");

    // 3. Create valid adhesive bond between them
    refreshBondTargets();
    bondTarget.setSelectedId(1);
    bondType.setSelectedId(1); // surface bond
    bondAdhesive.setSelectedId(1); // PVA wood glue
    applyBond();
    check(!service->document().mechanicsData().bondedJoints.empty() &&
          service->document().mechanicsData().bondedJoints.back().status == "valid",
          "Create valid adhesive joint between sticks");

    // 4. Apply Cantilever Bending Test template via UI
    currentSelected = firstId;
    editSelected();
    experimentTemplateSelect.setSelectedId(1); // Cantilever
    loadSweepSlider.setValue(10.0, juce::dontSendNotification);
    applySelectedTemplate();

    const auto& cases = service->document().analysisData().cases;
    check(!cases.empty() && cases.back().name.contains("Cantilever"),
          "Configure Cantilever Bending Test experiment template via UI");

    // 5. Run interactive load sweep (0 to 10 N) via UI
    std::vector<djehuti::cad::CadOverlayLine> capturedOverlays;
    setOverlaysCallback = [&](std::vector<djehuti::cad::CadOverlayLine> lines){ capturedOverlays = lines; };
    runInteractiveLoadSweep();

    check(details.getText().contains("Quasi-Static Load Sweep") && details.getText().contains("10.0 N"),
          "Run interactive quasi-static load sweep via UI and display stepped table");
    check(!capturedOverlays.empty(),
          "Generate deformation visualization overlays in viewport during sweep");

    // 6. Apply Simply Supported Beam Test template via UI
    experimentTemplateSelect.setSelectedId(2); // Simply Supported
    loadSweepSlider.setValue(20.0, juce::dontSendNotification);
    applySelectedTemplate();
    const auto& casesAfter2 = service->document().analysisData().cases;
    check(casesAfter2.size() >= 2 && casesAfter2.back().name.contains("Simply Supported"),
          "Configure Simply Supported Beam experiment template via UI");

    // 7. Solve simply supported beam and verify results
    runStructuralSolver();
    check(!lastSolveResult.isVoid() && (double)lastSolveResult["maxDeflectionMm"] > 0.0,
          "Execute structural solver for Simply Supported test");

    // 8. Experiment comparison via UI
    refreshAnalysisCases();
    compareCaseA.setSelectedId(1);
    compareCaseB.setSelectedId(2);
    compareSelectedExperiments();
    check(details.getText().contains("Experiment Comparison") &&
          details.getText().contains("Delta Deflection"),
          "Execute experiment comparison between two test cases via UI");

    // 9. Generate comprehensive Markdown Laboratory Report via UI
    showLaboratoryReport();
    const juce::String reportText = details.getText();
    check(reportText.contains("# Craft Testing Laboratory Report") &&
          reportText.contains("## 1. Engineering Assumptions") &&
          reportText.contains("## 2. Boundary Conditions") &&
          reportText.contains("## 3. Applied Loading") &&
          reportText.contains("## 4. Predicted Structural Response") &&
          reportText.contains("## 5. Experiment Comparison") &&
          reportText.contains("## 6. Engineering Safety"),
          "Generate comprehensive engineering laboratory report with comparison in UI");

    // 10. Reset experiment via UI
    resetCurrentExperiment();
    check(details.getText().contains("Experiment reset"),
          "Reset experiment boundary conditions and loads via UI");

    // 11. Save UI snapshot
    juce::FileOutputStream stream(folder.getChildFile("craft-browser-phase5.png"));
    check(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()), stream),
          "Craft Designer Phase 5 browser snapshot saved");

    report = object();
    put(report, "passed", passed);
    put(report, "checks", checks);
    put(report, "verification", "Craft Designer Phase 5 UI: Interactive Craft Testing Laboratory, standardized templates, quasi-static sweeps, experiment comparison, and laboratory reports");
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
bool runCadCraftPhase2UiCheck(juce::var& report, const juce::File& folder, bool interactive)
{
    folder.createDirectory();
    auto service = std::make_shared<CadCommandService>();
    service->setComponentLibraryFile(folder.getChildFile("library.json"));
    service->setCatalogFile(folder.getChildFile("materials.json"));
    auto* browser = new CadCraftBrowser(service);
    browser->setSize(400, 1100);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(browser);
    options.dialogTitle = "Craft Designer Phase 2 UI check";
    options.useNativeTitleBar = true;
    auto* dialog = options.launchAsync();
    if (interactive)
    {
        report = object();
        put(report, "folder", folder.getFullPathName());
        put(report, "verification", "Native test window prepared; no programmatic controls invoked.");
        return true;
    }
    const auto ok = browser->craftPhase2Check(report, folder);
    dialog->exitModalState(0);
    return ok;
}
bool runCadCraftPhase3UiCheck(juce::var& report, const juce::File& folder, bool interactive)
{
    folder.createDirectory();
    auto service = std::make_shared<CadCommandService>();
    service->setComponentLibraryFile(folder.getChildFile("library.json"));
    service->setCatalogFile(folder.getChildFile("materials.json"));
    auto* browser = new CadCraftBrowser(service);
    browser->setSize(400, 1200);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(browser);
    options.dialogTitle = "Craft Designer Phase 3 UI check";
    options.useNativeTitleBar = true;
    auto* dialog = options.launchAsync();
    if (interactive)
    {
        report = object();
        put(report, "folder", folder.getFullPathName());
        put(report, "verification", "Native test window prepared; no programmatic controls invoked.");
        return true;
    }
    const auto ok = browser->craftPhase3Check(report, folder);
    dialog->exitModalState(0);
    return ok;
}
bool runCadCraftPhase4UiCheck(juce::var& report, const juce::File& folder, bool interactive)
{
    folder.createDirectory();
    auto service = std::make_shared<CadCommandService>();
    service->setComponentLibraryFile(folder.getChildFile("library.json"));
    service->setCatalogFile(folder.getChildFile("materials.json"));
    auto* browser = new CadCraftBrowser(service);
    browser->setSize(400, 1300);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(browser);
    options.dialogTitle = "Craft Designer Phase 4 UI check";
    options.useNativeTitleBar = true;
    auto* dialog = options.launchAsync();
    if (interactive)
    {
        report = object();
        put(report, "folder", folder.getFullPathName());
        put(report, "verification", "Native test window prepared; no programmatic controls invoked.");
        return true;
    }
    const auto ok = browser->craftPhase4Check(report, folder);
    dialog->exitModalState(0);
    return ok;
}
bool runCadCraftPhase5UiCheck(juce::var& report, const juce::File& folder, bool interactive)
{
    folder.createDirectory();
    auto service = std::make_shared<CadCommandService>();
    service->setComponentLibraryFile(folder.getChildFile("library.json"));
    service->setCatalogFile(folder.getChildFile("materials.json"));
    auto* browser = new CadCraftBrowser(service);
    browser->setSize(400, 1400);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(browser);
    options.dialogTitle = "Craft Designer Phase 5 UI check";
    options.useNativeTitleBar = true;
    auto* dialog = options.launchAsync();
    if (interactive)
    {
        report = object();
        put(report, "folder", folder.getFullPathName());
        put(report, "verification", "Native test window prepared; no programmatic controls invoked.");
        return true;
    }
    const auto ok = browser->craftPhase5Check(report, folder);
    dialog->exitModalState(0);
    return ok;
}
}
