#include "CadDocument.h"

#include <atomic>

#include <BinDrivers.hxx>
#include <PCDM_ReaderStatus.hxx>
#include <PCDM_StoreStatus.hxx>
#include <TDataStd_AsciiString.hxx>
#include <TDataStd_Integer.hxx>
#include <TDataStd_Real.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_Label.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TNaming_Builder.hxx>
#include <TNaming_NamedShape.hxx>
#include <TNaming_Tool.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopExp.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <Bnd_Box.hxx>
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <Poly_Triangulation.hxx>
#include <GeomLProp_SLProps.hxx>
#include <TopLoc_Location.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>

namespace djehuti::cad
{
namespace
{
constexpr int kMetaTag = 1, kFeatureContainerTag = 2, kReferenceContainerTag = 3, kSketchContainerTag = 4, kMechanicsContainerTag = 5, kFeatureTagBase = 1000;
constexpr int kShapeTag = 30, kReferenceShapeTag = 5;
constexpr int kGeneratedHistoryTag = 31, kModifiedHistoryTag = 32, kDeletedHistoryTag = 33;
double number(const juce::var& value, double fallback) { return value.isDouble() || value.isInt() || value.isInt64() ? value.toString().getDoubleValue() : fallback; }
TDF_Label field(const TDF_Label& label, int tag) { return label.FindChild(tag, true); }
TDF_Label existingField(const TDF_Label& label, int tag) { return label.FindChild(tag, false); }
void writeString(const TDF_Label& label, int tag, const juce::String& value) { TDataStd_AsciiString::Set(field(label, tag), TCollection_AsciiString(value.toRawUTF8())); }
juce::String readString(const TDF_Label& label, int tag) { Handle(TDataStd_AsciiString) a; const auto child=existingField(label, tag); return !child.IsNull() && child.FindAttribute(TDataStd_AsciiString::GetID(), a) && !a.IsNull() ? juce::String(a->Get().ToCString()) : juce::String(); }
void writeReal(const TDF_Label& label, int tag, double value) { TDataStd_Real::Set(field(label, tag), value); }
double readReal(const TDF_Label& label, int tag, double fallback) { Handle(TDataStd_Real) a; const auto child=existingField(label, tag); return !child.IsNull() && child.FindAttribute(TDataStd_Real::GetID(), a) && !a.IsNull() ? a->Get() : fallback; }
void writeInteger(const TDF_Label& label, int tag, int value) { TDataStd_Integer::Set(field(label, tag), value); }
int readInteger(const TDF_Label& label, int tag, int fallback) { Handle(TDataStd_Integer) a; const auto child=existingField(label, tag); return !child.IsNull() && child.FindAttribute(TDataStd_Integer::GetID(), a) && !a.IsNull() ? a->Get() : fallback; }
int featureNumber(const juce::String& id) { return std::max(1, id.fromLastOccurrenceOf("-", false, false).getIntValue()); }
TCollection_ExtendedString pathString(const juce::File& file) { return TCollection_ExtendedString(file.getFullPathName().toRawUTF8(), Standard_True); }
Handle(TNaming_NamedShape) namedShape(const TDF_Label& label) { Handle(TNaming_NamedShape) result; if (!label.IsNull()) label.FindAttribute(TNaming_NamedShape::GetID(), result); return result; }
void recordGeneratedOrModifiedAt(const TDF_Label& target, const TopoDS_Shape& shape) { if (target.IsNull() || shape.IsNull()) return; const auto previous = namedShape(target); TNaming_Builder builder(target); if (!previous.IsNull() && !previous->IsEmpty()) { const auto oldShape = TNaming_Tool::CurrentShape(previous); if (!oldShape.IsNull()) { builder.Modify(oldShape, shape); return; } } builder.Generated(shape); }
void recordGeneratedOrModified(const TDF_Label& label, const TopoDS_Shape& shape) { recordGeneratedOrModifiedAt(label.FindChild(kShapeTag, true), shape); }
void recordDeleted(const TDF_Label& label) { const auto target = label.FindChild(kShapeTag, true); const auto previous = namedShape(target); if (previous.IsNull() || previous->IsEmpty()) return; const auto oldShape = TNaming_Tool::CurrentShape(previous); if (!oldShape.IsNull()) { TNaming_Builder builder(target); builder.Delete(oldShape); } }
bool endpoints(const TopoDS_Shape& shape, gp_Pnt& first, gp_Pnt& second) { if (shape.IsNull() || shape.ShapeType() != TopAbs_EDGE) return false; TopoDS_Vertex a,b; TopExp::Vertices(TopoDS::Edge(shape),a,b); if (a.IsNull() || b.IsNull()) return false; first=BRep_Tool::Pnt(a); second=BRep_Tool::Pnt(b); return true; }
bool sameEndpoints(const TopoDS_Shape& a, const TopoDS_Shape& b) { gp_Pnt a1,a2,b1,b2; if (!endpoints(a,a1,a2) || !endpoints(b,b1,b2)) return false; const auto close=[](const gp_Pnt& p,const gp_Pnt& q){ return p.Distance(q) < 1.0e-7; }; return (close(a1,b1) && close(a2,b2)) || (close(a1,b2) && close(a2,b1)); }
bool representativeBoxEdge(const CadFeature& feature, TopoDS_Shape& result) { if (feature.type != "box" || feature.shape.IsNull()) return false; const auto localBox=BRepPrimAPI_MakeBox(feature.width,feature.depth,feature.height).Shape(); TopoDS_Shape localEdge; for (TopExp_Explorer ex(localBox,TopAbs_EDGE); ex.More(); ex.Next()) { const auto candidate=ex.Current(); gp_Pnt a,b; if (endpoints(candidate,a,b) && std::abs(a.Y())<1.0e-7 && std::abs(b.Y())<1.0e-7 && std::abs(a.Z())<1.0e-7 && std::abs(b.Z())<1.0e-7 && std::abs(std::abs(a.X()-b.X())-feature.width)<1.0e-7) { localEdge=candidate; break; } } if (localEdge.IsNull()) return false; const auto expected = BRepBuilderAPI_Transform(localEdge, gp_Trsf(), true).Shape(); gp_Trsf transform; transform.SetTranslation(gp_Vec(feature.x,feature.y,feature.z)); auto placed=BRepBuilderAPI_Transform(expected,transform,true).Shape(); if (feature.rx!=0) { gp_Trsf r; r.SetRotation(gp_Ax1(gp_Pnt(feature.x,feature.y,feature.z),gp::DX()),feature.rx*M_PI/180.0); placed=BRepBuilderAPI_Transform(placed,r,true).Shape(); } if (feature.ry!=0) { gp_Trsf r; r.SetRotation(gp_Ax1(gp_Pnt(feature.x,feature.y,feature.z),gp::DY()),feature.ry*M_PI/180.0); placed=BRepBuilderAPI_Transform(placed,r,true).Shape(); } if (feature.rz!=0) { gp_Trsf r; r.SetRotation(gp_Ax1(gp_Pnt(feature.x,feature.y,feature.z),gp::DZ()),feature.rz*M_PI/180.0); placed=BRepBuilderAPI_Transform(placed,r,true).Shape(); } for (TopExp_Explorer ex(feature.shape,TopAbs_EDGE); ex.More(); ex.Next()) if (sameEndpoints(ex.Current(),placed)) { result=ex.Current(); return true; } return false; }
template <typename BooleanBuilder>
void collectBooleanHistory(BooleanBuilder& operation, const CadFeature& feature, const TopoDS_Shape& first, const TopoDS_Shape& second, std::vector<CadDocument::CadTopologyEvolution>& history)
{
    CadDocument::CadTopologyEvolution entry;
    entry.featureId=feature.id;
    const auto collect=[&](const TopoDS_Shape& source)
    {
        for (TopExp_Explorer ex(source,TopAbs_EDGE); ex.More(); ex.Next())
        {
            const auto original=ex.Current();
            for (TopTools_ListIteratorOfListOfShape it(operation.Generated(original)); it.More(); it.Next()) entry.generated.push_back({original,it.Value()});
            for (TopTools_ListIteratorOfListOfShape it(operation.Modified(original)); it.More(); it.Next()) entry.modified.push_back({original,it.Value()});
            if (operation.IsDeleted(original)) entry.deleted.push_back(original);
        }
    };
    collect(first); collect(second); if (!entry.generated.empty() || !entry.modified.empty() || !entry.deleted.empty()) history.push_back(std::move(entry));
}
void recordOperationHistory(const TDF_Label& label, const CadDocument::CadTopologyEvolution& entry)
{
    if (!entry.generated.empty()) { TNaming_Builder builder(label.FindChild(kGeneratedHistoryTag,true)); for (const auto& pair:entry.generated) builder.Generated(pair.first,pair.second); }
    if (!entry.modified.empty()) { TNaming_Builder builder(label.FindChild(kModifiedHistoryTag,true)); for (const auto& pair:entry.modified) builder.Modify(pair.first,pair.second); }
    if (!entry.deleted.empty()) { TNaming_Builder builder(label.FindChild(kDeletedHistoryTag,true)); for (const auto& shape:entry.deleted) builder.Delete(shape); }
}
}

namespace { std::atomic<std::uint64_t> documentInstanceCounter { 0 }; }
CadDocument::CadDocument() { clear(); }
void CadDocument::initializeOCAF() { ocafApplication = new TDocStd_Application(); BinDrivers::DefineFormat(ocafApplication); ocafApplication->NewDocument(TCollection_ExtendedString("BinOcaf"), ocafDocument); ocafDocument->SetUndoLimit(100); }
void CadDocument::clear() { instanceId=++documentInstanceCounter; items.clear(); sketches.clear(); mechanics = {}; mechanics.modelId = juce::Uuid().toDashedString(); references.clear(); operationHistory.clear(); nextNumber=1; documentRevision=0; dirty=false; documentFile={}; initializeOCAF(); juce::String ignored; syncOCAFFromItems(ignored); invalidate(); }
juce::String CadDocument::newId(const char* prefix, int number) { return juce::String(prefix)+"-"+juce::String(number); }
const CadFeature* CadDocument::find(const juce::String& id) const { for (const auto& f:items) if (f.id==id) return &f; return nullptr; }
CadSketch* CadDocument::mutableSketch(const juce::String& id) { for (auto& sketch:sketches) if (sketch.id==id) return &sketch; return nullptr; }
const CadSketch* CadDocument::findSketch(const juce::String& id) const { for (const auto& sketch:sketches) if (sketch.id==id) return &sketch; return nullptr; }
void CadDocument::invalidate() { ++documentRevision; cachedSnapshot.reset(); cachedRevision=0; }

bool CadDocument::mutate(std::function<bool(juce::String&)> action, juce::String& error)
{
    const auto oldItems=items; const auto oldSketches=sketches; const auto oldMechanics=mechanics; const auto oldRefs=references; const auto oldNext=nextNumber; ocafDocument->NewCommand();
    if (!action(error)) { items=oldItems; sketches=oldSketches; mechanics=oldMechanics; references=oldRefs; nextNumber=oldNext; ocafDocument->AbortCommand(); return false; }
    if (!rebuild(error) || !syncOCAFFromItems(error)) { items=oldItems; sketches=oldSketches; mechanics=oldMechanics; references=oldRefs; nextNumber=oldNext; ocafDocument->AbortCommand(); rebuild(error); return false; }
    writeString(ocafDocument->Main().FindChild(kSketchContainerTag,true),1,serializeSketches());
    writeString(ocafDocument->Main().FindChild(kMechanicsContainerTag,true),1,juce::JSON::toString(mech::toJson(mechanics),true));
    ocafDocument->CommitCommand();
    if (!syncItemsFromOCAF(error)) return false;
    dirty=true; invalidate(); return true;
}
bool CadDocument::createBox(double w,double d,double h,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){if(w<=0||d<=0||h<=0){e="Box dimensions must be greater than zero.";return false;} CadFeature f;f.id=newId("box",nextNumber++);f.name="Box "+juce::String(nextNumber-1);f.type="box";f.width=w;f.depth=d;f.height=h;items.push_back(f);id=f.id;return true;},error); }
bool CadDocument::createCylinder(double r,double h,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){if(r<=0||h<=0){e="Cylinder radius and height must be greater than zero.";return false;} CadFeature f;f.id=newId("cylinder",nextNumber++);f.name="Cylinder "+juce::String(nextNumber-1);f.type="cylinder";f.radius=r;f.height=h;items.push_back(f);id=f.id;return true;},error); }
bool CadDocument::createSketch(const juce::String& plane,double offset,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){const auto normalized=plane.toUpperCase();if(normalized!="XY"&&normalized!="XZ"&&normalized!="YZ"){e="Sketch plane must be XY, XZ, or YZ.";return false;}if(!std::isfinite(offset)){e="Sketch plane offset must be finite.";return false;}CadSketch sketch;sketch.id=newId("sketch",nextNumber++);sketch.plane=normalized;sketch.offset=offset;sketches.push_back(sketch);CadFeature feature;feature.id=sketches.back().id;feature.name="Sketch "+juce::String(nextNumber-1);feature.type="sketch";feature.sketchId=feature.id;feature.visible=true;items.push_back(feature);id=feature.id;return true;},error); }
bool CadDocument::addSketchLine(const juce::String& sketchId,double x1,double y1,double x2,double y2,juce::String& entityId,juce::String& error,const juce::String& firstConnection,const juce::String& secondConnection) { return mutate([&](juce::String& e){auto* sketch=mutableSketch(sketchId);if(sketch==nullptr){e="Unknown sketch id: "+sketchId;return false;}if(std::hypot(x2-x1,y2-y1)<1.0e-8){e="Sketch line must have nonzero length.";return false;}CadSketchEntity entity;entity.id=newId("line",nextNumber++);entity.type="line";entity.x1=x1;entity.y1=y1;entity.x2=x2;entity.y2=y2;sketch->entities.push_back(entity);entityId=entity.id;const auto addConnection=[&](const juce::String& target,const char* endpoint){if(target.isEmpty())return;CadSketchConstraint c;c.id=newId("constraint",nextNumber++);c.type="coincident";c.firstEntity=entity.id+endpoint;c.secondEntity=target;sketch->constraints.push_back(c);};addConnection(firstConnection,":p1");addConnection(secondConnection,":p2");return solveSketch(*sketch,e);},error); }
bool CadDocument::addSketchRectangle(const juce::String& sketchId,double x,double y,double w,double h,juce::String& groupId,juce::String& error) { return mutate([&](juce::String& e){auto* sketch=mutableSketch(sketchId);if(sketch==nullptr){e="Unknown sketch id: "+sketchId;return false;}if(w<=0||h<=0){e="Sketch rectangle dimensions must be greater than zero.";return false;}const auto group=newId("rect",nextNumber++);const auto add=[&](double x1,double y1,double x2,double y2,const char* axis){CadSketchEntity entity;entity.id=newId("line",nextNumber++);entity.type="line";entity.groupId=group;entity.x1=x1;entity.y1=y1;entity.x2=x2;entity.y2=y2;sketch->entities.push_back(entity);CadSketchConstraint constraint;constraint.id=newId("constraint",nextNumber++);constraint.type=axis;constraint.firstEntity=entity.id;sketch->constraints.push_back(constraint);return entity.id;};const auto bottom=add(x,y,x+w,y,"horizontal");const auto right=add(x+w,y,x+w,y+h,"vertical");const auto top=add(x+w,y+h,x,y+h,"horizontal");const auto left=add(x,y+h,x,y,"vertical");const auto coincident=[&](const juce::String& a,const juce::String& b){CadSketchConstraint c;c.id=newId("constraint",nextNumber++);c.type="coincident";c.firstEntity=a+":p2";c.secondEntity=b+":p1";sketch->constraints.push_back(c);};coincident(bottom,right);coincident(right,top);coincident(top,left);coincident(left,bottom);CadSketchConstraint width;width.id=newId("constraint",nextNumber++);width.type="distance";width.firstEntity=bottom;width.value=w;sketch->constraints.push_back(width);CadSketchConstraint height;height.id=newId("constraint",nextNumber++);height.type="distance";height.firstEntity=right;height.value=h;sketch->constraints.push_back(height);groupId=group;return solveSketch(*sketch,e);},error); }
bool CadDocument::addSketchCircle(const juce::String& sketchId,double x,double y,double radius,juce::String& entityId,juce::String& error) { return mutate([&](juce::String& e){auto* sketch=mutableSketch(sketchId);if(sketch==nullptr){e="Unknown sketch id: "+sketchId;return false;}if(radius<=0){e="Sketch circle radius must be greater than zero.";return false;}CadSketchEntity entity;entity.id=newId("circle",nextNumber++);entity.type="circle";entity.x1=x;entity.y1=y;entity.radius=radius;sketch->entities.push_back(entity);entityId=entity.id;return solveSketch(*sketch,e);},error); }
bool CadDocument::setSketchEntityDimension(const juce::String& sketchId,const juce::String& entityId,double value,juce::String& error) { return mutate([&](juce::String& e){auto* sketch=mutableSketch(sketchId);if(sketch==nullptr){e="Unknown sketch id: "+sketchId;return false;}if(value<=0||!std::isfinite(value)){e="Sketch dimension must be finite and greater than zero.";return false;}for(auto& entity:sketch->entities)if(entity.id==entityId){bool found=false;for(auto& c:sketch->constraints)if((c.type=="distance"||c.type=="length"||c.type=="radius"||c.type=="diameter")&&c.firstEntity==entityId){c.value=value;found=true;}if(entity.type=="circle"){if(!found){CadSketchConstraint c;c.id=newId("constraint",nextNumber++);c.type="radius";c.firstEntity=entityId;c.value=value;sketch->constraints.push_back(c);}}else if(!found){CadSketchConstraint c;c.id=newId("constraint",nextNumber++);c.type="distance";c.firstEntity=entityId;c.value=value;sketch->constraints.push_back(c);}return solveSketch(*sketch,e);}e="Unknown sketch entity id: "+entityId;return false;},error); }
bool CadDocument::setSketchEntityPosition(const juce::String& sketchId,const juce::String& entityId,double px,double py,juce::String& error) { return mutate([&](juce::String& e){auto* sketch=mutableSketch(sketchId);if(sketch==nullptr){e="Unknown sketch id: "+sketchId;return false;}if(!std::isfinite(px)||!std::isfinite(py)){e="Sketch position must be finite.";return false;}for(auto& entity:sketch->entities)if(entity.id==entityId){const auto dx=entity.x2-entity.x1,dy=entity.y2-entity.y1;entity.x1=px;entity.y1=py;if(entity.type!="circle"){entity.x2=px+dx;entity.y2=py+dy;}return solveSketch(*sketch,e);}e="Unknown sketch entity id: "+entityId;return false;},error); }
bool CadDocument::addSketchConstraint(const juce::String& sketchId,const juce::String& type,const juce::String& first,const juce::String& second,double value,juce::String& constraintId,juce::String& error) { return mutate([&](juce::String& e){auto* sketch=mutableSketch(sketchId);if(sketch==nullptr){e="Unknown sketch id: "+sketchId;return false;}const auto supported=type=="coincident"||type=="horizontal"||type=="vertical"||type=="fixed"||type=="length"||type=="distance"||type=="radius"||type=="diameter";if(!supported){e="Unsupported sketch constraint: "+type;return false;}CadSketchConstraint c;c.id=newId("constraint",nextNumber++);c.type=type;c.firstEntity=first;c.secondEntity=second;c.value=value;sketch->constraints.push_back(c);constraintId=c.id;return solveSketch(*sketch,e);},error); }
bool CadDocument::createExtrude(const juce::String& sketchId,double depth,bool reverse,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){const auto* sketch=findSketch(sketchId);if(sketch==nullptr){e="Unknown sketch id: "+sketchId;return false;}if(depth<=0||!std::isfinite(depth)){e="Extrusion depth must be finite and greater than zero.";return false;}if(!validateSketch(*sketch,e))return false;CadFeature f;f.id=newId("extrude",nextNumber++);f.name="Extrude "+juce::String(nextNumber-1);f.type="extrude";f.sketchId=sketchId;f.height=depth;f.depth=depth;f.reverse=reverse;f.firstOperand=sketchId;items.push_back(f);id=f.id;return true;},error); }
bool CadDocument::createCut(const juce::String& sketchId,const juce::String& target,double depth,bool throughAll,bool reverse,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){const auto* sketch=findSketch(sketchId);const auto* targetFeature=find(target);if(sketch==nullptr||targetFeature==nullptr||targetFeature->shape.IsNull()){e="Cut requires an existing sketch and solid target feature.";return false;}if(depth<=0&&!throughAll){e="Cut depth must be greater than zero unless through-all is selected.";return false;}if(!validateSketch(*sketch,e))return false;CadFeature f;f.id=newId("cut",nextNumber++);f.name="Cut "+juce::String(nextNumber-1);f.type="cut";f.sketchId=sketchId;f.targetFeatureId=target;f.firstOperand=target;f.secondOperand=sketchId;f.height=depth;f.depth=depth;f.reverse=reverse;f.throughAll=throughAll;for(auto& source:items)if(source.id==target)source.visible=false;items.push_back(f);id=f.id;return true;},error); }
bool CadDocument::setPlacement(const juce::String& id,double x,double y,double z,double rx,double ry,double rz,juce::String& error) { return mutate([&](juce::String& e){for(auto& f:items)if(f.id==id){f.x=x;f.y=y;f.z=z;f.rx=rx;f.ry=ry;f.rz=rz;return true;}e="Unknown feature id: "+id;return false;},error); }
bool CadDocument::setDimensions(const juce::String& id,double w,double d,double h,double r,juce::String& error) { return mutate([&](juce::String& e){for(auto& f:items)if(f.id==id){if(f.type=="box"&&(w<=0||d<=0||h<=0)){e="Box dimensions must be greater than zero.";return false;}if(f.type=="cylinder"&&(r<=0||h<=0)){e="Cylinder radius and height must be greater than zero.";return false;}if((f.type=="extrude"||f.type=="cut")&&h<=0){e="Feature depth must be greater than zero.";return false;}f.width=w;f.depth=d;f.height=h;f.radius=r;if(f.type=="extrude"||f.type=="cut")f.depth=h;return true;}e="Unknown feature id: "+id;return false;},error); }
bool CadDocument::booleanOp(const juce::String& kind,const juce::String& first,const juce::String& second,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){if((kind!="union"&&kind!="subtract"&&kind!="intersect")||find(first)==nullptr||find(second)==nullptr||first==second){e="Boolean operation requires two distinct existing features.";return false;}for(const auto* operand:{find(first),find(second)})if(operand->type=="sketch"||operand->shape.IsNull()){e=operand->name+" is not a solid body; Boolean operations need two solid bodies.";return false;}CadFeature f;f.id=newId(kind.toRawUTF8(),nextNumber++);f.name=kind.substring(0,1).toUpperCase()+kind.substring(1)+" "+juce::String(nextNumber-1);f.type=kind;f.firstOperand=first;f.secondOperand=second;for(auto& source:items)if(source.id==first||source.id==second)source.visible=false;items.push_back(f);juce::StringArray active;const auto result=evaluate(f.id,e,active);if(result.IsNull())return false;if(!TopExp_Explorer(result,TopAbs_SOLID).More()){e=f.name+" would produce no solid: the bodies do not overlap.";return false;}id=f.id;return true;},error); }
bool CadDocument::rename(const juce::String& id,const juce::String& name,juce::String& error) { return mutate([&](juce::String& e){for(auto& f:items)if(f.id==id){if(name.trim().isEmpty()){e="Name cannot be empty.";return false;}f.name=name.trim();return true;}e="Unknown feature id: "+id;return false;},error); }
bool CadDocument::setVisible(const juce::String& id,bool visible,juce::String& error) { return mutate([&](juce::String& e){for(auto& f:items)if(f.id==id){f.visible=visible;return true;}e="Unknown feature id: "+id;return false;},error); }
bool CadDocument::remove(const juce::String& id,juce::String& error) { return mutate([&](juce::String& e){for(const auto& instance:mechanics.instances)if(instance.featureId==id){e="Cannot delete a body used by assembly instance "+instance.name+"; delete the instance first.";return false;}for(const auto& f:items)if(f.firstOperand==id||f.secondOperand==id){e="Cannot delete a feature used by a Boolean result.";return false;}const auto old=items.size();items.erase(std::remove_if(items.begin(),items.end(),[&](const CadFeature& f){return f.id==id;}),items.end());if(items.size()==old){e="Unknown feature id: "+id;return false;}return true;},error); }
bool CadDocument::restoreMechanicsFromOCAF(juce::String& error)
{
    const auto text = readString(ocafDocument->Main().FindChild(kMechanicsContainerTag, false), 1);
    if (text.isEmpty()) { mechanics = {}; mechanics.modelId = juce::Uuid().toDashedString(); return true; }
    return mech::fromJson(juce::JSON::parse(text), mechanics, error);
}

// Reference-configuration world frame of an instance (all joints at their reference coordinates).
bool CadDocument::referenceWorld(const juce::String& instanceId, gp_Trsf& world, juce::String& error) const
{
    const auto* instance = mechanics.findInstance(instanceId);
    if (instance == nullptr) { error = "Unknown assembly instance: " + instanceId; return false; }
    world = instance->placement.toTrsf();
    return true;
}

bool CadDocument::jointFramesFromRequest(const CadJointRequest& request, CadJoint& joint, juce::String& error) const
{
    if (request.type != "fixed" && request.type != "revolute" && request.type != "prismatic") { error = "Joint type must be fixed, revolute or prismatic."; return false; }
    if (mechanics.findInstance(request.childInstance) == nullptr) { error = "Choose an existing child instance."; return false; }
    if (request.parentInstance.isNotEmpty() && mechanics.findInstance(request.parentInstance) == nullptr) { error = "Choose an existing parent instance or the world frame."; return false; }
    for (const auto v : { request.originX, request.originY, request.originZ, request.axisX, request.axisY, request.axisZ, request.reference, request.lower, request.upper })
        if (!std::isfinite(v)) { error = "Joint values must be finite numbers."; return false; }
    if (std::sqrt(request.axisX * request.axisX + request.axisY * request.axisY + request.axisZ * request.axisZ) < 1.0e-9) { error = "The joint axis must be a non-zero direction."; return false; }
    if (request.hasLimits && request.lower > request.upper) { error = "The lower limit must not exceed the upper limit."; return false; }
    // Keep the child exactly where it is now at the reference coordinate; the parent frame comes from the current reference pose.
    gp_Trsf childWorld, parentWorld;
    if (!referenceWorld(request.childInstance, childWorld, error)) return false;
    if (request.parentInstance.isNotEmpty() && !referenceWorld(request.parentInstance, parentWorld, error)) return false;
    const auto jointWorld = mech::frameFromOriginAxis(request.originX, request.originY, request.originZ, request.axisX, request.axisY, request.axisZ);
    joint.name = request.name.trim(); joint.type = request.type; joint.parentInstance = request.parentInstance; joint.childInstance = request.childInstance;
    joint.parentToJoint = CadFrame::fromTrsf(parentWorld.Inverted().Multiplied(jointWorld));
    joint.jointToChild = CadFrame::fromTrsf(jointWorld.Inverted().Multiplied(childWorld));
    joint.reference = request.reference; joint.hasLimits = request.hasLimits; joint.lower = request.lower; joint.upper = request.upper;
    return true;
}

bool CadDocument::createInstance(const juce::String& featureId,const juce::String& name,juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto* f = find(featureId);
        if (f == nullptr || f->type == "sketch" || f->shape.IsNull()) { e = "Assembly instances need an existing solid body."; return false; }
        CadMechanicalInstance instance;
        instance.id = newId("inst", nextNumber++);
        instance.name = name.trim().isNotEmpty() ? name.trim() : f->name + " instance";
        instance.featureId = featureId;
        // The instance frame starts at the body's authored placement point, so the rendered body does not move.
        instance.placement.x = f->x; instance.placement.y = f->y; instance.placement.z = f->z;
        mechanics.instances.push_back(instance);
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        id = instance.id;
        return true;
    }, error);
}

bool CadDocument::setInstancePlacement(const juce::String& id,const CadFrame& placement,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& instance : mechanics.instances)
            if (instance.id == id)
            {
                if (!instance.grounded && mechanics.instanceHasJoints(id)) { e = instance.name + " is positioned by its joints; ground it or edit the joints to move it."; return false; }
                const auto norm = std::sqrt(placement.qw * placement.qw + placement.qx * placement.qx + placement.qy * placement.qy + placement.qz * placement.qz);
                if (!std::isfinite(placement.x) || !std::isfinite(placement.y) || !std::isfinite(placement.z) || !std::isfinite(norm) || norm < 1.0e-9) { e = "Instance placement must be finite with a non-zero rotation."; return false; }
                instance.placement = placement;
                instance.placement.qw /= norm; instance.placement.qx /= norm; instance.placement.qy /= norm; instance.placement.qz /= norm;
                ++mechanics.interfaceRevision; ++mechanics.profileRevision;
                return true;
            }
        e = "Unknown assembly instance: " + id;
        return false;
    }, error);
}

bool CadDocument::removeInstance(const juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (const auto& joint : mechanics.joints) if (joint.parentInstance == id || joint.childInstance == id) { e = "Remove joint " + joint.name + " before deleting this instance."; return false; }
        for (const auto& task : mechanics.ikTasks)
            for (const auto& target : task.targets)
                if (target.instanceId == id || target.referenceInstance == id) { e = "IK task " + task.name + " uses this instance in target " + target.name + "; edit or delete that task first."; return false; }
        const auto before = mechanics.instances.size();
        mechanics.instances.erase(std::remove_if(mechanics.instances.begin(), mechanics.instances.end(), [&](const CadMechanicalInstance& i) { return i.id == id; }), mechanics.instances.end());
        if (mechanics.instances.size() == before) { e = "Unknown assembly instance: " + id; return false; }
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        return true;
    }, error);
}

// Renames keep identities, so they never change the interface or profile revision.
bool CadDocument::renameMechanical(const juce::String& id,const juce::String& name,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        if (name.trim().isEmpty()) { e = "Name cannot be empty."; return false; }
        for (auto& instance : mechanics.instances) if (instance.id == id) { instance.name = name.trim(); return true; }
        for (auto& coupling : mechanics.couplings) if (coupling.id == id) { coupling.name = name.trim(); return true; }
        for (auto& joint : mechanics.joints) if (joint.id == id) { joint.name = name.trim(); return true; }
        for (auto& task : mechanics.ikTasks) if (task.id == id) { task.name = name.trim(); return true; }
        e = "Unknown instance or joint: " + id;
        return false;
    }, error);
}

bool CadDocument::createJoint(const CadJointRequest& request,juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        CadJoint joint;
        joint.id = newId("joint", nextNumber++);
        if (!jointFramesFromRequest(request, joint, e)) return false;
        if (joint.name.isEmpty()) joint.name = joint.type.substring(0, 1).toUpperCase() + joint.type.substring(1) + " " + joint.id.fromLastOccurrenceOf("-", false, false);
        mechanics.joints.push_back(joint);
        if (!mech::validateModel(mechanics, e)) return false;
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        id = joint.id;
        return true;
    }, error);
}

bool CadDocument::updateJoint(const juce::String& id,const CadJointRequest& request,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& joint : mechanics.joints)
            if (joint.id == id)
            {
                // Evaluate the new frames against the current reference pose before replacing the joint.
                auto updated = joint;
                if (!jointFramesFromRequest(request, updated, e)) return false;
                if (updated.name.isEmpty()) updated.name = joint.name;
                joint = updated;
                if (!mech::validateModel(mechanics, e)) return false;
                if (const auto* mapping = mechanics.mappingForJoint(id); mapping != nullptr && !mech::quantityDrivesJoint(mapping->sourceQuantity, joint.type))
                    { e = "The saved signal mapping does not fit a " + joint.type + " joint; remove it first."; return false; }
                ++mechanics.interfaceRevision; ++mechanics.profileRevision;
                return true;
            }
        e = "Unknown joint: " + id;
        return false;
    }, error);
}

bool CadDocument::removeJoint(const juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto before = mechanics.joints.size();
        mechanics.joints.erase(std::remove_if(mechanics.joints.begin(), mechanics.joints.end(), [&](const CadJoint& j) { return j.id == id; }), mechanics.joints.end());
        if (mechanics.joints.size() == before) { e = "Unknown joint: " + id; return false; }
        mechanics.mappings.erase(std::remove_if(mechanics.mappings.begin(), mechanics.mappings.end(), [&](const CadSignalMapping& m) { return m.jointId == id; }), mechanics.mappings.end());
        mechanics.couplings.erase(std::remove_if(mechanics.couplings.begin(), mechanics.couplings.end(), [&](const CadCoupling& c) { return c.jointA == id || c.jointB == id; }), mechanics.couplings.end());
        for (auto& task : mechanics.ikTasks) task.adjustableJoints.removeString(id);   // the task stays; it no longer adjusts this joint
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        return true;
    }, error);
}

bool CadDocument::setJointMapping(const CadSignalMapping& mapping,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        if (!mech::validateMapping(mechanics, mapping, e)) return false;
        bool replaced = false;
        for (auto& existing : mechanics.mappings) if (existing.jointId == mapping.jointId) { existing = mapping; replaced = true; }
        if (!replaced) mechanics.mappings.push_back(mapping);
        ++mechanics.profileRevision;
        return true;
    }, error);
}

bool CadDocument::setInstanceGrounded(const juce::String& id,bool grounded,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& instance : mechanics.instances)
            if (instance.id == id) { instance.grounded = grounded; ++mechanics.interfaceRevision; ++mechanics.profileRevision; return true; }
        e = "Unknown assembly instance: " + id;
        return false;
    }, error);
}

bool CadDocument::setJointRole(const juce::String& jointId,const juce::String& role,double lockValue,bool enforceLimits,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& joint : mechanics.joints)
            if (joint.id == jointId)
            {
                if (joint.type == "fixed") { e = joint.name + " is fixed and has no coordinate role."; return false; }
                if (role != "driver" && mechanics.mappingForJoint(jointId) != nullptr) { e = joint.name + " is mapped to a DEWB signal, which makes it a driver; remove the mapping before making it " + role + "."; return false; }
                joint.role = role; joint.lockValue = lockValue; joint.enforceLimits = enforceLimits;
                if (!mech::validateModel(mechanics, e)) return false;
                ++mechanics.interfaceRevision; ++mechanics.profileRevision;
                return true;
            }
        e = "Unknown joint: " + jointId;
        return false;
    }, error);
}

bool CadDocument::createCoupling(const CadCoupling& request,juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        CadCoupling coupling = request;
        coupling.id = newId("coupling", nextNumber++);
        if (coupling.name.trim().isEmpty()) coupling.name = (coupling.type == "rackPinion" ? juce::String("Rack and pinion") : coupling.type.substring(0, 1).toUpperCase() + coupling.type.substring(1)) + " " + coupling.id.fromLastOccurrenceOf("-", false, false);
        if (!mech::validateCoupling(mechanics, coupling, e)) return false;
        mechanics.couplings.push_back(coupling);
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        id = coupling.id;
        return true;
    }, error);
}

bool CadDocument::updateCoupling(const juce::String& id,const CadCoupling& request,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& coupling : mechanics.couplings)
            if (coupling.id == id)
            {
                auto updated = request; updated.id = id;
                if (updated.name.trim().isEmpty()) updated.name = coupling.name;
                if (!mech::validateCoupling(mechanics, updated, e)) return false;
                coupling = updated;
                ++mechanics.interfaceRevision; ++mechanics.profileRevision;
                return true;
            }
        e = "Unknown coupling: " + id;
        return false;
    }, error);
}

bool CadDocument::removeCoupling(const juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto before = mechanics.couplings.size();
        mechanics.couplings.erase(std::remove_if(mechanics.couplings.begin(), mechanics.couplings.end(), [&](const CadCoupling& c) { return c.id == id; }), mechanics.couplings.end());
        if (mechanics.couplings.size() == before) { e = "Unknown coupling: " + id; return false; }
        ++mechanics.interfaceRevision; ++mechanics.profileRevision;
        return true;
    }, error);
}

bool CadDocument::createIkTask(const CadIkTask& request,juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        CadIkTask task = request;
        task.id = newId("iktask", nextNumber++);
        if (task.name.trim().isEmpty()) task.name = "IK task " + task.id.fromLastOccurrenceOf("-", false, false);
        for (auto& target : task.targets) if (target.id.isEmpty()) target.id = newId("iktarget", nextNumber++);
        mechanics.ikTasks.push_back(task);
        if (!mech::validateModel(mechanics, e)) return false;
        id = task.id;
        return true;
    }, error);
}

bool CadDocument::updateIkTask(const juce::String& id,const CadIkTask& request,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& task : mechanics.ikTasks)
            if (task.id == id)
            {
                auto updated = request; updated.id = id;
                if (updated.name.trim().isEmpty()) updated.name = task.name;
                for (auto& target : updated.targets) if (target.id.isEmpty()) target.id = newId("iktarget", nextNumber++);
                task = updated;
                return mech::validateModel(mechanics, e);
            }
        e = "Unknown IK task: " + id;
        return false;
    }, error);
}

bool CadDocument::removeIkTask(const juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto before = mechanics.ikTasks.size();
        mechanics.ikTasks.erase(std::remove_if(mechanics.ikTasks.begin(), mechanics.ikTasks.end(), [&](const CadIkTask& t) { return t.id == id; }), mechanics.ikTasks.end());
        if (mechanics.ikTasks.size() == before) { e = "Unknown IK task: " + id; return false; }
        return true;
    }, error);
}

bool CadDocument::removeJointMapping(const juce::String& jointId,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto before = mechanics.mappings.size();
        mechanics.mappings.erase(std::remove_if(mechanics.mappings.begin(), mechanics.mappings.end(), [&](const CadSignalMapping& m) { return m.jointId == jointId; }), mechanics.mappings.end());
        if (mechanics.mappings.size() == before) { e = "That joint has no signal mapping."; return false; }
        ++mechanics.profileRevision;
        return true;
    }, error);
}

bool CadDocument::solveSketch(CadSketch& sketch,juce::String& error) const
{
    auto entity = [&](const juce::String& id) -> CadSketchEntity* { for (auto& e : sketch.entities) if (e.id == id || id.startsWith(e.id + ":")) return &e; return nullptr; };
    auto endpoint = [&](const juce::String& token, gp_Pnt& point) -> CadSketchEntity* { const auto separator = token.indexOfChar(':'); const auto id = separator < 0 ? token : token.substring(0, separator); auto* e = entity(id); if (e == nullptr) return nullptr; const bool second = token.endsWith(":p2"); point = gp_Pnt(second ? e->x2 : e->x1, second ? e->y2 : e->y1, 0); return e; };
    if (sketch.entities.size() == 4 && std::all_of(sketch.entities.begin(), sketch.entities.end(), [](const auto& e) { return e.type == "line"; }))
    {
        double width = std::hypot(sketch.entities[0].x2 - sketch.entities[0].x1, sketch.entities[0].y2 - sketch.entities[0].y1);
        double height = std::hypot(sketch.entities[1].x2 - sketch.entities[1].x1, sketch.entities[1].y2 - sketch.entities[1].y1);
        for (const auto& c : sketch.constraints) { if (c.firstEntity == sketch.entities[0].id && c.type == "distance") width = c.value; if (c.firstEntity == sketch.entities[1].id && c.type == "distance") height = c.value; }
        if (width <= 0 || height <= 0 || !std::isfinite(width) || !std::isfinite(height)) { error = "Rectangle driving dimensions are invalid."; return false; }
        const auto x = sketch.entities[0].x1, y = sketch.entities[0].y1;
        sketch.entities[0].x2 = x + width; sketch.entities[0].y2 = y;
        sketch.entities[1].x1 = x + width; sketch.entities[1].y1 = y; sketch.entities[1].x2 = x + width; sketch.entities[1].y2 = y + height;
        sketch.entities[2].x1 = x + width; sketch.entities[2].y1 = y + height; sketch.entities[2].x2 = x; sketch.entities[2].y2 = y + height;
        sketch.entities[3].x1 = x; sketch.entities[3].y1 = y + height; sketch.entities[3].x2 = x; sketch.entities[3].y2 = y;
        return true;
    }
    for (int pass = 0; pass < 4; ++pass) for (const auto& c : sketch.constraints)
    {
        if (c.type == "horizontal" || c.type == "vertical") { auto* e = entity(c.firstEntity); if (e == nullptr || e->type != "line") { error = "Constraint references missing line: " + c.firstEntity; return false; } if (c.type == "horizontal") e->y2 = e->y1; else e->x2 = e->x1; }
        else if (c.type == "fixed") { auto* e = entity(c.firstEntity); if (e == nullptr) { error = "Fixed constraint references missing entity: " + c.firstEntity; return false; } e->x1 = 0; e->y1 = 0; }
        else if (c.type == "radius" || c.type == "diameter") { auto* e = entity(c.firstEntity); if (e == nullptr || e->type != "circle" || c.value <= 0) { error = "Invalid circle radius constraint."; return false; } e->radius = c.type == "diameter" ? c.value / 2.0 : c.value; }
        else if (c.type == "length" || c.type == "distance") { auto* e = entity(c.firstEntity); if (e == nullptr || e->type != "line" || c.value <= 0) { error = "Invalid line length constraint."; return false; } const auto dx = e->x2 - e->x1, dy = e->y2 - e->y1, length = std::hypot(dx, dy); if (length < 1.0e-8) { error = "Length constraint references a zero-length line."; return false; } e->x2 = e->x1 + dx * c.value / length; e->y2 = e->y1 + dy * c.value / length; }
        else if (c.type == "coincident") { gp_Pnt first, second; auto* a = endpoint(c.firstEntity, first); auto* b = endpoint(c.secondEntity, second); if (a == nullptr || b == nullptr) { error = "Coincident constraint references missing endpoints."; return false; } if (c.secondEntity.endsWith(":p2")) { b->x2 = first.X(); b->y2 = first.Y(); } else { b->x1 = first.X(); b->y1 = first.Y(); } }
    }
    for (size_t i = 0; i + 3 < sketch.entities.size(); ++i)
        if (sketch.entities[i].type == "line" && !sketch.entities[i].groupId.isEmpty())
        {
            const auto group = sketch.entities[i].groupId;
            std::vector<CadSketchEntity*> rectangle;
            for (auto& e : sketch.entities) if (e.groupId == group && e.type == "line") rectangle.push_back(&e);
            if (rectangle.size() == 4)
            {
                double width = std::hypot(rectangle[0]->x2 - rectangle[0]->x1, rectangle[0]->y2 - rectangle[0]->y1);
                double height = std::hypot(rectangle[1]->x2 - rectangle[1]->x1, rectangle[1]->y2 - rectangle[1]->y1);
                for (const auto& c : sketch.constraints) { if (c.firstEntity == rectangle[0]->id && c.type == "distance") width = c.value; if (c.firstEntity == rectangle[1]->id && c.type == "distance") height = c.value; }
                const auto x = rectangle[0]->x1, y = rectangle[0]->y1;
                rectangle[0]->x2 = x + width; rectangle[0]->y2 = y;
                rectangle[1]->x1 = x + width; rectangle[1]->y1 = y; rectangle[1]->x2 = x + width; rectangle[1]->y2 = y + height;
                rectangle[2]->x1 = x + width; rectangle[2]->y1 = y + height; rectangle[2]->x2 = x; rectangle[2]->y2 = y + height;
                rectangle[3]->x1 = x; rectangle[3]->y1 = y + height; rectangle[3]->x2 = x; rectangle[3]->y2 = y;
            }
            break;
        }
    const auto close = [](double a, double b) { return std::abs(a - b) < 1.0e-7; };
    for (const auto& c : sketch.constraints)
    {
        auto* e = entity(c.firstEntity);
        if ((c.type == "horizontal" && (e == nullptr || !close(e->y1, e->y2))) || (c.type == "vertical" && (e == nullptr || !close(e->x1, e->x2)))) { error = "Sketch orientation constraints are inconsistent: " + c.firstEntity + " (" + juce::String(e != nullptr ? e->x1 : 0.0) + "," + juce::String(e != nullptr ? e->y1 : 0.0) + ")->(" + juce::String(e != nullptr ? e->x2 : 0.0) + "," + juce::String(e != nullptr ? e->y2 : 0.0) + ")"; return false; }
        if (c.type == "coincident") { gp_Pnt a, b; if (endpoint(c.firstEntity, a) == nullptr || endpoint(c.secondEntity, b) == nullptr || a.Distance(b) > 1.0e-7) { error = "Sketch closure constraints are inconsistent."; return false; } }
        if (c.type == "length" || c.type == "distance") if (e == nullptr || std::abs(std::hypot(e->x2 - e->x1, e->y2 - e->y1) - c.value) > 1.0e-6) { error = "Sketch driving dimension could not be satisfied."; return false; }
        if (c.type == "radius" || c.type == "diameter") { const auto expected = c.type == "diameter" ? c.value / 2.0 : c.value; if (e == nullptr || std::abs(e->radius - expected) > 1.0e-7) { error = "Sketch circle dimension could not be satisfied."; return false; } }
    }
    for (const auto& e : sketch.entities) if (e.type == "line" && std::hypot(e.x2 - e.x1, e.y2 - e.y1) < 1.0e-8) { error = "Sketch contains a zero-length line."; return false; }
    return true;
}
bool CadDocument::validateSketch(const CadSketch& sketch,juce::String& error) const {if(sketch.entities.empty()){error="Sketch has no geometry.";return false;}CadSketch copy=sketch;if(!solveSketch(copy,error))return false;std::vector<TopoDS_Face> faces;if(!sketchProfiles(copy,faces,error))return false;if(faces.empty()){error="Sketch has no closed profile.";return false;}return true;}
bool CadDocument::sketchProfiles(const CadSketch& sketch,std::vector<TopoDS_Face>& faces,juce::String& error) const {const auto point=[&](double u,double v){if(sketch.plane=="XZ")return gp_Pnt(u,sketch.offset,v);if(sketch.plane=="YZ")return gp_Pnt(sketch.offset,u,v);return gp_Pnt(u,v,sketch.offset);};const auto normal=[&](){if(sketch.plane=="XZ")return gp_Dir(0,1,0);if(sketch.plane=="YZ")return gp_Dir(1,0,0);return gp_Dir(0,0,1);};std::vector<bool> used(sketch.entities.size(),false);for(size_t i=0;i<sketch.entities.size();++i){const auto& entity=sketch.entities[i];if(entity.type=="circle"){try{const gp_Circ circle(gp_Ax2(point(entity.x1,entity.y1),normal()),entity.radius);const auto edge=BRepBuilderAPI_MakeEdge(circle).Edge();const auto wire=BRepBuilderAPI_MakeWire(edge).Wire();faces.push_back(BRepBuilderAPI_MakeFace(wire).Face());}catch(const Standard_Failure&){error="Unable to build circle profile.";return false;}continue;}if(entity.type!="line"||used[i])continue;std::vector<size_t> loop;loop.push_back(i);used[i]=true;double startX=entity.x1,startY=entity.y1,currentX=entity.x2,currentY=entity.y2;for(size_t count=1;count<=sketch.entities.size();++count){if(std::hypot(currentX-startX,currentY-startY)<1.0e-7)break;size_t next=sketch.entities.size();bool reverse=false;for(size_t j=0;j<sketch.entities.size();++j)if(!used[j]&&sketch.entities[j].type=="line"){const auto& candidate=sketch.entities[j];if(std::hypot(candidate.x1-currentX,candidate.y1-currentY)<1.0e-7){next=j;break;}if(std::hypot(candidate.x2-currentX,candidate.y2-currentY)<1.0e-7){next=j;reverse=true;break;}}if(next==sketch.entities.size())break;used[next]=true;loop.push_back(next);const auto& candidate=sketch.entities[next];currentX=reverse?candidate.x1:candidate.x2;currentY=reverse?candidate.y1:candidate.y2;}if(std::hypot(currentX-startX,currentY-startY)>=1.0e-7){error="Sketch line profile is open; connect all endpoints before creating a solid.";return false;}try{BRepBuilderAPI_MakeWire wire;for(const auto index:loop){const auto& line=sketch.entities[index];const auto p1=point(line.x1,line.y1),p2=point(line.x2,line.y2);wire.Add(BRepBuilderAPI_MakeEdge(p1,p2).Edge());}if(!wire.IsDone()){error="Unable to build sketch wire.";return false;}faces.push_back(BRepBuilderAPI_MakeFace(wire.Wire()).Face());}catch(const Standard_Failure&){error="Unable to build closed sketch profile.";return false;}}return true;}
TopoDS_Shape CadDocument::extrudeSketch(const CadSketch& sketch,double depth,bool reverse,juce::String& error) const {std::vector<TopoDS_Face> faces;if(!sketchProfiles(sketch,faces,error))return{};if(depth<=0){error="Extrusion depth must be greater than zero.";return{};}gp_Vec vector;if(sketch.plane=="XZ")vector=gp_Vec(0,depth,0);else if(sketch.plane=="YZ")vector=gp_Vec(depth,0,0);else vector=gp_Vec(0,0,depth);if(reverse)vector.Reverse();TopoDS_Shape result;for(const auto& face:faces){const auto solid=BRepPrimAPI_MakePrism(face,vector).Shape();if(result.IsNull())result=solid;else result=BRepAlgoAPI_Fuse(result,solid).Shape();}return result;}

TopoDS_Shape CadDocument::place(const TopoDS_Shape& source,const CadFeature& f) const { gp_Trsf t;t.SetTranslation(gp_Vec(f.x,f.y,f.z));TopoDS_Shape result=BRepBuilderAPI_Transform(source,t,true).Shape();if(f.rx!=0){gp_Trsf r;r.SetRotation(gp_Ax1(gp_Pnt(f.x,f.y,f.z),gp::DX()),f.rx*M_PI/180.0);result=BRepBuilderAPI_Transform(result,r,true).Shape();}if(f.ry!=0){gp_Trsf r;r.SetRotation(gp_Ax1(gp_Pnt(f.x,f.y,f.z),gp::DY()),f.ry*M_PI/180.0);result=BRepBuilderAPI_Transform(result,r,true).Shape();}if(f.rz!=0){gp_Trsf r;r.SetRotation(gp_Ax1(gp_Pnt(f.x,f.y,f.z),gp::DZ()),f.rz*M_PI/180.0);result=BRepBuilderAPI_Transform(result,r,true).Shape();}return result; }
TopoDS_Shape CadDocument::evaluate(const juce::String& id,juce::String& error,juce::StringArray& active) { if(active.contains(id)){error="Circular feature dependency detected at "+id;return{};}active.add(id);const auto* f=find(id);if(!f){error="Missing feature dependency: "+id;return{};}try{TopoDS_Shape result;if(f->type=="sketch")result={};else if(f->type=="box")result=BRepPrimAPI_MakeBox(f->width,f->depth,f->height).Shape();else if(f->type=="cylinder")result=BRepPrimAPI_MakeCylinder(f->radius,f->height).Shape();else if(f->type=="extrude"){const auto* sketch=findSketch(f->sketchId.isEmpty()?f->firstOperand:f->sketchId);if(sketch==nullptr){error="Extrusion references a missing sketch.";return{};}result=extrudeSketch(*sketch,f->depth,f->reverse,error);}else if(f->type=="cut"){const auto* sketch=findSketch(f->sketchId.isEmpty()?f->secondOperand:f->sketchId);if(sketch==nullptr){error="Cut references a missing sketch.";return{};}auto target=evaluate(f->targetFeatureId.isEmpty()?f->firstOperand:f->targetFeatureId,error,active);if(target.IsNull())return{};const bool throughAll=f->throughAll||f->depth<=0;double cutDepth=throughAll?0.0:f->depth;if(throughAll){Bnd_Box bounds;BRepBndLib::Add(target,bounds);Standard_Real xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);cutDepth=std::sqrt((xmax-xmin)*(xmax-xmin)+(ymax-ymin)*(ymax-ymin)+(zmax-zmin)*(zmax-zmin))+2.0;}auto tool=extrudeSketch(*sketch,cutDepth,f->reverse,error);if(tool.IsNull())return{};BRepAlgoAPI_Cut operation(target,tool);operation.Build();if(!operation.IsDone()||operation.HasErrors()){error=f->name+": OCCT Boolean operation failed.";return{};}result=operation.Shape();collectBooleanHistory(operation,*f,target,tool,operationHistory);}else{auto a=evaluate(f->firstOperand,error,active);auto b=evaluate(f->secondOperand,error,active);if(a.IsNull()||b.IsNull())return{};if(f->type=="union"){BRepAlgoAPI_Fuse operation(a,b);operation.Build();if(!operation.IsDone()||operation.HasErrors()){error=f->name+": OCCT Boolean operation failed.";return{};}result=operation.Shape();collectBooleanHistory(operation,*f,a,b,operationHistory);}else if(f->type=="subtract"){BRepAlgoAPI_Cut operation(a,b);operation.Build();if(!operation.IsDone()||operation.HasErrors()){error=f->name+": OCCT Boolean operation failed.";return{};}result=operation.Shape();collectBooleanHistory(operation,*f,a,b,operationHistory);}else if(f->type=="intersect"){BRepAlgoAPI_Common operation(a,b);operation.Build();if(!operation.IsDone()||operation.HasErrors()){error=f->name+": OCCT Boolean operation failed.";return{};}result=operation.Shape();collectBooleanHistory(operation,*f,a,b,operationHistory);}else{error="Unsupported feature type: "+f->type;return{};}}active.removeString(id);return f->type=="sketch"?result:place(result,*f);}catch(const Standard_Failure& ex){error="OCCT failed evaluating "+f->name+": "+juce::String(ex.GetMessageString());return{};} }
bool CadDocument::rebuild(juce::String& error) { operationHistory.clear(); for(auto& f:items){juce::StringArray active;f.shape=evaluate(f.id,error,active);if(f.type!="sketch"&&f.shape.IsNull())return false;}return true; }

std::shared_ptr<const CadRenderSnapshot> CadDocument::snapshot() const
{
    if (cachedSnapshot != nullptr && cachedRevision == documentRevision) return cachedSnapshot;
    auto next = std::make_shared<CadRenderSnapshot>();
    next->revision = documentRevision;
    next->documentInstance = instanceId;
    for (const auto& f : items)
        if (f.visible && !f.shape.IsNull())
        {
            BRepMesh_IncrementalMesh mesh(f.shape, 0.1, false, 0.5, true);
            for (TopExp_Explorer ex(f.shape, TopAbs_FACE); ex.More(); ex.Next())
            {
                const auto face = TopoDS::Face(ex.Current());
                TopLoc_Location triangulationLocation, surfaceLocation;
                const auto tri = BRep_Tool::Triangulation(face, triangulationLocation);
                const auto surface = BRep_Tool::Surface(face, surfaceLocation);
                if (tri.IsNull()) continue;

                CadMesh out;
                out.featureId = f.id;
                std::vector<gp_Pnt> points;
                std::vector<gp_Dir> normals;
                std::vector<bool> normalValid;
                points.reserve(static_cast<size_t>(tri->NbNodes()));
                normals.reserve(static_cast<size_t>(tri->NbNodes()));
                normalValid.reserve(static_cast<size_t>(tri->NbNodes()));
                const bool reversed = face.Orientation() == TopAbs_REVERSED;
                const auto transformNormal = [&](gp_Dir normal)
                {
                    if (reversed) normal.Reverse();
                    return normal.Transformed(surfaceLocation.Transformation());
                };

                for (int i = 1; i <= tri->NbNodes(); ++i)
                {
                    const auto point = tri->Node(i).Transformed(triangulationLocation.Transformation());
                    points.push_back(point);
                    bool valid = false;
                    gp_Dir normal(0.0, 0.0, 1.0);
                    if (!surface.IsNull() && tri->HasUVNodes())
                    {
                        const auto uv = tri->UVNode(i);
                        GeomLProp_SLProps properties(surface, uv.X(), uv.Y(), 1, 1.0e-7);
                        if (properties.IsNormalDefined())
                        {
                            normal = transformNormal(properties.Normal());
                            valid = true;
                        }
                    }
                    normals.push_back(normal);
                    normalValid.push_back(valid);
                    out.vertices.insert(out.vertices.end(), {(float) point.X(), (float) point.Y(), (float) point.Z(), (float) normal.X(), (float) normal.Y(), (float) normal.Z()});
                }

                for (int i = 1; i <= tri->NbTriangles(); ++i)
                {
                    int a, b, c;
                    tri->Triangle(i).Get(a, b, c);
                    --a; --b; --c;
                    const auto& pa = points[static_cast<size_t>(a)];
                    const auto& pb = points[static_cast<size_t>(b)];
                    const auto& pc = points[static_cast<size_t>(c)];
                    const auto ab = gp_Vec(pa, pb), ac = gp_Vec(pa, pc);
                    const auto cross = ab.Crossed(ac);
                    if (cross.SquareMagnitude() <= 1.0e-20) continue;
                    const auto normalIndex = normalValid[static_cast<size_t>(a)] ? a : (normalValid[static_cast<size_t>(b)] ? b : c);
                    auto triangleNormal = gp_Dir(cross);
                    if (reversed) triangleNormal.Reverse();
                    const auto hasSurfaceNormal = normalValid[static_cast<size_t>(normalIndex)];
                    const auto& referenceNormal = hasSurfaceNormal ? normals[static_cast<size_t>(normalIndex)] : triangleNormal;
                    if (cross.Dot(gp_Vec(referenceNormal)) < 0.0) std::swap(b, c);
                    if (!hasSurfaceNormal)
                        for (const auto vertex : {a, b, c})
                        {
                            normals[static_cast<size_t>(vertex)] = triangleNormal;
                            normalValid[static_cast<size_t>(vertex)] = true;
                            const auto offset = static_cast<size_t>(vertex) * 6u + 3u;
                            out.vertices[offset] = (float) triangleNormal.X(); out.vertices[offset + 1] = (float) triangleNormal.Y(); out.vertices[offset + 2] = (float) triangleNormal.Z();
                        }
                    out.indices.insert(out.indices.end(), {(std::uint32_t) a, (std::uint32_t) b, (std::uint32_t) c});
                }
                next->meshes.push_back(std::move(out));
            }
        }
    cachedSnapshot = next;
    cachedRevision = documentRevision;
    return cachedSnapshot;
}

juce::String CadDocument::serializeSketches() const {juce::Array<juce::var> list;for(const auto& sketch:sketches){auto object=std::make_unique<juce::DynamicObject>();object->setProperty("id",sketch.id);object->setProperty("plane",sketch.plane);object->setProperty("offset",sketch.offset);juce::Array<juce::var> entities;for(const auto& entity:sketch.entities){auto value=std::make_unique<juce::DynamicObject>();value->setProperty("id",entity.id);value->setProperty("type",entity.type);value->setProperty("groupId",entity.groupId);value->setProperty("x1",entity.x1);value->setProperty("y1",entity.y1);value->setProperty("x2",entity.x2);value->setProperty("y2",entity.y2);value->setProperty("radius",entity.radius);entities.add(juce::var(value.release()));}object->setProperty("entities",juce::var(entities));juce::Array<juce::var> constraints;for(const auto& constraint:sketch.constraints){auto value=std::make_unique<juce::DynamicObject>();value->setProperty("id",constraint.id);value->setProperty("type",constraint.type);value->setProperty("firstEntity",constraint.firstEntity);value->setProperty("secondEntity",constraint.secondEntity);value->setProperty("value",constraint.value);constraints.add(juce::var(value.release()));}object->setProperty("constraints",juce::var(constraints));list.add(juce::var(object.release()));}return juce::JSON::toString(juce::var(list));}
bool CadDocument::deserializeSketches(const juce::String& json,juce::String& error) {const auto parsed=juce::JSON::parse(json);auto* list=parsed.getArray();if(!list){error="Stored sketch data is invalid.";return false;}std::vector<CadSketch> loaded;for(const auto& value:*list){auto* object=value.getDynamicObject();if(!object){error="Stored sketch entry is invalid.";return false;}CadSketch sketch;sketch.id=object->getProperty("id").toString();sketch.plane=object->getProperty("plane").toString();sketch.offset=number(object->getProperty("offset"),0);if(auto* entities=object->getProperty("entities").getArray())for(const auto& entityValue:*entities){auto* entityObject=entityValue.getDynamicObject();if(!entityObject)continue;CadSketchEntity entity;entity.id=entityObject->getProperty("id").toString();entity.type=entityObject->getProperty("type").toString();entity.groupId=entityObject->getProperty("groupId").toString();entity.x1=number(entityObject->getProperty("x1"),0);entity.y1=number(entityObject->getProperty("y1"),0);entity.x2=number(entityObject->getProperty("x2"),0);entity.y2=number(entityObject->getProperty("y2"),0);entity.radius=number(entityObject->getProperty("radius"),1);sketch.entities.push_back(entity);}if(auto* constraints=object->getProperty("constraints").getArray())for(const auto& constraintValue:*constraints){auto* constraintObject=constraintValue.getDynamicObject();if(!constraintObject)continue;CadSketchConstraint constraint;constraint.id=constraintObject->getProperty("id").toString();constraint.type=constraintObject->getProperty("type").toString();constraint.firstEntity=constraintObject->getProperty("firstEntity").toString();constraint.secondEntity=constraintObject->getProperty("secondEntity").toString();constraint.value=number(constraintObject->getProperty("value"),0);sketch.constraints.push_back(constraint);}loaded.push_back(std::move(sketch));}sketches=std::move(loaded);return true;}

bool CadDocument::syncOCAFFromItems(juce::String& error) { if(ocafDocument.IsNull()){error="OCAF document is not initialized.";return false;}const auto root=ocafDocument->Main();writeInteger(root.FindChild(kMetaTag,true),1,nextNumber);const auto features=root.FindChild(kFeatureContainerTag,true);for(TDF_ChildIterator it(features);it.More();it.Next()){const auto label=it.Value();if(readInteger(label,1,0)!=0){const auto oldId=readString(label,2);bool stillPresent=false;for(const auto& f:items)if(f.id==oldId){stillPresent=true;break;}if(!stillPresent)recordDeleted(label);}writeInteger(label,1,0);}for(const auto& f:items){if(f.shape.IsNull()&&f.type!="sketch"){error="Cannot record a null shape for feature: "+f.id;return false;}const auto label=features.FindChild(kFeatureTagBase+featureNumber(f.id),true);writeInteger(label,1,1);writeString(label,2,f.id);writeString(label,3,f.name);writeString(label,4,f.type);writeString(label,5,f.firstOperand);writeString(label,6,f.secondOperand);writeString(label,7,f.sketchId);writeString(label,8,f.targetFeatureId);writeReal(label,10,f.width);writeReal(label,11,f.depth);writeReal(label,12,f.height);writeReal(label,13,f.radius);writeReal(label,14,f.x);writeReal(label,15,f.y);writeReal(label,16,f.z);writeReal(label,17,f.rx);writeReal(label,18,f.ry);writeReal(label,19,f.rz);writeInteger(label,20,f.visible?1:0);writeInteger(label,21,f.reverse?1:0);writeInteger(label,22,f.throughAll?1:0);recordGeneratedOrModified(label,f.shape);}for(const auto& entry:operationHistory){for(TDF_ChildIterator it(features);it.More();it.Next())if(readString(it.Value(),2)==entry.featureId){recordOperationHistory(it.Value(),entry);break;}}const auto refs=root.FindChild(kReferenceContainerTag,true);for(TDF_ChildIterator it(refs);it.More();it.Next())writeInteger(it.Value(),1,0);for(size_t i=0;i<references.size();++i){const auto label=refs.FindChild((int)i+1,true);writeInteger(label,1,1);writeString(label,2,references[i].ownerFeatureId);writeString(label,3,references[i].topologyKind);writeString(label,4,references[i].semanticRole);const auto* owner=find(references[i].ownerFeatureId);if(owner==nullptr)continue;if(references[i].topologyKind=="edge"&&references[i].semanticRole=="box-bottom-x-edge"){TopoDS_Shape edge;if(!representativeBoxEdge(*owner,edge)){error="The supported persistent reference could not be mapped to current document geometry.";return false;}recordGeneratedOrModifiedAt(label.FindChild(kReferenceShapeTag,true).FindChild(kShapeTag,true),edge);}}return true; }
bool CadDocument::syncItemsFromOCAF(juce::String& error) { if(ocafDocument.IsNull()){error="OCAF document is not initialized.";return false;}const auto root=ocafDocument->Main();const auto meta=root.FindChild(kMetaTag,false);const auto features=root.FindChild(kFeatureContainerTag,false);if(features.IsNull()){error="Native CAD document has no feature container.";return false;}nextNumber=readInteger(meta,1,1);items.clear();bool allShapesNamed=true;for(TDF_ChildIterator it(features);it.More();it.Next()){const auto label=it.Value();if(readInteger(label,1,0)==0)continue;CadFeature f;f.id=readString(label,2);f.name=readString(label,3);f.type=readString(label,4);f.firstOperand=readString(label,5);f.secondOperand=readString(label,6);f.sketchId=readString(label,7);f.targetFeatureId=readString(label,8);f.width=readReal(label,10,f.width);f.depth=readReal(label,11,f.depth);f.height=readReal(label,12,f.height);f.radius=readReal(label,13,f.radius);f.x=readReal(label,14,0);f.y=readReal(label,15,0);f.z=readReal(label,16,0);f.rx=readReal(label,17,0);f.ry=readReal(label,18,0);f.rz=readReal(label,19,0);f.visible=readInteger(label,20,1)!=0;f.reverse=readInteger(label,21,0)!=0;f.throughAll=readInteger(label,22,0)!=0;if(f.id.isEmpty()||f.type.isEmpty()){error="Native CAD document contains an invalid feature.";return false;}const auto stored=namedShape(label.FindChild(kShapeTag,false));if((stored.IsNull()||stored->IsEmpty())&&f.type!="sketch"){allShapesNamed=false;}else if(!stored.IsNull()&&!stored->IsEmpty())f.shape=TNaming_Tool::CurrentShape(stored);items.push_back(std::move(f));}references.clear();const auto refs=root.FindChild(kReferenceContainerTag,false);if(!refs.IsNull())for(TDF_ChildIterator it(refs);it.More();it.Next())if(readInteger(it.Value(),1,0)!=0)references.push_back({readString(it.Value(),2),readString(it.Value(),3),readString(it.Value(),4)});if(allShapesNamed)return true;return rebuild(error); }

juce::String CadDocument::serialize() const { auto root=std::make_unique<juce::DynamicObject>();root->setProperty("formatVersion",5);root->setProperty("units","mm");root->setProperty("nextNumber",nextNumber);juce::Array<juce::var> list;for(const auto& f:items){auto o=std::make_unique<juce::DynamicObject>();o->setProperty("id",f.id);o->setProperty("name",f.name);o->setProperty("type",f.type);o->setProperty("firstOperand",f.firstOperand);o->setProperty("secondOperand",f.secondOperand);o->setProperty("sketchId",f.sketchId);o->setProperty("targetFeatureId",f.targetFeatureId);o->setProperty("width",f.width);o->setProperty("depth",f.depth);o->setProperty("height",f.height);o->setProperty("radius",f.radius);o->setProperty("reverse",f.reverse);o->setProperty("throughAll",f.throughAll);o->setProperty("x",f.x);o->setProperty("y",f.y);o->setProperty("z",f.z);o->setProperty("rx",f.rx);o->setProperty("ry",f.ry);o->setProperty("rz",f.rz);o->setProperty("visible",f.visible);list.add(juce::var(o.release()));}root->setProperty("features",juce::var(list));juce::Array<juce::var> refs;for(const auto& r:references){auto o=std::make_unique<juce::DynamicObject>();o->setProperty("ownerFeatureId",r.ownerFeatureId);o->setProperty("topologyKind",r.topologyKind);o->setProperty("semanticRole",r.semanticRole);refs.add(juce::var(o.release()));}root->setProperty("references",juce::var(refs));root->setProperty("sketches",juce::JSON::parse(serializeSketches()));root->setProperty("mechanics",mech::toJson(mechanics));return juce::JSON::toString(juce::var(root.release())); }
bool CadDocument::deserialize(const juce::String& json,juce::String& error) { const auto parsed=juce::JSON::parse(json);auto* root=parsed.getDynamicObject();if(!root){error="CAD document is not valid JSON.";return false;}if(root->getProperty("units").toString()!="mm"){error="Only millimeter CAD documents are supported.";return false;}std::vector<CadFeature> loaded;if(auto* array=root->getProperty("features").getArray())for(const auto& value:*array){auto* o=value.getDynamicObject();if(!o){error="CAD feature entry is invalid.";return false;}CadFeature f;f.id=o->getProperty("id").toString();f.name=o->getProperty("name").toString();f.type=o->getProperty("type").toString();f.firstOperand=o->getProperty("firstOperand").toString();f.secondOperand=o->getProperty("secondOperand").toString();f.sketchId=o->getProperty("sketchId").toString();f.targetFeatureId=o->getProperty("targetFeatureId").toString();f.width=number(o->getProperty("width"),f.width);f.depth=number(o->getProperty("depth"),f.depth);f.height=number(o->getProperty("height"),f.height);f.radius=number(o->getProperty("radius"),f.radius);f.reverse=static_cast<bool>(o->getProperty("reverse"));f.throughAll=static_cast<bool>(o->getProperty("throughAll"));f.x=number(o->getProperty("x"),0);f.y=number(o->getProperty("y"),0);f.z=number(o->getProperty("z"),0);f.rx=number(o->getProperty("rx"),0);f.ry=number(o->getProperty("ry"),0);f.rz=number(o->getProperty("rz"),0);f.visible=static_cast<bool>(o->getProperty("visible"));loaded.push_back(std::move(f));}const auto old=items;const auto oldSketches=sketches;const auto oldMechanicsJson=mechanics;const auto oldRefs=references;const auto oldNext=nextNumber;items=std::move(loaded);references.clear();nextNumber=(int)root->getProperty("nextNumber");if(nextNumber<1)nextNumber=(int)items.size()+1;if(auto* array=root->getProperty("references").getArray())for(const auto& value:*array)if(auto* o=value.getDynamicObject())references.push_back({o->getProperty("ownerFeatureId").toString(),o->getProperty("topologyKind").toString(),o->getProperty("semanticRole").toString()});if(root->hasProperty("mechanics")&&!mech::fromJson(root->getProperty("mechanics"),mechanics,error)){items=old;sketches=oldSketches;mechanics=oldMechanicsJson;references=oldRefs;nextNumber=oldNext;return false;}if(!root->hasProperty("mechanics")){mechanics={};mechanics.modelId=juce::Uuid().toDashedString();}if(root->hasProperty("sketches")&&!deserializeSketches(juce::JSON::toString(root->getProperty("sketches")),error)){mechanics=oldMechanicsJson;items=old;sketches=oldSketches;references=oldRefs;nextNumber=oldNext;return false;}if(!rebuild(error)||!syncOCAFFromItems(error)){items=old;sketches=oldSketches;mechanics=oldMechanicsJson;references=oldRefs;nextNumber=oldNext;rebuild(error);return false;}writeString(ocafDocument->Main().FindChild(kSketchContainerTag,true),1,serializeSketches());writeString(ocafDocument->Main().FindChild(kMechanicsContainerTag,true),1,juce::JSON::toString(mech::toJson(mechanics),true));return true; }
bool CadDocument::saveNative(const juce::File& file,juce::String& error) { if(ocafDocument.IsNull()){error="OCAF document is not initialized.";return false;}const auto encoded=juce::File(file.getFullPathName()+".cbf");encoded.deleteFile();const auto status=ocafApplication->SaveAs(ocafDocument,pathString(encoded));if(status!=PCDM_SS_OK||!encoded.existsAsFile()){error="Unable to write native CAD document: "+file.getFullPathName();return false;}file.deleteFile();if(!encoded.moveFileTo(file)){encoded.deleteFile();error="Unable to finalize native CAD document: "+file.getFullPathName();return false;}return true; }
// The file is staged under a unique name: a document saved in this session stays registered with OCAF under its
// "<file>.cbf" path, and OCAF refuses to retrieve a second document from a path that is already in session.
bool CadDocument::loadNative(const juce::File& file,juce::String& error) { const auto encoded=juce::File(file.getFullPathName()+"."+juce::Uuid().toDashedString().substring(0,8)+".cbf");encoded.deleteFile();if(!file.copyFileTo(encoded)){error="Unable to stage native CAD document: "+file.getFullPathName();return false;}Handle(TDocStd_Document) opened;const auto status=ocafApplication->Open(pathString(encoded),opened);encoded.deleteFile();if(status!=PCDM_RS_OK||opened.IsNull()){error="Unable to open native CAD document: "+file.getFullPathName();return false;}const auto oldDocument=ocafDocument;const auto oldItems=items;const auto oldSketches=sketches;const auto oldRefs=references;const auto oldNext=nextNumber;ocafDocument=opened;const auto sketchData=readString(ocafDocument->Main().FindChild(kSketchContainerTag,false),1);const auto oldMechanics=mechanics;if((!sketchData.isEmpty()&&!deserializeSketches(sketchData,error))||!syncItemsFromOCAF(error)||!restoreMechanicsFromOCAF(error)){ocafDocument=oldDocument;items=oldItems;sketches=oldSketches;mechanics=oldMechanics;references=oldRefs;nextNumber=oldNext;return false;}documentFile=file;dirty=false;ocafDocument->SetUndoLimit(100);ocafDocument->ClearUndos();ocafDocument->ClearRedos();invalidate();return true; }
bool CadDocument::save(const juce::File& file,juce::String& error) { return file.hasFileExtension("djo") ? saveNative(file,error) : exportJson(file,error); }
bool CadDocument::exportJson(const juce::File& file,juce::String& error) const { auto* writable=const_cast<CadDocument*>(this);if(!writable->syncItemsFromOCAF(error))return false;if(!file.replaceWithText(serialize())){error="Unable to write CAD JSON: "+file.getFullPathName();return false;}return true; }
bool CadDocument::load(const juce::File& file,juce::String& error) { if(!loadFile(file,error))return false; instanceId=++documentInstanceCounter; invalidate(); return true; }
bool CadDocument::loadFile(const juce::File& file,juce::String& error) { if(file.hasFileExtension("djo"))return loadNative(file,error);const auto text=file.loadFileAsString();if(text.isEmpty()){error="CAD document is empty or unreadable.";return false;}const auto oldDocument=ocafDocument;const auto oldItems=items;const auto oldRefs=references;const auto oldNext=nextNumber;const auto oldFile=documentFile;const auto oldDirty=dirty;initializeOCAF();if(!deserialize(text,error)){ocafDocument=oldDocument;items=oldItems;references=oldRefs;nextNumber=oldNext;documentFile=oldFile;dirty=oldDirty;invalidate();return false;}ocafDocument->ClearUndos();ocafDocument->ClearRedos();documentFile=file;dirty=false;invalidate();return true; }
bool CadDocument::undo(juce::String& error) { if(!ocafDocument->Undo()){error="Nothing to undo.";return false;}const auto sketchData=readString(ocafDocument->Main().FindChild(kSketchContainerTag,false),1);if((!sketchData.isEmpty()&&!deserializeSketches(sketchData,error))||!syncItemsFromOCAF(error)||!restoreMechanicsFromOCAF(error))return false;dirty=true;invalidate();return true; }
bool CadDocument::redo(juce::String& error) { if(!ocafDocument->Redo()){error="Nothing to redo.";return false;}const auto sketchData=readString(ocafDocument->Main().FindChild(kSketchContainerTag,false),1);if((!sketchData.isEmpty()&&!deserializeSketches(sketchData,error))||!syncItemsFromOCAF(error)||!restoreMechanicsFromOCAF(error))return false;dirty=true;invalidate();return true; }
bool CadDocument::makeRepresentativeReference(const juce::String& featureId,CadPersistentReference& reference,juce::String& error) { const auto* f=find(featureId);if(f==nullptr||f->type!="box"){error="Representative persistent reference currently supports box edges only.";return false;}reference={featureId,"edge","box-bottom-x-edge"};ocafDocument->NewCommand();references.push_back(reference);if(!syncOCAFFromItems(error)){references.pop_back();ocafDocument->AbortCommand();return false;}ocafDocument->CommitCommand();dirty=true;invalidate();return true; }
bool CadDocument::resolveReference(const CadPersistentReference& reference,juce::String& error) const { const auto* f=find(reference.ownerFeatureId);if(f==nullptr){error="Persistent reference owner is missing.";return false;}if(reference.topologyKind!="edge"||reference.semanticRole!="box-bottom-x-edge"||f->type!="box"){error="Persistent reference role is unsupported or ambiguous.";return false;}const auto refs=ocafDocument->Main().FindChild(kReferenceContainerTag,false);if(refs.IsNull()){error="Persistent reference container is missing.";return false;}for(TDF_ChildIterator it(refs);it.More();it.Next()){const auto label=it.Value();if(readInteger(label,1,0)==0||readString(label,2)!=reference.ownerFeatureId||readString(label,3)!=reference.topologyKind||readString(label,4)!=reference.semanticRole)continue;const auto shapeLabel=label.FindChild(kReferenceShapeTag,false).FindChild(kShapeTag,false);const auto stored=namedShape(shapeLabel);if(stored.IsNull()||stored->IsEmpty()){error="Persistent reference has no named topology in the current document.";return false;}const auto edge=TNaming_Tool::CurrentShape(stored);if(edge.IsNull()||edge.ShapeType()!=TopAbs_EDGE){error="Persistent reference resolved to deleted or ambiguous topology.";return false;}for(TopExp_Explorer ex(f->shape,TopAbs_EDGE);ex.More();ex.Next())if(ex.Current().IsSame(edge))return true;error="Persistent reference topology is not present in the current owner shape.";return false;}error="Persistent reference role is not present in the document.";return false; }
bool CadCommandService::newDocument(){model.clear();return true;}
bool CadCommandService::createBox(double w,double d,double h,juce::String& id,juce::String& error){return model.createBox(w,d,h,id,error);}
bool CadCommandService::createCylinder(double r,double h,juce::String& id,juce::String& error){return model.createCylinder(r,h,id,error);}
bool CadCommandService::setPlacement(const juce::String& id,double x,double y,double z,double rx,double ry,double rz,juce::String& error){return model.setPlacement(id,x,y,z,rx,ry,rz,error);}
bool CadCommandService::setDimensions(const juce::String& id,double w,double d,double h,double r,juce::String& error){return model.setDimensions(id,w,d,h,r,error);}
bool CadCommandService::booleanOp(const juce::String& k,const juce::String& a,const juce::String& b,juce::String& id,juce::String& error){return model.booleanOp(k,a,b,id,error);}
}

