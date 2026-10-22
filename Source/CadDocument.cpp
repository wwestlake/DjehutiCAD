#include <cstring>
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
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeShape.hxx>
#include <BRepGProp.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GProp_GProps.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <algorithm>
#include <set>
#include <cmath>

namespace djehuti::cad
{
namespace
{
constexpr int kMetaTag = 1, kFeatureContainerTag = 2, kReferenceContainerTag = 3, kSketchContainerTag = 4, kMechanicsContainerTag = 5, kMaterialsContainerTag = 6, kAnalysisContainerTag = 7, kStockContainerTag = 8, kProjectKitContainerTag = 10, kFeatureTagBase = 1000;
constexpr int kShapeTag = 30, kReferenceShapeTag = 5, kFaceReferenceTagBase = 1000;
constexpr int kGeneratedHistoryTag = 31, kModifiedHistoryTag = 32, kDeletedHistoryTag = 33;
double number(const juce::var& value, double fallback) { return value.isDouble() || value.isInt() || value.isInt64() ? value.toString().getDoubleValue() : fallback; }
TDF_Label field(const TDF_Label& label, int tag) { return label.FindChild(tag, true); }
TDF_Label existingField(const TDF_Label& label, int tag) { return label.FindChild(tag, false); }
void writeString(const TDF_Label& label, int tag, const juce::String& value) { TDataStd_AsciiString::Set(field(label, tag), TCollection_AsciiString(value.toRawUTF8())); }
juce::String readString(const TDF_Label& label, int tag) { Handle(TDataStd_AsciiString) a; const auto child=existingField(label, tag); return !child.IsNull() && child.FindAttribute(TDataStd_AsciiString::GetID(), a) && !a.IsNull() ? juce::String::fromUTF8(a->Get().ToCString()) : juce::String(); }   // stored as UTF-8 bytes by writeString
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
// Encode a persistent edge token from geometric and topological features on a shape
juce::String encodeEdgeToken(const TopoDS_Shape& shape, const TopoDS_Shape& edgeShape, const CadFaceOrigins* origins = nullptr)
{
    if (edgeShape.IsNull() || edgeShape.ShapeType() != TopAbs_EDGE) return {};
    const TopoDS_Edge edge = TopoDS::Edge(edgeShape);
    gp_Pnt p1(0,0,0), p2(0,0,0);
    endpoints(edge, p1, p2);
    // Canonicalize endpoint ordering
    if (p1.X() > p2.X() || (std::abs(p1.X() - p2.X()) < 1e-7 && (p1.Y() > p2.Y() || (std::abs(p1.Y() - p2.Y()) < 1e-7 && p1.Z() > p2.Z()))))
        std::swap(p1, p2);

    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter(), last = curve.LastParameter();
    const gp_Pnt mid = curve.Value(0.5 * (first + last));
    GProp_GProps props;
    BRepGProp::LinearProperties(edge, props);
    const double len = props.Mass();
    const auto kind = curve.GetType() == GeomAbs_Line ? "L" : curve.GetType() == GeomAbs_Circle ? "C" : "E";

    juce::String token = juce::String(kind) + ":"
        + juce::String(len, 3) + ":"
        + juce::String(mid.X(), 3) + "," + juce::String(mid.Y(), 3) + "," + juce::String(mid.Z(), 3) + ":"
        + juce::String(p1.X(), 3) + "," + juce::String(p1.Y(), 3) + "," + juce::String(p1.Z(), 3) + ":"
        + juce::String(p2.X(), 3) + "," + juce::String(p2.Y(), 3) + "," + juce::String(p2.Z(), 3);

    // If origins are available, find adjacent face tokens
    if (origins != nullptr && !shape.IsNull())
    {
        TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
        if (edgeToFaces.Contains(edge))
        {
            const auto& faceList = edgeToFaces.FindFromKey(edge);
            juce::StringArray faceTokens;
            for (TopTools_ListIteratorOfListOfShape it(faceList); it.More(); it.Next())
            {
                for (const auto& [candidateFace, fTokens] : *origins)
                {
                    if (candidateFace.IsSame(it.Value()) && !fTokens.isEmpty())
                    {
                        for (const auto& ft : fTokens)
                            faceTokens.addIfNotAlreadyThere(ft);
                    }
                }
            }
            if (!faceTokens.isEmpty())
            {
                faceTokens.sort(false);
                token << "|faces:" << faceTokens.joinIntoString(",");
            }
        }
    }
    return token;
}

// Resolve an edge on shape matching an edge token
bool resolveEdgeFromToken(const TopoDS_Shape& shape, const juce::String& token, const CadFaceOrigins* origins, TopoDS_Edge& resolvedEdge)
{
    if (shape.IsNull() || token.isEmpty()) return false;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);

    const auto parts = juce::StringArray::fromTokens(token, "|", {});
    const auto geomPart = parts[0];
    juce::StringArray wantedFaces;
    if (parts.size() > 1 && parts[1].startsWith("faces:"))
        wantedFaces = juce::StringArray::fromTokens(parts[1].substring(6), ",", {});

    const auto geomTokens = juce::StringArray::fromTokens(geomPart, ":", {});
    if (geomTokens.size() < 5) return false;
    const double expectedLen = geomTokens[1].getDoubleValue();
    const auto midCoords = juce::StringArray::fromTokens(geomTokens[2], ",", {});
    const auto p1Coords = juce::StringArray::fromTokens(geomTokens[3], ",", {});
    const auto p2Coords = juce::StringArray::fromTokens(geomTokens[4], ",", {});
    if (midCoords.size() < 3 || p1Coords.size() < 3 || p2Coords.size() < 3) return false;
    const gp_Pnt expectedMid(midCoords[0].getDoubleValue(), midCoords[1].getDoubleValue(), midCoords[2].getDoubleValue());
    const gp_Pnt expectedP1(p1Coords[0].getDoubleValue(), p1Coords[1].getDoubleValue(), p1Coords[2].getDoubleValue());
    const gp_Pnt expectedP2(p2Coords[0].getDoubleValue(), p2Coords[1].getDoubleValue(), p2Coords[2].getDoubleValue());

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    if (!wantedFaces.isEmpty() && origins != nullptr)
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

    TopoDS_Edge bestCandidate;
    double bestScore = 1e9;

    for (int i = 1; i <= edges.Extent(); ++i)
    {
        const auto edge = TopoDS::Edge(edges(i));
        gp_Pnt p1(0,0,0), p2(0,0,0);
        if (!endpoints(edge, p1, p2)) continue;
        if (p1.X() > p2.X() || (std::abs(p1.X() - p2.X()) < 1e-7 && (p1.Y() > p2.Y() || (std::abs(p1.Y() - p2.Y()) < 1e-7 && p1.Z() > p2.Z()))))
            std::swap(p1, p2);

        BRepAdaptor_Curve curve(edge);
        const double first = curve.FirstParameter(), last = curve.LastParameter();
        const gp_Pnt mid = curve.Value(0.5 * (first + last));
        GProp_GProps props;
        BRepGProp::LinearProperties(edge, props);
        const double len = props.Mass();

        const double dMid = mid.Distance(expectedMid);
        const double dP1 = p1.Distance(expectedP1);
        const double dP2 = p2.Distance(expectedP2);
        const double dLen = std::abs(len - expectedLen);

        // Distance penalty
        double score = dMid * 2.0 + dP1 + dP2 + dLen;

        // Check face adjacency if available
        int matchedFaces = 0;
        if (!wantedFaces.isEmpty() && edgeToFaces.Contains(edge))
        {
            const auto& faceList = edgeToFaces.FindFromKey(edge);
            for (TopTools_ListIteratorOfListOfShape it(faceList); it.More(); it.Next())
            {
                for (const auto& [candidateFace, fTokens] : *origins)
                {
                    if (candidateFace.IsSame(it.Value()))
                    {
                        for (const auto& wf : wantedFaces)
                            if (fTokens.contains(wf)) { ++matchedFaces; break; }
                    }
                }
            }
        }

        // Exact or near-exact geometric match
        if (dMid < 0.1 && dP1 < 0.1 && dP2 < 0.1)
        {
            if (matchedFaces == wantedFaces.size()) score -= 1000.0;
            else if (matchedFaces > 0) score -= 500.0;
            if (score < bestScore)
            {
                bestScore = score;
                bestCandidate = edge;
            }
        }
        // Topological match: when upstream dimensions change (e.g. depth 40 -> 45),
        // the edge between the same two faces (e.g. top and right) is the exact intended topological edge.
        else if (!wantedFaces.isEmpty() && matchedFaces == wantedFaces.size() && matchedFaces >= 2)
        {
            // Topological match between the exact same faces!
            // Score primarily by topological identity with a tie-breaker on midpoint proximity.
            const double topoScore = -500.0 + dMid;
            if (topoScore < bestScore)
            {
                bestScore = topoScore;
                bestCandidate = edge;
            }
        }
    }

    if (!bestCandidate.IsNull())
    {
        resolvedEdge = bestCandidate;
        return true;
    }
    return false;
}

bool resolveChamferEdge(const TopoDS_Shape& shape,const juce::String& token,const CadFaceOrigins& origins,TopoDS_Edge& edge)
{
    if(!resolveEdgeFromToken(shape,token,&origins,edge))return false;
    const auto pieces=juce::StringArray::fromTokens(token,"|",{});
    if(pieces.size()<2||!pieces[1].startsWith("faces:"))return true;
    const auto wanted=juce::StringArray::fromTokens(pieces[1].substring(6),",",{});if(wanted.size()<2)return true;
    TopTools_IndexedMapOfShape all;TopExp::MapShapes(shape,TopAbs_EDGE,all);int matches=0;
    for(int i=1;i<=all.Extent();++i)
    {
        const auto candidate=juce::StringArray::fromTokens(encodeEdgeToken(shape,all(i),&origins),"|",{});
        if(candidate.size()<2)continue;const auto faces=juce::StringArray::fromTokens(candidate[1].substring(6),",",{});
        bool complete=true;for(const auto& f:wanted)if(!faces.contains(f)){complete=false;break;}if(complete)++matches;
    }
    if(matches<=1)return true;
    // A split face pair no longer uniquely identifies an edge. Only retain an exact geometric match.
    const auto current=juce::StringArray::fromTokens(encodeEdgeToken(shape,edge,&origins),"|",{});
    return !current.isEmpty()&&current[0]==pieces[0];
}

void collectFilletHistory(BRepFilletAPI_MakeFillet& operation, const CadFeature& feature, const TopoDS_Shape& source, std::vector<CadDocument::CadTopologyEvolution>& history)
{
    CadDocument::CadTopologyEvolution entry;
    entry.featureId = feature.id;
    for (TopExp_Explorer ex(source, TopAbs_EDGE); ex.More(); ex.Next())
    {
        const auto original = ex.Current();
        for (TopTools_ListIteratorOfListOfShape it(operation.Generated(original)); it.More(); it.Next()) entry.generated.push_back({original, it.Value()});
        for (TopTools_ListIteratorOfListOfShape it(operation.Modified(original)); it.More(); it.Next()) entry.modified.push_back({original, it.Value()});
        if (operation.IsDeleted(original)) entry.deleted.push_back(original);
    }
    for (TopExp_Explorer ex(source, TopAbs_FACE); ex.More(); ex.Next())
    {
        const auto original = ex.Current();
        for (TopTools_ListIteratorOfListOfShape it(operation.Generated(original)); it.More(); it.Next()) entry.generated.push_back({original, it.Value()});
        for (TopTools_ListIteratorOfListOfShape it(operation.Modified(original)); it.More(); it.Next()) entry.modified.push_back({original, it.Value()});
        if (operation.IsDeleted(original)) entry.deleted.push_back(original);
    }
    if (!entry.generated.empty() || !entry.modified.empty() || !entry.deleted.empty()) history.push_back(std::move(entry));
}

// Face construction origins (see CadFaceOrigins).
void addFaceOrigin(CadFaceOrigins& origins, const TopoDS_Shape& face, const juce::StringArray& tokens)
{
    for (auto& entry : origins) if (entry.first.IsSame(face)) { for (const auto& t : tokens) entry.second.addIfNotAlreadyThere(t); return; }
    origins.push_back({ face, tokens });
}
void addFaceOrigin(CadFaceOrigins& origins, const TopoDS_Shape& face, const juce::String& token) { addFaceOrigin(origins, face, juce::StringArray { token }); }
// Carries origins through an OCCT operation using its history: deleted faces drop out, modified faces pass their
// tokens to every image, untouched faces keep them; only faces present in the result remain.
CadFaceOrigins mapFaceOrigins(BRepBuilderAPI_MakeShape& operation, const CadFaceOrigins& in, const TopoDS_Shape& result)
{
    TopTools_IndexedMapOfShape present; TopExp::MapShapes(result, TopAbs_FACE, present);
    CadFaceOrigins out;
    for (const auto& [face, tokens] : in)
    {
        if (operation.IsDeleted(face)) continue;
        const auto& images = operation.Modified(face);
        if (images.IsEmpty()) { if (present.Contains(face)) addFaceOrigin(out, face, tokens); continue; }
        for (TopTools_ListIteratorOfListOfShape it(images); it.More(); it.Next()) if (it.Value().ShapeType() == TopAbs_FACE && present.Contains(it.Value())) addFaceOrigin(out, it.Value(), tokens);
    }
    return out;
}
gp_Dir outwardNormal(const TopoDS_Face& face)
{
    BRepAdaptor_Surface surface(face, true);
    gp_Dir normal(0, 0, 1);
    if (surface.GetType() == GeomAbs_Plane) normal = surface.Plane().Axis().Direction();
    else { const auto u = 0.5 * (surface.FirstUParameter() + surface.LastUParameter()), v = 0.5 * (surface.FirstVParameter() + surface.LastVParameter()); gp_Pnt p; gp_Vec du, dv; surface.D1(u, v, p, du, dv); const auto n = du.Crossed(dv); if (n.Magnitude() > 1.0e-12) normal = gp_Dir(n); }
    if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
    return normal;
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
void CadDocument::clear() { geometryCache.clear(); instanceId=++documentInstanceCounter; items.clear(); sketches.clear(); mechanics = {}; mechanics.modelId = juce::Uuid().toDashedString(); materials = {}; sheetStocks = {}; analysisCases = {}; kitMetadata.reset(); componentDefinitions.clear(); references.clear(); operationHistory.clear(); nextNumber=1; documentRevision=0; dirty=false; documentFile={}; initializeOCAF(); juce::String ignored; syncOCAFFromItems(ignored); invalidate(); }
juce::String CadDocument::newId(const char* prefix, int number) { return juce::String(prefix)+"-"+juce::String(number); }
const CadFeature* CadDocument::find(const juce::String& id) const { for (const auto& f:items) if (f.id==id) return &f; return nullptr; }
CadSketch* CadDocument::mutableSketch(const juce::String& id) { for (auto& sketch:sketches) if (sketch.id==id) return &sketch; return nullptr; }
const CadSketch* CadDocument::findSketch(const juce::String& id) const { for (const auto& sketch:sketches) if (sketch.id==id) return &sketch; return nullptr; }
void CadDocument::invalidate() { ++documentRevision; cachedSnapshot.reset(); cachedRevision=0; }

bool CadDocument::mutate(std::function<bool(juce::String&)> action, juce::String& error)
{
    const auto oldItems=items; const auto oldSketches=sketches; const auto oldMechanics=mechanics; const auto oldMaterials=materials; const auto oldStocks=sheetStocks; const auto oldAnalysis=analysisCases; const auto oldComponents=componentDefinitions; const auto oldKit=kitMetadata; const auto oldRefs=references; const auto oldNext=nextNumber; ocafDocument->NewCommand();
    if (!action(error)) { items=oldItems; sketches=oldSketches; mechanics=oldMechanics; materials=oldMaterials; sheetStocks=oldStocks; analysisCases=oldAnalysis; componentDefinitions=oldComponents; kitMetadata=oldKit; references=oldRefs; nextNumber=oldNext; ocafDocument->AbortCommand(); return false; }
    if (!stock::validateLibrary(sheetStocks, materials, error) || !rebuild(error) || !syncOCAFFromItems(error)) { items=oldItems; sketches=oldSketches; mechanics=oldMechanics; materials=oldMaterials; sheetStocks=oldStocks; analysisCases=oldAnalysis; componentDefinitions=oldComponents; kitMetadata=oldKit; references=oldRefs; nextNumber=oldNext; ocafDocument->AbortCommand(); rebuild(error); return false; }
    writeString(ocafDocument->Main().FindChild(kSketchContainerTag,true),1,serializeSketches());
    writeString(ocafDocument->Main().FindChild(kMechanicsContainerTag,true),1,juce::JSON::toString(mech::toJson(mechanics),true));
    // Material data lives in its own optional container; documents that never had materials gain one only when
    // they are first given material data, so their stored content is otherwise unchanged.
    if (!materials.empty() || !ocafDocument->Main().FindChild(kMaterialsContainerTag,false).IsNull())
        writeString(ocafDocument->Main().FindChild(kMaterialsContainerTag,true),1,juce::JSON::toString(matl::toJson(materials),true));
    if (!analysisCases.empty() || !ocafDocument->Main().FindChild(kAnalysisContainerTag,false).IsNull())
        writeString(ocafDocument->Main().FindChild(kAnalysisContainerTag,true),1,juce::JSON::toString(analysis::toJson(analysisCases),true));
    if (!sheetStocks.empty() || !ocafDocument->Main().FindChild(kStockContainerTag,false).IsNull())
        writeString(ocafDocument->Main().FindChild(kStockContainerTag,true),1,juce::JSON::toString(stock::toJson(sheetStocks),true));
    if (kitMetadata.has_value() || !ocafDocument->Main().FindChild(kProjectKitContainerTag,false).IsNull())
    {
        if (kitMetadata.has_value())
            writeString(ocafDocument->Main().FindChild(kProjectKitContainerTag,true),1,juce::JSON::toString(kit::toJson(*kitMetadata),true));
    }
    std::erase_if(componentDefinitions,[&](const auto& d){return find(d.featureId)==nullptr;});
    writeComponentsToOCAF();
    ocafDocument->CommitCommand();
    const auto evaluated=items;   // shapes are shared handles; this copies no geometry
    if (!syncItemsFromOCAF(error,&evaluated)) return false;
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

// ---- Interactive sketch editing (docs/SKETCH_EDITING_ARCHITECTURE.md) -------------------------------------------
namespace
{
CadSketchEntity* sketchEntity(CadSketch& sketch, const juce::String& id) { for (auto& e : sketch.entities) if (e.id == id) return &e; return nullptr; }
bool splitPointRef(const juce::String& ref, juce::String& entity, juce::String& which)
{
    const auto colon = ref.indexOfChar(':');
    if (colon <= 0) return false;
    entity = ref.substring(0, colon); which = ref.substring(colon + 1);
    return which == "p1" || which == "p2" || which == "c1" || which == "c2";
}
double& pointX(CadSketchEntity& e, const juce::String& which) { return which == "p1" ? e.x1 : which == "p2" ? e.x2 : which == "c1" ? e.cx1 : e.cx2; }
double& pointY(CadSketchEntity& e, const juce::String& which) { return which == "p1" ? e.y1 : which == "p2" ? e.y2 : which == "c1" ? e.cy1 : e.cy2; }
bool isSegment(const CadSketchEntity& e) { return e.type == "line" || e.type == "bezier"; }

// Every endpoint joined to `start` ("id:p1|p2"): coincident constraints in either direction, transitively, plus
// endpoints at exactly the same position (the profile builder joins those too).
juce::StringArray joinedEndpoints(CadSketch& sketch, const juce::String& start)
{
    juce::StringArray result { start };
    for (int i = 0; i < result.size(); ++i)
    {
        juce::String id, which; if (!splitPointRef(result[i], id, which)) continue;
        auto* e = sketchEntity(sketch, id); if (e == nullptr) continue;
        const auto px = pointX(*e, which), py = pointY(*e, which);
        for (const auto& c : sketch.constraints)
            if (c.type == "coincident") { if (c.firstEntity == result[i]) result.addIfNotAlreadyThere(c.secondEntity); else if (c.secondEntity == result[i]) result.addIfNotAlreadyThere(c.firstEntity); }
        for (const auto& other : sketch.entities)
            if (isSegment(other))
            {
                if (std::hypot(other.x1 - px, other.y1 - py) < 1.0e-7) result.addIfNotAlreadyThere(other.id + ":p1");
                if (std::hypot(other.x2 - px, other.y2 - py) < 1.0e-7) result.addIfNotAlreadyThere(other.id + ":p2");
            }
    }
    return result;
}

// The four lines of a Rectangle group in creation order (bottom, right, top, left), or empty.
std::vector<CadSketchEntity*> rectangleGroup(CadSketch& sketch, const juce::String& group)
{
    std::vector<CadSketchEntity*> lines;
    if (group.isEmpty()) return lines;
    for (auto& e : sketch.entities) if (e.groupId == group && e.type == "line") lines.push_back(&e);
    if (lines.size() != 4) lines.clear();
    return lines;
}
}

bool CadDocument::editSketchEntity(const juce::String& sketchId,const juce::String& entityId,bool move,double px,double py,bool resize,double value,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        auto* sketch = mutableSketch(sketchId);
        if (sketch == nullptr) { e = "Unknown sketch id: " + sketchId; return false; }
        auto* entity = sketchEntity(*sketch, entityId);
        if (entity == nullptr) { e = "Unknown sketch entity id: " + entityId; return false; }
        if (move)
        {
            if (!std::isfinite(px) || !std::isfinite(py)) { e = "Sketch position must be finite."; return false; }
            const auto dx = px - entity->x1, dy = py - entity->y1;
            entity->x1 = px; entity->y1 = py;
            if (entity->type != "circle") { entity->x2 += dx; entity->y2 += dy; }
            if (entity->type == "bezier") { entity->cx1 += dx; entity->cy1 += dy; entity->cx2 += dx; entity->cy2 += dy; }
        }
        if (resize)
        {
            if (value <= 0 || !std::isfinite(value)) { e = "Sketch dimension must be finite and greater than zero."; return false; }
            if (entity->type == "bezier") { e = "A curve has no single dimension; move its points or handles instead."; return false; }
            bool found = false;
            for (auto& c : sketch->constraints) if ((c.type == "distance" || c.type == "length" || c.type == "radius" || c.type == "diameter") && c.firstEntity == entityId) { c.value = value; found = true; }
            if (!found) { CadSketchConstraint c; c.id = newId("constraint", nextNumber++); c.type = entity->type == "circle" ? "radius" : "distance"; c.firstEntity = entityId; c.value = value; sketch->constraints.push_back(c); }
        }
        return solveSketch(*sketch, e);
    }, error);
}

bool CadDocument::moveSketchPoint(const juce::String& sketchId,const juce::String& pointRef,double x,double y,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        auto* sketch = mutableSketch(sketchId);
        if (sketch == nullptr) { e = "Unknown sketch id: " + sketchId; return false; }
        if (!std::isfinite(x) || !std::isfinite(y)) { e = "Sketch position must be finite."; return false; }
        juce::String id, which;
        if (!splitPointRef(pointRef, id, which)) { e = "A sketch point is written <entity>:p1, :p2, :c1 or :c2 (got " + pointRef + ")."; return false; }
        auto* entity = sketchEntity(*sketch, id);
        if (entity == nullptr) { e = "Unknown sketch entity id: " + id; return false; }
        if (which.startsWith("c"))
        {
            if (entity->type != "bezier") { e = id + " has no control points; only curves do."; return false; }
            pointX(*entity, which) = x; pointY(*entity, which) = y;
            return solveSketch(*sketch, e);
        }
        if (!isSegment(*entity)) { e = "Only line and curve endpoints can be moved this way; edit a circle in its panel."; return false; }
        const auto ox = pointX(*entity, which), oy = pointY(*entity, which), dx = x - ox, dy = y - oy;
        const auto joined = joinedEndpoints(*sketch, pointRef);
        // Rectangle corners: reshape the rectangle about its opposite corner, keeping its constraints and updating
        // its width/height dimensions.
        juce::StringArray reshaped;
        for (const auto& ref : joined)
        {
            juce::String rid, rw; if (!splitPointRef(ref, rid, rw)) continue;
            auto* r = sketchEntity(*sketch, rid); if (r == nullptr || r->groupId.isEmpty() || reshaped.contains(r->groupId)) continue;
            auto rect = rectangleGroup(*sketch, r->groupId); if (rect.empty()) continue;
            reshaped.add(r->groupId);
            double minX = rect[0]->x1, maxX = rect[0]->x1, minY = rect[0]->y1, maxY = rect[0]->y1;
            for (auto* l : rect) for (const auto& p : { std::pair<double, double> { l->x1, l->y1 }, { l->x2, l->y2 } }) { minX = std::min(minX, p.first); maxX = std::max(maxX, p.first); minY = std::min(minY, p.second); maxY = std::max(maxY, p.second); }
            const auto oppX = std::abs(ox - minX) < 1.0e-7 ? maxX : minX, oppY = std::abs(oy - minY) < 1.0e-7 ? maxY : minY;
            if (std::abs(x - oppX) < 1.0e-6 || std::abs(y - oppY) < 1.0e-6) { e = "That would collapse the rectangle to zero width or height."; return false; }
            const auto nx = std::min(x, oppX), ny = std::min(y, oppY), w = std::abs(x - oppX), h = std::abs(y - oppY);
            rect[0]->x1 = nx; rect[0]->y1 = ny; rect[0]->x2 = nx + w; rect[0]->y2 = ny;
            rect[1]->x1 = nx + w; rect[1]->y1 = ny; rect[1]->x2 = nx + w; rect[1]->y2 = ny + h;
            rect[2]->x1 = nx + w; rect[2]->y1 = ny + h; rect[2]->x2 = nx; rect[2]->y2 = ny + h;
            rect[3]->x1 = nx; rect[3]->y1 = ny + h; rect[3]->x2 = nx; rect[3]->y2 = ny;
            for (auto& c : sketch->constraints) if (c.type == "distance" || c.type == "length") { if (c.firstEntity == rect[0]->id) c.value = w; else if (c.firstEntity == rect[1]->id) c.value = h; }
        }
        for (const auto& ref : joined)
        {
            juce::String rid, rw; if (!splitPointRef(ref, rid, rw)) continue;
            auto* r = sketchEntity(*sketch, rid); if (r == nullptr || reshaped.contains(r->groupId)) continue;
            pointX(*r, rw) = x; pointY(*r, rw) = y;
            if (r->type == "bezier") { const auto c = rw == "p1" ? juce::String("c1") : juce::String("c2"); pointX(*r, c) += dx; pointY(*r, c) += dy; }   // a handle travels with its endpoint
        }
        if (!solveSketch(*sketch, e)) return false;
        for (const auto& ref : joined)
        {
            juce::String rid, rw; if (!splitPointRef(ref, rid, rw)) continue;
            auto* r = sketchEntity(*sketch, rid);
            if (r != nullptr && std::hypot(pointX(*r, rw) - x, pointY(*r, rw) - y) > 1.0e-6)
            {
                juce::StringArray held;
                for (const auto& c : sketch->constraints) for (const auto& j : joined) if (j.startsWith(c.firstEntity + ":") && c.type != "coincident") held.addIfNotAlreadyThere(c.type + " on " + c.firstEntity);
                e = "The sketch's constraints keep that point from moving there" + (held.isEmpty() ? juce::String(".") : " (" + held.joinIntoString(", ") + ").") + " Change the dimension in the entity panel instead.";
                return false;
            }
        }
        return true;
    }, error);
}

bool CadDocument::insertSketchVertex(const juce::String& sketchId,const juce::String& lineId,double x,double y,juce::String& newLineId,juce::StringArray& removed,juce::String& error)
{
    removed.clear();
    return mutate([&](juce::String& e)
    {
        auto* sketch = mutableSketch(sketchId);
        if (sketch == nullptr) { e = "Unknown sketch id: " + sketchId; return false; }
        auto* line = sketchEntity(*sketch, lineId);
        if (line == nullptr || line->type != "line") { e = "A vertex can be inserted only into a line."; return false; }
        const auto dx = line->x2 - line->x1, dy = line->y2 - line->y1, q = dx * dx + dy * dy;
        if (!std::isfinite(x) || !std::isfinite(y) || q <= 0.0) { e = "Invalid insertion point."; return false; }
        const auto t = ((x - line->x1) * dx + (y - line->y1) * dy) / q;
        const auto length = std::sqrt(q);
        if (t * length < 1.0e-4 || (1.0 - t) * length < 1.0e-4) { e = "The new vertex must lie inside the line, away from its ends."; return false; }
        const auto px = line->x1 + t * dx, py = line->y1 + t * dy;   // on the line: the path is unchanged
        // A split Rectangle edge turns the rectangle into a polygon: the group stops being treated as a rectangle.
        if (line->groupId.isNotEmpty()) { const auto group = line->groupId; for (auto& other : sketch->entities) if (other.groupId == group) other.groupId.clear(); removed.add("rectangle grouping of " + group); }
        CadSketchEntity second; second.id = newId("line", nextNumber++); second.type = "line";
        second.x1 = px; second.y1 = py; second.x2 = line->x2; second.y2 = line->y2;
        line->x2 = px; line->y2 = py;
        // The line's own orientation and length constraints no longer describe either part; remove them openly.
        auto& constraints = sketch->constraints;
        for (auto it = constraints.begin(); it != constraints.end();)
            if (it->firstEntity == lineId && it->type != "coincident") { removed.add(it->type + " (" + it->id + ")"); it = constraints.erase(it); }
            else ++it;
        // Whatever was joined to the old far end is now joined to the new line's far end.
        for (auto& c : constraints) { if (c.firstEntity == lineId + ":p2") c.firstEntity = second.id + ":p2"; if (c.secondEntity == lineId + ":p2") c.secondEntity = second.id + ":p2"; }
        CadSketchConstraint join; join.id = newId("constraint", nextNumber++); join.type = "coincident"; join.firstEntity = lineId + ":p2"; join.secondEntity = second.id + ":p1";
        constraints.push_back(join);
        const auto index = (size_t) std::distance(sketch->entities.data(), line);
        sketch->entities.insert(sketch->entities.begin() + (std::ptrdiff_t) index + 1, second);
        newLineId = second.id;
        return solveSketch(*sketch, e);
    }, error);
}

bool CadDocument::addSketchBezier(const juce::String& sketchId,double x1,double y1,double cx1,double cy1,double cx2,double cy2,double x2,double y2,juce::String& entityId,juce::String& error,const juce::String& firstConnection,const juce::String& secondConnection)
{
    return mutate([&](juce::String& e)
    {
        auto* sketch = mutableSketch(sketchId);
        if (sketch == nullptr) { e = "Unknown sketch id: " + sketchId; return false; }
        for (const auto v : { x1, y1, cx1, cy1, cx2, cy2, x2, y2 }) if (!std::isfinite(v)) { e = "Curve points must be finite."; return false; }
        if (std::hypot(x2 - x1, y2 - y1) < 1.0e-8) { e = "A curve needs two distinct endpoints."; return false; }
        CadSketchEntity curve; curve.id = newId("bezier", nextNumber++); curve.type = "bezier";
        curve.x1 = x1; curve.y1 = y1; curve.cx1 = cx1; curve.cy1 = cy1; curve.cx2 = cx2; curve.cy2 = cy2; curve.x2 = x2; curve.y2 = y2;
        sketch->entities.push_back(curve); entityId = curve.id;
        const auto connect = [&](const juce::String& target, const char* endpoint)
        {
            if (target.isEmpty()) return;
            CadSketchConstraint c; c.id = newId("constraint", nextNumber++); c.type = "coincident"; c.firstEntity = curve.id + endpoint; c.secondEntity = target; sketch->constraints.push_back(c);
        };
        connect(firstConnection, ":p1"); connect(secondConnection, ":p2");
        return solveSketch(*sketch, e);
    }, error);
}

bool CadDocument::smoothSketchJoint(const juce::String& sketchId,const juce::String& bezierId,const juce::String& end,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        auto* sketch = mutableSketch(sketchId);
        if (sketch == nullptr) { e = "Unknown sketch id: " + sketchId; return false; }
        auto* curve = sketchEntity(*sketch, bezierId);
        if (curve == nullptr || curve->type != "bezier") { e = "Smoothing applies to a curve."; return false; }
        if (end != "p1" && end != "p2") { e = "The end must be p1 or p2."; return false; }
        const auto handle = end == "p1" ? juce::String("c1") : juce::String("c2");
        const auto jx = pointX(*curve, end), jy = pointY(*curve, end);
        // The segment joined at that end, and the direction in which it arrives there.
        double tx = 0.0, ty = 0.0;
        for (const auto& ref : joinedEndpoints(*sketch, bezierId + ":" + end))
        {
            juce::String rid, rw; if (!splitPointRef(ref, rid, rw) || rid == bezierId) continue;
            auto* other = sketchEntity(*sketch, rid); if (other == nullptr) continue;
            double fx, fy;
            if (other->type == "bezier") { const auto c = rw == "p1" ? juce::String("c1") : juce::String("c2"); fx = pointX(*other, c); fy = pointY(*other, c); if (std::hypot(jx - fx, jy - fy) < 1.0e-9) { fx = rw == "p1" ? other->x2 : other->x1; fy = rw == "p1" ? other->y2 : other->y1; } }
            else { fx = rw == "p1" ? other->x2 : other->x1; fy = rw == "p1" ? other->y2 : other->y1; }
            tx = jx - fx; ty = jy - fy;
            break;
        }
        const auto n = std::hypot(tx, ty);
        if (n < 1.0e-9) { e = "Nothing is connected at that end of " + bezierId + " to be tangent to."; return false; }
        double length = std::hypot(pointX(*curve, handle) - jx, pointY(*curve, handle) - jy);
        if (length < 1.0e-9) length = std::hypot(curve->x2 - curve->x1, curve->y2 - curve->y1) / 3.0;
        pointX(*curve, handle) = jx + tx / n * length; pointY(*curve, handle) = jy + ty / n * length;
        return solveSketch(*sketch, e);
    }, error);
}

bool CadDocument::createExtrude(const juce::String& sketchId,double depth,bool reverse,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){const auto* sketch=findSketch(sketchId);if(sketch==nullptr){e="Unknown sketch id: "+sketchId;return false;}if(depth<=0||!std::isfinite(depth)){e="Extrusion depth must be finite and greater than zero.";return false;}if(!validateSketch(*sketch,e))return false;CadFeature f;f.id=newId("extrude",nextNumber++);f.name="Extrude "+juce::String(nextNumber-1);f.type="extrude";f.sketchId=sketchId;f.height=depth;f.depth=depth;f.reverse=reverse;f.firstOperand=sketchId;for(auto& source:items)if(source.id==sketchId)source.visible=false;items.push_back(f);id=f.id;return true;},error); }
bool CadDocument::createCut(const juce::String& sketchId,const juce::String& target,double depth,bool throughAll,bool reverse,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){const auto* sketch=findSketch(sketchId);const auto* targetFeature=find(target);if(sketch==nullptr||targetFeature==nullptr||targetFeature->shape.IsNull()){e="Cut requires an existing sketch and solid target feature.";return false;}if(depth<=0&&!throughAll){e="Cut depth must be greater than zero unless through-all is selected.";return false;}if(!validateSketch(*sketch,e))return false;CadFeature f;f.id=newId("cut",nextNumber++);f.name="Cut "+juce::String(nextNumber-1);f.type="cut";f.sketchId=sketchId;f.targetFeatureId=target;f.firstOperand=target;f.secondOperand=sketchId;f.height=depth;f.depth=depth;f.reverse=reverse;f.throughAll=throughAll;for(auto& source:items)if(source.id==target||source.id==sketchId)source.visible=false;items.push_back(f);id=f.id;return true;},error); }
std::shared_ptr<const CadRenderSnapshot> CadDocument::chamferPreview(const juce::var& args,juce::var& result,juce::String& error) const
{
    CadDocument draft;draft.geometryCache=geometryCache;
    if(!draft.deserialize(serialize(),error))return {};
    auto request=juce::JSON::parse(juce::JSON::toString(args,false,17));
    const bool editing=request.getProperty("id","").toString().isNotEmpty();
    request.getDynamicObject()->setProperty("action",editing?"update":"create");
    result=draft.chamferCommand(request);
    if(!(bool)result["ok"]){error=result["error"].toString();return {};}
    const auto candidate=result["id"].toString();GProp_GProps props;BRepGProp::VolumeProperties(draft.find(candidate)->shape,props);
    result.getDynamicObject()->setProperty("preview",true);result.getDynamicObject()->setProperty("committed",false);result.getDynamicObject()->setProperty("volume_mm3",props.Mass());
    if(!editing)result.getDynamicObject()->removeProperty("id");
    auto snapshot=std::make_shared<CadRenderSnapshot>(*draft.snapshot());
    static std::atomic<std::uint64_t> previewRevision{0};snapshot->revision=(std::uint64_t(1)<<63)|++previewRevision;snapshot->documentInstance=instanceId;
    return snapshot;
}

juce::var CadDocument::chamferCommand(const juce::var& args)
{
    auto* reply=new juce::DynamicObject();juce::var result(reply);juce::String error;
    const auto fail=[&](const char* code,const juce::String& text){reply->setProperty("ok",false);reply->setProperty("code",code);reply->setProperty("error",text);return result;};
    if(!args.isObject())return fail("invalid_arguments","Chamfer arguments must be an object.");
    const auto action=args.getProperty("action","get").toString();auto id=args.getProperty("id","").toString();
    if(!juce::StringArray{"create","get","update","delete","preview","edges"}.contains(action))return fail("invalid_arguments","Unknown Chamfer action.");
    if(action=="preview")
    {juce::var details;if(chamferPreview(args,details,error)==nullptr)return fail("modeling_failed",error);return details;}
    if(action=="edges")
    {
        const auto* target=find(args.getProperty("target","").toString());
        if(target==nullptr||target->shape.IsNull())return fail("not_found","Choose an existing solid target.");
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(target->shape,TopAbs_EDGE,edges);juce::Array<juce::var> list;
        for(int i=1;i<=edges.Extent();++i)
        {
            const auto edge=TopoDS::Edge(edges(i));auto* item=new juce::DynamicObject();GProp_GProps props;BRepGProp::LinearProperties(edge,props);BRepAdaptor_Curve curve(edge);
            item->setProperty("index",i-1);item->setProperty("reference",encodeEdgeToken(target->shape,edge,&target->faceOrigins));item->setProperty("length_mm",props.Mass());item->setProperty("curve",curve.GetType()==GeomAbs_Line?"line":curve.GetType()==GeomAbs_Circle?"circle":"curve");
            gp_Pnt a,b;if(endpoints(edge,a,b)){item->setProperty("start",juce::Array<juce::var>{a.X(),a.Y(),a.Z()});item->setProperty("end",juce::Array<juce::var>{b.X(),b.Y(),b.Z()});}list.add(item);
        }
        reply->setProperty("ok",true);reply->setProperty("edges",list);return result;
    }
    const auto* feature=find(id);
    if(action!="create"&&(feature==nullptr||feature->type!="chamfer"))return fail("not_found","Unknown Chamfer feature: "+id);
    if(action=="delete")
    {if(!remove(id,error))return fail("dependency_error",error);reply->setProperty("ok",true);reply->setProperty("id",id);reply->setProperty("deleted",true);return result;}
    if(action=="create"||action=="update")
    {
        const auto params=args.getProperty("parameters",juce::var());if(!params.isObject())return fail("invalid_arguments","Chamfer parameters must be an object.");
        for(const auto& prop:params.getDynamicObject()->getProperties())if(!juce::StringArray{"target","distance","edges"}.contains(prop.name.toString()))return fail("invalid_arguments","Unknown Chamfer parameter: "+prop.name.toString());
        const auto targetId=params.getProperty("target",action=="update"?feature->targetFeatureId:juce::String()).toString();
        const auto* target=find(targetId);
        if(target==nullptr||target->shape.IsNull()||!TopExp_Explorer(target->shape,TopAbs_SOLID).More())return fail("invalid_arguments","Chamfer requires a solid target body.");
        if(targetId==id)return fail("invalid_arguments","A Chamfer cannot use itself as its target.");
        const auto value=params.getProperty("distance",action=="update"?feature->width:1.0);
        if(!(value.isDouble()||value.isInt()||value.isInt64())||!std::isfinite((double)value)||(double)value<=0)return fail("invalid_arguments","Chamfer distance must be finite and greater than zero in millimetres.");
        juce::StringArray tokens=action=="update"?feature->edges:juce::StringArray();
        if(action=="update"&&targetId!=feature->targetFeatureId&&!params.hasProperty("edges"))return fail("invalid_arguments","Select edges again when changing the Chamfer target.");
        if(params.hasProperty("edges"))
        {
            const auto* indices=params["edges"].getArray();if(indices==nullptr||indices->isEmpty())return fail("invalid_arguments","Select at least one edge.");
            TopTools_IndexedMapOfShape edges;TopExp::MapShapes(target->shape,TopAbs_EDGE,edges);tokens.clear();
            for(const auto& index:*indices)
            {
                if(!(index.isInt()||index.isInt64()||index.isDouble())||!std::isfinite((double)index)||(double)index!=std::floor((double)index)||(double)index<0||(double)index>=edges.Extent())return fail("invalid_arguments","Chamfer edge index is invalid for this target.");
                const auto token=encodeEdgeToken(target->shape,edges((int)index+1),&target->faceOrigins);if(token.isEmpty())return fail("invalid_arguments","This edge cannot be referenced.");tokens.addIfNotAlreadyThere(token);
            }
        }
        if(tokens.isEmpty())return fail("invalid_arguments","Select at least one edge.");
        auto created=id;
        const bool ok=mutate([&](juce::String&)
        {
            if(action=="create")
            {CadFeature f;f.id=newId("chamfer",nextNumber++);f.name="Chamfer "+juce::String(nextNumber-1);f.type="chamfer";items.push_back(f);created=f.id;}
            for(auto& f:items)if(f.id==created){f.targetFeatureId=targetId;f.firstOperand=targetId;f.width=(double)value;f.edges=tokens;}
            for(auto& f:items)if(f.id==targetId)f.visible=false;
            return true;
        },error);
        if(!ok)return fail("modeling_failed",error);id=created;feature=find(id);
    }
    auto* params=new juce::DynamicObject();params->setProperty("target",feature->targetFeatureId);params->setProperty("distance",feature->width);
    juce::Array<juce::var> indices,refs;const auto* target=find(feature->targetFeatureId);TopTools_IndexedMapOfShape map;
    if(target)TopExp::MapShapes(target->shape,TopAbs_EDGE,map);
    for(const auto& token:feature->edges)
    {refs.add(token);TopoDS_Edge edge;if(target&&resolveEdgeFromToken(target->shape,token,&target->faceOrigins,edge))indices.add(map.FindIndex(edge)-1);}
    params->setProperty("edges",indices);reply->setProperty("ok",true);reply->setProperty("id",id);reply->setProperty("type","chamfer");reply->setProperty("parameters",params);reply->setProperty("edgeReferences",refs);return result;
}

bool CadDocument::createFillet(const juce::String& target,const std::vector<int>& edgeIndices,double radius,juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto* targetFeature = find(target);
        if (targetFeature == nullptr || targetFeature->shape.IsNull() || targetFeature->type == "sketch")
        {
            e = "Fillet requires an existing solid target body.";
            return false;
        }
        if (radius <= 0 || !std::isfinite(radius))
        {
            e = "Fillet radius must be finite and greater than zero.";
            return false;
        }
        if (edgeIndices.empty())
        {
            e = "Fillet requires at least one selected edge.";
            return false;
        }
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(targetFeature->shape, TopAbs_EDGE, edgeMap);
        juce::StringArray tokens;
        for (const int idx : edgeIndices)
        {
            if (idx < 0 || idx >= edgeMap.Extent())
            {
                e = "Edge index " + juce::String(idx) + " is invalid for " + targetFeature->name + ".";
                return false;
            }
            const auto token = encodeEdgeToken(targetFeature->shape, edgeMap(idx + 1), &targetFeature->faceOrigins);
            if (token.isEmpty())
            {
                e = "Unable to encode edge reference for edge " + juce::String(idx) + ".";
                return false;
            }
            tokens.addIfNotAlreadyThere(token);
        }
        CadFeature f;
        f.id = newId("fillet", nextNumber++);
        f.name = "Fillet " + juce::String(nextNumber - 1);
        f.type = "fillet";
        f.targetFeatureId = target;
        f.firstOperand = target;
        f.radius = radius;
        f.edges = tokens;
        for (auto& source : items)
            if (source.id == target) source.visible = false;
        items.push_back(f);
        juce::StringArray active;
        const auto result = evaluate(f.id, e, active);
        if (result.IsNull()) return false;
        if (!TopExp_Explorer(result, TopAbs_SOLID).More())
        {
            e = f.name + " would produce no solid.";
            return false;
        }
        id = f.id;
        return true;
    }, error);
}
bool CadDocument::setPlacement(const juce::String& id,double x,double y,double z,double rx,double ry,double rz,juce::String& error) { return mutate([&](juce::String& e){for(auto& f:items)if(f.id==id){f.x=x;f.y=y;f.z=z;f.rx=rx;f.ry=ry;f.rz=rz;return true;}e="Unknown feature id: "+id;return false;},error); }
bool CadDocument::setDimensions(const juce::String& id,double w,double d,double h,double r,juce::String& error) { return mutate([&](juce::String& e){for(auto& f:items)if(f.id==id){if(f.type=="box"&&(w<=0||d<=0||h<=0)){e="Box dimensions must be greater than zero.";return false;}if(f.type=="cylinder"&&(r<=0||h<=0)){e="Cylinder radius and height must be greater than zero.";return false;}if((f.type=="extrude"||f.type=="cut")&&h<=0){e="Feature depth must be greater than zero.";return false;}if(f.type=="fillet"&&r<=0){e="Fillet radius must be greater than zero.";return false;}f.width=w;f.depth=d;f.height=h;f.radius=r;if(f.type=="extrude"||f.type=="cut")f.depth=h;return true;}e="Unknown feature id: "+id;return false;},error); }
bool CadDocument::booleanOp(const juce::String& kind,const juce::String& first,const juce::String& second,juce::String& id,juce::String& error) { return mutate([&](juce::String& e){if((kind!="union"&&kind!="subtract"&&kind!="intersect")||find(first)==nullptr||find(second)==nullptr||first==second){e="Boolean operation requires two distinct existing features.";return false;}for(const auto* operand:{find(first),find(second)})if(operand->type=="sketch"||operand->shape.IsNull()){e=operand->name+" is not a solid body; Boolean operations need two solid bodies.";return false;}CadFeature f;f.id=newId(kind.toRawUTF8(),nextNumber++);f.name=kind.substring(0,1).toUpperCase()+kind.substring(1)+" "+juce::String(nextNumber-1);f.type=kind;f.firstOperand=first;f.secondOperand=second;for(auto& source:items)if(source.id==first||source.id==second)source.visible=false;items.push_back(f);juce::StringArray active;const auto result=evaluate(f.id,e,active);if(result.IsNull())return false;if(!TopExp_Explorer(result,TopAbs_SOLID).More()){e=f.name+" would produce no solid: the bodies do not overlap.";return false;}id=f.id;return true;},error); }
bool CadDocument::rename(const juce::String& id,const juce::String& name,juce::String& error) { return mutate([&](juce::String& e){for(auto& f:items)if(f.id==id){if(name.trim().isEmpty()){e="Name cannot be empty.";return false;}f.name=name.trim();return true;}e="Unknown feature id: "+id;return false;},error); }
bool CadDocument::setVisible(const juce::String& id,bool visible,juce::String& error) { return mutate([&](juce::String& e){for(auto& f:items)if(f.id==id){f.visible=visible;return true;}e="Unknown feature id: "+id;return false;},error); }
bool CadDocument::remove(const juce::String& id,juce::String& error) { return mutate([&](juce::String& e){for(const auto& sketch:sketches)if(sketch.supportFeature==id){e="Cannot delete a body that supports "+(find(sketch.id)!=nullptr?find(sketch.id)->name:sketch.id)+"; delete that sketch first.";return false;}for(const auto& instance:mechanics.instances)if(instance.featureId==id){e="Cannot delete a body used by assembly instance "+instance.name+"; delete the instance first.";return false;}for(const auto& f:items)if(f.firstOperand==id||f.secondOperand==id){e="Cannot delete a feature used by another modeling feature.";return false;}const auto old=items.size();items.erase(std::remove_if(items.begin(),items.end(),[&](const CadFeature& f){return f.id==id;}),items.end());if(items.size()==old){e="Unknown feature id: "+id;return false;}auto& assigned=materials.assignments;assigned.erase(std::remove_if(assigned.begin(),assigned.end(),[&](const CadMaterialAssignment& a){return a.featureId==id;}),assigned.end());return true;},error); }
bool CadDocument::restoreMechanicsFromOCAF(juce::String& error)
{
    const auto text = readString(ocafDocument->Main().FindChild(kMechanicsContainerTag, false), 1);
    if (text.isEmpty()) { mechanics = {}; mechanics.modelId = juce::Uuid().toDashedString(); return true; }
    return mech::fromJson(juce::JSON::parse(text), mechanics, error);
}

// Documents without a material container (all documents before materials existed) have no materials.
bool CadDocument::restoreMaterialsFromOCAF(juce::String& error)
{
    const auto container = ocafDocument->Main().FindChild(kMaterialsContainerTag, false);
    const auto text = container.IsNull() ? juce::String() : readString(container, 1);
    if (text.isEmpty()) { materials = {}; return true; }
    return matl::fromJson(juce::JSON::parse(text), materials, error);
}

bool CadDocument::restoreStocksFromOCAF(juce::String& error)
{
    const auto container = ocafDocument->Main().FindChild(kStockContainerTag, false);
    const auto text = container.IsNull() ? juce::String() : readString(container, 1);
    if (text.isEmpty()) { sheetStocks = {}; return true; }
    return stock::fromJson(juce::JSON::parse(text), sheetStocks, materials, error);
}

bool CadDocument::restoreAuthoredFromOCAF(juce::String& error)
{
    // Material and stock definitions must be available to future evaluators during open/undo/redo.
    const auto text = readString(ocafDocument->Main().FindChild(kSketchContainerTag, false), 1);
    if (text.isEmpty()) sketches.clear();
    else if (!deserializeSketches(text, error)) return false;
    return restoreMechanicsFromOCAF(error) && restoreMaterialsFromOCAF(error) && restoreStocksFromOCAF(error)
        && restoreAnalysisFromOCAF(error) && restoreComponentsFromOCAF(error) && restoreKitFromOCAF(error) && syncItemsFromOCAF(error) && materialAssignmentsResolve(error);
}

// Documents without an analysis container (all documents before analysis cases existed) have no cases.
bool CadDocument::restoreAnalysisFromOCAF(juce::String& error)
{
    const auto container = ocafDocument->Main().FindChild(kAnalysisContainerTag, false);
    const auto text = container.IsNull() ? juce::String() : readString(container, 1);
    if (text.isEmpty()) { analysisCases = {}; return true; }
    return analysis::fromJson(juce::JSON::parse(text), analysisCases, error);
}

bool CadDocument::restoreKitFromOCAF(juce::String& error)
{
    const auto container = ocafDocument->Main().FindChild(kProjectKitContainerTag, false);
    const auto text = container.IsNull() ? juce::String() : readString(container, 1);
    if (text.isEmpty()) { kitMetadata.reset(); return true; }
    kit::CadCraftKitMetadata meta;
    if (!kit::fromJson(juce::JSON::parse(text), meta, error)) return false;
    kitMetadata = meta;
    return true;
}

bool CadDocument::setKitMetadata(const kit::CadCraftKitMetadata& meta, juce::String& error)
{
    return mutate([&](juce::String& e){
        kitMetadata = meta;
        return true;
    }, error);
}

bool CadDocument::createAnalysisCase(const CadAnalysisCase& definition, juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        CadAnalysisCase c = definition;
        c.id = "case-" + juce::String(analysisCases.nextNumber); c.revision = 1;
        if (!analysis::validateCase(c, e)) return false;
        if (const auto problems = analysis::referenceProblems(*this, c); !problems.isEmpty()) { e = problems.joinIntoString(" "); return false; }
        ++analysisCases.nextNumber; analysisCases.cases.push_back(c); id = c.id;
        return true;
    }, error);
}

bool CadDocument::updateAnalysisCase(const juce::String& id, const CadAnalysisCase& definition, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& existing : analysisCases.cases)
        {
            if (existing.id != id) continue;
            CadAnalysisCase c = definition;
            c.id = existing.id; c.revision = existing.revision + 1; c.nextItem = juce::jmax(c.nextItem, existing.nextItem);
            if (!analysis::validateCase(c, e)) return false;
            if (const auto problems = analysis::referenceProblems(*this, c); !problems.isEmpty()) { e = problems.joinIntoString(" "); return false; }
            existing = c;
            return true;
        }
        e = "Unknown analysis case: " + id; return false;
    }, error);
}

bool CadDocument::removeAnalysisCase(const juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        auto& list = analysisCases.cases;
        const auto before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const CadAnalysisCase& c) { return c.id == id; }), list.end());
        if (list.size() == before) { e = "Unknown analysis case: " + id; return false; }
        return true;
    }, error);
}

bool CadDocument::materialAssignmentsResolve(juce::String& error) const
{
    for (const auto& a : materials.assignments)
    {
        const auto* f = find(a.featureId);
        if (f == nullptr || f->type == "sketch") { error = "Material " + a.materialId + " is assigned to " + a.featureId + ", which is not a part in this document."; return false; }
    }
    return true;
}

bool CadDocument::createMaterial(const CadMaterial& definition, juce::String& id, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        CadMaterial m = definition;
        while (materials.find("mat-" + juce::String(materials.nextNumber))) ++materials.nextNumber;
        m.id = "mat-" + juce::String(materials.nextNumber); m.revision = 1; m.name = m.name.trim();
        if (!matl::validateMaterial(m, e)) return false;
        ++materials.nextNumber; materials.materials.push_back(m); id = m.id;
        return true;
    }, error);
}

bool CadDocument::updateMaterial(const juce::String& id, const CadMaterial& definition, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        for (auto& m : materials.materials)
        {
            if (m.id != id) continue;
            CadMaterial next = definition;
            next.id = m.id; next.revision = m.revision + 1; next.name = next.name.trim();
            if (!matl::validateMaterial(next, e)) return false;
            m = next;
            return true;
        }
        e = "Unknown material: " + id; return false;
    }, error);
}

bool CadDocument::removeMaterial(const juce::String& id, bool clearAssignments, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        if (materials.find(id) == nullptr) { e = "Unknown material: " + id; return false; }
        const auto stockUsers = sheetStocks.usingMaterial(id);
        if (!stockUsers.isEmpty()) { e = "Material is required by stock: " + stockUsers.joinIntoString(", "); return false; }
        const auto users = materials.featuresUsing(id);
        if (!users.isEmpty() && !clearAssignments)
        {
            juce::StringArray names; for (const auto& f : users) names.add(find(f) != nullptr ? find(f)->name : f);
            e = "Material " + id + " is assigned to " + names.joinIntoString(", ") + ". Clear those assignments first, or delete with clearAssignments.";
            return false;
        }
        auto& list = materials.assignments;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const CadMaterialAssignment& a) { return a.materialId == id; }), list.end());
        materials.materials.erase(std::remove_if(materials.materials.begin(), materials.materials.end(), [&](const CadMaterial& m) { return m.id == id; }), materials.materials.end());
        return true;
    }, error);
}

bool CadDocument::assignMaterial(const juce::String& featureId, const juce::String& materialId, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto* f = find(featureId);
        if (f == nullptr) { e = "Unknown part: " + featureId; return false; }
        if (f->type == "sketch") { e = f->name + " is a sketch; materials are assigned to solid parts."; return false; }
        if (materials.find(materialId) == nullptr) { e = "Unknown material: " + materialId; return false; }
        for (auto& a : materials.assignments) if (a.featureId == featureId) { a.materialId = materialId; return true; }
        materials.assignments.push_back({ featureId, materialId });
        return true;
    }, error);
}

bool CadDocument::clearMaterialAssignment(const juce::String& featureId, juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto* f = find(featureId);
        if (f == nullptr) { e = "Unknown part: " + featureId; return false; }
        auto& list = materials.assignments;
        const auto before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const CadMaterialAssignment& a) { return a.featureId == featureId; }), list.end());
        if (list.size() == before) { e = f->name + " has no material assigned."; return false; }
        return true;
    }, error);
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
                juce::String reevalErr; reevaluateBondedJoints(reevalErr);
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
    // A sketch that is exactly one Rectangle (four lines of one rectangle group). Four free lines are a polygon and
    // must keep their shape, so this applies only to the group the Rectangle tool created.
    if (sketch.entities.size() == 4 && std::all_of(sketch.entities.begin(), sketch.entities.end(), [&](const auto& e) { return e.type == "line" && e.groupId.isNotEmpty() && e.groupId == sketch.entities[0].groupId; }))
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
    for (const auto& e : sketch.entities)
        if (e.type == "bezier")
        {
            for (const auto v : { e.x1, e.y1, e.x2, e.y2, e.cx1, e.cy1, e.cx2, e.cy2 }) if (!std::isfinite(v)) { error = "Sketch curve " + e.id + " has a non-finite point."; return false; }
            if (std::hypot(e.x2 - e.x1, e.y2 - e.y1) < 1.0e-8) { error = "Sketch curve " + e.id + " must have two distinct endpoints."; return false; }
        }
    return true;
}
bool CadDocument::validateSketch(const CadSketch& sketch,juce::String& error) const {if(sketch.entities.empty()){error="Sketch has no geometry.";return false;}CadSketch copy=sketch;if(!solveSketch(copy,error))return false;std::vector<TopoDS_Face> faces;if(!sketchProfiles(copy,originPlaneFrame(copy),faces,error))return false;if(faces.empty()){error="Sketch has no closed profile.";return false;}return true;}

// Origin-plane sketches keep their historical axes and extrusion normal (XZ extrudes along +Y).
CadSketchFrame CadDocument::originPlaneFrame(const CadSketch& sketch)
{
    CadSketchFrame f;
    if (sketch.plane == "XZ") { f.origin = gp_Pnt(0, sketch.offset, 0); f.u = gp_Dir(1, 0, 0); f.v = gp_Dir(0, 0, 1); f.normal = gp_Dir(0, 1, 0); }
    else if (sketch.plane == "YZ") { f.origin = gp_Pnt(sketch.offset, 0, 0); f.u = gp_Dir(0, 1, 0); f.v = gp_Dir(0, 0, 1); f.normal = gp_Dir(1, 0, 0); }
    else { f.origin = gp_Pnt(0, 0, sketch.offset); f.u = gp_Dir(1, 0, 0); f.v = gp_Dir(0, 1, 0); f.normal = gp_Dir(0, 0, 1); }
    return f;
}

bool CadDocument::sketchProfiles(const CadSketch& sketch,const CadSketchFrame& frame,std::vector<TopoDS_Face>& faces,juce::String& error,std::vector<std::pair<TopoDS_Shape,juce::String>>* edgeEntities) const
{
    const auto point=[&](double u,double v){return frame.origin.Translated(gp_Vec(frame.u)*u+gp_Vec(frame.v)*v);};
    const auto bezierCurve=[&](const CadSketchEntity& e){TColgp_Array1OfPnt poles(1,4);poles(1)=point(e.x1,e.y1);poles(2)=point(e.cx1,e.cy1);poles(3)=point(e.cx2,e.cy2);poles(4)=point(e.x2,e.y2);return Handle(Geom_Curve)(new Geom_BezierCurve(poles));};
    // Profile edges are named by the sketch entity they come from, so extruded side faces get stable names.
    const auto nameEdges=[&](const TopoDS_Face& face)
    {
        if(edgeEntities==nullptr)return;
        for(TopExp_Explorer ex(face,TopAbs_EDGE);ex.More();ex.Next())
        {
            BRepAdaptor_Curve curve(TopoDS::Edge(ex.Current()));
            const auto mid=curve.Value(0.5*(curve.FirstParameter()+curve.LastParameter()));
            for(const auto& entity:sketch.entities)
            {
                bool on=false;
                if(entity.type=="circle")on=std::abs(mid.Distance(point(entity.x1,entity.y1))-entity.radius)<1.0e-6;
                else if(entity.type=="bezier"){GeomAPI_ProjectPointOnCurve projection(mid,bezierCurve(entity));on=projection.NbPoints()>0&&projection.LowerDistance()<1.0e-6;}
                else{const auto a=point(entity.x1,entity.y1),b=point(entity.x2,entity.y2);const gp_Vec ab(a,b),am(a,mid);const auto t=juce::jlimit(0.0,1.0,ab.SquareMagnitude()>0?am.Dot(ab)/ab.SquareMagnitude():0.0);on=mid.Distance(a.Translated(ab*t))<1.0e-6;}
                if(on){edgeEntities->push_back({ex.Current(),entity.id});break;}
            }
        }
    };
    // Lines and Bezier curves are chained end to end (positions within 1e-7 mm) into closed wires; circles are
    // profiles on their own. A Bezier becomes a genuine OCCT Geom_BezierCurve edge.
    const auto segment=[](const CadSketchEntity& e){return e.type=="line"||e.type=="bezier";};
    std::vector<bool> used(sketch.entities.size(),false);
    for(size_t i=0;i<sketch.entities.size();++i)
    {
        const auto& entity=sketch.entities[i];
        if(entity.type=="circle")
        {
            try{const gp_Circ circle(gp_Ax2(point(entity.x1,entity.y1),frame.normal,frame.u),entity.radius);const auto edge=BRepBuilderAPI_MakeEdge(circle).Edge();const auto wire=BRepBuilderAPI_MakeWire(edge).Wire();faces.push_back(BRepBuilderAPI_MakeFace(wire).Face());nameEdges(faces.back());}
            catch(const Standard_Failure&){error="Unable to build circle profile.";return false;}
            continue;
        }
        if(!segment(entity)||used[i])continue;
        std::vector<size_t> loop;loop.push_back(i);used[i]=true;
        double startX=entity.x1,startY=entity.y1,currentX=entity.x2,currentY=entity.y2;
        for(size_t count=1;count<=sketch.entities.size();++count)
        {
            if(std::hypot(currentX-startX,currentY-startY)<1.0e-7)break;
            size_t next=sketch.entities.size();bool reverse=false;
            for(size_t j=0;j<sketch.entities.size();++j)if(!used[j]&&segment(sketch.entities[j])){const auto& candidate=sketch.entities[j];if(std::hypot(candidate.x1-currentX,candidate.y1-currentY)<1.0e-7){next=j;break;}if(std::hypot(candidate.x2-currentX,candidate.y2-currentY)<1.0e-7){next=j;reverse=true;break;}}
            if(next==sketch.entities.size())break;
            used[next]=true;loop.push_back(next);const auto& candidate=sketch.entities[next];currentX=reverse?candidate.x1:candidate.x2;currentY=reverse?candidate.y1:candidate.y2;
        }
        if(std::hypot(currentX-startX,currentY-startY)>=1.0e-7){error="Sketch line profile is open; connect all endpoints before creating a solid.";return false;}
        try
        {
            BRepBuilderAPI_MakeWire wire;
            for(const auto index:loop)
            {
                const auto& item=sketch.entities[index];
                if(item.type=="bezier"){TColgp_Array1OfPnt poles(1,4);poles(1)=point(item.x1,item.y1);poles(2)=point(item.cx1,item.cy1);poles(3)=point(item.cx2,item.cy2);poles(4)=point(item.x2,item.y2);wire.Add(BRepBuilderAPI_MakeEdge(Handle(Geom_BezierCurve)(new Geom_BezierCurve(poles))).Edge());}
                else wire.Add(BRepBuilderAPI_MakeEdge(point(item.x1,item.y1),point(item.x2,item.y2)).Edge());
                if(!wire.IsDone()){error="Unable to join sketch segment "+item.id+" into the profile.";return false;}
            }
            BRepBuilderAPI_MakeFace face(wire.Wire(),Standard_True);
            if(!face.IsDone()){error="The closed sketch profile could not be turned into a planar face (check for self-intersecting curves).";return false;}
            faces.push_back(face.Face());nameEdges(faces.back());
        }
        catch(const Standard_Failure&){error="Unable to build closed sketch profile.";return false;}
    }
    return true;
}

TopoDS_Shape CadDocument::extrudeSketch(const CadSketch& sketch,const CadSketchFrame& frame,double depth,bool reverse,juce::String& error,CadFaceOrigins* origins,const juce::String& owner) const
{
    std::vector<TopoDS_Face> faces;std::vector<std::pair<TopoDS_Shape,juce::String>> edgeEntities;
    if(!sketchProfiles(sketch,frame,faces,error,origins!=nullptr?&edgeEntities:nullptr))return{};
    if(depth<=0){error="Extrusion depth must be greater than zero.";return{};}
    gp_Vec vector=gp_Vec(frame.normal)*depth;if(reverse)vector.Reverse();
    TopoDS_Shape result;CadFaceOrigins resultOrigins;
    for(const auto& face:faces)
    {
        BRepPrimAPI_MakePrism prism(face,vector);const auto solid=prism.Shape();
        CadFaceOrigins local;
        if(origins!=nullptr)
        {
            addFaceOrigin(local,prism.FirstShape(),owner+"/start");addFaceOrigin(local,prism.LastShape(),owner+"/end");
            for(TopExp_Explorer ex(face,TopAbs_EDGE);ex.More();ex.Next())
                for(const auto& [edge,entityId]:edgeEntities)
                    if(edge.IsSame(ex.Current()))
                        for(TopTools_ListIteratorOfListOfShape it(prism.Generated(ex.Current()));it.More();it.Next())if(it.Value().ShapeType()==TopAbs_FACE)addFaceOrigin(local,it.Value(),owner+"/side:"+entityId);
        }
        if(result.IsNull()){result=solid;resultOrigins=local;continue;}
        BRepAlgoAPI_Fuse fuse(result,solid);fuse.Build();if(!fuse.IsDone()){error="Unable to combine sketch profiles.";return{};}
        const auto fused=fuse.Shape();
        if(origins!=nullptr){CadFaceOrigins both=resultOrigins;for(const auto& o:local)addFaceOrigin(both,o.first,o.second);resultOrigins=mapFaceOrigins(fuse,both,fused);}
        result=fused;
    }
    if(origins!=nullptr)*origins=resultOrigins;
    return result;
}

gp_Trsf CadDocument::placement(const CadFeature& f) const
{
    gp_Trsf t;t.SetTranslation(gp_Vec(f.x,f.y,f.z));
    const gp_Pnt p(f.x,f.y,f.z);
    gp_Trsf rx,ry,rz;rx.SetRotation(gp_Ax1(p,gp::DX()),f.rx*M_PI/180.0);ry.SetRotation(gp_Ax1(p,gp::DY()),f.ry*M_PI/180.0);rz.SetRotation(gp_Ax1(p,gp::DZ()),f.rz*M_PI/180.0);
    return rz.Multiplied(ry).Multiplied(rx).Multiplied(t);
}

TopoDS_Shape CadDocument::place(const TopoDS_Shape& source,const CadFeature& f,CadFaceOrigins* origins) const
{
    const auto apply=[&](const TopoDS_Shape& shape,const gp_Trsf& transform)
    {
        BRepBuilderAPI_Transform operation(shape,transform,true);const auto out=operation.Shape();
        if(origins!=nullptr){CadFaceOrigins mapped;for(const auto& [face,tokens]:*origins){const auto image=operation.ModifiedShape(face);if(!image.IsNull())addFaceOrigin(mapped,image,tokens);}*origins=mapped;}
        return out;
    };
    gp_Trsf t;t.SetTranslation(gp_Vec(f.x,f.y,f.z));TopoDS_Shape result=apply(source,t);
    if(f.rx!=0){gp_Trsf r;r.SetRotation(gp_Ax1(gp_Pnt(f.x,f.y,f.z),gp::DX()),f.rx*M_PI/180.0);result=apply(result,r);}
    if(f.ry!=0){gp_Trsf r;r.SetRotation(gp_Ax1(gp_Pnt(f.x,f.y,f.z),gp::DY()),f.ry*M_PI/180.0);result=apply(result,r);}
    if(f.rz!=0){gp_Trsf r;r.SetRotation(gp_Ax1(gp_Pnt(f.x,f.y,f.z),gp::DZ()),f.rz*M_PI/180.0);result=apply(result,r);}
    return result;
}

// Exactly one current face must carry the token; a missing, split or curved support is an error, never a guess.
bool CadDocument::supportFace(const CadFeature& feature,const juce::String& token,TopoDS_Face& face,juce::String& error) const
{
    std::vector<TopoDS_Shape> matches;
    for(const auto& [candidate,tokens]:feature.faceOrigins)if(tokens.contains(token))matches.push_back(candidate);
    if(matches.empty()){error="face "+token+" of "+feature.name+" no longer exists";return false;}
    if(matches.size()>1){error="face "+token+" of "+feature.name+" is now split into "+juce::String((int)matches.size())+" faces, so the attachment is ambiguous";return false;}
    face=TopoDS::Face(matches.front());
    if(BRepAdaptor_Surface(face,true).GetType()!=GeomAbs_Plane){error="face "+token+" of "+feature.name+" is not planar";return false;}
    return true;
}

// The frame of a face sketch: outward face normal; U is the first axis of the face's generating feature (X, then
// Y, then Z of its placed frame) that is not nearly normal to the face, projected into the plane; V = N x U;
// origin is that feature's placement origin projected onto the plane. Parameter edits move the plane with the
// face without flipping or sliding the in-plane axes.
bool CadDocument::resolveSketchFrame(const CadSketch& sketch,CadSketchFrame& frame,juce::String& error,juce::StringArray& active)
{
    if(!sketch.onFace()){frame=originPlaneFrame(sketch);return true;}
    const auto label=find(sketch.id)!=nullptr?find(sketch.id)->name:sketch.id;
    const auto* support=find(sketch.supportFeature);
    if(support==nullptr){error=label+" is attached to "+sketch.supportFeature+", which no longer exists. The attachment was not moved to another face.";return false;}
    CadFeature evaluated=*support;juce::String inner;
    evaluated.shape=evaluate(sketch.supportFeature,inner,active,&evaluated.faceOrigins);
    if(evaluated.shape.IsNull()){error=inner.isNotEmpty()?inner:label+": its supporting body could not be evaluated.";return false;}
    TopoDS_Face face;
    if(!supportFace(evaluated,sketch.supportFace,face,inner)){error=label+": its support "+inner+". The attachment was not moved to another face; undo the change or reattach the sketch.";return false;}
    const auto plane=BRepAdaptor_Surface(face,true).Plane();
    gp_Dir normal=plane.Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    const auto* generator=find(sketch.supportFace.upToFirstOccurrenceOf("/",false,false));
    const auto trsf=generator!=nullptr?placement(*generator):gp_Trsf();
    gp_Dir u(1,0,0);bool found=false;
    for(const auto& axis:{gp_Dir(1,0,0),gp_Dir(0,1,0),gp_Dir(0,0,1)})
    {
        const auto a=gp_Vec(axis.Transformed(trsf));
        const auto projected=a-gp_Vec(normal)*a.Dot(gp_Vec(normal));
        if(std::abs(a.Dot(gp_Vec(normal)))<0.9&&projected.Magnitude()>1.0e-9){u=gp_Dir(projected);found=true;break;}
    }
    if(!found){error=label+": no stable in-plane axis for its support face.";return false;}
    const auto anchor=gp_Pnt(0,0,0).Transformed(trsf);
    const auto distance=gp_Vec(plane.Location(),anchor).Dot(gp_Vec(normal));
    frame.origin=anchor.Translated(gp_Vec(normal)*-distance);frame.normal=normal;frame.u=u;frame.v=normal.Crossed(u);
    return true;
}

namespace
{
juce::String exact(double v) { juce::int64 bits; std::memcpy(&bits, &v, sizeof bits); return juce::String::toHexString(bits); }
}

juce::String CadDocument::sketchSignature(const juce::String& sketchId,juce::StringArray& visiting) const
{
    const auto* s=findSketch(sketchId);
    if(s==nullptr)return "nosketch:"+sketchId;
    juce::String sig="S:"+s->id+"|"+s->plane+"|"+exact(s->offset)+"|"+s->supportFeature+"|"+s->supportFace;
    for(const auto& e:s->entities){sig<<"|e:"<<e.id<<","<<e.type<<","<<e.groupId<<","<<exact(e.x1)<<","<<exact(e.y1)<<","<<exact(e.x2)<<","<<exact(e.y2)<<","<<exact(e.radius);if(e.type=="bezier")sig<<",c"<<exact(e.cx1)<<","<<exact(e.cy1)<<","<<exact(e.cx2)<<","<<exact(e.cy2);}
    for(const auto& c:s->constraints)sig<<"|c:"<<c.id<<","<<c.type<<","<<c.firstEntity<<","<<c.secondEntity<<","<<exact(c.value);
    if(s->onFace())sig<<"|support("<<geometrySignature(s->supportFeature,visiting)<<")";
    return sig;
}

juce::String CadDocument::geometrySignature(const juce::String& id,juce::StringArray& visiting) const
{
    if(signaturePass!=nullptr)if(const auto hit=signaturePass->find(id);hit!=signaturePass->end())return hit->second;
    if(visiting.contains(id))return "cycle:"+id;   // evaluate() reports the cycle; this signature never matches a cached one
    const auto* f=find(id);
    if(f==nullptr)return "missing:"+id;
    visiting.add(id);
    juce::String sig="F:"+f->id+"|"+f->type+"|"+exact(f->width)+"|"+exact(f->depth)+"|"+exact(f->height)+"|"+exact(f->radius)+"|"+exact(f->x)+"|"+exact(f->y)+"|"+exact(f->z)
                    +"|"+exact(f->rx)+"|"+exact(f->ry)+"|"+exact(f->rz)+"|"+(f->reverse?"r":"-")+(f->throughAll?"t":"-")+"|"+f->firstOperand+"|"+f->secondOperand+"|"+f->sketchId+"|"+f->targetFeatureId;
    if(f->type=="component"){if(const auto* record=componentFor(f->id))sig<<"|component:"<<components::digest(juce::JSON::toString(*record));}
    else if(f->type=="revolve"){sig<<"|revolve:"<<f->revolve.axisType<<"|"<<exact(f->revolve.angleDegrees)<<"|"<<(f->revolve.reverse?"reverse":"forward");for(const auto v:f->revolve.origin)sig<<"|"<<exact(v);for(const auto v:f->revolve.direction)sig<<"|"<<exact(v);sig<<"|("<<sketchSignature(f->sketchId,visiting)<<")";}
    else if(f->type=="extrude")sig<<"|("<<sketchSignature(f->sketchId.isEmpty()?f->firstOperand:f->sketchId,visiting)<<")";
    else if(f->type=="cut")sig<<"|("<<geometrySignature(f->targetFeatureId.isEmpty()?f->firstOperand:f->targetFeatureId,visiting)<<")("<<sketchSignature(f->sketchId.isEmpty()?f->secondOperand:f->sketchId,visiting)<<")";
    else if(f->type=="fillet"||f->type=="chamfer")sig<<"|("<<geometrySignature(f->targetFeatureId.isEmpty()?f->firstOperand:f->targetFeatureId,visiting)<<")|edges:"<<f->edges.joinIntoString(";");
    else if(f->type=="union"||f->type=="subtract"||f->type=="intersect")sig<<"|("<<geometrySignature(f->firstOperand,visiting)<<")("<<geometrySignature(f->secondOperand,visiting)<<")";
    visiting.removeString(id);
    if(signaturePass!=nullptr)(*signaturePass)[id]=sig;
    return sig;
}

TopoDS_Shape CadDocument::evaluate(const juce::String& id,juce::String& error,juce::StringArray& active,CadFaceOrigins* origins)
{
    if(evaluationPass!=nullptr)
        if(const auto hit=evaluationPass->find(id);hit!=evaluationPass->end()){if(origins!=nullptr)*origins=hit->second.origins;return hit->second.shape;}
    juce::String signature;
    const auto historyStart=operationHistory.size();
    if(!active.contains(id))
    {
        juce::StringArray visiting;signature=geometrySignature(id,visiting);
        if(const auto cached=geometryCache.find(id);cached!=geometryCache.end()&&cached->second.signature==signature)
        {
            // Unchanged definition and dependencies: reuse the result. Inside a rebuild pass its Boolean history is
            // replayed too, so OCAF naming sees every operation exactly once.
            if(origins!=nullptr)*origins=cached->second.origins;
            if(evaluationPass!=nullptr)
            {
                operationHistory.insert(operationHistory.end(),cached->second.history.begin(),cached->second.history.end());
                (*evaluationPass)[id]={cached->second.shape,cached->second.origins};
            }
            return cached->second.shape;
        }
    }
    if(active.contains(id)){error="Circular feature dependency detected at "+id+(findSketch(id)!=nullptr?" (a sketch is attached to a face of a feature that uses it)":"");return{};}
    active.add(id);
    const auto* f=find(id);if(!f){error="Missing feature dependency: "+id;return{};}
    CadFaceOrigins local;CadFaceOrigins* trace=origins!=nullptr?&local:nullptr;
    const auto sketchFor=[&](const juce::String& sketchId,CadSketchFrame& frame)->const CadSketch*
    {
        const auto* sketch=findSketch(sketchId);if(sketch==nullptr)return nullptr;
        if(!resolveSketchFrame(*sketch,frame,error,active))return nullptr;
        sketchFrames[sketch->id]=frame;return sketch;
    };
    try
    {
        TopoDS_Shape result;
        if(f->type=="sketch"){active.removeString(id);return{};}   // sketch frames are resolved by rebuild()
        else if(f->type=="component")
        {
            const auto* record=componentFor(f->id);if(!record){error="Missing library component definition: "+f->id;return {};}
            result=components::shape(*record,error);if(result.IsNull())return {};
            if(trace!=nullptr){int index=0;for(TopExp_Explorer ex(result,TopAbs_FACE);ex.More();ex.Next())addFaceOrigin(local,ex.Current(),f->id+"/face:"+juce::String(index++));}
        }
        else if(f->type=="box")
        {
            BRepPrimAPI_MakeBox maker(f->width,f->depth,f->height);result=maker.Shape();
            if(trace!=nullptr)for(TopExp_Explorer ex(result,TopAbs_FACE);ex.More();ex.Next()){const auto n=outwardNormal(TopoDS::Face(ex.Current()));const auto role=n.Z()>0.5?"top":n.Z()<-0.5?"bottom":n.X()>0.5?"right":n.X()<-0.5?"left":n.Y()>0.5?"back":"front";addFaceOrigin(local,ex.Current(),f->id+"/"+role);}
        }
        else if(f->type=="cylinder")
        {
            result=BRepPrimAPI_MakeCylinder(f->radius,f->height).Shape();
            if(trace!=nullptr)for(TopExp_Explorer ex(result,TopAbs_FACE);ex.More();ex.Next()){const auto face=TopoDS::Face(ex.Current());const bool planar=BRepAdaptor_Surface(face,true).GetType()==GeomAbs_Plane;addFaceOrigin(local,face,f->id+"/"+(planar?(outwardNormal(face).Z()>0?"top":"bottom"):"side"));}
        }
        else if(f->type=="revolve")
        {
            CadSketchFrame frame; const auto* sketch=sketchFor(f->sketchId,frame);
            if(sketch==nullptr){if(error.isEmpty())error=f->name+": Missing source sketch "+f->sketchId;return{};}
            result=revolveSketch(*f,*sketch,frame,local,error);
            if(result.IsNull()){error=f->name+": "+error;return{};}
        }
        else if(f->type=="extrude")
        {
            CadSketchFrame frame;const auto* sketch=sketchFor(f->sketchId.isEmpty()?f->firstOperand:f->sketchId,frame);
            if(sketch==nullptr){if(error.isEmpty())error="Extrusion references a missing sketch.";return{};}
            result=extrudeSketch(*sketch,frame,f->depth,f->reverse,error,trace,f->id);
        }
        else if(f->type=="cut")
        {
            CadFaceOrigins targetOrigins,toolOrigins;
            auto target=evaluate(f->targetFeatureId.isEmpty()?f->firstOperand:f->targetFeatureId,error,active,trace!=nullptr?&targetOrigins:nullptr);if(target.IsNull())return{};
            CadSketchFrame frame;const auto* sketch=sketchFor(f->sketchId.isEmpty()?f->secondOperand:f->sketchId,frame);
            if(sketch==nullptr){if(error.isEmpty())error="Cut references a missing sketch.";return{};}
            const bool throughAll=f->throughAll||f->depth<=0;double cutDepth=throughAll?0.0:f->depth;
            if(throughAll){Bnd_Box bounds;BRepBndLib::Add(target,bounds);Standard_Real xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);cutDepth=std::sqrt((xmax-xmin)*(xmax-xmin)+(ymax-ymin)*(ymax-ymin)+(zmax-zmin)*(zmax-zmin))+2.0;}
            // A sketch on a face cuts into its support (against the outward normal) unless reversed.
            auto tool=extrudeSketch(*sketch,frame,cutDepth,f->reverse!=sketch->onFace(),error,trace!=nullptr?&toolOrigins:nullptr,f->id);if(tool.IsNull())return{};
            BRepAlgoAPI_Cut operation(target,tool);operation.Build();
            if(!operation.IsDone()||operation.HasErrors()){error=f->name+": OCCT Boolean operation failed.";return{};}
            result=operation.Shape();collectBooleanHistory(operation,*f,target,tool,operationHistory);
            if(trace!=nullptr){for(const auto& o:toolOrigins)addFaceOrigin(targetOrigins,o.first,o.second);local=mapFaceOrigins(operation,targetOrigins,result);}
        }
        else if(f->type=="chamfer")
        {
            CadFaceOrigins sourceOrigins;const auto source=evaluate(f->targetFeatureId,error,active,&sourceOrigins);if(source.IsNull())return{};
            if(!std::isfinite(f->width)||f->width<=0||f->edges.isEmpty()){error=f->name+": Select edges and a positive chamfer distance.";return{};}
            BRepBuilderAPI_Copy copy(source,true,false);const auto target=copy.Shape();CadFaceOrigins copiedOrigins;
            for(const auto& origin:sourceOrigins)copiedOrigins.push_back({copy.ModifiedShape(origin.first),origin.second});
            BRepFilletAPI_MakeChamfer operation(target);std::vector<TopoDS_Edge> selected;
            for(const auto& token:f->edges)
            {
                TopoDS_Edge edge;if(!resolveChamferEdge(target,token,copiedOrigins,edge)){error=f->name+": A selected edge is missing or ambiguous after the source edit; reselect it.";return{};}
                operation.Add(f->width,edge);selected.push_back(edge);
            }
            operation.Build();
            if(!operation.IsDone()||operation.Shape().IsNull()){error=f->name+": OCCT could not chamfer these edges at this distance. Reduce the distance or change the selection.";return{};}
            result=operation.Shape();if(!BRepCheck_Analyzer(result,true).IsValid()||!TopExp_Explorer(result,TopAbs_SOLID).More()){error=f->name+": Chamfer did not produce a valid solid.";return{};}
            for(TopExp_Explorer ex(result,TopAbs_SOLID);ex.More();ex.Next()){GProp_GProps props;BRepGProp::VolumeProperties(ex.Current(),props);if(!std::isfinite(props.Mass())||props.Mass()<=1e-12){error=f->name+": Chamfer produced a zero-volume or inverted solid.";return{};}}
            local=mapFaceOrigins(operation,copiedOrigins,result);
            for(size_t i=0;i<selected.size();++i)for(TopTools_ListIteratorOfListOfShape it(operation.Generated(selected[i]));it.More();it.Next())if(it.Value().ShapeType()==TopAbs_FACE)addFaceOrigin(local,it.Value(),f->id+"/bevel:"+juce::String((int)i));
            CadTopologyEvolution history;history.featureId=f->id;
            for(const auto kind:{TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer ex(source,kind);ex.More();ex.Next())
            {
                const auto copied=copy.ModifiedShape(ex.Current());
                for(TopTools_ListIteratorOfListOfShape it(operation.Modified(copied));it.More();it.Next())history.modified.push_back({ex.Current(),it.Value()});
                for(TopTools_ListIteratorOfListOfShape it(operation.Generated(copied));it.More();it.Next())history.generated.push_back({ex.Current(),it.Value()});
                if(operation.IsDeleted(copied))history.deleted.push_back(ex.Current());
            }
            operationHistory.push_back(std::move(history));
        }
        else if(f->type=="fillet")
        {
            CadFaceOrigins targetOrigins;
            auto target=evaluate(f->targetFeatureId.isEmpty()?f->firstOperand:f->targetFeatureId,error,active,trace!=nullptr?&targetOrigins:nullptr);
            if(target.IsNull())return{};
            if(f->radius<=0||!std::isfinite(f->radius)){error=f->name+": Fillet radius must be greater than zero.";return{};}
            if(f->edges.isEmpty()){error=f->name+": No edges specified for fillet.";return{};}

            BRepFilletAPI_MakeFillet operation(target);
            int resolvedCount = 0;
            for(const auto& token : f->edges)
            {
                TopoDS_Edge resolvedEdge;
                if(!resolveEdgeFromToken(target, token, trace!=nullptr?&targetOrigins:nullptr, resolvedEdge))
                {
                    error=f->name+": Unresolved edge reference. The source geometry or edge has changed.";
                    return{};
                }
                operation.Add(f->radius, resolvedEdge);
                ++resolvedCount;
            }
            if(resolvedCount == 0){error=f->name+": Could not resolve any target edges.";return{};}

            operation.Build();
            if(!operation.IsDone() || operation.Shape().IsNull())
            {
                error=f->name+": OCCT fillet operation failed. The radius may be too large for the adjacent geometry.";
                return{};
            }
            if(operation.HasResult() || operation.NbFaultyContours() > 0 || operation.NbFaultyVertices() > 0)
            {
                error=f->name+": OCCT fillet produced faulty contours. Radius "+juce::String(f->radius)+" mm may exceed geometry limits.";
                return{};
            }
            result=operation.Shape();
            collectFilletHistory(operation,*f,target,operationHistory);
            if(trace!=nullptr)
            {
                local=mapFaceOrigins(operation,targetOrigins,result);
                // Also assign fillet face tokens to newly generated fillet blend faces
                for(TopExp_Explorer ex(result,TopAbs_FACE);ex.More();ex.Next())
                {
                    const auto face=ex.Current();
                    bool hasOrigin=false;
                    for(const auto& [candidate,tokens]:local)
                    {
                        if(candidate.IsSame(face)&&!tokens.isEmpty()){hasOrigin=true;break;}
                    }
                    if(!hasOrigin)
                        addFaceOrigin(local,face,f->id+"/blend");
                }
            }
        }
        else
        {
            CadFaceOrigins aOrigins,bOrigins;
            auto a=evaluate(f->firstOperand,error,active,trace!=nullptr?&aOrigins:nullptr);auto b=evaluate(f->secondOperand,error,active,trace!=nullptr?&bOrigins:nullptr);
            if(a.IsNull()||b.IsNull())return{};
            for(const auto& o:bOrigins)addFaceOrigin(aOrigins,o.first,o.second);
            const auto run=[&](auto& operation){operation.Build();if(!operation.IsDone()||operation.HasErrors()){error=f->name+": OCCT Boolean operation failed.";return false;}result=operation.Shape();collectBooleanHistory(operation,*f,a,b,operationHistory);if(trace!=nullptr)local=mapFaceOrigins(operation,aOrigins,result);return true;};
            if(f->type=="union"){BRepAlgoAPI_Fuse operation(a,b);if(!run(operation))return{};}
            else if(f->type=="subtract"){BRepAlgoAPI_Cut operation(a,b);if(!run(operation))return{};}
            else if(f->type=="intersect"){BRepAlgoAPI_Common operation(a,b);if(!run(operation))return{};}
            else{error="Unsupported feature type: "+f->type;return{};}
        }
        active.removeString(id);
        const auto placed=place(result,*f,trace);
        if(origins!=nullptr)*origins=local;
        if(evaluationPass!=nullptr&&trace!=nullptr)(*evaluationPass)[id]={placed,local};   // traced results serve every caller
        if(signature.isNotEmpty()&&trace!=nullptr&&evaluationPass!=nullptr)   // only complete, traced rebuild results are kept
        {
            CachedFeature entry{signature,placed,local,{}};
            for(size_t i=historyStart;i<operationHistory.size();++i)if(operationHistory[i].featureId==id)entry.history.push_back(operationHistory[i]);
            geometryCache[id]=std::move(entry);
        }
        return placed;
    }
    catch(const Standard_Failure& ex){error="OCCT failed evaluating "+f->name+": "+juce::String(ex.GetMessageString());return{};}
}

// Every sketch frame is resolved on rebuild, so a sketch whose support face disappeared fails the edit even when
// nothing consumes the sketch yet.
bool CadDocument::rebuild(juce::String& error)
{
    std::map<juce::String, EvaluatedFeature> pass;
    std::map<juce::String, juce::String> signatures;
    evaluationPass=&pass; signaturePass=&signatures;
    const struct EndPass { CadDocument& d; ~EndPass(){ d.evaluationPass=nullptr; d.signaturePass=nullptr; } } endPass { *this };
    operationHistory.clear(); sketchFrames.clear();
    for(auto& f:items)
    {
        juce::StringArray active;f.faceOrigins.clear();
        if(f.type=="sketch")
        {
            f.shape={};
            if(const auto* sketch=findSketch(f.id)){CadSketchFrame frame;active.add(f.id);if(!resolveSketchFrame(*sketch,frame,error,active))return false;sketchFrames[sketch->id]=frame;}
            continue;
        }
        f.shape=evaluate(f.id,error,active,&f.faceOrigins);
        if(f.shape.IsNull())return false;
    }
    for(auto it=geometryCache.begin();it!=geometryCache.end();)it=find(it->first)==nullptr?geometryCache.erase(it):std::next(it);   // drop deleted features
    return true;
}

bool CadDocument::describeFace(const juce::String& featureId,int faceIndex,CadFaceInfo& info,juce::String& error) const
{
    const auto* f=find(featureId);
    if(f==nullptr||f->shape.IsNull()){error="Unknown solid feature: "+featureId;return false;}
    int index=-1;
    for(TopExp_Explorer ex(f->shape,TopAbs_FACE);ex.More();ex.Next())
    {
        if(++index!=faceIndex)continue;
        const auto face=TopoDS::Face(ex.Current());
        info={};info.featureId=featureId;info.faceIndex=faceIndex;
        BRepAdaptor_Surface surface(face,true);info.planar=surface.GetType()==GeomAbs_Plane;info.normal=outwardNormal(face);
        GProp_GProps props;BRepGProp::SurfaceProperties(face,props);info.area=props.Mass();info.centre=props.CentreOfMass();
        for(const auto& [candidate,tokens]:f->faceOrigins)if(candidate.IsSame(face)&&!tokens.isEmpty()){info.token=tokens[0];break;}
        return true;
    }
    error="Face "+juce::String(faceIndex)+" does not exist on "+f->name+".";
    return false;
}

bool CadDocument::listFaces(const juce::String& featureId,std::vector<CadFaceInfo>& faces,juce::String& error) const
{
    const auto* f=find(featureId);
    if(f==nullptr||f->shape.IsNull()){error="Unknown solid feature: "+featureId;return false;}
    int count=0;for(TopExp_Explorer ex(f->shape,TopAbs_FACE);ex.More();ex.Next())++count;
    faces.clear();
    for(int i=0;i<count;++i){CadFaceInfo info;if(describeFace(featureId,i,info,error))faces.push_back(info);}
    return true;
}

bool CadDocument::describeEdge(const juce::String& featureId,int edgeIndex,juce::String& description,juce::String& error) const
{
    const auto* f=find(featureId);
    if(f==nullptr||f->shape.IsNull()){error="Unknown solid feature: "+featureId;return false;}
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(f->shape,TopAbs_EDGE,edges);
    if(edgeIndex<0||edgeIndex>=edges.Extent()){error="Edge "+juce::String(edgeIndex)+" does not exist on "+f->name+".";return false;}
    BRepAdaptor_Curve curve(TopoDS::Edge(edges(edgeIndex+1)));
    GProp_GProps props;BRepGProp::LinearProperties(edges(edgeIndex+1),props);
    const auto kind=curve.GetType()==GeomAbs_Line?"line":curve.GetType()==GeomAbs_Circle?"circle":"curve";
    description=juce::String(kind)+" edge "+juce::String(edgeIndex)+", length "+juce::String(props.Mass(),3)+" mm";
    if(curve.GetType()==GeomAbs_Circle)description<<", radius "<<juce::String(curve.Circle().Radius(),3)<<" mm";
    return true;
}

bool CadDocument::sketchFrame(const juce::String& sketchId,CadSketchFrame& frame) const
{
    const auto found=sketchFrames.find(sketchId);
    if(found==sketchFrames.end())return false;
    frame=found->second;return true;
}

bool CadDocument::createFaceSketch(const juce::String& featureId,const juce::String& faceToken,juce::String& id,juce::String& error)
{
    return mutate([&](juce::String& e)
    {
        const auto* f=find(featureId);
        if(f==nullptr||f->shape.IsNull()||f->type=="sketch"){e="Choose a solid body for the sketch support.";return false;}
        if(faceToken.isEmpty()){e="This face has no persistent name, so a sketch cannot be attached to it.";return false;}
        TopoDS_Face face;juce::String reason;
        if(!supportFace(*f,faceToken,face,reason)){e="Cannot attach a sketch: "+reason+".";return false;}
        CadSketch sketch;sketch.id=newId("sketch",nextNumber++);sketch.plane="FACE";sketch.supportFeature=featureId;sketch.supportFace=faceToken;
        sketches.push_back(sketch);
        CadFeature feature;feature.id=sketch.id;feature.name="Sketch "+juce::String(nextNumber-1);feature.type="sketch";feature.sketchId=sketch.id;feature.visible=true;
        items.push_back(feature);id=feature.id;return true;
    },error);
}

gp_Trsf CadDocument::instanceShapeTransform(const juce::String& featureId, const gp_Trsf& instanceWorld) const
{
    // The instance frame starts at the body's placement point, so only the motion relative to it is applied.
    gp_Trsf anchor;
    if (const auto* feature = find(featureId)) anchor.SetTranslation(gp_Vec(feature->x, feature->y, feature->z));
    return instanceWorld.Multiplied(anchor.Inverted());
}

std::shared_ptr<const CadRenderSnapshot> CadDocument::snapshot(bool includeHidden) const
{
    if (!includeHidden && cachedSnapshot != nullptr && cachedRevision == documentRevision) return cachedSnapshot;
    auto next = std::make_shared<CadRenderSnapshot>();
    next->revision = documentRevision;
    next->documentInstance = instanceId;
    for (const auto& f : items) if ((f.visible || includeHidden) && !f.shape.IsNull())
    {
        CadLocalFrame local; local.id=f.id; local.name=f.name; const auto transform=placement(f);
        for (int c=0;c<4;++c) for (int row=0;row<3;++row) local.matrix[(size_t)(c*4+row)]=(float)transform.Value(row+1,c+1);
        local.matrix[15]=1; next->localFrames.push_back(std::move(local));
    }

    for (const auto& f : items)
        if ((f.visible || includeHidden) && !f.shape.IsNull())
        {
            BRepMesh_IncrementalMesh mesh(f.shape, 0.1, false, 0.5, true);
            int faceIndex = -1;
            for (TopExp_Explorer ex(f.shape, TopAbs_FACE); ex.More(); ex.Next())
            {
                ++faceIndex;
                const auto face = TopoDS::Face(ex.Current());
                TopLoc_Location triangulationLocation, surfaceLocation;
                const auto tri = BRep_Tool::Triangulation(face, triangulationLocation);
                const auto surface = BRep_Tool::Surface(face, surfaceLocation);
                if (tri.IsNull()) continue;

                CadMesh out;
                out.featureId = f.id;
                out.faceIndex = faceIndex;
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
            // Model edges in the feature's unique-edge order (the same order describeEdge uses).
            TopTools_IndexedMapOfShape edges; TopExp::MapShapes(f.shape, TopAbs_EDGE, edges);
            for (int e = 1; e <= edges.Extent(); ++e)
            {
                CadPolyline line; line.featureId = f.id; line.edgeIndex = e - 1;
                try
                {
                    BRepAdaptor_Curve curve(TopoDS::Edge(edges(e)));
                    GCPnts_QuasiUniformDeflection sampler(curve, 0.05);
                    if (!sampler.IsDone()) continue;
                    for (int i = 1; i <= sampler.NbPoints(); ++i) { const auto q = sampler.Value(i); line.points.insert(line.points.end(), { (float) q.X(), (float) q.Y(), (float) q.Z() }); }
                }
                catch (const Standard_Failure&) { continue; }
                if (line.points.size() >= 6) next->edges.push_back(std::move(line));
            }
        }
    // Visible sketches in their resolved plane, so a face attachment can be seen in the model.
    for (const auto& f : items)
    {
        if (f.type != "sketch" || !f.visible) continue;
        const auto* sketch = findSketch(f.id); const auto frame = sketchFrames.find(f.id);
        if (sketch == nullptr || frame == sketchFrames.end()) continue;
        const auto& fr = frame->second;
        const auto at = [&](double u, double v) { return fr.origin.Translated(gp_Vec(fr.u) * u + gp_Vec(fr.v) * v); };
        CadPolyline outline; outline.featureId = f.id;
        const auto segment = [&](const gp_Pnt& a, const gp_Pnt& b) { outline.points.insert(outline.points.end(), { (float) a.X(), (float) a.Y(), (float) a.Z(), (float) b.X(), (float) b.Y(), (float) b.Z() }); };
        for (const auto& entity : sketch->entities)
        {
            if (entity.type == "circle") for (int k = 0; k < 48; ++k) { const auto a0 = k * 2.0 * M_PI / 48.0, a1 = (k + 1) * 2.0 * M_PI / 48.0; segment(at(entity.x1 + entity.radius * std::cos(a0), entity.y1 + entity.radius * std::sin(a0)), at(entity.x1 + entity.radius * std::cos(a1), entity.y1 + entity.radius * std::sin(a1))); }
            else if (entity.type == "bezier")
            {
                const auto b = [&](double t, double p0, double p1, double p2, double p3) { const auto s = 1.0 - t; return s * s * s * p0 + 3 * s * s * t * p1 + 3 * s * t * t * p2 + t * t * t * p3; };
                for (int k = 0; k < 32; ++k) { const auto t0 = k / 32.0, t1 = (k + 1) / 32.0; segment(at(b(t0, entity.x1, entity.cx1, entity.cx2, entity.x2), b(t0, entity.y1, entity.cy1, entity.cy2, entity.y2)), at(b(t1, entity.x1, entity.cx1, entity.cx2, entity.x2), b(t1, entity.y1, entity.cy1, entity.cy2, entity.y2))); }
            }
            else segment(at(entity.x1, entity.y1), at(entity.x2, entity.y2));
        }
        if (sketch->onFace())   // small U/V axis marks at the sketch origin
        { const auto size = 6.0; segment(fr.origin, at(size, 0)); segment(fr.origin, at(0, size)); }
        if (!outline.points.empty()) next->sketchOutlines.push_back(std::move(outline));
    }
    if (includeHidden) return next;
    cachedSnapshot = next;
    cachedRevision = documentRevision;
    return cachedSnapshot;
}

juce::String CadDocument::serializeSketches() const {juce::Array<juce::var> list;for(const auto& sketch:sketches){auto object=std::make_unique<juce::DynamicObject>();object->setProperty("id",sketch.id);object->setProperty("plane",sketch.plane);object->setProperty("offset",sketch.offset);if(sketch.onFace()){object->setProperty("supportFeature",sketch.supportFeature);object->setProperty("supportFace",sketch.supportFace);}juce::Array<juce::var> entities;for(const auto& entity:sketch.entities){auto value=std::make_unique<juce::DynamicObject>();value->setProperty("id",entity.id);value->setProperty("type",entity.type);value->setProperty("groupId",entity.groupId);value->setProperty("x1",entity.x1);value->setProperty("y1",entity.y1);value->setProperty("x2",entity.x2);value->setProperty("y2",entity.y2);value->setProperty("radius",entity.radius);if(entity.type=="bezier"){value->setProperty("cx1",entity.cx1);value->setProperty("cy1",entity.cy1);value->setProperty("cx2",entity.cx2);value->setProperty("cy2",entity.cy2);}entities.add(juce::var(value.release()));}object->setProperty("entities",juce::var(entities));juce::Array<juce::var> constraints;for(const auto& constraint:sketch.constraints){auto value=std::make_unique<juce::DynamicObject>();value->setProperty("id",constraint.id);value->setProperty("type",constraint.type);value->setProperty("firstEntity",constraint.firstEntity);value->setProperty("secondEntity",constraint.secondEntity);value->setProperty("value",constraint.value);constraints.add(juce::var(value.release()));}object->setProperty("constraints",juce::var(constraints));list.add(juce::var(object.release()));}return juce::JSON::toString(juce::var(list));}
bool CadDocument::deserializeSketches(const juce::String& json,juce::String& error) {const auto parsed=juce::JSON::parse(json);auto* list=parsed.getArray();if(!list){error="Stored sketch data is invalid.";return false;}std::vector<CadSketch> loaded;for(const auto& value:*list){auto* object=value.getDynamicObject();if(!object){error="Stored sketch entry is invalid.";return false;}CadSketch sketch;sketch.id=object->getProperty("id").toString();sketch.plane=object->getProperty("plane").toString();sketch.offset=number(object->getProperty("offset"),0);sketch.supportFeature=object->getProperty("supportFeature").toString();sketch.supportFace=object->getProperty("supportFace").toString();if(sketch.onFace())sketch.plane="FACE";if(auto* entities=object->getProperty("entities").getArray())for(const auto& entityValue:*entities){auto* entityObject=entityValue.getDynamicObject();if(!entityObject)continue;CadSketchEntity entity;entity.id=entityObject->getProperty("id").toString();entity.type=entityObject->getProperty("type").toString();entity.groupId=entityObject->getProperty("groupId").toString();entity.x1=number(entityObject->getProperty("x1"),0);entity.y1=number(entityObject->getProperty("y1"),0);entity.x2=number(entityObject->getProperty("x2"),0);entity.y2=number(entityObject->getProperty("y2"),0);entity.radius=number(entityObject->getProperty("radius"),1);entity.cx1=number(entityObject->getProperty("cx1"),0);entity.cy1=number(entityObject->getProperty("cy1"),0);entity.cx2=number(entityObject->getProperty("cx2"),0);entity.cy2=number(entityObject->getProperty("cy2"),0);sketch.entities.push_back(entity);}if(auto* constraints=object->getProperty("constraints").getArray())for(const auto& constraintValue:*constraints){auto* constraintObject=constraintValue.getDynamicObject();if(!constraintObject)continue;CadSketchConstraint constraint;constraint.id=constraintObject->getProperty("id").toString();constraint.type=constraintObject->getProperty("type").toString();constraint.firstEntity=constraintObject->getProperty("firstEntity").toString();constraint.secondEntity=constraintObject->getProperty("secondEntity").toString();constraint.value=number(constraintObject->getProperty("value"),0);sketch.constraints.push_back(constraint);}loaded.push_back(std::move(sketch));}sketches=std::move(loaded);return true;}

bool CadDocument::syncOCAFFromItems(juce::String& error) { if(ocafDocument.IsNull()){error="OCAF document is not initialized.";return false;}const auto root=ocafDocument->Main();writeInteger(root.FindChild(kMetaTag,true),1,nextNumber);const auto features=root.FindChild(kFeatureContainerTag,true);for(TDF_ChildIterator it(features);it.More();it.Next()){const auto label=it.Value();if(readInteger(label,1,0)!=0){const auto oldId=readString(label,2);bool stillPresent=false;for(const auto& f:items)if(f.id==oldId){stillPresent=true;break;}if(!stillPresent)recordDeleted(label);}writeInteger(label,1,0);}for(const auto& f:items){if(f.shape.IsNull()&&f.type!="sketch"){error="Cannot record a null shape for feature: "+f.id;return false;}const auto label=features.FindChild(kFeatureTagBase+featureNumber(f.id),true);writeInteger(label,1,1);writeString(label,2,f.id);writeString(label,3,f.name);writeString(label,4,f.type);writeString(label,5,f.firstOperand);writeString(label,6,f.secondOperand);writeString(label,7,f.sketchId);writeString(label,8,f.targetFeatureId);writeReal(label,10,f.width);writeReal(label,11,f.depth);writeReal(label,12,f.height);writeReal(label,13,f.radius);writeReal(label,14,f.x);writeReal(label,15,f.y);writeReal(label,16,f.z);writeReal(label,17,f.rx);writeReal(label,18,f.ry);writeReal(label,19,f.rz);writeInteger(label,20,f.visible?1:0);writeInteger(label,21,f.reverse?1:0);writeInteger(label,22,f.throughAll?1:0);writeString(label,23,f.edges.joinIntoString("\n"));if(f.type=="revolve")writeString(label,24,juce::JSON::toString(revolveParametersToJson(f.revolve),false,17));writeInteger(label,25,f.lathe?1:0);recordGeneratedOrModified(label,f.shape);}for(const auto& entry:operationHistory){for(TDF_ChildIterator it(features);it.More();it.Next())if(readString(it.Value(),2)==entry.featureId){recordOperationHistory(it.Value(),entry);break;}}const auto refs=root.FindChild(kReferenceContainerTag,true);for(TDF_ChildIterator it(refs);it.More();it.Next())writeInteger(it.Value(),1,0);for(size_t i=0;i<references.size();++i){const auto label=refs.FindChild((int)i+1,true);writeInteger(label,1,1);writeString(label,2,references[i].ownerFeatureId);writeString(label,3,references[i].topologyKind);writeString(label,4,references[i].semanticRole);const auto* owner=find(references[i].ownerFeatureId);if(owner==nullptr)continue;if(references[i].topologyKind=="edge"&&references[i].semanticRole=="box-bottom-x-edge"){TopoDS_Shape edge;if(!representativeBoxEdge(*owner,edge)){error="The supported persistent reference could not be mapped to current document geometry.";return false;}recordGeneratedOrModifiedAt(label.FindChild(kReferenceShapeTag,true).FindChild(kShapeTag,true),edge);}}
    // Face supports of sketches: one label per sketch holds the support feature, the face token and the current
    // face as a TNaming named shape (modified across rebuilds), so the attachment is real document topology.
    for(const auto& sketch:sketches)
    {
        if(!sketch.onFace())continue;
        const auto label=refs.FindChild(kFaceReferenceTagBase+featureNumber(sketch.id),true);
        writeInteger(label,1,1);writeString(label,2,sketch.supportFeature);writeString(label,3,"face");writeString(label,4,sketch.supportFace);writeString(label,5,sketch.id);
        const auto* owner=find(sketch.supportFeature);TopoDS_Face face;juce::String reason;
        if(owner==nullptr||!supportFace(*owner,sketch.supportFace,face,reason)){error="The support of "+sketch.id+" could not be mapped to current geometry: "+reason+".";return false;}
        recordGeneratedOrModifiedAt(label.FindChild(kReferenceShapeTag,true).FindChild(kShapeTag,true),face);
    }
    return true; }
namespace
{
bool sameDefinition(const CadFeature& a,const CadFeature& b)
{
    return a.id==b.id&&a.name==b.name&&a.type==b.type&&a.firstOperand==b.firstOperand&&a.secondOperand==b.secondOperand&&a.sketchId==b.sketchId
        &&a.targetFeatureId==b.targetFeatureId&&a.width==b.width&&a.depth==b.depth&&a.height==b.height&&a.radius==b.radius
        &&a.x==b.x&&a.y==b.y&&a.z==b.z&&a.rx==b.rx&&a.ry==b.ry&&a.rz==b.rz&&a.visible==b.visible&&a.reverse==b.reverse&&a.throughAll==b.throughAll&&a.lathe==b.lathe&&a.edges==b.edges&&a.revolve.axisType==b.revolve.axisType&&a.revolve.origin==b.revolve.origin&&a.revolve.direction==b.revolve.direction&&a.revolve.angleDegrees==b.revolve.angleDegrees&&a.revolve.reverse==b.revolve.reverse;
}
}

bool CadDocument::syncItemsFromOCAF(juce::String& error,const std::vector<CadFeature>* evaluated) { if(ocafDocument.IsNull()){error="OCAF document is not initialized.";return false;}const auto root=ocafDocument->Main();const auto meta=root.FindChild(kMetaTag,false);const auto features=root.FindChild(kFeatureContainerTag,false);if(features.IsNull()){error="Native CAD document has no feature container.";return false;}nextNumber=readInteger(meta,1,1);items.clear();bool allShapesNamed=true;for(TDF_ChildIterator it(features);it.More();it.Next()){const auto label=it.Value();if(readInteger(label,1,0)==0)continue;CadFeature f;f.id=readString(label,2);f.name=readString(label,3);f.type=readString(label,4);f.firstOperand=readString(label,5);f.secondOperand=readString(label,6);f.sketchId=readString(label,7);f.targetFeatureId=readString(label,8);f.width=readReal(label,10,f.width);f.depth=readReal(label,11,f.depth);f.height=readReal(label,12,f.height);f.radius=readReal(label,13,f.radius);f.x=readReal(label,14,0);f.y=readReal(label,15,0);f.z=readReal(label,16,0);f.rx=readReal(label,17,0);f.ry=readReal(label,18,0);f.rz=readReal(label,19,0);f.visible=readInteger(label,20,1)!=0;f.reverse=readInteger(label,21,0)!=0;f.throughAll=readInteger(label,22,0)!=0;if(f.type=="revolve"&&!readRevolveParameters(juce::JSON::parse(readString(label,24)),f.revolve,error))return false;f.lathe=readInteger(label,25,0)!=0;const auto edgeStr=readString(label,23);if(edgeStr.isNotEmpty())f.edges=juce::StringArray::fromTokens(edgeStr,"\n",{});if(f.id.isEmpty()||f.type.isEmpty()){error="Native CAD document contains an invalid feature.";return false;}const auto stored=namedShape(label.FindChild(kShapeTag,false));if((stored.IsNull()||stored->IsEmpty())&&f.type!="sketch"){allShapesNamed=false;}else if(!stored.IsNull()&&!stored->IsEmpty())f.shape=TNaming_Tool::CurrentShape(stored);items.push_back(std::move(f));}references.clear();const auto refs=root.FindChild(kReferenceContainerTag,false);if(!refs.IsNull())for(TDF_ChildIterator it(refs);it.More();it.Next())if(readInteger(it.Value(),1,0)!=0&&readString(it.Value(),3)!="face")references.push_back({readString(it.Value(),2),readString(it.Value(),3),readString(it.Value(),4)});juce::ignoreUnused(allShapesNamed);
    // Reuse is only valid when the sketches are also unchanged, which holds for callers that pass `evaluated`
    // right after evaluating these same in-memory sketches (commit, JSON export). Undo/redo/open rebuild.
    if(evaluated!=nullptr&&evaluated->size()==items.size()&&std::equal(items.begin(),items.end(),evaluated->begin(),sameDefinition))
    {
        for(size_t i=0;i<items.size();++i){items[i].shape=(*evaluated)[i].shape;items[i].faceOrigins=(*evaluated)[i].faceOrigins;}
        return true;
    }
    return rebuild(error); }   // rebuild derives face names and sketch frames

juce::String CadDocument::serialize() const { auto root=std::make_unique<juce::DynamicObject>();root->setProperty("formatVersion",10);root->setProperty("units","mm");root->setProperty("nextNumber",nextNumber);juce::Array<juce::var> list;for(const auto& f:items){auto o=std::make_unique<juce::DynamicObject>();o->setProperty("id",f.id);o->setProperty("name",f.name);o->setProperty("type",f.type);if(f.lathe)o->setProperty("lathe",true);if(f.type=="revolve")o->setProperty("revolve",revolveParametersToJson(f.revolve));o->setProperty("firstOperand",f.firstOperand);o->setProperty("secondOperand",f.secondOperand);o->setProperty("sketchId",f.sketchId);o->setProperty("targetFeatureId",f.targetFeatureId);o->setProperty("width",f.width);o->setProperty("depth",f.depth);o->setProperty("height",f.height);o->setProperty("radius",f.radius);o->setProperty("reverse",f.reverse);o->setProperty("throughAll",f.throughAll);o->setProperty("x",f.x);o->setProperty("y",f.y);o->setProperty("z",f.z);o->setProperty("rx",f.rx);o->setProperty("ry",f.ry);o->setProperty("rz",f.rz);o->setProperty("visible",f.visible);juce::Array<juce::var> edgeVarList;for(const auto& ed:f.edges)edgeVarList.add(ed);o->setProperty("edges",juce::var(edgeVarList));list.add(juce::var(o.release()));}root->setProperty("features",juce::var(list));juce::Array<juce::var> refs;for(const auto& r:references){auto o=std::make_unique<juce::DynamicObject>();o->setProperty("ownerFeatureId",r.ownerFeatureId);o->setProperty("topologyKind",r.topologyKind);o->setProperty("semanticRole",r.semanticRole);refs.add(juce::var(o.release()));}root->setProperty("references",juce::var(refs));root->setProperty("sketches",juce::JSON::parse(serializeSketches()));root->setProperty("mechanics",mech::toJson(mechanics));if(!materials.empty())root->setProperty("materials",matl::toJson(materials));if(!sheetStocks.empty())root->setProperty("sheetStocks",stock::toJson(sheetStocks));if(!analysisCases.empty())root->setProperty("analysisCases",analysis::toJson(analysisCases));if(!componentDefinitions.empty())root->setProperty("components",componentData());if(kitMetadata.has_value())root->setProperty("projectKit",kitMetadata->toVar());return juce::JSON::toString(juce::var(root.release())); }
bool CadDocument::deserialize(const juce::String& json,juce::String& error) { const auto parsed=juce::JSON::parse(json);auto* root=parsed.getDynamicObject();if(!root){error="CAD document is not valid JSON.";return false;}if(root->getProperty("units").toString()!="mm"){error="Only millimeter CAD documents are supported.";return false;}std::vector<CadFeature> loaded;if(auto* array=root->getProperty("features").getArray())for(const auto& value:*array){auto* o=value.getDynamicObject();if(!o){error="CAD feature entry is invalid.";return false;}CadFeature f;f.id=o->getProperty("id").toString();f.name=o->getProperty("name").toString();f.type=o->getProperty("type").toString();f.lathe=static_cast<bool>(o->getProperty("lathe"));if(f.type=="revolve"&&!readRevolveParameters(o->getProperty("revolve"),f.revolve,error))return false;f.firstOperand=o->getProperty("firstOperand").toString();f.secondOperand=o->getProperty("secondOperand").toString();f.sketchId=o->getProperty("sketchId").toString();f.targetFeatureId=o->getProperty("targetFeatureId").toString();f.width=number(o->getProperty("width"),f.width);f.depth=number(o->getProperty("depth"),f.depth);f.height=number(o->getProperty("height"),f.height);f.radius=number(o->getProperty("radius"),f.radius);f.reverse=static_cast<bool>(o->getProperty("reverse"));f.throughAll=static_cast<bool>(o->getProperty("throughAll"));f.x=number(o->getProperty("x"),0);f.y=number(o->getProperty("y"),0);f.z=number(o->getProperty("z"),0);f.rx=number(o->getProperty("rx"),0);f.ry=number(o->getProperty("ry"),0);f.rz=number(o->getProperty("rz"),0);f.visible=static_cast<bool>(o->getProperty("visible"));if(auto* eArray=o->getProperty("edges").getArray())for(const auto& eV:*eArray)f.edges.add(eV.toString());loaded.push_back(std::move(f));}const auto old=items;const auto oldSketches=sketches;const auto oldMechanicsJson=mechanics;const auto oldKit=kitMetadata; const auto oldRefs=references;const auto oldNext=nextNumber;items=std::move(loaded);references.clear();nextNumber=(int)root->getProperty("nextNumber");if(nextNumber<1)nextNumber=(int)items.size()+1;if(auto* array=root->getProperty("references").getArray())for(const auto& value:*array)if(auto* o=value.getDynamicObject())references.push_back({o->getProperty("ownerFeatureId").toString(),o->getProperty("topologyKind").toString(),o->getProperty("semanticRole").toString()});if(root->hasProperty("mechanics")&&!mech::fromJson(root->getProperty("mechanics"),mechanics,error)){items=old;sketches=oldSketches;mechanics=oldMechanicsJson;references=oldRefs;nextNumber=oldNext;return false;}if(!root->hasProperty("mechanics")){mechanics={};mechanics.modelId=juce::Uuid().toDashedString();}const auto oldMaterials=materials; const auto oldStocks=sheetStocks;materials={};if(root->hasProperty("materials")&&!matl::fromJson(root->getProperty("materials"),materials,error)){items=old;sketches=oldSketches;mechanics=oldMechanicsJson;materials=oldMaterials; sheetStocks=oldStocks;references=oldRefs;nextNumber=oldNext;return false;}sheetStocks={};if(root->hasProperty("sheetStocks")&&!stock::fromJson(root->getProperty("sheetStocks"),sheetStocks,materials,error)){items=old;sketches=oldSketches;mechanics=oldMechanicsJson;materials=oldMaterials;sheetStocks=oldStocks;references=oldRefs;nextNumber=oldNext;return false;}const auto oldAnalysis=analysisCases; const auto oldComponents=componentDefinitions;analysisCases={};if(root->hasProperty("analysisCases")&&!analysis::fromJson(root->getProperty("analysisCases"),analysisCases,error)){items=old;sketches=oldSketches;mechanics=oldMechanicsJson;materials=oldMaterials; sheetStocks=oldStocks;analysisCases=oldAnalysis; componentDefinitions=oldComponents;kitMetadata=oldKit;references=oldRefs;nextNumber=oldNext;return false;}if(root->hasProperty("projectKit"))kitMetadata=kit::CadCraftKitMetadata::fromVar(root->getProperty("projectKit"));else kitMetadata.reset();if(root->hasProperty("sketches")&&!deserializeSketches(juce::JSON::toString(root->getProperty("sketches")),error)){mechanics=oldMechanicsJson;materials=oldMaterials; sheetStocks=oldStocks;analysisCases=oldAnalysis; componentDefinitions=oldComponents;items=old;sketches=oldSketches;references=oldRefs;nextNumber=oldNext;return false;}if(!readComponentData(root->getProperty("components"),error)||!rebuild(error)||!syncOCAFFromItems(error)||!materialAssignmentsResolve(error)){items=old;sketches=oldSketches;mechanics=oldMechanicsJson;materials=oldMaterials; sheetStocks=oldStocks;analysisCases=oldAnalysis; componentDefinitions=oldComponents;references=oldRefs;nextNumber=oldNext;rebuild(error);return false;}writeString(ocafDocument->Main().FindChild(kSketchContainerTag,true),1,serializeSketches());writeString(ocafDocument->Main().FindChild(kMechanicsContainerTag,true),1,juce::JSON::toString(mech::toJson(mechanics),true));if(!materials.empty())writeString(ocafDocument->Main().FindChild(kMaterialsContainerTag,true),1,juce::JSON::toString(matl::toJson(materials),true));if(!sheetStocks.empty())writeString(ocafDocument->Main().FindChild(kStockContainerTag,true),1,juce::JSON::toString(stock::toJson(sheetStocks),true));if(!analysisCases.empty())writeString(ocafDocument->Main().FindChild(kAnalysisContainerTag,true),1,juce::JSON::toString(analysis::toJson(analysisCases),true));writeComponentsToOCAF();if(kitMetadata.has_value())writeString(ocafDocument->Main().FindChild(kProjectKitContainerTag,true),1,juce::JSON::toString(kit::toJson(*kitMetadata),true));return true; }
bool CadDocument::saveNative(const juce::File& file,juce::String& error) { if(ocafDocument.IsNull()){error="OCAF document is not initialized.";return false;}const auto encoded=juce::File(file.getFullPathName()+".cbf");encoded.deleteFile();const auto status=ocafApplication->SaveAs(ocafDocument,pathString(encoded));if(status!=PCDM_SS_OK||!encoded.existsAsFile()){error="Unable to write native CAD document: "+file.getFullPathName();return false;}file.deleteFile();if(!encoded.moveFileTo(file)){encoded.deleteFile();error="Unable to finalize native CAD document: "+file.getFullPathName();return false;}return true; }
// The file is staged under a unique name: a document saved in this session stays registered with OCAF under its
// "<file>.cbf" path, and OCAF refuses to retrieve a second document from a path that is already in session.
bool CadDocument::loadNative(const juce::File& file,juce::String& error) { const auto encoded=juce::File(file.getFullPathName()+"."+juce::Uuid().toDashedString().substring(0,8)+".cbf");encoded.deleteFile();if(!file.copyFileTo(encoded)){error="Unable to stage native CAD document: "+file.getFullPathName();return false;}Handle(TDocStd_Document) opened;const auto status=ocafApplication->Open(pathString(encoded),opened);encoded.deleteFile();if(status!=PCDM_RS_OK||opened.IsNull()){error="Unable to open native CAD document: "+file.getFullPathName();return false;}const auto oldDocument=ocafDocument;const auto oldItems=items;const auto oldSketches=sketches;const auto oldKit=kitMetadata; const auto oldRefs=references;const auto oldNext=nextNumber;ocafDocument=opened;const auto oldMechanics=mechanics;const auto oldMaterials=materials;const auto oldStocks=sheetStocks;const auto oldAnalysis=analysisCases; const auto oldComponents=componentDefinitions;if(!restoreAuthoredFromOCAF(error)){ocafDocument=oldDocument;items=oldItems;sketches=oldSketches;mechanics=oldMechanics;materials=oldMaterials; sheetStocks=oldStocks;analysisCases=oldAnalysis; componentDefinitions=oldComponents;references=oldRefs;nextNumber=oldNext;return false;}documentFile=file;dirty=false;ocafDocument->SetUndoLimit(100);ocafDocument->ClearUndos();ocafDocument->ClearRedos();invalidate();return true; }
bool CadDocument::save(const juce::File& file,juce::String& error) { return file.hasFileExtension("djo") ? saveNative(file,error) : exportJson(file,error); }
bool CadDocument::exportJson(const juce::File& file,juce::String& error) const { auto* writable=const_cast<CadDocument*>(this);const auto evaluated=items;if(!writable->syncItemsFromOCAF(error,&evaluated))return false;if(!file.replaceWithText(serialize())){error="Unable to write CAD JSON: "+file.getFullPathName();return false;}return true; }
bool CadDocument::load(const juce::File& file,juce::String& error) { if(!loadFile(file,error))return false; instanceId=++documentInstanceCounter; invalidate(); return true; }
bool CadDocument::loadFile(const juce::File& file,juce::String& error) { if(file.hasFileExtension("djo"))return loadNative(file,error);const auto text=file.loadFileAsString();if(text.isEmpty()){error="CAD document is empty or unreadable.";return false;}const auto oldDocument=ocafDocument;const auto oldItems=items;const auto oldKit=kitMetadata; const auto oldRefs=references;const auto oldNext=nextNumber;const auto oldFile=documentFile;const auto oldDirty=dirty;const auto oldSketches=sketches;const auto oldMechanics=mechanics;const auto oldMaterials=materials;const auto oldStocks=sheetStocks;const auto oldAnalysis=analysisCases; const auto oldComponents=componentDefinitions;initializeOCAF();if(!deserialize(text,error)){ocafDocument=oldDocument;items=oldItems;references=oldRefs;nextNumber=oldNext;documentFile=oldFile;dirty=oldDirty;sketches=oldSketches;mechanics=oldMechanics;materials=oldMaterials;sheetStocks=oldStocks;analysisCases=oldAnalysis; componentDefinitions=oldComponents;invalidate();return false;}ocafDocument->ClearUndos();ocafDocument->ClearRedos();documentFile=file;dirty=false;invalidate();return true; }
bool CadDocument::undo(juce::String& error) { if(!ocafDocument->Undo()){error="Nothing to undo.";return false;}if(!restoreAuthoredFromOCAF(error))return false;dirty=true;invalidate();return true; }
bool CadDocument::redo(juce::String& error) { if(!ocafDocument->Redo()){error="Nothing to redo.";return false;}if(!restoreAuthoredFromOCAF(error))return false;dirty=true;invalidate();return true; }
bool CadDocument::makeRepresentativeReference(const juce::String& featureId,CadPersistentReference& reference,juce::String& error) { const auto* f=find(featureId);if(f==nullptr||f->type!="box"){error="Representative persistent reference currently supports box edges only.";return false;}reference={featureId,"edge","box-bottom-x-edge"};ocafDocument->NewCommand();references.push_back(reference);if(!syncOCAFFromItems(error)){references.pop_back();ocafDocument->AbortCommand();return false;}ocafDocument->CommitCommand();dirty=true;invalidate();return true; }
bool CadDocument::resolveReference(const CadPersistentReference& reference,juce::String& error) const { const auto* f=find(reference.ownerFeatureId);if(f==nullptr){error="Persistent reference owner is missing.";return false;}if(reference.topologyKind!="edge"||reference.semanticRole!="box-bottom-x-edge"||f->type!="box"){error="Persistent reference role is unsupported or ambiguous.";return false;}const auto refs=ocafDocument->Main().FindChild(kReferenceContainerTag,false);if(refs.IsNull()){error="Persistent reference container is missing.";return false;}for(TDF_ChildIterator it(refs);it.More();it.Next()){const auto label=it.Value();if(readInteger(label,1,0)==0||readString(label,2)!=reference.ownerFeatureId||readString(label,3)!=reference.topologyKind||readString(label,4)!=reference.semanticRole)continue;const auto shapeLabel=label.FindChild(kReferenceShapeTag,false).FindChild(kShapeTag,false);const auto stored=namedShape(shapeLabel);if(stored.IsNull()||stored->IsEmpty()){error="Persistent reference has no named topology in the current document.";return false;}const auto edge=TNaming_Tool::CurrentShape(stored);if(edge.IsNull()||edge.ShapeType()!=TopAbs_EDGE){error="Persistent reference resolved to deleted or ambiguous topology.";return false;}for(TopExp_Explorer ex(f->shape,TopAbs_EDGE);ex.More();ex.Next())if(ex.Current().IsSame(edge))return true;error="Persistent reference topology is not present in the current owner shape.";return false;}error="Persistent reference role is not present in the document.";return false; }
bool CadCommandService::newDocument(){model.clear();return true;}
bool CadCommandService::listFacesJson(const juce::String& featureId,juce::String& json,juce::String& error) const
{
    std::vector<CadFaceInfo> faces;
    if(!model.listFaces(featureId,faces,error))return false;
    juce::Array<juce::var> list;
    for(const auto& face:faces)
    {
        auto o=std::make_unique<juce::DynamicObject>();
        o->setProperty("index",face.faceIndex);o->setProperty("name",face.token);o->setProperty("planar",face.planar);o->setProperty("canSupportSketch",face.planar&&face.token.isNotEmpty());
        o->setProperty("normal",juce::Array<juce::var>{face.normal.X(),face.normal.Y(),face.normal.Z()});
        o->setProperty("centre",juce::Array<juce::var>{face.centre.X(),face.centre.Y(),face.centre.Z()});o->setProperty("areaMm2",face.area);
        list.add(juce::var(o.release()));
    }
    json=juce::JSON::toString(juce::var(list));
    return true;
}
bool CadCommandService::createBox(double w,double d,double h,juce::String& id,juce::String& error){return model.createBox(w,d,h,id,error);}
bool CadCommandService::createCylinder(double r,double h,juce::String& id,juce::String& error){return model.createCylinder(r,h,id,error);}
bool CadCommandService::setPlacement(const juce::String& id,double x,double y,double z,double rx,double ry,double rz,juce::String& error){return model.setPlacement(id,x,y,z,rx,ry,rz,error);}
bool CadCommandService::setDimensions(const juce::String& id,double w,double d,double h,double r,juce::String& error){return model.setDimensions(id,w,d,h,r,error);}
bool CadCommandService::booleanOp(const juce::String& k,const juce::String& a,const juce::String& b,juce::String& id,juce::String& error){return model.booleanOp(k,a,b,id,error);}
}

