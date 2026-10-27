#include "CadMaterialsManager.h"
#include <cmath>
#include <cstdlib>

namespace
{
using namespace djehuti::cad;
class TextField final : public juce::TextPropertyComponent
{
public:
    TextField(const juce::String& name, juce::String& valueIn, bool multiline = false)
        : TextPropertyComponent(name, 4096, multiline), value(valueIn) {}
    void setText(const juce::String& text) override { value = text; }
    juce::String getText() const override { return value; }
private:
    juce::String& value;
};
class ChoiceField final : public juce::ChoicePropertyComponent
{
public:
    ChoiceField(const juce::String& name, juce::String& valueIn, const juce::StringArray& labels, juce::StringArray idsIn)
        : ChoicePropertyComponent(name), value(valueIn), ids(std::move(idsIn)) { choices = labels; }
    void setIndex(int index) override { if (juce::isPositiveAndBelow(index, ids.size())) value = ids[index]; }
    int getIndex() const override { return ids.indexOf(value); }
private:
    juce::String& value;
    juce::StringArray ids;
};
class Manager final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    Manager(std::shared_ptr<CadCommandService> serviceIn, const juce::String& feature) : service(std::move(serviceIn)), partsSelection(feature)
    {
        setSize(1120, 780);
        title.setText("Materials and Sheet Stock", juce::dontSendNotification); title.setFont(juce::Font(21.0f, juce::Font::bold));
        scope.addItemList({"Application catalog", "Current document"}, 1); scope.setSelectedId(1);
        kind.addItemList({"Engineering materials", "Sheet stock"}, 1); kind.setSelectedId(1);
        scope.onChange = [this] { selected.clear(); reload(); }; kind.onChange = [this] { selected.clear(); reload(); };
        search.setTextToShowWhenEmpty("Search materials or stock...", juce::Colour(0xff8da5b6));
        search.onTextChange = [this] { reload(); };
        list.setModel(this); list.setRowHeight(48); list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff18232c));
        part.onChange = [this] { partsSelection = part.getSelectedId() > 0 ? partIds[part.getSelectedId()-1] : juce::String(); updateSummary(); };
        for (auto* c : std::vector<juce::Component*>{&title,&scope,&kind,&search,&list,&properties,&part,&summary,&status,&newButton,&edit,&duplicate,&remove,&save,&cancel,&importButton,&assign,&clearMaterial,&makeDefault,&clearDefault,&undo,&redo,&importFile,&exportFile,&refresh}) addAndMakeVisible(*c);
        newButton.onClick = [this] { begin("create"); }; edit.onClick = [this] { begin("update"); }; duplicate.onClick = [this] { begin("duplicate"); };
        cancel.onClick = [this] { editing = false; reload(); };
        save.onClick = [this] { saveDraft(); };
        remove.onClick = [this]
        {
            auto args = object(); args.getDynamicObject()->setProperty("action", catalog() ? (material() ? "deleteMaterial" : "deleteStock") : "delete");
            args.getDynamicObject()->setProperty(material() ? "material" : "stock", selected);
            perform(catalog() ? service->catalogCommand(args) : material() ? service->materialCommand("cad_material_delete", args) : service->stockCommand(args));
        };
        importButton.onClick = [this] { use(false); };
        assign.onClick = [this] { use(true); };
        clearMaterial.onClick = [this] { auto a=object(); a.getDynamicObject()->setProperty("feature",partsSelection); perform(service->materialCommand("cad_material_clear",a)); };
        makeDefault.onClick = [this] { if(catalog()) use(false,true); else {auto a=object();a.getDynamicObject()->setProperty("action","setDefault");a.getDynamicObject()->setProperty("stock",selected);perform(service->stockCommand(a));} };
        clearDefault.onClick = [this] {auto a=object();a.getDynamicObject()->setProperty("action","setDefault");perform(service->stockCommand(a));};
        undo.onClick = [this] { juce::String e; historyResult(service->undo(e),e); }; redo.onClick = [this] { juce::String e; historyResult(service->redo(e),e); };
        importFile.onClick = [this] { chooseFile(false); }; exportFile.onClick = [this] { chooseFile(true); };
        refresh.onClick = [this] { auto a=object();a.getDynamicObject()->setProperty("action","reload"); perform(service->catalogCommand(a)); };
        summary.setJustificationType(juce::Justification::topLeft); status.setJustificationType(juce::Justification::topLeft);
        status.setColour(juce::Label::textColourId,juce::Colour(0xffa9c9dd));
        importButton.setTooltip("Copy a self-contained snapshot into this document. Catalog changes never update it automatically.");
        makeDefault.setTooltip("Choose stock for future sheet features; this does not convert or modify any existing solid.");
        reload(); message("Select a definition to inspect. Duplicate bundled references to customize; blank forming values are unspecified."); startTimerHz(3);
    }
    ~Manager() override { stopTimer(); list.setModel(nullptr); properties.clear(); }
    bool smokeCheck(const juce::File& folder, juce::var& report)
    {
        juce::Array<juce::var> checks;
        bool passed = true;
        const auto check = [&](bool ok, const juce::String& label)
        {
            auto* item = new juce::DynamicObject(); item->setProperty("check", label); item->setProperty("passed", ok);
            checks.add(item); passed = passed && ok;
        };
        const auto finish = [&]
        {
            auto* result = new juce::DynamicObject(); result->setProperty("passed", passed); result->setProperty("checks", checks);
            result->setProperty("folder", folder.getFullPathName()); result->setProperty("verification", "Actual JUCE controls and component renders; desktop input/capture unavailable"); report = result;
            return passed;
        };
        const auto field = [&](const juce::String& label, const juce::String& value)
        {
            std::function<TextField*(juce::Component&)> find = [&](juce::Component& c) -> TextField*
            {
                if (auto* text = dynamic_cast<TextField*>(&c); text && text->getName() == label) return text;
                for (auto* child : c.getChildren()) if (auto* found = find(*child)) return found;
                return nullptr;
            };
            if (auto* control = find(properties)) { control->setText(value); control->refresh(); return true; }
            check(false, "Missing UI property: " + label); return false;
        };
        const auto capture = [&](const char* filename)
        {
            const auto image = createComponentSnapshot(getLocalBounds(), true);
            juce::FileOutputStream output(folder.getChildFile(filename));
            check(output.openedOk() && juce::PNGImageFormat().writeImageToStream(image, output), juce::String("Render ") + filename);
        };
        search.setText("PETG",true);
        reload();
        check(rows.size()==1&&rows[0]["id"].toString()=="mat-research-mat-010"&&current()["model"]=="referenceOnly",
              "Material search finds the researched PETG reference and its incomplete model (rows="+juce::String(rows.size())+", selected="+selected+")");
        search.setText(juce::String(),true);
        reload();
        check(scope.getText() == "Application catalog" && material() && !edit.isEnabled() && duplicate.isEnabled(), "Bundled references are inspectable and read-only");
        capture("01-reference-material.png");
        newButton.onClick(); save.onClick();
        check(editing && status.getText().contains("required"), "Blank required material fields are rejected in the editor");
        field("Name", "UI smoke aluminum"); field("Material family", "Aluminum"); field("Alloy / specification", "DIAGNOSTIC ONLY");
        field("Condition / temper", "UI fixture"); field("Density (kg/m^3)", "2700"); field("Young's modulus (MPa)", "69000"); field("Poisson's ratio", "0.33");
        field("Source / provenance", "In-application UI smoke fixture, not production reference data");
        save.onClick();
        const auto materialId = selected;
        check(!editing && current()["name"].toString() == "UI smoke aluminum" && edit.isEnabled(), "New user material saved through GUI controls");
        if (editing) return finish();
        edit.onClick(); field("Condition / temper", "UI fixture revised"); save.onClick();
        check(!editing && (int) current()["revision"] == 2, "Material editor retains identity and advances revision");
        kind.setSelectedId(2, juce::sendNotificationSync); newButton.onClick();
        field("Name", "UI smoke stock"); field("Substrate thickness (mm)", "0");
        // Choose the same catalog material through the real choice property's UI setter.
        std::function<ChoiceField*(juce::Component&)> choice = [&](juce::Component& c) -> ChoiceField*
        {
            if (auto* p = dynamic_cast<ChoiceField*>(&c); p && p->getName() == "Engineering material") return p;
            for (auto* child : c.getChildren()) if (auto* found = choice(*child)) return found;
            return nullptr;
        };
        auto listing = service->catalogCommand(object()); int materialIndex = 0;
        for (const auto& m : *listing["catalog"]["materials"].getArray()) { if (m["id"].toString() == materialId) break; ++materialIndex; }
        if (auto* p = choice(properties)) { p->setIndex(materialIndex); p->refresh(); } else check(false, "Stock material choice exists");
        save.onClick(); check(editing && status.getText().contains("thickness"), "Stock editor rejects zero substrate thickness");
        field("Substrate thickness (mm)", "1.5"); field("Finished thickness (mm)", "1.6");
        field("Sheet width (mm)", "1000"); field("Sheet length (mm)", "2000"); save.onClick();
        const auto catalogStockId = selected;
        check(!editing && current()["name"].toString() == "UI smoke stock" && !current().hasProperty("kFactor"), "Stock saved with material selection and unspecified forming values");
        if (editing) return finish();
        capture("02-user-stock.png");
        makeDefault.onClick();
        const auto documentStockId = selected;
        const auto* documentStock = service->document().stocksData().find(documentStockId);
        const auto documentMaterialId = documentStock ? documentStock->materialId : juce::String();
        check(!catalog() && documentStock && service->document().stocksData().defaultStockId == documentStockId
            && documentStock->catalogId == catalogStockId, "Use as default imports linked snapshots and changes manager scope");
        if (!documentStock) return finish();
        kind.setSelectedId(1, juce::sendNotificationSync); assign.onClick();
        const auto* assignment = service->document().materialsData().assignmentFor(partsSelection);
        check(assignment && assignment->materialId == documentMaterialId && summary.getText().contains("UI smoke aluminum"), "Assignment control and current-assignment display share document definitions");
        capture("03-assigned-material.png");
        kind.setSelectedId(2, juce::sendNotificationSync); edit.onClick(); field("Substrate thickness (mm)", "1.55"); save.onClick();
        check(service->document().stocksData().find(documentStockId)->revision == 2, "Document stock editor commits a shared revision");
        undo.onClick(); check(service->document().stocksData().find(documentStockId)->thicknessMm == 1.5, "GUI Undo restores stock parameters");
        redo.onClick(); check(service->document().stocksData().find(documentStockId)->thicknessMm == 1.55, "GUI Redo restores edited stock parameters");
        clearDefault.onClick(); check(service->document().stocksData().defaultStockId.isEmpty(), "GUI clears default stock");
        undo.onClick(); check(service->document().stocksData().defaultStockId == documentStockId, "GUI Undo restores default selection");
        auto query = object(); query.getDynamicObject()->setProperty("stock", documentStockId);
        check((double) service->stockCommand(query)["stock"]["thicknessMm"] == 1.55, "Agent stock query sees the GUI-edited definition");
        query = object(); query.getDynamicObject()->setProperty("feature", partsSelection);
        check(service->materialCommand("cad_material_list", query)["assignment"]["material"].toString() == documentMaterialId, "Agent material query sees the GUI assignment");
        capture("04-document-stock.png");
        const auto file = folder.getChildFile("ui-document.djo"); juce::String error;
        check(service->save(file, error), "Document containing GUI-authored definitions saves natively");
        return finish();
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff111a22)); }
    void resized() override
    {
        auto area=getLocalBounds().reduced(16); title.setBounds(area.removeFromTop(34));
        auto filters=area.removeFromTop(38); scope.setBounds(filters.removeFromLeft(245).reduced(2)); kind.setBounds(filters.removeFromLeft(235).reduced(2));
        importFile.setBounds(filters.removeFromLeft(140).reduced(2)); exportFile.setBounds(filters.removeFromLeft(140).reduced(2)); refresh.setBounds(filters.removeFromLeft(90).reduced(2));
        auto actions=area.removeFromTop(38); for(auto* b:{&newButton,&edit,&duplicate,&remove,&save,&cancel}) b->setBounds(actions.removeFromLeft(100).reduced(2));
        search.setBounds(area.removeFromTop(34).removeFromLeft(340).reduced(2));
        auto bottom=area.removeFromBottom(165); status.setBounds(bottom.removeFromBottom(44));
        auto history=bottom.removeFromBottom(36); undo.setBounds(history.removeFromLeft(85).reduced(2)); redo.setBounds(history.removeFromLeft(85).reduced(2)); clearDefault.setBounds(history.removeFromLeft(170).reduced(2));
        auto usage=bottom.removeFromBottom(38); importButton.setBounds(usage.removeFromLeft(172).reduced(2)); assign.setBounds(usage.removeFromLeft(182).reduced(2)); clearMaterial.setBounds(usage.removeFromLeft(150).reduced(2)); makeDefault.setBounds(usage.removeFromLeft(190).reduced(2));
        part.setBounds(bottom.removeFromLeft(270).reduced(2)); summary.setBounds(bottom.reduced(4,0));
        list.setBounds(area.removeFromLeft(340).reduced(2)); properties.setBounds(area.reduced(8,2));
    }
private:
    static juce::var object() { return new juce::DynamicObject(); }
    bool catalog() const { return scope.getSelectedId()==1; }
    bool material() const { return kind.getSelectedId()==1; }
    int getNumRows() override { return rows.size(); }
    void paintListBoxItem(int index,juce::Graphics& g,int width,int height,bool isSelected) override
    {
        if(!juce::isPositiveAndBelow(index,rows.size()))return;
        const auto& row=rows[index]; if(isSelected)g.fillAll(juce::Colour(0xff28525d));
        g.setColour(juce::Colours::white); g.setFont(14.0f); g.drawFittedText(row["name"].toString(),8,3,width-16,23,juce::Justification::centredLeft,1);
        g.setColour(juce::Colour(0xffaac0d1));g.setFont(11.0f);
        const auto origin=catalog()?((bool)row["bundled"]?"Bundled / read-only":"User catalog"):"Document snapshot";
        const auto family=material()?row["family"].toString():juce::String();
        g.drawText(juce::String(origin)+(family.isNotEmpty()?"  |  "+family:juce::String())+"  |  rev "+row["revision"].toString(),8,26,width-16,height-28,juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int index) override
    {
        if(reloading||!juce::isPositiveAndBelow(index,rows.size()))return;
        if(editing){const juce::ScopedValueSetter<bool> guard(reloading,true);list.selectRow(rowIndex());return;}
        selected=rows[index]["id"].toString();showRow();
    }
    int rowIndex() const { for(int i=0;i<rows.size();++i)if(rows[i]["id"].toString()==selected)return i;return -1; }
    juce::var current() const {const auto i=rowIndex();return i>=0?rows[i]:juce::var();}
    void reload()
    {
        const juce::ScopedValueSetter<bool> guard(reloading,true); rows.clear();
        if(catalog())
        {
            auto result=service->catalogCommand(object());
            if(!(bool)result["ok"]){message(result["error"].toString(),true);}
            else if(auto* entries=result["catalog"][material()?"materials":"stocks"].getArray())rows=*entries;
        }
        else if(material())for(const auto& m:service->document().materialsData().materials)rows.add(matl::materialToJson(m));
        else for(const auto& s:service->document().stocksData().stocks)rows.add(stock::toJson(s));
        const auto query=search.getText().trim();
        if(query.isNotEmpty())
            for(int i=rows.size()-1;i>=0;--i)
            {
                const auto& row=rows.getReference(i);
                if(!(row["name"].toString()+" "+row["family"].toString()+" "+row["specification"].toString()+" "+row["condition"].toString()+" "+row["source"].toString()).containsIgnoreCase(query))rows.remove(i);
            }
        if(rowIndex()<0)selected=rows.isEmpty()?juce::String():rows[0]["id"].toString();
        list.updateContent();list.selectRow(rowIndex());showRow();updateSummary();
        shownRevision=service->document().revision();shownDocument=service->document().documentInstance();
    }
    void showRow()
    {
        if(editing)return;
        fillDraft(current());buildProperties();updateButtons();
    }
    void fillDraft(const juce::var& value)
    {
        properties.clear();
        draft.clear();
        if(material())
        {
            draft["model"] = value.getProperty("model", "isotropic").toString();
            for(auto key:{"name","family","specification","condition","source","note"})draft[key]=value[key].toString();
            for(auto pair:{std::pair<const char*,const char*>{"densityKgPerM3","density"},{"youngsModulusMPa","youngsModulus"},{"poissonsRatio","poissonsRatio"},{"yieldStrengthMPa","yieldStrength"},{"tensileStrengthMPa","tensileStrength"}})
                draft[pair.first]=value["properties"][pair.second]["value"].toString();
        }
        else
        {
            for(auto key:{"name","material","thicknessMm","finishedThicknessMm","sheetWidthMm","sheetLengthMm","gauge","gaugeStandard","finish","coating","insideRadiusMm","kFactor","source","note","qualification","formingSource"})draft[key]=value[key].toString();
            if(draft["qualification"].isEmpty())draft["qualification"]="unqualified";
        }
        draft["identity"] = value.isObject() ? value["id"].toString() + " / revision " + value["revision"].toString() : "New identity on Save";
        if(value["catalogId"].toString().isNotEmpty()) draft["identity"] += " / from " + value["catalogId"].toString() + " revision " + value["catalogRevision"].toString();
    }
    void buildProperties()
    {
        properties.clear();juce::Array<juce::PropertyComponent*> fields;
        const auto text=[&](const char* key,const char* label,bool multi=false){fields.add(new TextField(label,draft[key],multi));};
        text("name","Name");
        if(material())
        {
            fields.add(new ChoiceField("Material model", draft["model"], {"Isotropic elastic", "Density only (no structural properties)", "Reference only (incomplete properties)"}, {"isotropic", "densityOnly", "referenceOnly"}));
            text("family","Material family");text("specification","Alloy / specification");text("condition","Condition / temper");
            text("densityKgPerM3","Density (kg/m^3)");text("youngsModulusMPa","Young's modulus (MPa)");text("poissonsRatio","Poisson's ratio");text("yieldStrengthMPa","Yield strength (MPa)");text("tensileStrengthMPa","Tensile strength (MPa)");
        }
        else
        {
            juce::StringArray labels,ids;
            if(catalog())
            {
                auto r=service->catalogCommand(object());if(auto* entries=r["catalog"]["materials"].getArray())for(const auto& m:*entries){labels.add(m["name"].toString());ids.add(m["id"].toString());}
            }
            else for(const auto& m:service->document().materialsData().materials){labels.add(m.name);ids.add(m.id);}
            fields.add(new ChoiceField("Engineering material",draft["material"],labels,ids));
            text("thicknessMm","Substrate thickness (mm)");text("finishedThicknessMm","Finished thickness (mm)");
            text("sheetWidthMm","Sheet width (mm)");text("sheetLengthMm","Sheet length (mm)");text("gauge","Gauge designation");text("gaugeStandard","Gauge standard");
            text("finish","Finish");text("coating","Coating");text("insideRadiusMm","Inside radius (mm)");text("kFactor","K-factor (0..1, optional)");
            fields.add(new ChoiceField("Forming qualification",draft["qualification"],{"Unqualified","Reference","Shop qualified"},{"unqualified","reference","shopQualified"}));
            text("formingSource","Forming qualification source",true);
        }
        text("source","Source / provenance",true);text("note","Notes",true);properties.addProperties(fields);properties.setEnabled(editing);
        auto* identity = new TextField("Identity / imported origin", draft["identity"], true); identity->setEnabled(false);
        properties.addProperties({identity});
    }
    void begin(const juce::String& action)
    {
        operation=action;draftId=selected;draftRevision=(int)current()["revision"];draftDocument=service->document().documentInstance();
        if(action=="create")fillDraft({});else fillDraft(current());
        if(action=="duplicate")draft["name"]+=" copy";
        editing=true;buildProperties();updateButtons();
        message(action=="update"?"Save updates this shared definition for all its users. Duplicate for an independent variant.":"Enter the definition, then Save. Blank optional values remain unspecified.");
    }
    bool definition(juce::var& result,juce::String& error)
    {
        result=object();auto* o=result.getDynamicObject();
        const juce::StringArray numbers=material()?juce::StringArray{"densityKgPerM3","youngsModulusMPa","poissonsRatio","yieldStrengthMPa","tensileStrengthMPa"}:juce::StringArray{"thicknessMm","finishedThicknessMm","sheetWidthMm","sheetLengthMm","insideRadiusMm","kFactor"};
        juce::Array<juce::var> clear;
        for(const auto& [key,value]:draft)
        {
            if(key=="identity") continue;
            if(!numbers.contains(key)){o->setProperty(key,value);continue;}
            const auto raw=value.trim().toStdString();
            if(raw.empty())
            {
                if((key=="densityKgPerM3"&&draft["model"]!="referenceOnly")||key=="thicknessMm"||((key=="youngsModulusMPa"||key=="poissonsRatio")&&draft["model"]=="isotropic")){error=key+" is required.";return false;}
                if(material())clear.add(key);else o->setProperty(key,juce::var());continue;
            }
            char* end=nullptr;const auto number=std::strtod(raw.c_str(),&end);
            if(end!=raw.c_str()+raw.size()||!std::isfinite(number)){error=key+" must be a finite number.";return false;}
            o->setProperty(key,number);
        }
        if(material())o->setProperty("clear",clear);return true;
    }
    void saveDraft()
    {
        if(!catalog()&&draftDocument!=service->document().documentInstance()){message("The active document changed. Cancel and reload this definition.",true);return;}
        if(operation=="update")
        {
            int revision=0;
            if(catalog()) {auto r=service->catalogCommand(object());if(auto* entries=r["catalog"][material()?"materials":"stocks"].getArray())for(const auto& v:*entries)if(v["id"].toString()==draftId)revision=(int)v["revision"];}
            else if(material()){if(const auto* m=service->document().materialsData().find(draftId))revision=m->revision;}
            else if(const auto* s=service->document().stocksData().find(draftId))revision=s->revision;
            if(revision!=draftRevision){message("Definition changed while editing. Cancel and reload before applying changes.",true);return;}
        }
        juce::String error;juce::var def;if(!definition(def,error)){message(error,true);return;}
        auto a=object();a.getDynamicObject()->setProperty(material()?"material":"stock",draftId);
        juce::var result;
        if(catalog()) {a.getDynamicObject()->setProperty("action",operation+(material()?"Material":"Stock"));a.getDynamicObject()->setProperty("definition",def);result=service->catalogCommand(a);}
        else if(material())
        {
            def.getDynamicObject()->setProperty("material",draftId);
            result=service->materialCommand("cad_material_"+operation,def);
        }
        else {a.getDynamicObject()->setProperty("action",operation);a.getDynamicObject()->setProperty("definition",def);result=service->stockCommand(a);}
        if((bool)result["ok"])editing=false;perform(result);
    }
    void use(bool assigning,bool defaultStock=false)
    {
        auto a=object();a.getDynamicObject()->setProperty(material()?"material":"stock",selected);
        if(catalog())
        {
            a.getDynamicObject()->setProperty("action",material()?"useMaterial":"useStock");a.getDynamicObject()->setProperty("setDefault",defaultStock);
            if(assigning)a.getDynamicObject()->setProperty("feature",partsSelection);
            auto r=service->catalogCommand(a);
            if((bool)r["ok"]){scope.setSelectedId(2,juce::dontSendNotification);selected=r["id"].toString();}perform(r);
        }
        else {a.getDynamicObject()->setProperty("feature",partsSelection);perform(service->materialCommand("cad_material_assign",a));}
    }
    void updateButtons()
    {
        const bool exists=current().isObject();const bool writable=exists&&(!catalog()||!(bool)current()["bundled"]);
        scope.setEnabled(!editing);kind.setEnabled(!editing);newButton.setEnabled(!editing);edit.setEnabled(!editing&&writable);duplicate.setEnabled(!editing&&exists);remove.setEnabled(!editing&&writable);
        save.setEnabled(editing);cancel.setEnabled(editing);importButton.setEnabled(!editing&&catalog()&&exists);assign.setEnabled(!editing&&material()&&exists&&partsSelection.isNotEmpty());
        clearMaterial.setEnabled(!editing&&partsSelection.isNotEmpty()&&service->document().materialsData().assignmentFor(partsSelection));
        makeDefault.setEnabled(!editing&&!material()&&exists);clearDefault.setEnabled(!editing&&service->document().stocksData().defaultStockId.isNotEmpty());
        undo.setEnabled(!editing);redo.setEnabled(!editing);importFile.setEnabled(!editing);exportFile.setEnabled(!editing);refresh.setEnabled(!editing);search.setEnabled(!editing);
    }
    void updateSummary()
    {
        const juce::ScopedValueSetter<bool> guard(reloading,true);part.clear(juce::dontSendNotification);partIds.clear();int chosen=0;
        for(const auto& f:service->document().features())if(!f.shape.IsNull()&&TopExp_Explorer(f.shape,TopAbs_SOLID).More()){partIds.add(f.id);part.addItem(f.name,partIds.size());if(f.id==partsSelection)chosen=partIds.size();}
        part.setSelectedId(chosen,juce::dontSendNotification);if(chosen==0)partsSelection.clear();part.setTextWhenNothingSelected("Select a solid part to assign material");
        juce::String text="Material: none";
        if(const auto* a=service->document().materialsData().assignmentFor(partsSelection))if(const auto* m=service->document().materialsData().find(a->materialId))text="Material: "+m->name+" ("+m->id+")";
        text+="\nDefault stock: ";if(const auto* s=service->document().stocksData().find(service->document().stocksData().defaultStockId))text+=s->name;else text+="none";
        summary.setText(text,juce::dontSendNotification);updateButtons();
    }
    void message(const juce::String& text,bool error=false){status.setColour(juce::Label::textColourId,error?juce::Colour(0xffffa48e):juce::Colour(0xffa9c9dd));status.setText(text,juce::dontSendNotification);}
    void perform(const juce::var& result){if(!(bool)result["ok"]){message(result["error"].toString(),true);return;}if(result["id"].toString().isNotEmpty())selected=result["id"].toString();reload();message("Saved. Document operations use CAD undo/redo; catalog entries are saved to user data.");}
    void historyResult(bool ok,const juce::String& e){if(ok){reload();message("Document history restored.");}else message(e,true);}
    void chooseFile(bool exporting)
    {
        chooser=std::make_unique<juce::FileChooser>(exporting?"Export portable engineering catalog":"Import engineering catalog",juce::File(),"*.json");
        const juce::Component::SafePointer<Manager> safe(this);
        chooser->launchAsync(exporting?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting:juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,exporting](const juce::FileChooser& fc)
        {if(safe==nullptr||fc.getResult()==juce::File())return;auto a=object();a.getDynamicObject()->setProperty("action",exporting?"exportFile":"importFile");a.getDynamicObject()->setProperty("path",fc.getResult().getFullPathName());a.getDynamicObject()->setProperty("overwrite",exporting);safe->perform(safe->service->catalogCommand(a));});
    }
    void timerCallback() override {if(!editing&&(shownRevision!=service->document().revision()||shownDocument!=service->document().documentInstance()))reload();}
    std::shared_ptr<CadCommandService> service;
    juce::Label title,summary,status;juce::ComboBox scope,kind,part;juce::TextEditor search;juce::ListBox list;juce::PropertyPanel properties;
    juce::TextButton newButton{"New"},edit{"Edit"},duplicate{"Duplicate"},remove{"Delete"},save{"Save"},cancel{"Cancel edit"},importButton{"Import into document"},assign{"Assign to selected part"},clearMaterial{"Clear material"},makeDefault{"Use as default stock"},clearDefault{"Clear default stock"},undo{"Undo"},redo{"Redo"},importFile{"Import catalog..."},exportFile{"Export catalog..."},refresh{"Reload"};
    juce::Array<juce::var> rows;juce::String selected,partsSelection,operation,draftId;juce::StringArray partIds;std::map<juce::String,juce::String> draft;
    std::unique_ptr<juce::FileChooser> chooser;bool editing=false,reloading=false;int draftRevision=0;std::uint64_t draftDocument=0,shownRevision=0,shownDocument=0;
};
}
juce::DialogWindow* showCadMaterialsManager(std::shared_ptr<djehuti::cad::CadCommandService> service,const juce::String& selected)
{
    juce::DialogWindow::LaunchOptions options;options.dialogTitle="Materials and Sheet Stock";options.dialogBackgroundColour=juce::Colour(0xff111a22);
    options.content.setOwned(new Manager(std::move(service),selected));options.useNativeTitleBar=true;options.resizable=true;options.escapeKeyTriggersCloseButton=true;
    auto* window=options.launchAsync();window->setResizeLimits(940,640,1800,1400);return window;
}

bool runCadStockUiCheck(juce::var& report, const juce::File& folder)
{
    folder.createDirectory();
    auto service = std::make_shared<djehuti::cad::CadCommandService>(); service->setCatalogFile(folder.getChildFile("catalog.json"));
    juce::String box, error; service->createBox(20, 30, 5, box, error);
    auto* dialog = showCadMaterialsManager(service, box);
    auto* manager = dynamic_cast<Manager*>(dialog->getContentComponent());
    const bool passed = manager && manager->smokeCheck(folder, report);
    dialog->exitModalState(0);
    return passed;
}
