#pragma once
#include <JuceHeader.h>
#include <cstdlib>
#include <cmath>

class CadSketchSymmetryControls final : public juce::Component
{
public:
    std::function<void(const juce::var&)> operate;
    std::function<void()> pickCentre;
    CadSketchSymmetryControls()
    {
        title.setText("Symmetry",juce::dontSendNotification);addAndMakeVisible(title);
        mode.addItemList({"Off","Horizontal","Vertical","Both"},1);mode.setTooltip("Live linked symmetry for newly drawn primitives");addAndMakeVisible(mode);
        mode.onChange=[this]{configure();};
        x.setInputRestrictions(32,"0123456789.eE+-");y.setInputRestrictions(32,"0123456789.eE+-");x.setTooltip("Center X (sketch local horizontal, mm)");y.setTooltip("Center Y (sketch local vertical, mm)");addAndMakeVisible(x);addAndMakeVisible(y);
        x.onReturnKey=y.onReturnKey=[this]{configure();};
        applyCentre.onClick=[this]{configure();};pick.onClick=[this]{if(pickCentre)pickCentre();};reset.onClick=[this]{x.setText("0");y.setText("0");configure();};
        for(auto* b:{&applyCentre,&pick,&reset,&mirror,&construction,&update,&remove})addAndMakeVisible(*b);
        axis.addItemList({"Horizontal axis","Vertical axis","Both axes"},1);axis.setSelectedId(3,juce::dontSendNotification);addAndMakeVisible(axis);
        linked.setToggleState(true,juce::dontSendNotification);addAndMakeVisible(linked);
        mirror.onClick=[this]{auto v=parameters("mirror");juce::Array<juce::var> ids;for(const auto& t:entities)if(t->getToggleState())ids.add(t->getName());v.getDynamicObject()->setProperty("entities",ids);v.getDynamicObject()->setProperty("linked",linked.getToggleState());emit(v);};
        construction.setTooltip("Toggle the selected independent line as construction geometry (excluded from solid profiles)");construction.onClick=[this]{if(selected.isEmpty())return;auto* v=new juce::DynamicObject();v->setProperty("operation","construction");v->setProperty("entity",selected);v->setProperty("value",!selectedConstruction);emit(v);};
        addAndMakeVisible(relationships);relationships.onChange=[this]{const auto id=relationships.getText();if(const auto* list=state["mirrors"].getArray())for(const auto& m:*list)if(m["id"].toString()==id){x.setText(juce::String((double)m["x"],12));y.setText(juce::String((double)m["y"],12));int a=juce::StringArray{"horizontal","vertical","both"}.indexOf(m["axis"].toString())+1;if(a==0)for(int i=4;i<=axis.getNumItems();++i)if(axis.getItemText(i-1).endsWith("["+m["axisEntity"].toString()+"]"))a=i;axis.setSelectedId(a,juce::dontSendNotification);}};
        update.onClick=[this]{auto v=parameters("update");v.getDynamicObject()->setProperty("id",relationships.getText());emit(v);};
        remove.onClick=[this]{auto* v=new juce::DynamicObject();v->setProperty("operation","delete");v->setProperty("id",relationships.getText());emit(v);};
        scroll.setViewedComponent(&sourceList,false);scroll.setScrollBarsShown(true,false);addAndMakeVisible(scroll);setSize(260,465);
    }
    void setModel(const juce::var& next,const juce::var& sourceEntities,const juce::String& first,const juce::String& second)
    {
        const auto key=juce::JSON::toString(next)+juce::JSON::toString(sourceEntities)+first+second;if(key==last)return;last=key;state=next;selected=first;
        mode.setSelectedId(juce::StringArray{"off","horizontal","vertical","both"}.indexOf(state["mode"].toString())+1,juce::dontSendNotification);x.setText(juce::String((double)state["x"],12));y.setText(juce::String((double)state["y"],12));
        juce::StringArray checked;for(const auto& b:entities)if(b->getToggleState())checked.add(b->getName());entities.clear();axis.clear(juce::dontSendNotification);axis.addItemList({"Horizontal axis","Vertical axis","Both axes"},1);axis.setSelectedId(3,juce::dontSendNotification);selectedConstruction=false;
        if(const auto* list=sourceEntities.getArray())for(const auto& e:*list)if(e["mirrorOwner"].toString().isEmpty())
        {const auto id=e["id"].toString();if(id==first)selectedConstruction=(bool)e["construction"];if(e["type"].toString()=="line"&&(bool)e["construction"])axis.addItem("Construction ["+id+"]",axis.getNumItems()+1);auto b=std::make_unique<juce::ToggleButton>(e["type"].toString()+" ["+id+"]");b->setName(id);b->setToggleState(checked.contains(id)||id==first||id==second,juce::dontSendNotification);sourceList.addAndMakeVisible(*b);entities.push_back(std::move(b));}
        relationships.clear(juce::dontSendNotification);if(auto* list=state["mirrors"].getArray())for(const auto& m:*list)relationships.addItem(m["id"].toString(),relationships.getNumItems()+1);relationships.setTextWhenNothingSelected("Edit linked relationship...");update.setEnabled(relationships.getNumItems()>0);remove.setEnabled(relationships.getNumItems()>0);resized();
    }
    void resized() override
    {auto a=getLocalBounds().reduced(6);title.setBounds(a.removeFromTop(22));mode.setBounds(a.removeFromTop(26));a.removeFromTop(4);auto row=a.removeFromTop(25);x.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(1));y.setBounds(row.reduced(1));row=a.removeFromTop(26);applyCentre.setBounds(row.removeFromLeft(88));pick.setBounds(row.removeFromLeft(70));reset.setBounds(row);construction.setBounds(a.removeFromTop(26));a.removeFromTop(6);axis.setBounds(a.removeFromTop(25));linked.setBounds(a.removeFromTop(24));scroll.setBounds(a.removeFromTop(100));sourceList.setSize(scroll.getWidth()-14,(int)entities.size()*24);int pos=0;for(auto& b:entities){b->setBounds(0,pos,sourceList.getWidth(),24);pos+=24;}mirror.setBounds(a.removeFromTop(28));a.removeFromTop(8);relationships.setBounds(a.removeFromTop(25));row=a.removeFromTop(27);update.setBounds(row.removeFromLeft(row.getWidth()/2));remove.setBounds(row);}
    void paint(juce::Graphics& g) override{g.fillAll(juce::Colour(0xff1b2933));}
private:
    juce::var parameters(const juce::String& op)
    {auto* v=new juce::DynamicObject();v->setProperty("operation",op);const int a=axis.getSelectedId();v->setProperty("axis",a<=3?juce::StringArray{"horizontal","vertical","both"}[juce::jmax(0,a-1)]:juce::String("line"));if(a>3)v->setProperty("axisEntity",axis.getText().fromLastOccurrenceOf("[",false,false).upToLastOccurrenceOf("]",false,false));v->setProperty("x",x.getText().trim().isNotEmpty()?juce::var(x.getText().getDoubleValue()):juce::var());v->setProperty("y",y.getText().trim().isNotEmpty()?juce::var(y.getText().getDoubleValue()):juce::var());return v;}
    void configure(){auto v=parameters("configure");v.getDynamicObject()->setProperty("mode",juce::StringArray{"off","horizontal","vertical","both"}[juce::jmax(0,mode.getSelectedId()-1)]);emit(v);}
    void emit(const juce::var& v){const auto valid=[](const juce::String& text){const auto t=text.trim();if(t.isEmpty())return false;char* end=nullptr;const auto bytes=t.toStdString();const auto d=std::strtod(bytes.c_str(),&end);return end!=bytes.c_str()&&*end=='\0'&&std::isfinite(d);};if(v.hasProperty("x")&&(!valid(x.getText())||!valid(y.getText()))){x.setColour(juce::TextEditor::outlineColourId,juce::Colours::red);y.setColour(juce::TextEditor::outlineColourId,juce::Colours::red);return;}x.removeColour(juce::TextEditor::outlineColourId);y.removeColour(juce::TextEditor::outlineColourId);if(operate)operate(v);}
    juce::Label title;juce::ComboBox mode,axis,relationships;juce::TextEditor x,y;juce::ToggleButton linked{"Linked mirror"};juce::TextButton applyCentre{"Set centre"},pick{"Pick"},reset{"Origin"},mirror{"Mirror checked entities"},construction{"Toggle selected construction line"},update{"Update link"},remove{"Delete link"};juce::Viewport scroll;juce::Component sourceList;
    std::vector<std::unique_ptr<juce::ToggleButton>> entities;juce::var state;juce::String last,selected;bool selectedConstruction=false;
};
