#include "CadWorkspacePanel.h"

using djehuti::cad::CadFeature;

class CadWorkspacePanel::FeatureItem final : public juce::TreeViewItem
{
public:
    FeatureItem(CadWorkspacePanel& owner, const CadFeature& feature) : panel(owner), id(feature.id), label(feature.name + "  [" + feature.type + "]") {}
    bool mightContainSubItems() override { return false; }
    void paintItem(juce::Graphics& g, int w, int h) override { g.setColour(isSelected() ? juce::Colour(0xff78dcca) : juce::Colour(0xffdce9ee)); g.drawText(label, 4, 0, w - 8, h, juce::Justification::centredLeft, true); }
    void itemSelectionChanged(bool selected) override { if (selected) panel.selectFeature(id); }
private:
    CadWorkspacePanel& panel; juce::String id, label;
};

class CadRootItem final : public juce::TreeViewItem { public: bool mightContainSubItems() override { return true; } };

class CadWorkspacePanel::SketchCanvas final : public juce::Component
{
public:
    explicit SketchCanvas(CadWorkspacePanel& ownerIn) : owner(ownerIn) { setWantsKeyboardFocus(true); }
    void setSketch(const juce::String& id) { sketchId = id; selectedEntity.clear(); repaint(); }
    void setMode(const juce::String& next) { mode = next; setMouseCursor(mode == "select" ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b1014)); g.setColour(juce::Colour(0xff1a2930));
        for (int x = 0; x < getWidth(); x += 20) g.drawVerticalLine(x, 0.0f, (float)getHeight());
        for (int y = 0; y < getHeight(); y += 20) g.drawHorizontalLine(y, 0.0f, (float)getWidth());
        g.setColour(juce::Colour(0xff5b777d)); g.drawVerticalLine(getWidth() / 2, 0.0f, (float)getHeight()); g.drawHorizontalLine(getHeight() / 2, 0.0f, (float)getWidth());
        const auto* sketch = owner.service->document().findSketch(sketchId); if (sketch == nullptr) return;
        for (const auto& entity : sketch->entities)
        {
            g.setColour(entity.id == selectedEntity ? juce::Colour(0xffffb86b) : juce::Colour(0xff78dcca));
            if (entity.type == "circle") { auto c = toPoint(entity.x1, entity.y1); g.drawEllipse(c.x - (float)(entity.radius * scale), c.y - (float)(entity.radius * scale), (float)(entity.radius * scale * 2.0), (float)(entity.radius * scale * 2.0), 2.0f); }
            else { auto a = toPoint(entity.x1, entity.y1); auto b = toPoint(entity.x2, entity.y2); g.drawLine(a.x, a.y, b.x, b.y, 2.0f); }
        }
        if (gestureActive && !snapTarget.isEmpty()) { g.setColour(juce::Colour(0xffffdf7e)); auto p = toPoint(snapPreview.x, snapPreview.y); g.drawEllipse(p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f, 2.0f); }
        g.setColour(juce::Colour(0xff9fb5bd)); g.drawText("Sketch plane | drag to create | Shift-click second entity", 8, 8, getWidth() - 16, 20, juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus(); gestureActive = true; gestureCancelled = false; start = fromPoint(e.position); snapTarget.clear(); startConnection.clear(); endConnection.clear();
        if (mode == "line") { const auto snap = nearestSnap(start); start = snap.point; startConnection = snap.target; snapPreview = start; snapTarget = snap.target; }
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!gestureActive || mode == "select") return;
        const auto raw = fromPoint(e.position); const auto snap = mode == "line" ? nearestSnap(raw) : Snap { raw, {} };
        snapPreview = snap.point; snapTarget = snap.target; repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!gestureActive) return;
        gestureActive = false;
        if (gestureCancelled) { gestureCancelled = false; snapTarget.clear(); repaint(); return; }
        const auto rawEnd = fromPoint(e.position); const auto endSnap = mode == "line" ? nearestSnap(rawEnd) : Snap { rawEnd, {} }; const auto end = endSnap.point; endConnection = endSnap.target; juce::String error;
        if (mode == "select")
        {
            selectedEntity.clear(); if (const auto* sketch = owner.service->document().findSketch(sketchId)) { double best = 2.5; for (const auto& entity : sketch->entities) { const auto d = entity.type == "circle" ? std::abs(std::hypot(end.x - entity.x1, end.y - entity.y1) - entity.radius) : distanceToSegment(end.x, end.y, entity.x1, entity.y1, entity.x2, entity.y2); if (d < best) { best = d; selectedEntity = entity.id; } } }
            owner.selectSketchEntity(selectedEntity, e.mods.isShiftDown()); repaint(); return;
        }
        juce::String entityId;
        if (mode == "rectangle") owner.service->addSketchRectangle(sketchId, std::min(start.x, end.x), std::min(start.y, end.y), std::abs(end.x - start.x), std::abs(end.y - start.y), entityId, error);
        else if (mode == "circle") owner.service->addSketchCircle(sketchId, start.x, start.y, std::max(0.1, std::hypot(end.x - start.x, end.y - start.y)), entityId, error);
        else if (mode == "line") owner.service->addSketchLine(sketchId, start.x, start.y, end.x, end.y, entityId, error, startConnection, endConnection);
        snapTarget.clear();
        if (!error.isEmpty()) owner.showError(error); else { mode = "select"; repaint(); owner.refreshTree(); }
    }
    bool keyPressed(const juce::KeyPress& key) override { if (key == juce::KeyPress::escapeKey) { cancelGesture(); return true; } return false; }
    void cancelGesture() { if (gestureActive) { gestureCancelled = true; snapTarget.clear(); repaint(); } }
    void finishEditing() { gestureActive = false; gestureCancelled = true; mode = "select"; snapTarget.clear(); repaint(); }
private:
    struct Point { double x = 0, y = 0; }; struct Snap { Point point; juce::String target; }; CadWorkspacePanel& owner; juce::String sketchId, selectedEntity, mode { "select" }, snapTarget, startConnection, endConnection; Point start, snapPreview; bool gestureActive = false, gestureCancelled = false; static constexpr double scale = 4.0;
    juce::Point<float> toPoint(double x, double y) const { return { (float)(getWidth() * 0.5 + x * scale), (float)(getHeight() * 0.5 - y * scale) }; }
    Point fromPoint(juce::Point<float> p) const { return { (p.x - getWidth() * 0.5) / scale, (getHeight() * 0.5 - p.y) / scale }; }
    Snap nearestSnap(Point candidate) const { Snap result { candidate, {} }; double bestPixels = 8.0; if (const auto* sketch = owner.service->document().findSketch(sketchId)) for (const auto& entity : sketch->entities) if (entity.type == "line") for (const auto& endpoint : { std::pair<double, double> { entity.x1, entity.y1 }, std::pair<double, double> { entity.x2, entity.y2 } }) { const auto screen = toPoint(endpoint.first, endpoint.second); const auto pointer = toPoint(candidate.x, candidate.y); const auto pixels = screen.getDistanceFrom(pointer); if (pixels < bestPixels) { bestPixels = pixels; result.point = { endpoint.first, endpoint.second }; result.target = entity.id + (endpoint.first == entity.x1 && endpoint.second == entity.y1 ? ":p1" : ":p2"); } } return result; }
    static double distanceToSegment(double px, double py, double ax, double ay, double bx, double by) { const auto dx = bx - ax, dy = by - ay, q = dx * dx + dy * dy; const auto t = q > 0 ? juce::jlimit(0.0, 1.0, ((px - ax) * dx + (py - ay) * dy) / q) : 0.0; return std::hypot(px - (ax + t * dx), py - (ay + t * dy)); }
};

CadWorkspacePanel::~CadWorkspacePanel() = default;

CadWorkspacePanel::CadWorkspacePanel(std::shared_ptr<djehuti::cad::CadCommandService> s) : service(std::move(s)), sketchCanvas(std::make_unique<SketchCanvas>(*this))
{
    title.setText("CAD Workspace", juce::dontSendNotification); title.setFont(juce::Font(22.0f, juce::Font::bold)); units.setText("Units: mm | Right-handed XYZ coordinates", juce::dontSendNotification); status.setText("Ready", juce::dontSendNotification);
    addAndMakeVisible(tree); addAndMakeVisible(name); addAndMakeVisible(width); addAndMakeVisible(depth); addAndMakeVisible(height); addAndMakeVisible(radius); addAndMakeVisible(x); addAndMakeVisible(y); addAndMakeVisible(z); addAndMakeVisible(firstOperand); addAndMakeVisible(secondOperand); addAndMakeVisible(title); addAndMakeVisible(units); addAndMakeVisible(status); addAndMakeVisible(boxButton); addAndMakeVisible(cylinderButton); addAndMakeVisible(sketchButton); addAndMakeVisible(rectangleButton); addAndMakeVisible(circleButton); addAndMakeVisible(lineButton); addAndMakeVisible(extrudeButton); addAndMakeVisible(cutButton); addAndMakeVisible(finishSketchButton); addAndMakeVisible(cancelSketchButton); addAndMakeVisible(constraintButton); addAndMakeVisible(reverseToggle); addAndMakeVisible(throughAllToggle); addAndMakeVisible(subtractButton); addAndMakeVisible(unionButton); addAndMakeVisible(intersectButton); addAndMakeVisible(applyButton); addAndMakeVisible(undoButton); addAndMakeVisible(redoButton); addAndMakeVisible(hideButton); addAndMakeVisible(deleteButton); addAndMakeVisible(useFirstButton); addAndMakeVisible(useSecondButton); addAndMakeVisible(useTargetButton); addAndMakeVisible(sketchCanvas.get()); addAndMakeVisible(plane);
    constraintType.addItem("Horizontal", 1); constraintType.addItem("Vertical", 2); constraintType.addItem("Coincident", 3); constraintType.addItem("Fixed / Origin", 4); constraintType.addItem("Length", 5); constraintType.addItem("Radius", 6); constraintType.addItem("Diameter", 7); constraintType.setSelectedId(1);
    tree.setRootItemVisible(false); tree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff10151a)); plane.addItem("XY", 1); plane.addItem("XZ", 2); plane.addItem("YZ", 3); plane.setSelectedId(1);
    auto set = [](juce::TextEditor& e, const char* value) { e.setText(value, false); e.setInputRestrictions(24, "0123456789.-"); e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1c252c)); e.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee)); };
    set(width,"10"); set(depth,"10"); set(height,"10"); set(radius,"5"); set(x,"0"); set(y,"0"); set(z,"0"); firstOperand.setTextToShowWhenEmpty("First operand id", juce::Colour(0xff8fa4ad)); secondOperand.setTextToShowWhenEmpty("Second operand id", juce::Colour(0xff8fa4ad));
    for (auto* e : { &firstOperand, &secondOperand }) { e->setReadOnly(true); e->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1c252c)); e->setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee)); }
    boxButton.onClick=[this]{juce::String id,e;if(!service->createBox(width.getText().getDoubleValue(),depth.getText().getDoubleValue(),height.getText().getDoubleValue(),id,e))showError(e);else{refreshTree();selectFeature(id);}}; cylinderButton.onClick=[this]{juce::String id,e;if(!service->createCylinder(radius.getText().getDoubleValue(),height.getText().getDoubleValue(),id,e))showError(e);else{refreshTree();selectFeature(id);}};
    sketchButton.onClick=[this]{juce::String id,e;if(!service->createSketch(plane.getText(),0.0,id,e))showError(e);else{refreshTree();selectFeature(id);sketchCanvas->setMode("select");}}; rectangleButton.onClick=[this]{sketchCanvas->setMode("rectangle");}; circleButton.onClick=[this]{sketchCanvas->setMode("circle");}; lineButton.onClick=[this]{sketchCanvas->setMode("line");}; finishSketchButton.onClick=[this]{sketchCanvas->finishEditing();status.setText("Sketch editing finished; select the sketch to re-enter",juce::dontSendNotification);}; cancelSketchButton.onClick=[this]{sketchCanvas->cancelGesture();status.setText("Gesture cancelled; committed sketch geometry preserved",juce::dontSendNotification);}; constraintButton.onClick=[this]{const auto* sketch=service->document().findSketch(selectedId);if(sketch==nullptr||sketchEntityId.isEmpty()){showError("Select a sketch entity before adding a constraint.");return;}juce::String type;switch(constraintType.getSelectedId()){case 1:type="horizontal";break;case 2:type="vertical";break;case 3:type="coincident";break;case 4:type="fixed";break;case 5:type="length";break;case 6:type="radius";break;case 7:type="diameter";break;default:showError("Choose a supported sketch constraint.");return;}if(type=="coincident"&&sketchSecondEntityId.isEmpty()){showError("Shift-click a second sketch entity for coincidence.");return;}const auto value=(type=="length"||type=="radius"||type=="diameter")?width.getText().getDoubleValue():0.0;juce::String id,error;if(!service->addSketchConstraint(selectedId,type,sketchEntityId,sketchSecondEntityId,value,id,error))showError(error);else{status.setText("Applied "+type+" constraint",juce::dontSendNotification);sketchCanvas->repaint();}};
    extrudeButton.onClick=[this]{juce::String id,e;if(!service->createExtrude(selectedId,depth.getText().getDoubleValue(),false,id,e))showError(e);else{refreshTree();selectFeature(id);}}; useTargetButton.onClick=[this]{if(!selectedId.isEmpty()){targetId=selectedId;status.setText("Cut target: "+targetId,juce::dontSendNotification);}}; cutButton.onClick=[this]{juce::String id,e;const bool through=throughAllToggle.getToggleState();if(!service->createCut(selectedId,targetId,through?0.0:depth.getText().getDoubleValue(),through,reverseToggle.getToggleState(),id,e))showError(e);else{refreshTree();selectFeature(id);}};
    useFirstButton.onClick=[this]{if(!selectedId.isEmpty())firstOperand.setText(selectedId,false);}; useSecondButton.onClick=[this]{if(!selectedId.isEmpty())secondOperand.setText(selectedId,false);}; applyButton.onClick=[this]{applyProperties();}; hideButton.onClick=[this]{if(!selectedId.isEmpty()){juce::String e;const auto* f=service->document().find(selectedId);if(f&&!service->setVisible(selectedId,!f->visible,e))showError(e);refreshTree();}}; deleteButton.onClick=[this]{if(!selectedId.isEmpty()){juce::String e;if(!service->remove(selectedId,e))showError(e);else{selectedId.clear();refreshTree();}}};
    auto booleanClick=[this](const juce::String& op){juce::String a,b,e,id;if(!selectedPair(a,b)){showError("Choose both Boolean operands with the operand buttons.");return;}if(!service->booleanOp(op,a,b,id,e))showError(e);else{refreshTree();firstOperand.clear();secondOperand.clear();selectFeature(id);}}; subtractButton.onClick=[this,booleanClick]{booleanClick("subtract");}; unionButton.onClick=[this,booleanClick]{booleanClick("union");}; intersectButton.onClick=[this,booleanClick]{booleanClick("intersect");}; undoButton.onClick=[this]{juce::String e;if(!service->undo(e))showError(e);else refreshTree();}; redoButton.onClick=[this]{juce::String e;if(!service->redo(e))showError(e);else refreshTree();}; sketchCanvas->setVisible(false); refreshTree();
}

void CadWorkspacePanel::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff11161b));g.setColour(juce::Colour(0xff8fa4ad));g.drawText("Model tree",24,62,250,22,juce::Justification::left);g.drawText("Properties",300,62,250,22,juce::Justification::left);}
void CadWorkspacePanel::resized(){auto a=getLocalBounds().reduced(24);title.setBounds(a.removeFromTop(30));units.setBounds(a.removeFromTop(24));auto toolbar=a.removeFromTop(34);boxButton.setBounds(toolbar.removeFromLeft(52));cylinderButton.setBounds(toolbar.removeFromLeft(68));sketchButton.setBounds(toolbar.removeFromLeft(58));rectangleButton.setBounds(toolbar.removeFromLeft(72));circleButton.setBounds(toolbar.removeFromLeft(58));lineButton.setBounds(toolbar.removeFromLeft(48));extrudeButton.setBounds(toolbar.removeFromLeft(64));cutButton.setBounds(toolbar.removeFromLeft(82));subtractButton.setBounds(toolbar.removeFromLeft(76));unionButton.setBounds(toolbar.removeFromLeft(58));intersectButton.setBounds(toolbar.removeFromLeft(70));undoButton.setBounds(toolbar.removeFromRight(62));redoButton.setBounds(toolbar.removeFromRight(62));auto left=a.removeFromLeft(260).withTrimmedTop(32);tree.setBounds(left);auto p=a.withTrimmedTop(32);auto canvas=p.removeFromTop(220);sketchCanvas->setBounds(canvas);finishSketchButton.setBounds(p.removeFromTop(26));p.removeFromTop(4);cancelSketchButton.setBounds(p.removeFromTop(26));p.removeFromTop(4);constraintType.setBounds(p.removeFromTop(26));p.removeFromTop(4);constraintButton.setBounds(p.removeFromTop(26));p.removeFromTop(4);reverseToggle.setBounds(p.removeFromTop(24));throughAllToggle.setBounds(p.removeFromTop(24));p.removeFromTop(4);auto row=[&](juce::TextEditor& e){e.setBounds(p.removeFromTop(26));p.removeFromTop(5);};plane.setBounds(p.removeFromTop(26));p.removeFromTop(5);row(name);row(width);row(depth);row(height);row(radius);row(x);row(y);row(z);firstOperand.setBounds(p.removeFromTop(26));p.removeFromTop(4);secondOperand.setBounds(p.removeFromTop(26));p.removeFromTop(4);useFirstButton.setBounds(p.removeFromTop(26));p.removeFromTop(4);useSecondButton.setBounds(p.removeFromTop(26));p.removeFromTop(4);useTargetButton.setBounds(p.removeFromTop(26));p.removeFromTop(4);applyButton.setBounds(p.removeFromTop(30));hideButton.setBounds(p.removeFromTop(30));deleteButton.setBounds(p.removeFromTop(30));status.setBounds(p.removeFromTop(36));}
void CadWorkspacePanel::refreshTree(){tree.setRootItem(nullptr);auto* root=new CadRootItem();for(const auto& f:service->document().features())root->addSubItem(new FeatureItem(*this,f));tree.setRootItem(root);root->setOpen(true);}
void CadWorkspacePanel::selectFeature(const juce::String& id){selectedId=id;sketchEntityId.clear();sketchSecondEntityId.clear();secondOperand.clear();const auto* f=service->document().find(id);if(!f)return;name.setText(f->name,false);width.setText(juce::String(f->width),false);depth.setText(juce::String(f->depth),false);height.setText(juce::String(f->height),false);radius.setText(juce::String(f->radius),false);x.setText(juce::String(f->x),false);y.setText(juce::String(f->y),false);z.setText(juce::String(f->z),false);const bool isSketch=f->type=="sketch";sketchCanvas->setVisible(isSketch);if(isSketch){sketchCanvas->setSketch(f->sketchId.isEmpty()?f->id:f->sketchId);sketchCanvas->toFront(false);}}
void CadWorkspacePanel::selectSketchEntity(const juce::String& id,bool second){if(second){sketchSecondEntityId=id;secondOperand.setText(id,false);}else{sketchEntityId=id;firstOperand.setText(id,false);sketchSecondEntityId.clear();secondOperand.clear();}if(const auto* sketch=service->document().findSketch(selectedId))for(const auto& entity:sketch->entities)if(entity.id==id){width.setText(entity.type=="circle"?juce::String(entity.radius):juce::String(std::hypot(entity.x2-entity.x1,entity.y2-entity.y1)),false);x.setText(juce::String(entity.x1),false);y.setText(juce::String(entity.y1),false);status.setText((second?"Second ":"First ")+entity.type+" selected: "+id,juce::dontSendNotification);return;}status.setText(id.isEmpty()?"No sketch entity selected":"Sketch entity not found: "+id,juce::dontSendNotification);}
void CadWorkspacePanel::applyProperties(){if(selectedId.isEmpty())return;juce::String e;if(!sketchEntityId.isEmpty()&&service->document().findSketch(selectedId)!=nullptr){if(!service->setSketchEntityPosition(selectedId,sketchEntityId,x.getText().getDoubleValue(),y.getText().getDoubleValue(),e)||!service->setSketchEntityDimension(selectedId,sketchEntityId,width.getText().getDoubleValue(),e)){showError(e);return;}sketchCanvas->repaint();refreshTree();}else if(!service->rename(selectedId,name.getText(),e)||!service->setDimensions(selectedId,width.getText().getDoubleValue(),depth.getText().getDoubleValue(),height.getText().getDoubleValue(),radius.getText().getDoubleValue(),e)||!service->setPlacement(selectedId,x.getText().getDoubleValue(),y.getText().getDoubleValue(),z.getText().getDoubleValue(),0,0,0,e))showError(e);else refreshTree();}
bool CadWorkspacePanel::selectedPair(juce::String& first,juce::String& second) const{first=firstOperand.getText().trim();second=secondOperand.getText().trim();return !first.isEmpty()&&!second.isEmpty()&&first!=second&&service->document().find(first)!=nullptr&&service->document().find(second)!=nullptr;}
void CadWorkspacePanel::showError(const juce::String& error){status.setText(error,juce::dontSendNotification);}
void CadWorkspacePanel::newDocument(){service->newDocument();selectedId.clear();targetId.clear();sketchEntityId.clear();sketchSecondEntityId.clear();sketchCanvas->setVisible(false);refreshTree();status.setText("New CAD document",juce::dontSendNotification);}
void CadWorkspacePanel::openDocument(){fileChooser=std::make_unique<juce::FileChooser>("Open CAD document",juce::File(),"*.djo;*.djc");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& chooser){if(auto file=chooser.getResult();file.existsAsFile()){juce::String e;if(!service->load(file,e))showError(e);else refreshTree();}fileChooser.reset();});}
void CadWorkspacePanel::saveDocument(){auto file=service->document().file();if(file==juce::File())saveDocumentAs();else{juce::String e;if(!service->save(file,e))showError(e);else service->document().markSaved();}}
void CadWorkspacePanel::saveDocumentAs(){fileChooser=std::make_unique<juce::FileChooser>("Save native CAD document",juce::File(),"*.djo");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& chooser){auto file=chooser.getResult().withFileExtension("djo");if(file!=juce::File()){juce::String e;if(!service->save(file,e))showError(e);else{service->document().setFile(file);service->document().markSaved();}}fileChooser.reset();});}

