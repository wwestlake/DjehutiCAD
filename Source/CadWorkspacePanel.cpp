#include "CadWorkspacePanel.h"

class CadWorkspacePanel::FeatureItem final : public juce::TreeViewItem
{
public:
    FeatureItem(CadWorkspacePanel& owner, const djehuti::cad::CadFeature& feature) : panel(owner), id(feature.id), label(feature.name + "  [" + feature.type + "]") {}
    bool mightContainSubItems() override { return false; }
    void paintItem(juce::Graphics& g, int w, int h) override { g.setColour(isSelected() ? juce::Colour(0xff78dcca) : juce::Colour(0xffdce9ee)); g.drawText(label, 4, 0, w - 8, h, juce::Justification::centredLeft, true); }
    void itemSelectionChanged(bool selected) override { if (selected) panel.selectFeature(id); }
private:
    CadWorkspacePanel& panel;
    juce::String id, label;
};

class CadRootItem final : public juce::TreeViewItem
{
public:
    bool mightContainSubItems() override { return true; }
};

CadWorkspacePanel::CadWorkspacePanel(std::shared_ptr<djehuti::cad::CadCommandService> s) : service(std::move(s))
{
    title.setText("CAD Workspace", juce::dontSendNotification); title.setFont(juce::Font(22.0f, juce::Font::bold)); units.setText("Units: mm | Right-handed XYZ coordinates", juce::dontSendNotification); status.setText("Ready", juce::dontSendNotification);
    addAndMakeVisible(tree); addAndMakeVisible(name); addAndMakeVisible(width); addAndMakeVisible(depth); addAndMakeVisible(height); addAndMakeVisible(radius); addAndMakeVisible(x); addAndMakeVisible(y); addAndMakeVisible(z); addAndMakeVisible(firstOperand); addAndMakeVisible(secondOperand); addAndMakeVisible(title); addAndMakeVisible(units); addAndMakeVisible(status); addAndMakeVisible(boxButton); addAndMakeVisible(cylinderButton); addAndMakeVisible(subtractButton); addAndMakeVisible(unionButton); addAndMakeVisible(intersectButton); addAndMakeVisible(applyButton); addAndMakeVisible(undoButton); addAndMakeVisible(redoButton); addAndMakeVisible(hideButton); addAndMakeVisible(deleteButton); addAndMakeVisible(useFirstButton); addAndMakeVisible(useSecondButton);
    tree.setRootItemVisible(false); tree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff10151a));
    auto set = [](juce::TextEditor& e, const char* value) { e.setText(value, false); e.setInputRestrictions(24, "0123456789.-"); e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1c252c)); e.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee)); };
    set(width,"10"); set(depth,"10"); set(height,"10"); set(radius,"5"); set(x,"0"); set(y,"0"); set(z,"0");
    firstOperand.setTextToShowWhenEmpty("First operand id", juce::Colour(0xff8fa4ad));
    secondOperand.setTextToShowWhenEmpty("Second operand id", juce::Colour(0xff8fa4ad));
    for (auto* e : { &firstOperand, &secondOperand }) { e->setReadOnly(true); e->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1c252c)); e->setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee)); }
    boxButton.onClick=[this]{juce::String id,e; if(!service->createBox(width.getText().getDoubleValue(),depth.getText().getDoubleValue(),height.getText().getDoubleValue(),id,e))showError(e);else{refreshTree();selectFeature(id);}};
    cylinderButton.onClick=[this]{juce::String id,e; if(!service->createCylinder(radius.getText().getDoubleValue(),height.getText().getDoubleValue(),id,e))showError(e);else{refreshTree();selectFeature(id);}};
    useFirstButton.onClick=[this]{if(!selectedId.isEmpty())firstOperand.setText(selectedId,false);};
    useSecondButton.onClick=[this]{if(!selectedId.isEmpty())secondOperand.setText(selectedId,false);};
    applyButton.onClick=[this]{applyProperties();};
    hideButton.onClick=[this]{if(!selectedId.isEmpty()){juce::String e; if(!service->setVisible(selectedId,!service->document().find(selectedId)->visible,e))showError(e); refreshTree();}};
    deleteButton.onClick=[this]{if(!selectedId.isEmpty()){juce::String e; if(!service->remove(selectedId,e))showError(e);else{selectedId.clear();refreshTree();}}};
    auto booleanClick=[this](const juce::String& op){juce::String a,b,e,id; if(!selectedPair(a,b)){showError("Choose both Boolean operands with the operand buttons.");return;} if(!service->booleanOp(op,a,b,id,e))showError(e);else{refreshTree();firstOperand.clear();secondOperand.clear();selectFeature(id);}};
    subtractButton.onClick=[this,booleanClick]{booleanClick("subtract");}; unionButton.onClick=[this,booleanClick]{booleanClick("union");}; intersectButton.onClick=[this,booleanClick]{booleanClick("intersect");};
    undoButton.onClick=[this]{juce::String e;if(!service->undo(e))showError(e);else refreshTree();}; redoButton.onClick=[this]{juce::String e;if(!service->redo(e))showError(e);else refreshTree();}; refreshTree();
}
void CadWorkspacePanel::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff11161b));g.setColour(juce::Colour(0xff8fa4ad));g.drawText("Model tree",24,62,250,22,juce::Justification::left);g.drawText("Properties",300,62,250,22,juce::Justification::left);}
void CadWorkspacePanel::resized(){auto a=getLocalBounds().reduced(24);title.setBounds(a.removeFromTop(30));units.setBounds(a.removeFromTop(24));auto toolbar=a.removeFromTop(34);boxButton.setBounds(toolbar.removeFromLeft(58));cylinderButton.setBounds(toolbar.removeFromLeft(78));subtractButton.setBounds(toolbar.removeFromLeft(86));unionButton.setBounds(toolbar.removeFromLeft(65));intersectButton.setBounds(toolbar.removeFromLeft(82));undoButton.setBounds(toolbar.removeFromRight(65));redoButton.setBounds(toolbar.removeFromRight(65));auto left=a.removeFromLeft(260).withTrimmedTop(32);tree.setBounds(left);auto p=a.withTrimmedTop(32);auto row=[&](juce::TextEditor& e){e.setBounds(p.removeFromTop(28));p.removeFromTop(8);};row(name);row(width);row(depth);row(height);row(radius);row(x);row(y);row(z);firstOperand.setBounds(p.removeFromTop(28));p.removeFromTop(6);secondOperand.setBounds(p.removeFromTop(28));p.removeFromTop(6);useFirstButton.setBounds(p.removeFromTop(28));p.removeFromTop(5);useSecondButton.setBounds(p.removeFromTop(28));p.removeFromTop(6);applyButton.setBounds(p.removeFromTop(32));hideButton.setBounds(p.removeFromTop(32));deleteButton.setBounds(p.removeFromTop(32));status.setBounds(p.removeFromTop(36));}
void CadWorkspacePanel::refreshTree(){tree.setRootItem(nullptr);auto* root=new CadRootItem();for(const auto& f:service->document().features())root->addSubItem(new FeatureItem(*this,f));tree.setRootItem(root);root->setOpen(true);}
void CadWorkspacePanel::selectFeature(const juce::String& id){selectedId=id;const auto* f=service->document().find(id);if(!f)return;name.setText(f->name,false);width.setText(juce::String(f->width),false);depth.setText(juce::String(f->depth),false);height.setText(juce::String(f->height),false);radius.setText(juce::String(f->radius),false);x.setText(juce::String(f->x),false);y.setText(juce::String(f->y),false);z.setText(juce::String(f->z),false);}
void CadWorkspacePanel::applyProperties(){if(selectedId.isEmpty())return;juce::String e;if(!service->rename(selectedId,name.getText(),e)||!service->setDimensions(selectedId,width.getText().getDoubleValue(),depth.getText().getDoubleValue(),height.getText().getDoubleValue(),radius.getText().getDoubleValue(),e)||!service->setPlacement(selectedId,x.getText().getDoubleValue(),y.getText().getDoubleValue(),z.getText().getDoubleValue(),0,0,0,e))showError(e);else refreshTree();}
bool CadWorkspacePanel::selectedPair(juce::String& first,juce::String& second) const{first=firstOperand.getText().trim();second=secondOperand.getText().trim();return !first.isEmpty()&&!second.isEmpty()&&first!=second&&service->document().find(first)!=nullptr&&service->document().find(second)!=nullptr;}
void CadWorkspacePanel::showError(const juce::String& error){status.setText(error,juce::dontSendNotification);}
void CadWorkspacePanel::newDocument(){service->newDocument();selectedId.clear();refreshTree();status.setText("New CAD document",juce::dontSendNotification);}
void CadWorkspacePanel::openDocument(){fileChooser=std::make_unique<juce::FileChooser>("Open CAD document",juce::File(),"*.djc");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& chooser){if(auto file=chooser.getResult();file.existsAsFile()){juce::String e;if(!service->load(file,e))showError(e);else refreshTree();}fileChooser.reset();});}
void CadWorkspacePanel::saveDocument(){auto file=service->document().file();if(file==juce::File())saveDocumentAs();else{juce::String e;if(!service->save(file,e))showError(e);else service->document().markSaved();}}
void CadWorkspacePanel::saveDocumentAs(){fileChooser=std::make_unique<juce::FileChooser>("Save CAD document",juce::File(),"*.djc");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& chooser){auto file=chooser.getResult().withFileExtension("djc");if(file!=juce::File()){juce::String e;if(!service->save(file,e))showError(e);else{service->document().setFile(file);service->document().markSaved();}}fileChooser.reset();});}
