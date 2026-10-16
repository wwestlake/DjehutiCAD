#include "CadDocument.h"

#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>

namespace djehuti::cad
{
namespace
{
double number(const juce::var& value, double fallback) { return value.isDouble() || value.isInt() || value.isInt64() ? value.toString().getDoubleValue() : fallback; }
juce::var objValue(const juce::var& object, const char* key, const juce::var& fallback) { return object.getProperty(key, fallback); }
}

CadDocument::CadDocument() { clear(); }
void CadDocument::clear() { items.clear(); undoStack.clear(); redoStack.clear(); nextNumber = 1; documentRevision = 0; dirty = false; documentFile = {}; invalidate(); }
juce::String CadDocument::newId(const char* prefix, int number) { return juce::String(prefix) + "-" + juce::String(number); }
const CadFeature* CadDocument::find(const juce::String& id) const { for (const auto& item : items) if (item.id == id) return &item; return nullptr; }
void CadDocument::rememberUndo() { undoStack.push_back({ serialize() }); redoStack.clear(); }
void CadDocument::invalidate() { ++documentRevision; cachedSnapshot.reset(); cachedRevision = 0; }
bool CadDocument::mutate(std::function<bool(juce::String&)> action, juce::String& error)
{
    const auto before = serialize();
    if (!action(error)) return false;
    if (!rebuild(error)) { deserialize(before, error); return false; }
    undoStack.push_back({ before }); redoStack.clear(); dirty = true; invalidate(); return true;
}

bool CadDocument::createBox(double w, double d, double h, juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e) { if (w <= 0 || d <= 0 || h <= 0) { e = "Box dimensions must be greater than zero."; return false; } CadFeature f; f.id = newId("box", nextNumber++); f.name = "Box " + juce::String(nextNumber - 1); f.type = "box"; f.width = w; f.depth = d; f.height = h; items.push_back(f); id = f.id; return true; }, error);
}
bool CadDocument::createCylinder(double r, double h, juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e) { if (r <= 0 || h <= 0) { e = "Cylinder radius and height must be greater than zero."; return false; } CadFeature f; f.id = newId("cylinder", nextNumber++); f.name = "Cylinder " + juce::String(nextNumber - 1); f.type = "cylinder"; f.radius = r; f.height = h; items.push_back(f); id = f.id; return true; }, error);
}
bool CadDocument::setPlacement(const juce::String& id, double x, double y, double z, double rx, double ry, double rz, juce::String& error)
{
    return mutate([&](juce::String& e) { for (auto& f : items) if (f.id == id) { f.x=x; f.y=y; f.z=z; f.rx=rx; f.ry=ry; f.rz=rz; return true; } e = "Unknown feature id: " + id; return false; }, error);
}
bool CadDocument::setDimensions(const juce::String& id, double w, double d, double h, double r, juce::String& error)
{
    return mutate([&](juce::String& e) { for (auto& f : items) if (f.id == id) { if (f.type == "box" && (w <= 0 || d <= 0 || h <= 0)) { e="Box dimensions must be greater than zero."; return false; } if (f.type == "cylinder" && (r <= 0 || h <= 0)) { e="Cylinder radius and height must be greater than zero."; return false; } f.width=w; f.depth=d; f.height=h; f.radius=r; return true; } e = "Unknown feature id: " + id; return false; }, error);
}
bool CadDocument::booleanOp(const juce::String& kind, const juce::String& first, const juce::String& second, juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e) { if ((kind != "union" && kind != "subtract" && kind != "intersect") || find(first) == nullptr || find(second) == nullptr || first == second) { e = "Boolean operation requires two distinct existing features."; return false; } CadFeature f; f.id = newId(kind.toRawUTF8(), nextNumber++); f.name = kind.substring(0, 1).toUpperCase() + kind.substring(1) + " " + juce::String(nextNumber - 1); f.type = kind; f.firstOperand=first; f.secondOperand=second; f.visible=true; for (auto& source : items) if (source.id == first || source.id == second) source.visible=false; items.push_back(f); id=f.id; return true; }, error);
}
bool CadDocument::rename(const juce::String& id, const juce::String& name, juce::String& error) { return mutate([&](juce::String& e) { for (auto& f : items) if (f.id == id) { if (name.trim().isEmpty()) { e="Name cannot be empty."; return false; } f.name=name.trim(); return true; } e="Unknown feature id: "+id; return false; }, error); }
bool CadDocument::setVisible(const juce::String& id, bool visible, juce::String& error) { return mutate([&](juce::String& e) { for (auto& f : items) if (f.id == id) { f.visible=visible; return true; } e="Unknown feature id: "+id; return false; }, error); }
bool CadDocument::remove(const juce::String& id, juce::String& error) { return mutate([&](juce::String& e) { for (const auto& f : items) if (f.firstOperand == id || f.secondOperand == id) { e="Cannot delete a feature used by a Boolean result."; return false; } const auto old=items.size(); items.erase(std::remove_if(items.begin(), items.end(), [&](const CadFeature& f){return f.id==id;}), items.end()); if (items.size()==old) { e="Unknown feature id: "+id; return false; } return true; }, error); }

TopoDS_Shape CadDocument::place(const TopoDS_Shape& source, const CadFeature& f) const
{
    gp_Trsf t; t.SetTranslation(gp_Vec(f.x, f.y, f.z));
    TopoDS_Shape result = BRepBuilderAPI_Transform(source, t, true).Shape();
    if (f.rx != 0) { gp_Trsf r; r.SetRotation(gp_Ax1(gp_Pnt(f.x, f.y, f.z), gp::DX()), f.rx * M_PI / 180.0); result = BRepBuilderAPI_Transform(result, r, true).Shape(); }
    if (f.ry != 0) { gp_Trsf r; r.SetRotation(gp_Ax1(gp_Pnt(f.x, f.y, f.z), gp::DY()), f.ry * M_PI / 180.0); result = BRepBuilderAPI_Transform(result, r, true).Shape(); }
    if (f.rz != 0) { gp_Trsf r; r.SetRotation(gp_Ax1(gp_Pnt(f.x, f.y, f.z), gp::DZ()), f.rz * M_PI / 180.0); result = BRepBuilderAPI_Transform(result, r, true).Shape(); }
    return result;
}
TopoDS_Shape CadDocument::evaluate(const juce::String& id, juce::String& error, juce::StringArray& active)
{
    if (active.contains(id)) { error = "Circular feature dependency detected at " + id; return {}; } active.add(id); const auto* f=find(id); if (!f) { error="Missing feature dependency: "+id; return {}; }
    try { TopoDS_Shape result; if (f->type == "box") result=BRepPrimAPI_MakeBox(f->width,f->depth,f->height).Shape(); else if (f->type == "cylinder") result=BRepPrimAPI_MakeCylinder(f->radius,f->height).Shape(); else { auto a=evaluate(f->firstOperand,error,active); auto b=evaluate(f->secondOperand,error,active); if (a.IsNull() || b.IsNull()) return {}; if (f->type=="union") result=BRepAlgoAPI_Fuse(a,b).Shape(); else if (f->type=="subtract") result=BRepAlgoAPI_Cut(a,b).Shape(); else if (f->type=="intersect") result=BRepAlgoAPI_Common(a,b).Shape(); else { error="Unsupported feature type: "+f->type; return {}; } } active.removeString(id); return place(result,*f); } catch (const Standard_Failure& ex) { error = "OCCT failed evaluating " + f->name + ": " + juce::String(ex.GetMessageString()); return {}; }
}
bool CadDocument::rebuild(juce::String& error) { for (auto& f : items) { juce::StringArray active; f.shape=evaluate(f.id,error,active); if (f.shape.IsNull()) return false; } return true; }

std::shared_ptr<const CadRenderSnapshot> CadDocument::snapshot() const
{
    if (cachedSnapshot != nullptr && cachedRevision == documentRevision) return cachedSnapshot;
    auto next = std::make_shared<CadRenderSnapshot>(); next->revision=documentRevision;
    for (const auto& f : items) if (f.visible && !f.shape.IsNull()) { BRepMesh_IncrementalMesh mesh(f.shape, 0.1, false, 0.5, true); for (TopExp_Explorer ex(f.shape, TopAbs_FACE); ex.More(); ex.Next()) { const auto face=TopoDS::Face(ex.Current()); TopLoc_Location loc; const auto tri=BRep_Tool::Triangulation(face,loc); if (tri.IsNull()) continue; CadMesh out; for (int i=1;i<=tri->NbNodes();++i) { const auto p=tri->Node(i).Transformed(loc.Transformation()); out.vertices.insert(out.vertices.end(), {static_cast<float>(p.X()),static_cast<float>(p.Y()),static_cast<float>(p.Z()),0.0f,0.0f,1.0f}); } for (int i=1;i<=tri->NbTriangles();++i) { int a,b,c; tri->Triangle(i).Get(a,b,c); out.indices.insert(out.indices.end(), {static_cast<std::uint32_t>(a-1),static_cast<std::uint32_t>(b-1),static_cast<std::uint32_t>(c-1)}); } next->meshes.push_back(std::move(out)); } }
    cachedSnapshot=next; cachedRevision=documentRevision; return cachedSnapshot;
}

juce::String CadDocument::serialize() const
{
    auto root=std::make_unique<juce::DynamicObject>(); root->setProperty("formatVersion",1); root->setProperty("units","mm"); root->setProperty("nextNumber",nextNumber); juce::Array<juce::var> list; for (const auto& f:items) { auto o=std::make_unique<juce::DynamicObject>(); o->setProperty("id",f.id); o->setProperty("name",f.name); o->setProperty("type",f.type); o->setProperty("firstOperand",f.firstOperand); o->setProperty("secondOperand",f.secondOperand); o->setProperty("width",f.width); o->setProperty("depth",f.depth); o->setProperty("height",f.height); o->setProperty("radius",f.radius); o->setProperty("x",f.x); o->setProperty("y",f.y); o->setProperty("z",f.z); o->setProperty("rx",f.rx); o->setProperty("ry",f.ry); o->setProperty("rz",f.rz); o->setProperty("visible",f.visible); list.add(juce::var(o.release())); } root->setProperty("features",juce::var(list)); return juce::JSON::toString(juce::var(root.release()));
}
bool CadDocument::deserialize(const juce::String& json, juce::String& error)
{
    const auto parsed=juce::JSON::parse(json); auto* root=parsed.getDynamicObject(); if (!root) { error="CAD document is not valid JSON."; return false; } if (root->getProperty("units").toString() != "mm") { error="Only millimeter CAD documents are supported."; return false; } std::vector<CadFeature> loaded; if (auto* array=root->getProperty("features").getArray()) for (const auto& value:*array) { auto* o=value.getDynamicObject(); if (!o) { error="CAD feature entry is invalid."; return false; } CadFeature f; f.id=o->getProperty("id").toString(); f.name=o->getProperty("name").toString(); f.type=o->getProperty("type").toString(); f.firstOperand=o->getProperty("firstOperand").toString(); f.secondOperand=o->getProperty("secondOperand").toString(); f.width=number(o->getProperty("width"),f.width); f.depth=number(o->getProperty("depth"),f.depth); f.height=number(o->getProperty("height"),f.height); f.radius=number(o->getProperty("radius"),f.radius); f.x=number(o->getProperty("x"),0); f.y=number(o->getProperty("y"),0); f.z=number(o->getProperty("z"),0); f.rx=number(o->getProperty("rx"),0); f.ry=number(o->getProperty("ry"),0); f.rz=number(o->getProperty("rz"),0); f.visible=static_cast<bool>(o->getProperty("visible")); loaded.push_back(f); } const auto old=items; items=std::move(loaded); nextNumber=static_cast<int>(root->getProperty("nextNumber")); if (nextNumber<1) nextNumber=static_cast<int>(items.size()+1); if (!rebuild(error)) { items=old; return false; } return true;
}
bool CadDocument::undo(juce::String& error) { if (undoStack.empty()) { error="Nothing to undo."; return false; } const auto current=serialize(); const auto state=undoStack.back(); undoStack.pop_back(); if (!deserialize(state.json,error)) return false; redoStack.push_back({current}); dirty=true; invalidate(); return true; }
bool CadDocument::redo(juce::String& error) { if (redoStack.empty()) { error="Nothing to redo."; return false; } const auto current=serialize(); const auto state=redoStack.back(); redoStack.pop_back(); if (!deserialize(state.json,error)) return false; undoStack.push_back({current}); dirty=true; invalidate(); return true; }
bool CadDocument::save(const juce::File& file, juce::String& error) const { if (!file.replaceWithText(serialize())) { error="Unable to write CAD document: "+file.getFullPathName(); return false; } return true; }
bool CadDocument::load(const juce::File& file, juce::String& error) { const auto text=file.loadFileAsString(); if (text.isEmpty()) { error="CAD document is empty or unreadable."; return false; } if (!deserialize(text,error)) return false; undoStack.clear(); redoStack.clear(); documentFile=file; dirty=false; invalidate(); return true; }

bool CadCommandService::newDocument() { model.clear(); return true; }
bool CadCommandService::createBox(double w,double d,double h,juce::String& id,juce::String& error){return model.createBox(w,d,h,id,error);}
bool CadCommandService::createCylinder(double r,double h,juce::String& id,juce::String& error){return model.createCylinder(r,h,id,error);}
bool CadCommandService::setPlacement(const juce::String& id,double x,double y,double z,double rx,double ry,double rz,juce::String& error){return model.setPlacement(id,x,y,z,rx,ry,rz,error);}
bool CadCommandService::setDimensions(const juce::String& id,double w,double d,double h,double r,juce::String& error){return model.setDimensions(id,w,d,h,r,error);}
bool CadCommandService::booleanOp(const juce::String& k,const juce::String& a,const juce::String& b,juce::String& id,juce::String& error){return model.booleanOp(k,a,b,id,error);}
}
