#include "CadFixturesPanel.h"
#include "Viewport3D.h"
#include <cstdlib>
#include <cmath>

namespace
{
using namespace djehuti::cad;
juce::var obj(){return new juce::DynamicObject();}
void put(juce::var& v,const juce::Identifier& k,const juce::var& value){v.getDynamicObject()->setProperty(k,value);}
class FixtureField final : public juce::TextPropertyComponent
{
public:
    FixtureField(const juce::String& name,juce::String& value):TextPropertyComponent(name,4096,false),text(value){}
    void setText(const juce::String& value)override{text=value;}
    juce::String getText()const override{return text;}
private:juce::String& text;
};
class FixtureEditor final : public juce::Component,private juce::Timer
{
public:
    FixtureEditor(std::shared_ptr<CadFixtureService> s,std::shared_ptr<CadCommandService> c,std::shared_ptr<CadTestFixture> f):service(std::move(s)),cad(std::move(c)),fixture(std::move(f)),view(0)
    {
        for(auto* component:std::vector<juce::Component*>{&title,&rename,&save,&saveAs,&undo,&redo,&validate,&method,&run,&status,&source,&addSource,&part,&addPart,&addAssembly,&acceptSource,&relocate,&navigate,&models,&equipment,&kind,&newEquipment,&saveEquipment,&removeEquipment,&applyPlacement,&substitution,&applyMaterial,&properties,&view,&results,&removeModel,&removeSource,&applyGravity,&applyBeam,&pickMode,&dragPlane,&fit})addAndMakeVisible(*component);
        title.setText(fixture->name());title.setTooltip("Independent fixture document name");rename.onClick=[this]{auto a=obj();put(a,"name",title.getText());call("rename",a);};
        save.onClick=[this]{saveFile(false);};saveAs.onClick=[this]{saveFile(true);};undo.onClick=[this]{call("undo");};redo.onClick=[this]{call("redo");};validate.onClick=[this]{refresh(true);};
        const auto methods=CadTestFixture::methods();for(const auto& m:*methods.getArray())method.addItem(m["id"].toString()+((bool)m["available"]?"":" (unavailable)"),method.getNumItems()+1);
        method.onChange=[this]{auto d=obj();put(d,"method",juce::StringArray{"geometricProperties","massProperties","kinematics","linearStatic","modal","thermal","dynamics"}[method.getSelectedId()-1]);auto a=obj();put(a,"definition",d);call("settings",a);};run.onClick=[this]{call("run");};
        addSource.onClick=[this]{chooseSource(false);};relocate.onClick=[this]{chooseSource(true);};source.onChange=[this]{sourceChanged();};
        addPart.onClick=[this]{addModel(false);};addAssembly.onClick=[this]{addModel(true);};
        removeModel.onClick=[this]{auto a=obj();put(a,"item",selectedModel());call("removeModel",a);};removeSource.onClick=[this]{auto a=obj();put(a,"item",selectedSource());call("removeSource",a);};
        acceptSource.onClick=[this]{auto a=obj();put(a,"source",selectedSource());call("acceptSource",a);};
        navigate.onClick=[this]{if(onOpenSource&&source.getSelectedId()>0){const auto& s=fixture->configuration()["sources"][source.getSelectedId()-1];onOpenSource(juce::File(s["path"].toString()));}};
        models.onChange=[this]{fillModel();};equipment.onChange=[this]{fillEquipment();};
        kind.addItemList({"fixed","support","force","moment","pressure","distributedForce","point","displacement","reaction","stress","strain","safetyFactor","jointLock"},1);kind.setSelectedId(1);
        kind.onChange=[this]{if(kind.getText()=="moment")draft["unit"]="N*m";else if(kind.getText()=="pressure")draft["unit"]="Pa";else if(kind.getText()=="force"||kind.getText()=="distributedForce")draft["unit"]="N";buildProperties();};
        newEquipment.onClick=[this]{equipment.setSelectedId(0,juce::dontSendNotification);for(const auto* key:{"equipmentId","equipmentName","face","joint","anchor"})draft[key].clear();selection->clear();buildProperties();};
        saveEquipment.onClick=[this]{storeEquipment();};removeEquipment.onClick=[this]{if(equipment.getSelectedId()>0){auto a=obj();put(a,"item",fixture->configuration()["equipment"][equipment.getSelectedId()-1]["id"]);call("removeEquipment",a);}};
        applyPlacement.onClick=[this]{auto a=obj(),d=obj(),p=obj();juce::String error;for(const auto* k:{"x","y","z","rx","ry","rz"}){double value=0;if(!number(k,value,error)){show(error);return;}put(p,k,value);}put(d,"placement",p);put(d,"name",draft["modelName"]);put(a,"model",selectedModel());put(a,"definition",d);call("updateModel",a);};
        applyMaterial.onClick=[this]{auto a=obj();put(a,"model",selectedModel());if(substitution.getSelectedId()>1)put(a,"material",catalogMaterials[substitution.getSelectedId()-2]);call("substituteMaterial",a);};
        applyGravity.onClick=[this]{double x=0,y=0,z=0;juce::String error;if(!number("gx",x,error)||!number("gy",y,error)||!number("gz",z,error)){show(error);return;}auto a=obj(),d=obj();put(d,"gravity",juce::Array<juce::var>{x,y,z});put(a,"definition",d);call("settings",a);};
        applyBeam.onClick=[this]{double scale=0;juce::String error;if(!number("visualScale",scale,error)){show(error);return;}auto a=obj(),d=obj();auto options=juce::JSON::parse(juce::JSON::toString(fixture->configuration()["settings"]["options"]));put(options,"beamAxis",draft["beamAxis"].trim().toUpperCase());put(options,"bendingAxis",draft["bendingAxis"].trim().toUpperCase());put(options,"visualizationScale",scale);put(d,"options",options);put(d,"method","linearStatic");put(a,"definition",d);call("settings",a);};
        fit.onClick=[this]{view.fitAll();};pickMode.addItemList({"Pick faces","Pick bodies","Pick equipment"},1);pickMode.setSelectedId(1,juce::dontSendNotification);
        pickMode.onChange=[this]{selection->setMode(pickMode.getSelectedId()==2?CadSelection::Mode::body:CadSelection::Mode::face);view.setPickMarkers(pickMode.getSelectedId()==3?fixture->equipmentMarkers():std::vector<std::pair<juce::String,std::array<float,3>>>{});};
        dragPlane.addItemList({"Orbit / pick","Move in world XY","Move in world XZ","Move in world YZ"},1);dragPlane.setSelectedId(1,juce::dontSendNotification);dragPlane.onChange=[this]{updateDragHandle();};
        view.setPoseSource([this]{return dragPreview;});
        view.onTargetDragged=[this](const juce::String& target,const std::array<float,3>& point,bool finished)
        {
            if(!dragReference||!dragReference->dragHandle.active)return;
            dragging=!finished;auto pose=std::make_shared<CadPoseSnapshot>(*dragReference);pose->serial=++frameSerial;
            const auto& config=fixture->configuration();const juce::var* model=nullptr;for(const auto& m:*config["models"].getArray())if(m["id"].toString()==target)model=&m;if(!model)return;
            auto placement=juce::JSON::parse(juce::JSON::toString((*model)["placement"]));std::array<float,3> delta{};
            for(int i=0;i<3;++i){delta[i]=point[i]-dragReference->dragHandle.point[i];const auto key=juce::StringArray{"x","y","z"}[i];put(placement,juce::Identifier(key),(double)placement[juce::Identifier(key)]+delta[i]);draft[key]=placement[juce::Identifier(key)].toString();}
            for(auto& line:pose->overlay)for(int i=0;i<3;++i){line.a[i]+=delta[i];line.b[i]+=delta[i];}pose->dragHandle.point=point;
            for(const auto& mesh:dragGeometry->meshes)if(mesh.featureId.startsWith(target+"|")&&pose->instancedFeatures.insert(mesh.featureId).second)
            {CadPoseInstance instance;instance.instanceId=instance.featureId=mesh.featureId;instance.matrix={1,0,0,0,0,1,0,0,0,0,1,0,delta[0],delta[1],delta[2],1};pose->instances.push_back(instance);}
            dragPreview=pose;properties.refreshAll();
            if(finished){auto a=obj(),d=obj();put(d,"placement",placement);put(a,"model",target);put(a,"definition",d);const auto r=call("updateModel",a);if(!(bool)r["ok"])updateDragHandle();}
        };
        view.onMarkerPicked=[this](const juce::String& id){for(int i=0;i<fixture->configuration()["equipment"].size();++i)if(fixture->configuration()["equipment"][i]["id"].toString()==id)equipment.setSelectedId(i+1,juce::sendNotificationSync);};
        selection=std::make_shared<CadSelection>();selection->setMode(CadSelection::Mode::face);view.setSelection(selection);
        selection->addListener([this]
        {
            const auto picked=selection->selected();if(!picked.valid())return;const auto target=picked.drawId.upToFirstOccurrenceOf("|",false,false),component=picked.drawId.fromFirstOccurrenceOf("|",false,false);
            const auto& config=fixture->configuration();for(int i=0;i<config["models"].size();++i)if(config["models"][i]["id"].toString()==target)models.setSelectedId(i+1,juce::sendNotificationSync);
            draft["component"]=component;draft["face"]=fixture->faceToken(picked.drawId,picked.face);draft["anchor"]="faceCentre";
            const auto point=fixture->facePoint(picked.drawId,picked.face);if(point.isArray())for(int i=0;i<3;++i)draft[juce::StringArray{"px","py","pz"}[i]]=point[i].toString();buildProperties();
            view.setHighlights({{picked.drawId,juce::Colour(0xff55ddbb)}});
        });
        results.setMultiLine(true);results.setReadOnly(true);results.setScrollbarsShown(true);results.setFont(juce::Font(12.0f));
        results.setText("Pick an occurrence and end faces to attach equipment. For a rectangular cantilever, define local beam/bending axes, apply settings, then Run. Gravity is not included in this beam backend.");
        status.setJustificationType(juce::Justification::centredLeft);view.setTitle("Reference geometry | supports: green, loads: amber, probes: blue | pick a face");
        draft["magnitude"]="100";draft["unit"]="N";draft["dx"]="0";draft["dy"]="0";draft["dz"]="-1";draft["px"]="0";draft["py"]="0";draft["pz"]="0";draft["fixed"]="ux,uy,uz";draft["coordinate"]="0";
        refresh(true);startTimerHz(15);
    }
    ~FixtureEditor()override{stopTimer();properties.clear();}
    std::function<void(const juce::File&)> onOpenSource;
    std::shared_ptr<CadTestFixture> document()const{return fixture;}
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff131e27));}
    void resized()override
    {
        auto a=getLocalBounds().reduced(8);auto row=a.removeFromTop(34);title.setBounds(row.removeFromLeft(210).reduced(2));rename.setBounds(row.removeFromLeft(70).reduced(2));save.setBounds(row.removeFromLeft(65).reduced(2));saveAs.setBounds(row.removeFromLeft(80).reduced(2));undo.setBounds(row.removeFromLeft(60).reduced(2));redo.setBounds(row.removeFromLeft(60).reduced(2));validate.setBounds(row.removeFromLeft(80).reduced(2));method.setBounds(row.removeFromLeft(210).reduced(2));run.setBounds(row.removeFromLeft(65).reduced(2));
        row=a.removeFromTop(34);addSource.setBounds(row.removeFromLeft(132).reduced(2));source.setBounds(row.removeFromLeft(230).reduced(2));part.setBounds(row.removeFromLeft(220).reduced(2));addPart.setBounds(row.removeFromLeft(110).reduced(2));addAssembly.setBounds(row.removeFromLeft(130).reduced(2));
        row=a.removeFromTop(32);acceptSource.setBounds(row.removeFromLeft(175).reduced(2));relocate.setBounds(row.removeFromLeft(130).reduced(2));navigate.setBounds(row.removeFromLeft(130).reduced(2));removeSource.setBounds(row.removeFromLeft(125).reduced(2));removeModel.setBounds(row.removeFromLeft(130).reduced(2));status.setBounds(a.removeFromTop(27).reduced(4,0));
        row=a.removeFromTop(30);pickMode.setBounds(row.removeFromLeft(145).reduced(2));dragPlane.setBounds(row.removeFromLeft(180).reduced(2));fit.setBounds(row.removeFromLeft(70).reduced(2));applyBeam.setBounds(row.removeFromRight(350).reduced(2));
        results.setBounds(a.removeFromBottom(155).reduced(2));auto form=a.removeFromRight(350);models.setBounds(form.removeFromTop(32).reduced(2));applyPlacement.setBounds(form.removeFromBottom(30).reduced(2));
        auto material=form.removeFromBottom(32);applyMaterial.setBounds(material.removeFromRight(100).reduced(2));substitution.setBounds(material.reduced(2));
        auto buttons=form.removeFromBottom(32);newEquipment.setBounds(buttons.removeFromLeft(85).reduced(2));saveEquipment.setBounds(buttons.removeFromLeft(145).reduced(2));removeEquipment.setBounds(buttons.reduced(2));
        equipment.setBounds(form.removeFromTop(32).reduced(2));kind.setBounds(form.removeFromTop(30).reduced(2));applyGravity.setBounds(form.removeFromBottom(28).reduced(2));properties.setBounds(form.reduced(2));view.setBounds(a.reduced(2));
    }
    bool smoke(const juce::File& folder,juce::var& report)
    {
        juce::Array<juce::var> checks;bool passed=true;const auto check=[&](bool ok,const char* label){auto v=obj();put(v,"check",label);put(v,"passed",ok);checks.add(v);passed&=ok;};
        const auto field=[&](const juce::String& name,const juce::String& value)
        {std::function<FixtureField*(juce::Component&)> find=[&](juce::Component& c)->FixtureField*{if(auto* field=dynamic_cast<FixtureField*>(&c);field&&field->getName()==name)return field;for(auto* child:c.getChildren())if(auto* field=find(*child))return field;return nullptr;};if(auto* control=find(properties)){control->setText(value);control->refresh();return true;}return false;};
        check(models.getNumItems()>0,"Fixture occurrences are selectable in the GUI");models.setSelectedId(1,juce::sendNotificationSync);
        check(field("Occurrence X (mm)","25"),"Occurrence property editor exists");applyPlacement.onClick();check((double)fixture->configuration()["models"][0]["placement"]["x"]==25,"GUI placement edits the fixture occurrence");
        kind.setSelectedId(3,juce::sendNotificationSync);check(field("Load magnitude","125")&&field("Equipment name","UI load"),"Equipment property editors exist");saveEquipment.onClick();check(fixture->configuration()["equipment"].size()==1,"GUI force control uses shared fixture definitions");
        method.setSelectedId(1,juce::sendNotificationSync);run.onClick();check(fixture->inspect()["result"]["status"].toString()=="completed","GUI executes the geometric-property backend");
        // Component wiring checks only. Native mouse/keyboard and OpenGL capture are a separate sign-off.
        std::vector<CadFaceInfo> faces;juce::String error;const auto feature=fixture->configuration()["models"][0]["feature"].toString();cad->document().listFaces(feature,faces,error);int root=-1,tip=-1;
        for(const auto& face:faces){if(std::abs(face.centre.X())<1e-8)root=face.faceIndex;if(std::abs(face.centre.X()-400)<1e-8)tip=face.faceIndex;}
        const auto drawn=selectedModel()+"|";equipment.setSelectedId(1,juce::sendNotificationSync);selection->setSelected({drawn,drawn,tip,-1});field("Load magnitude","100");saveEquipment.onClick();
        newEquipment.onClick();selection->setSelected({drawn,drawn,root,-1});kind.setSelectedId(1,juce::sendNotificationSync);field("Equipment name","Fixed clamp");saveEquipment.onClick();
        newEquipment.onClick();selection->setSelected({drawn,drawn,tip,-1});kind.setSelectedId(8,juce::sendNotificationSync);field("Equipment name","Tip displacement");saveEquipment.onClick();
        method.setSelectedId(4,juce::sendNotificationSync);check(field("Beam axis (local X/Y/Z)","X")&&field("Bending axis (local X/Y/Z)","Y")&&field("Deformation scale (> 0)","5"),"Cantilever settings are editable through the property panel");applyBeam.onClick();run.onClick();
        auto structural=fixture->inspect()["result"];check(structural["status"].toString()=="completed"&&std::abs((double)structural["rows"][0]["tipDisplacementMagnitudeM"]-0.0064)<1e-10,"GUI command path executes configured cantilever analysis");
        check(results.getText().contains("TIP displacement: 6.400000 mm")&&results.getText().contains("120.000000 MPa")&&results.getText().contains("CLAMP reaction")&&results.getText().contains("elastic strain"),"Numerical displacement/reaction/stress/strain appear in the result editor");
        int deformed=0;for(const auto& line:fixture->overlays())deformed+=line.argb==0xffff66dd;check(deformed==40&&results.getText().contains("deformation x5"),"Calculated deformation and its display scale reach the fixture view");
        dragPlane.setSelectedId(2,juce::sendNotificationSync);auto handle=dragReference->dragHandle.point;handle[0]+=10;handle[1]+=20;view.onTargetDragged(selectedModel(),handle,false);
        check((double)fixture->configuration()["models"][0]["placement"]["x"]==25&&draft["x"].getDoubleValue()==35,"Drag preview synchronizes numeric fields without a document transaction");view.onTargetDragged(selectedModel(),handle,true);
        check((double)fixture->configuration()["models"][0]["placement"]["x"]==35&&(double)fixture->configuration()["models"][0]["placement"]["y"]==20,"Handle release commits fixture translation");undo.onClick();
        check((double)fixture->configuration()["models"][0]["placement"]["x"]==25&&(double)fixture->configuration()["models"][0]["placement"]["y"]==0,"A single undo reverses the complete drag");
        check(cad->document().find(feature)->x==0&&cad->document().find(feature)->y==0,"Occurrence drag protects authoritative source placement");dragPlane.setSelectedId(1,juce::sendNotificationSync);run.onClick();
        juce::FileOutputStream image(folder.getChildFile("fixture-workspace.png"));check(image.openedOk()&&juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image),"Fixture workspace component render saved");
        auto v=obj();put(v,"passed",passed);put(v,"checks",checks);put(v,"folder",folder.getFullPathName());put(v,"verification","Actual JUCE controls and component render; OpenGL pixels and Windows input require desktop smoke testing");report=v;return passed;
    }
private:
    juce::String selectedSource()const{const auto index=source.getSelectedId()-1;return index>=0?fixture->configuration()["sources"][index]["id"].toString():juce::String();}
    juce::String selectedModel()const{const auto index=models.getSelectedId()-1;return index>=0?fixture->configuration()["models"][index]["id"].toString():juce::String();}
    void show(const juce::String& text){status.setText(text,juce::dontSendNotification);status.setTooltip(text);}
    juce::var call(const juce::String& action,juce::var a=obj())
    {put(a,"fixture",fixture->id());put(a,"action",action);auto r=service->command(a);if(!(bool)r["ok"])show(r["error"].toString());else{refresh(true);if(action=="upsertEquipment")for(int i=0;i<fixture->configuration()["equipment"].size();++i)if(fixture->configuration()["equipment"][i]["id"]==r["id"])equipment.setSelectedId(i+1,juce::sendNotificationSync);show("Fixture updated; source CAD remains protected.");}return r;}
    void sourceChanged()
    {part.clear(juce::dontSendNotification);const auto choices=fixture->targetChoices();for(const auto& s:*choices.getArray())if(s["id"].toString()==selectedSource())for(const auto& f:*s["features"].getArray())part.addItem(f["name"].toString()+" ["+f["id"].toString()+"]",part.getNumItems()+1);part.setSelectedId(part.getNumItems()>0?1:0,juce::dontSendNotification);}
    void addModel(bool assembly)
    {
        auto a=obj(),d=obj();put(d,"source",selectedSource());put(d,"kind",assembly?"assembly":"part");put(d,"name",assembly?"Assembly":part.getText());
        if(!assembly){const auto choices=fixture->targetChoices();for(const auto& s:*choices.getArray())if(s["id"].toString()==selectedSource()&&part.getSelectedId()>0)put(d,"feature",s["features"][part.getSelectedId()-1]["id"]);}
        put(a,"definition",d);call("addModel",a);
    }
    void chooseSource(bool relinking)
    {
        chooser=std::make_unique<juce::FileChooser>(relinking?"Relocate the same protected source model":"Add a saved CAD model",juce::File(),"*.djo;*.json");const juce::Component::SafePointer<FixtureEditor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,relinking](const juce::FileChooser& fc){if(safe==nullptr||fc.getResult()==juce::File())return;auto a=obj();put(a,"path",fc.getResult().getFullPathName());if(relinking)put(a,"source",safe->selectedSource());safe->call(relinking?"relocateSource":"addSource",a);});
    }
    void saveFile(bool saveNew)
    {
        if(!saveNew&&fixture->file()!=juce::File()){call("save");return;}
        chooser=std::make_unique<juce::FileChooser>("Save independent test fixture",fixture->file(),"*.djfixture");const juce::Component::SafePointer<FixtureEditor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe](const juce::FileChooser& fc){if(safe==nullptr||fc.getResult()==juce::File())return;if(!fc.getResult().hasFileExtension("djfixture")){safe->show("Save with the .djfixture file extension.");return;}auto a=obj();put(a,"path",fc.getResult().getFullPathName());put(a,"overwrite",true);safe->call("save",a);});
    }
    bool number(const juce::String& key,double& value,juce::String& error)
    {const auto raw=draft[key].trim().toStdString();char* end=nullptr;value=std::strtod(raw.c_str(),&end);if(raw.empty()||end!=raw.c_str()+raw.size()||!std::isfinite(value)){error=key+" requires a finite number.";return false;}return true;}
    void updateDragHandle()
    {
        const auto plane=juce::StringArray{"","XY","XZ","YZ"}[juce::jmax(0,dragPlane.getSelectedId()-1)];
        dragGeometry=fixture->snapshot();dragReference=fixture->dragPose(plane.isEmpty()?juce::String():selectedModel(),plane);dragPreview=dragReference;view.setTargetDragEnabled(plane.isNotEmpty());
        view.setTitle(plane.isEmpty()?"Reference solid | green: supports, amber: loads, blue: probes, red: invalid | magenta: calculated deflection":"Drag white origin handle in world "+plane+"; release to commit one placement edit. Rotation: numeric controls.");
    }
    void buildProperties()
    {
        properties.clear();juce::Array<juce::PropertyComponent*> fields;
        const auto add=[&](const char* key,const char* name){fields.add(new FixtureField(name,draft[key]));};
        const auto effective=fixture->materialFor(selectedModel(),draft["component"]);
        if(effective.isObject())draft["materialInfo"]=effective["name"].toString()+((bool)effective["substituted"]?" [SUBSTITUTED]":" [source]");
        auto* info=new FixtureField("Material",draft["materialInfo"]);info->setEnabled(false);fields.add(info);
        for(const auto pair:{std::pair<const char*,const char*>{"density","Density (kg/m^3)"},{"youngsModulus","Young's modulus (MPa)"},{"poissonsRatio","Poisson's ratio"},{"yieldStrength","Yield (MPa)"}})
        {draft[pair.first]=effective["properties"][pair.first]["value"].toString();auto* field=new FixtureField(pair.second,draft[pair.first]);field->setEnabled(false);fields.add(field);}
        properties.addSection("Engineering material",fields,false);fields.clear();
        add("modelName","Occurrence name");add("x","Occurrence X (mm)");add("y","Y (mm)");add("z","Z (mm)");add("rx","Rotate X (deg)");add("ry","Rotate Y (deg)");add("rz","Rotate Z (deg)");
        properties.addSection("Occurrence placement",fields,false);fields.clear();
        add("beamAxis","Beam axis (local X/Y/Z)");add("bendingAxis","Bending axis (local X/Y/Z)");add("visualScale","Deformation scale (> 0)");
        properties.addSection("Rectangular cantilever (no gravity)",fields,fixture->configuration()["settings"]["method"].toString()=="linearStatic");fields.clear();
        add("equipmentName","Equipment name");if(models.getSelectedId()>0&&fixture->configuration()["models"][models.getSelectedId()-1]["kind"].toString()=="assembly")add("component","Assembly instance ID");add("face","Persistent face token");
        if(juce::StringArray{"force","moment","pressure","distributedForce"}.contains(kind.getText())){add("magnitude","Load magnitude");add("unit","Load unit");add("dx","Direction X");add("dy","Direction Y");add("dz","Direction Z");}
        add("px","Point X (source mm)");add("py","Point Y (mm)");add("pz","Point Z (mm)");
        draft["attachmentInfo"]=draft["anchor"]=="faceCentre"?"Face-centre anchor; remaps on accepted revision":"Exact point; must remain on its face";auto* anchor=new FixtureField("Attachment",draft["attachmentInfo"]);anchor->setEnabled(false);fields.add(anchor);
        if(kind.getText()=="support")add("fixed","Fixed DOFs (comma list)");if(kind.getText()=="jointLock"){add("joint","Source joint ID");add("coordinate","Joint lock (rad or m)");}
        properties.addSection("Test equipment",fields,true);fields.clear();add("gx","Gravity X (m/s^2)");add("gy","Gravity Y (m/s^2)");add("gz","Gravity Z (m/s^2)");properties.addSection("Gravity settings",fields,false);
    }
    void fillModel()
    {
        properties.clear();const auto index=models.getSelectedId()-1;if(index<0)return;const auto m=fixture->configuration()["models"][index];
        for(const auto* key:{"x","y","z","rx","ry","rz"})draft[key]=m["placement"][key].toString();
        draft["modelName"]=m["name"].toString();
        for(const auto* key:{"component","face","anchor","equipmentId","equipmentName"})draft[key].clear();draft["materialInfo"]=m.hasProperty("material")?"SUBSTITUTION: "+m["material"]["name"].toString():"Authoritative source assignments";buildProperties();updateDragHandle();
        substitution.setSelectedId(1,juce::dontSendNotification);
    }
    void fillEquipment()
    {
        const auto index=equipment.getSelectedId()-1;if(index<0)return;properties.clear();const auto e=fixture->configuration()["equipment"][index];
        for(int i=0;i<fixture->configuration()["models"].size();++i)if(fixture->configuration()["models"][i]["id"]==e["target"])models.setSelectedId(i+1,juce::sendNotificationSync);
        draft["equipmentId"]=e["id"].toString();draft["equipmentName"]=e["name"].toString();for(const auto* key:{"component","face","magnitude","unit","joint","coordinate","anchor"})draft[key]=e[key].toString();
        for(int i=0;i<3;++i){draft[juce::StringArray{"px","py","pz"}[i]]=e["pointMm"][i].toString();draft[juce::StringArray{"dx","dy","dz"}[i]]=e["direction"][i].toString();}
        juce::StringArray fixed;if(auto* a=e["fixed"].getArray())for(const auto& v:*a)fixed.add(v.toString());draft["fixed"]=fixed.joinIntoString(",");for(int i=0;i<kind.getNumItems();++i)if(kind.getItemText(i)==e["kind"].toString())kind.setSelectedId(i+1,juce::dontSendNotification);buildProperties();
    }
    void storeEquipment()
    {
        auto a=obj(),d=obj();put(d,"id",draft["equipmentId"]);put(d,"name",draft["equipmentName"]);put(d,"kind",kind.getText());put(d,"target",selectedModel());for(const auto* key:{"component","face","joint"})put(d,key,draft[key]);if(draft["anchor"].isNotEmpty())put(d,"anchor",draft["anchor"]);
        juce::String error;double x=0,y=0,z=0;if(!number("px",x,error)||!number("py",y,error)||!number("pz",z,error)){show(error);return;}put(d,"pointMm",juce::Array<juce::var>{x,y,z});if(!number("dx",x,error)||!number("dy",y,error)||!number("dz",z,error)){show(error);return;}put(d,"direction",juce::Array<juce::var>{x,y,z});
        if(juce::StringArray{"force","moment","pressure","distributedForce"}.contains(kind.getText())){if(!number("magnitude",x,error)){show(error);return;}put(d,"magnitude",x);put(d,"unit",draft["unit"]);}
        if(kind.getText()=="support"){juce::Array<juce::var> fixed;for(auto token:juce::StringArray::fromTokens(draft["fixed"],",",{}))fixed.add(token.trim());put(d,"fixed",fixed);}
        if(kind.getText()=="jointLock"){if(!number("coordinate",x,error)){show(error);return;}put(d,"coordinate",x);}
        put(a,"definition",d);call("upsertEquipment",a);
    }
    void refresh(bool force)
    {
        const auto state=fixture->inspect();const auto json=juce::JSON::toString(state["configuration"]);const auto validation=juce::JSON::toString(state["validation"]);
        if(force||json!=shownConfig)
        {
            const auto options=state["configuration"]["settings"]["options"];draft["beamAxis"]=options.getProperty("beamAxis","X").toString();draft["bendingAxis"]=options.getProperty("bendingAxis","Y").toString();draft["visualScale"]=options.getProperty("visualizationScale",10.0).toString();
            shownConfig=json;for(int i=0;i<3;++i)draft[juce::StringArray{"gx","gy","gz"}[i]]=fixture->configuration()["settings"]["gravity"][i].toString();title.setText(fixture->name(),false);const auto previous=selectedModel();const int previousSource=source.getSelectedId();source.clear(juce::dontSendNotification);for(const auto& s:*fixture->configuration()["sources"].getArray())source.addItem(juce::File(s["path"].toString()).getFileName(),source.getNumItems()+1);source.setSelectedId(juce::jlimit(0,source.getNumItems(),previousSource>0?previousSource:1),juce::dontSendNotification);sourceChanged();
            models.clear(juce::dontSendNotification);int chosen=0;for(const auto& m:*fixture->configuration()["models"].getArray()){models.addItem(m["name"].toString().isNotEmpty()?m["name"].toString():m["id"].toString(),models.getNumItems()+1);if(m["id"].toString()==previous)chosen=models.getNumItems();}models.setSelectedId(chosen?chosen:models.getNumItems()>0?1:0,juce::dontSendNotification);
            const auto previousEquipment=equipment.getSelectedId();equipment.clear(juce::dontSendNotification);for(const auto& e:*fixture->configuration()["equipment"].getArray())equipment.addItem(e["kind"].toString()+": "+e["name"].toString(),equipment.getNumItems()+1);equipment.setTextWhenNothingSelected("New equipment / select saved equipment");equipment.setSelectedId(juce::jmin(previousEquipment,equipment.getNumItems()),juce::dontSendNotification);
            method.setSelectedId(juce::StringArray{"geometricProperties","massProperties","kinematics","linearStatic","modal","thermal","dynamics"}.indexOf(fixture->configuration()["settings"]["method"].toString())+1,juce::dontSendNotification);
            substitution.clear(juce::dontSendNotification);substitution.addItem("Source materials",1);catalogMaterials.clear();const auto library=cad->catalogCommand(obj());if(auto* list=library["catalog"]["materials"].getArray())for(const auto& m:*list){catalogMaterials.add(m);substitution.addItem(m["name"].toString(),substitution.getNumItems()+1);}fillModel();if(equipment.getSelectedId()>0)fillEquipment();
        }
        if(force||json!=shownGeometry||validation!=shownValidation)
        {shownGeometry=json;view.setSnapshot(fixture->snapshot());view.setInspectionLines(fixture->overlays());view.setPickMarkers(pickMode.getSelectedId()==3?fixture->equipmentMarkers():std::vector<std::pair<juce::String,std::array<float,3>>>{});updateDragHandle();view.prepareFrame(0,++frameSerial);}
        if(force||validation!=shownValidation){shownValidation=validation;show((bool)state["validation"]["valid"]?"Source references valid. Fixture changes never edit CAD.":juce::JSON::toString(state["validation"]["problems"],true));}
        const auto r=state["result"];if(r.isObject())
        {
            juce::String text="Analysis: "+r["method"].toString()+" | "+r["status"].toString()+((bool)r["stale"]?" | STALE - rerun required":"")+"\n"+r["message"].toString()+"\n"+r["scope"].toString();
            if(!(bool)r["stale"]&&r["status"].toString()=="completed")
            {if(r.hasProperty("totalMassKg"))text+="\nTotal mass: "+r["totalMassKg"].toString()+" kg | centroid (mm): "+juce::JSON::toString(r["centroidMm"],true);if(auto* rows=r["rows"].getArray())for(const auto& row:*rows){text+="\n"+row["name"].toString();if(r["method"].toString()=="kinematics")text+=" : "+row["status"].toString()+"; coordinates (rad/m): "+juce::JSON::toString(row["coordinates"],true)+"; closure "+row["positionResidualMm"].toString()+" mm";else if(r["method"].toString()!="linearStatic")text+=" : volume "+row["volumeMm3"].toString()+" mm^3; centroid "+juce::JSON::toString(row["centroidMm"],true)+(row.hasProperty("massKg")?"; mass "+row["massKg"].toString()+" kg; self-weight "+juce::JSON::toString(row["selfWeightN"],true)+" N":"");}}
            if(!(bool)r["stale"]&&r["status"].toString()=="completed"&&r["method"].toString()=="linearStatic")for(const auto& row:*r["rows"].getArray())
            {
                text+="\nTIP displacement: "+juce::String(1000*(double)row["tipDisplacementMagnitudeM"],6)+" mm | world vector (m): "+juce::JSON::toString(row["tipDisplacementM"],true);
                text+="\nCLAMP reaction (N): "+juce::JSON::toString(row["reactionForceN"],true)+" | moment (N*m): "+juce::JSON::toString(row["reactionMomentNm"],true);
                text+="\nMAX bending stress: "+juce::String((double)row["maximumBendingStressPa"]/1e6,6)+" MPa | elastic strain: "+row["maximumElasticStrain"].toString();
                text+="\nSection A: "+row["section"]["areaM2"].toString()+" m^2 | I: "+row["section"]["secondMomentM4"].toString()+" m^4 | E: "+row["youngsModulusPa"].toString()+" Pa";
                text+="\nMAGENTA calculated neutral-axis deformation x"+row["visualizationScale"].toString()+" (display scale only). Solid is undeformed.";
                if(!(bool)row["elasticLimitChecked"])text+="\nYield strength unspecified: linear elasticity is assumed; yield margin is unverified.";
                text+="\nBending moment distribution [x (mm): M (N*m)]: ";for(const auto& s:*row["stations"].getArray())text+=juce::String(1000*(double)s["xM"],1)+": "+juce::String((double)s["bendingMomentNm"],3)+"; ";
                view.setTitle("Undeformed solid | magenta: calculated deflection x"+row["visualizationScale"].toString()+" | supports green, loads amber, probes blue");
            }
            if(auto* mats=r["materials"].getArray())for(const auto& m:*mats)text+="\nMaterial: "+m["name"].toString()+((bool)m["substituted"]?" [FIXTURE SUBSTITUTION]":" [source assignment]");
            if(auto* requests=r["measurements"].getArray())for(const auto& request:*requests)text+="\n"+request["kind"].toString()+": "+request["status"].toString()+((bool)r["stale"]?"":request.hasProperty("value")?" "+juce::JSON::toString(request["value"],true)+" "+request["unit"].toString():"")+" - "+request["reason"].toString();if(!(bool)state["validation"]["valid"])text+="\nValidation problems: "+juce::JSON::toString(state["validation"]["problems"]);if(results.getText()!=text)results.setText(text,false);
        }
        else if(!(bool)state["validation"]["valid"])results.setText("Model validation: "+juce::JSON::toString(state["validation"]["problems"]),false);
    }
    void timerCallback()override{if(!isShowing())return;if(!dragging&&++ticks%30==0)refresh(false);view.prepareFrame(0,++frameSerial);}
    std::shared_ptr<CadFixtureService> service;std::shared_ptr<CadCommandService> cad;std::shared_ptr<CadTestFixture> fixture;
    Viewport3D view;std::shared_ptr<CadSelection> selection;juce::TextEditor title,results;juce::Label status;
    juce::TextButton rename{"Rename"},save{"Save"},saveAs{"Save as..."},undo{"Undo"},redo{"Redo"},validate{"Validate"},run{"Run"},addSource{"Add CAD source..."},addPart{"Add part"},addAssembly{"Add assembly"},acceptSource{"Accept source revision"},relocate{"Relocate source..."},navigate{"Open CAD source"},newEquipment{"New item"},saveEquipment{"Save equipment"},removeEquipment{"Remove"},applyPlacement{"Apply occurrence placement"},applyMaterial{"Substitute"};
    juce::TextButton removeModel{"Remove occurrence"},removeSource{"Remove source"},applyGravity{"Apply gravity settings"};
    juce::TextButton applyBeam{"Apply cantilever axes / display scale"},fit{"Fit all"};juce::ComboBox pickMode,dragPlane;
    juce::ComboBox source,part,models,equipment,kind,method,substitution;juce::PropertyPanel properties;std::map<juce::String,juce::String> draft;juce::Array<juce::var> catalogMaterials;
    std::unique_ptr<juce::FileChooser> chooser;juce::String shownConfig,shownGeometry,shownValidation;int ticks=0;std::uint64_t frameSerial=0;bool dragging=false;
    std::shared_ptr<const CadPoseSnapshot> dragReference,dragPreview;
    std::shared_ptr<const CadRenderSnapshot> dragGeometry;
};
}
CadFixturesPanel::CadFixturesPanel(std::shared_ptr<djehuti::cad::CadFixtureService> s,std::shared_ptr<djehuti::cad::CadCommandService> c):service(std::move(s)),cad(std::move(c))
{
    for(auto* child:std::vector<juce::Component*>{&workspaceMenu,&tabs,&newButton,&openButton,&duplicate,&close,&remove,&message})addAndMakeVisible(*child);
    newButton.onClick=[this]{createFixture();};openButton.onClick=[this]{openFixture();};duplicate.onClick=[this]{manage("duplicate");};close.onClick=[this]{manage("close");};remove.onClick=[this]{manage("delete");};
    workspaceMenu.sections=[] {return std::vector<WorkspaceMenu::Section>{{"Fixtures",{{1,"New fixture"},{2,"Open fixture..."},{3,"Duplicate fixture"},{4,"Close fixture"},{5,"Delete fixture..."}}}};};workspaceMenu.invoke=[this](int id){if(id==1)createFixture();else if(id==2)openFixture();else manage(id==3?"duplicate":id==4?"close":"delete");};
    message.setText("Independent engineering setups reference saved CAD files. Save CAD changes before adding a source.",juce::dontSendNotification);startTimerHz(2);
}
CadFixturesPanel::~CadFixturesPanel(){stopTimer();tabs.clearTabs();}
void CadFixturesPanel::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff131e27));}
void CadFixturesPanel::resized(){auto a=getLocalBounds().reduced(6);workspaceMenu.setBounds(a.removeFromTop(28));auto row=a.removeFromTop(35);for(auto* b:{&newButton,&openButton,&duplicate,&close,&remove})b->setBounds(row.removeFromLeft(142).reduced(2));message.setBounds(a.removeFromBottom(26));tabs.setBounds(a);}
void CadFixturesPanel::createFixture(){auto a=obj();put(a,"action","create");put(a,"name","Test fixture "+juce::String(service->documents().size()+1));const auto r=service->command(a);synchronize(r["id"].toString());}
void CadFixturesPanel::openFixture()
{
    chooser=std::make_unique<juce::FileChooser>("Open engineering fixture",juce::File(),"*.djfixture");const juce::Component::SafePointer<CadFixturesPanel> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(safe==nullptr||fc.getResult()==juce::File())return;auto a=obj();put(a,"action","open");put(a,"path",fc.getResult().getFullPathName());auto r=safe->service->command(a);if((bool)r["ok"])safe->synchronize(r["id"].toString());else safe->message.setText(r["error"].toString(),juce::dontSendNotification);});
}
void CadFixturesPanel::synchronize(const juce::String& activate)
{
    for(int i=tabs.getNumTabs();--i>=0;)if(!service->find(tabIds[i])){tabs.removeTab(i);tabIds.remove(i);}
    for(const auto& doc:service->documents())
    {int index=tabIds.indexOf(doc->id());if(index<0){auto* editor=new FixtureEditor(service,cad,doc);const juce::Component::SafePointer<CadFixturesPanel> safe(this);editor->onOpenSource=[safe](const juce::File& f){if(safe!=nullptr&&safe->onOpenSource)safe->onOpenSource(f);};tabs.addTab(doc->name(),juce::Colour(0xff243640),editor,true);tabIds.add(doc->id());index=tabIds.size()-1;}tabs.setTabName(index,doc->name()+(doc->modified()?" *":""));if(activate==doc->id())tabs.setCurrentTabIndex(index);}
}
void CadFixturesPanel::manage(const juce::String& action)
{
    const auto index=tabs.getCurrentTabIndex();if(!juce::isPositiveAndBelow(index,tabIds.size()))return;const auto doc=service->find(tabIds[index]);if(!doc)return;
    const auto perform=[safe=juce::Component::SafePointer<CadFixturesPanel>(this),id=doc->id(),action](bool consent){if(safe==nullptr||!consent)return;auto a=obj();put(a,"action",action);put(a,"fixture",id);put(a,"confirm",true);put(a,"discard",true);auto r=safe->service->command(a);if(!(bool)r["ok"])safe->message.setText(r["error"].toString(),juce::dontSendNotification);safe->synchronize(r["id"].toString());};
    if(action=="delete"||(action=="close"&&doc->modified()))juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,action=="delete"?"Delete fixture":"Unsaved fixture",action=="delete"?"Delete this independent fixture and its saved file? Source CAD files remain protected.":"Discard this fixture's unsaved changes and close its tab?","Continue","Cancel",this,juce::ModalCallbackFunction::create([perform](int r){perform(r!=0);}));else perform(true);
}
void CadFixturesPanel::confirmClose(std::function<void()> callback)
{
    bool dirty=false;for(const auto& f:service->documents())dirty|=f->modified();if(!dirty){callback();return;}
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,"Unsaved test fixtures","Close DjehutiCAD and discard unsaved fixture changes? Cancel to save the fixture tabs first.","Discard and close","Cancel",this,juce::ModalCallbackFunction::create([callback](int r){if(r)callback();}));
}
void CadFixturesPanel::timerCallback(){synchronize();}
bool CadFixturesPanel::smokeCheck(const juce::File& folder,juce::var& report){synchronize();auto* editor=dynamic_cast<FixtureEditor*>(tabs.getCurrentContentComponent());return editor&&editor->smoke(folder,report);}
bool runCadFixtureUiCheck(juce::var& report,const juce::File& folder,bool keepOpen)
{
    folder.createDirectory();auto cad=std::make_shared<djehuti::cad::CadCommandService>();cad->setCatalogFile(folder.getChildFile("catalog.json"));auto service=std::make_shared<djehuti::cad::CadFixtureService>();juce::String box,error;cad->createBox(400,20,10,box,error);
    auto material=obj();put(material,"name","Cantilever test steel");put(material,"densityKgPerM3",7850);put(material,"youngsModulusMPa",200000);put(material,"poissonsRatio",0.3);put(material,"yieldStrengthMPa",250);const auto materialId=cad->materialCommand("cad_material_create",material)["id"].toString();material=obj();put(material,"feature",box);put(material,"material",materialId);cad->materialCommand("cad_material_assign",material);cad->save(folder.getChildFile("source.djo"),error);
    auto a=obj();put(a,"action","create");const auto id=service->command(a)["id"].toString();a=obj();put(a,"action","addSource");put(a,"fixture",id);put(a,"path",folder.getChildFile("source.djo").getFullPathName());const auto source=service->command(a)["id"].toString();a=obj();put(a,"action","addModel");put(a,"fixture",id);auto d=obj();put(d,"source",source);put(d,"kind","part");put(d,"feature",box);put(d,"name","Protected bracket");put(a,"definition",d);service->command(a);
    juce::DialogWindow::LaunchOptions options;auto* panel=new CadFixturesPanel(service,cad);panel->setSize(1280,960);options.content.setOwned(panel);options.dialogTitle="Fixture UI check";options.useNativeTitleBar=true;auto* dialog=options.launchAsync();
    if(keepOpen){panel->synchronize(id);report=obj();put(report,"folder",folder.getFullPathName());put(report,"verification","Prepared protected beam source and one occurrence. No programmatic GUI controls invoked; native interaction is pending.");return true;}
    const bool ok=panel->smokeCheck(folder,report);dialog->exitModalState(0);return ok;
}
