#pragma once

#include <JuceHeader.h>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include "CadMechanics.h"
#include "CadConstruction.h"
#include "CadMaterials.h"
#include "CadSheetStock.h"
#include "CadEngineeringCatalog.h"
#include "CadAnalysisCases.h"
#include "CadComponents.h"
#include "CadExport.h"
#include "CadCraftKit.h"

#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace djehuti::cad
{
// Millimetres: comfortably above OCCT confusion, without imposing a screen-size minimum.
inline constexpr double minimumSketchCircleRadius = 1.0e-6;
// Construction origins of the faces of an evaluated shape: each face carries tokens "<featureId>/<role>" naming
// the feature that generated it and its role there (box "top", extrude "end", "side:<sketch entity>", ...).
// Tokens follow faces through Booleans and placements via OCCT's modified/deleted history; they are how a
// face is named persistently. Derived on every rebuild, never stored as geometry.
using CadFaceOrigins = std::vector<std::pair<TopoDS_Shape, juce::StringArray>>;

struct CadRevolveParameters
{
    juce::String axisType = "sketchV";
    std::array<double, 3> origin { 0, 0, 0 }, direction { 0, 1, 0 };
    double angleDegrees = 360.0;
    bool reverse = false;
    juce::StringArray entities; // Explicit contour membership; empty preserves legacy whole-sketch Revolve.
    juce::String axisSketch, axisEntity;
};
juce::var revolveParametersToJson(const CadRevolveParameters&);
bool readRevolveParameters(const juce::var&, CadRevolveParameters&, juce::String& error);
juce::var latheProfileFromText(const juce::String& outer, const juce::String& inner, juce::String& error);
juce::String latheStationsText(const juce::var& stations);

struct CadHoleParameters
{
    juce::String face, style = "plain";
    double u = 0, v = 0, diameter = 6, depth = 10;
    bool throughAll = false;
    double headDiameter = 12, headDepth = 3, countersinkAngleDegrees = 90;
};
juce::var holeParametersToJson(const CadHoleParameters&);
bool readHoleParameters(const juce::var&, CadHoleParameters&, juce::String& error);
inline bool isReplicationType(const juce::String& type) { return type=="mirror"||type=="linearPattern"||type=="circularPattern"; }
juce::var replicationDefaults(const juce::String& type);
bool validateReplication(const juce::String& type, const juce::var&, juce::String& error);
juce::StringArray replicationDependencies(const juce::var&);
inline bool isSurfaceType(const juce::String& type) { return juce::StringArray{"surface","surface_trim","surface_extend","surface_split","surface_split_region","surface_sew","surface_solidify","surface_thicken","surface_offset","surface_hollow"}.contains(type); }
// "gear": parametric gear solid (CadGear.h), stored and edited through the same modeling command boundary.
inline bool isModelingType(const juce::String& type) { return type=="sweep"||type=="loft"||type=="mill"||type=="gear"||type=="din_rail"||type=="din_bottom"||type=="din_clip"||type=="din_screw"||type=="tslot_rail"||type=="tslot_half"||isSurfaceType(type); }
namespace gear { struct PairPlan; }
juce::var surfaceDefaults(const juce::String&);
bool validateSurface(const juce::String&,const juce::var&,juce::String&);
juce::var surfaceTopology(const TopoDS_Shape&);
juce::String modelingLabel(const juce::String&);
juce::var modelingDefaults(const juce::String& type);
bool validateModeling(const juce::String& type,const juce::var&,juce::String& error);
juce::StringArray modelingDependencies(const juce::var&);

struct CadFeature
{
    juce::String id, name, type;
    juce::String firstOperand, secondOperand;
    juce::String sketchId, targetFeatureId;
    double width = 10.0, depth = 10.0, height = 10.0, radius = 5.0;
    bool reverse = false, throughAll = false;
    double x = 0.0, y = 0.0, z = 0.0, rx = 0.0, ry = 0.0, rz = 0.0;
    bool visible = true;
    juce::StringArray edges;  // for fillet: persistent edge reference tokens
    CadRevolveParameters revolve;
    bool lathe = false; // Lathe workflow; the editable source sketch remains the profile authority.
    CadHoleParameters hole;
    juce::var replication; // Structured, versioned Mirror/Pattern definition; OCAF is authoritative.
    juce::var modeling; // Sweep/Loft/Router definitions, persisted through the same OCAF command boundary.
    TopoDS_Shape shape;
    CadFaceOrigins faceOrigins;   // derived with shape
};

// type "line": p1 = (x1, y1), p2 = (x2, y2). type "circle": centre (x1, y1), radius.
// type "bezier": cubic Bezier from p1 to p2 with control points c1 = (cx1, cy1) and c2 = (cx2, cy2); an OCCT
// Geom_BezierCurve edge in profiles. Endpoints are referenced as "<id>:p1" / "<id>:p2", controls as ":c1" / ":c2".
// Endpoints are not shared objects: connections are coincident constraints (see docs/SKETCH_EDITING_ARCHITECTURE.md).
struct CadSketchEntity
{
    juce::String id, type, groupId, mirrorOwner;
    bool construction=false;
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0, radius = 1.0;
    double cx1 = 0.0, cy1 = 0.0, cx2 = 0.0, cy2 = 0.0;
    // Ellipse semiaxes and local rotation in radians; circles keep their original representation.
    double radiusY = 1.0, angle = 0.0;
};

struct CadSketchConstraint
{
    juce::String id, type, firstEntity, secondEntity;
    double value = 0.0;
};

struct CadSketchMirror
{
    juce::String id,axis="both",axisEntity;
    juce::StringArray sources;
    double cx=0,cy=0;
    bool live=false;
};
struct CadSketch
{
    juce::String id, plane = "XY";    // "XY", "XZ", "YZ", or "FACE" for a sketch attached to a model face
    double offset = 0.0;
    // Face attachment: the planar face named by its construction token on supportFeature (see CadFaceOrigins).
    juce::String supportFeature, supportFace;
    bool onFace() const { return supportFeature.isNotEmpty(); }
    std::vector<CadSketchEntity> entities;
    std::vector<CadSketchConstraint> constraints;
    juce::String symmetryMode="off";double symmetryX=0,symmetryY=0;
    std::vector<CadSketchMirror> mirrors;
    // Display only: features, other sketches and assembly instances shown as background references while this
    // sketch is edited. Empty for a new sketch. Never part of the geometry signature, so changing it rebuilds nothing.
    juce::StringArray references;
};

struct CadMesh
{
    juce::String featureId;
    bool doubleSided = false;
    int faceIndex = -1;   // face of the feature's shape in TopExp order (transient render index)
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
};

// A polyline in feature coordinates: a model edge (edgeIndex in the feature's unique-edge order) or a sketch outline.
struct CadPolyline
{
    juce::String featureId;
    int edgeIndex = -1;
    std::vector<float> points;   // x, y, z triples
};

struct CadLocalFrame
{
    juce::String id, name;
    std::array<float,16> matrix {};
};

struct CadRenderSnapshot
{
    std::uint64_t revision = 0;
    std::uint64_t documentInstance = 0;   // which document (New/Open) the meshes belong to
    std::vector<CadMesh> meshes;
    std::vector<CadLocalFrame> localFrames;
    std::vector<CadPolyline> edges;           // model edges, for edge picking and highlighting
    std::vector<CadPolyline> sketchOutlines;  // visible sketches drawn in their resolved 3-D plane
};

// A face of a feature resolved for display, picking and automation.
struct CadFaceInfo
{
    juce::String featureId, token;   // token empty: the face has no persistent name and cannot support a sketch
    int faceIndex = -1;
    bool planar = false;
    gp_Pnt centre;
    gp_Dir normal { 0, 0, 1 };
    double area = 0.0;
};

// Resolved local frame of a sketch: world points are origin + u * U + v * V; normal is the sketch normal.
struct CadSketchFrame
{
    gp_Pnt origin;
    gp_Dir u { 1, 0, 0 }, v { 0, 1, 0 }, normal { 0, 0, 1 };
};

struct CadPersistentReference
{
    juce::String ownerFeatureId;
    juce::String topologyKind;
    juce::String semanticRole;
};

// Joint authoring input in document units: world millimetres for the joint origin, a world direction for the
// joint +Z axis, both at the reference configuration; coordinates and limits in radians or metres.
struct CadJointRequest
{
    juce::String name, type, parentInstance, childInstance;
    double originX = 0.0, originY = 0.0, originZ = 0.0, axisX = 0.0, axisY = 0.0, axisZ = 1.0;
    double reference = 0.0;
    bool hasLimits = false;
    double lower = 0.0, upper = 0.0;
};

class CadDocument
{
public:
    CadDocument();

    void clear();
    bool createBox(double width, double depth, double height, juce::String& id, juce::String& error);
    bool createCylinder(double radius, double height, juce::String& id, juce::String& error);
    bool createSketch(const juce::String& plane, double offset, juce::String& id, juce::String& error);
    // Sketch on the planar face of featureId named by faceToken (from listFaces/describeFace).
    bool createFaceSketch(const juce::String& featureId, const juce::String& faceToken, juce::String& id, juce::String& error);
    bool listFaces(const juce::String& featureId, std::vector<CadFaceInfo>& faces, juce::String& error) const;
    bool describeFace(const juce::String& featureId, int faceIndex, CadFaceInfo& face, juce::String& error) const;
    bool describeEdge(const juce::String& featureId, int edgeIndex, juce::String& description, juce::String& error) const;
    bool sketchFrame(const juce::String& sketchId, CadSketchFrame& frame) const;
    juce::var sketchSymmetryState(const juce::String&) const;
    bool sketchSymmetryCommand(const juce::String&,const juce::var&,juce::String&);
    // Replaces the sketch's background reference list (one undoable command; unchanged lists are not recorded).
    bool setSketchReferences(const juce::String& sketchId, const juce::StringArray& ids, juce::String& error);
    bool addSketchLine(const juce::String& sketchId, double x1, double y1, double x2, double y2, juce::String& entityId, juce::String& error, const juce::String& firstConnection = {}, const juce::String& secondConnection = {});
    bool addSketchRectangle(const juce::String& sketchId, double x, double y, double width, double height, juce::String& groupId, juce::String& error);
    bool addSketchCircle(const juce::String& sketchId, double x, double y, double radius, juce::String& entityId, juce::String& error);
    bool editSketchGeometry(const juce::String& sketchId, const juce::StringArray& selection,
                            const juce::String& operation, const juce::var& parameters,
                            juce::StringArray& resultIds, juce::String& error);
    // Removes entities and their constraints atomically; dependent rebuild failures roll back.
    bool deleteSketchEntities(const juce::String& sketchId, const juce::StringArray& entityIds, juce::String& error);
    bool setSketchEntityDimension(const juce::String& sketchId, const juce::String& entityId, double value, juce::String& error);
    bool setSketchEntityPosition(const juce::String& sketchId, const juce::String& entityId, double x, double y, juce::String& error);
    bool addSketchConstraint(const juce::String& sketchId, const juce::String& type, const juce::String& firstEntity, const juce::String& secondEntity, double value, juce::String& constraintId, juce::String& error);
    // Interactive editing; each call is one undoable command.
    // Position and dimension of an entity together (the entity panel's Apply).
    bool editSketchEntity(const juce::String& sketchId, const juce::String& entityId, bool move, double x, double y, bool resize, double value, juce::String& error);
    // Atomically edits both line endpoints, carries their joins, updates an existing length driver and
    // rejects incompatible constraints. Length/direction editors derive these same sketch-local endpoints.
    bool setSketchLineGeometry(const juce::String& sketchId, const juce::String& entityId, double x1, double y1, double x2, double y2, juce::String& error);
    // Moves a point "<entity>:p1|p2|c1|c2". An endpoint carries every endpoint joined to it (coincident constraints,
    // transitively, and endpoints at exactly the same position) and a Bezier endpoint carries its control point;
    // a rectangle corner reshapes the rectangle about the opposite corner. Refused if constraints cannot hold.
    bool moveSketchPoint(const juce::String& sketchId, const juce::String& pointRef, double x, double y, juce::String& error);
    // Splits a line at the point of it nearest (x, y): the line keeps its id up to the new vertex, a new line
    // continues to the old end, joined by a coincident constraint. The split line's own orientation/length
    // constraints are removed and named in `removed`; a split rectangle becomes a polygon.
    bool insertSketchVertex(const juce::String& sketchId, const juce::String& lineId, double x, double y, juce::String& newLineId, juce::StringArray& removed, juce::String& error);
    bool addSketchBezier(const juce::String& sketchId, double x1, double y1, double cx1, double cy1, double cx2, double cy2, double x2, double y2, juce::String& entityId, juce::String& error, const juce::String& firstConnection = {}, const juce::String& secondConnection = {});
    // Tangent (G1) join at one end of a Bezier: its control point at that end is turned onto the direction of the
    // segment connected there, keeping the handle length. A one-time edit, not a persistent constraint.
    bool smoothSketchJoint(const juce::String& sketchId, const juce::String& bezierId, const juce::String& end, juce::String& error);
    bool createExtrude(const juce::String& sketchId, double depth, bool reverse, juce::String& id, juce::String& error);
    bool createRevolve(const juce::String& sketchId, const CadRevolveParameters&, juce::String& id, juce::String& error);
    bool updateRevolve(const juce::String& id, const juce::String& sketchId, const CadRevolveParameters&, juce::String& error);
    juce::var latheCommand(const juce::var& args);
    juce::var latheContours(const juce::String& sketchId) const;
    std::shared_ptr<const CadRenderSnapshot> lathePreview(const juce::var&, juce::var& result, juce::String& error) const;
    bool revolveAxis(const juce::String& sketchId, const CadRevolveParameters&, gp_Pnt&, gp_Dir&, juce::String& error) const;
    juce::var chamferCommand(const juce::var& args);
    juce::var holeCommand(const juce::var& args);
    juce::var replicationCommand(const juce::String& type, const juce::var& args);
    juce::var modelingCommand(const juce::String& type,const juce::var& args);
    std::shared_ptr<const CadRenderSnapshot> modelingPreview(const juce::String& type,const juce::var&,juce::var& result,juce::String& error) const;
    std::shared_ptr<const CadRenderSnapshot> replicationPreview(const juce::String& type, const juce::var&, juce::var& result, juce::String& error) const;
    // Parametric gears (CadGearCommands.cpp). cad_gear: one gear feature through modelingCommand("gear") plus
    // derived dimensions and pair status. cad_gear_pair: plan/preview/create/update/check/couple/unpair a
    // compatible external spur pair; create and update are single undoable transactions.
    juce::var gearCommand(const juce::var& args);
    juce::var gearPairCommand(const juce::var& args);
    // Parametric DIN-rail system (CadDinCommands.cpp): rail, module bottom, clip and friction screw features, and modules.
    juce::var dinCommand(const juce::var& args);
    // Parametric T-slot structural system (CadTSlotCommands.cpp): rails, connectors and their materials.
    juce::var tslotCommand(const juce::var& args);
    // Construction references (CadConstruction.cpp): document-owned points, axes, coordinate systems and planes, plus
    // reference-based move/rotate/align/mirror/scale. Each mutation is one undoable command.
    const std::vector<CadConstructionRef>& constructionData() const { return constructionRefs; }
    const CadConstructionRef* findConstruction(const juce::String& id) const;
    CadResolvedRef resolveConstruction(const juce::String& id) const;
    juce::var constructionToJson(const CadConstructionRef&) const;
    juce::var referenceCommand(const juce::var& args);
    juce::var transformCommand(const juce::var& args);
    std::vector<CadSnapPoint> snapPoints(const juce::String& bodyId) const;   // body or occurrence id, world millimetres
    bool transformBody(const juce::String& id, const gp_Trsf& delta, juce::String& error);   // rigid placement change of a body or occurrence
    juce::var gearPairStatus(const juce::String& gearId) const;
    // Current pair definition (cad_gear_pair parameters) of the pair containing gearId; void when unpaired.
    juce::var gearPairParameters(const juce::String& gearId) const;
    std::shared_ptr<const CadRenderSnapshot> gearPairPreview(const juce::var& args, juce::var& result, juce::String& error) const;
    std::shared_ptr<const CadRenderSnapshot> chamferPreview(const juce::var& args, juce::var& result, juce::String& error) const;
    bool createCut(const juce::String& sketchId, const juce::String& targetFeatureId, double depth, bool throughAll, bool reverse, juce::String& id, juce::String& error);
    bool createFillet(const juce::String& targetFeatureId, const std::vector<int>& edgeIndices, double radius, juce::String& id, juce::String& error);
    bool updateFillet(const juce::String& id, const juce::String& targetFeatureId, const std::vector<int>& edgeIndices, double radius, juce::String& error);
    std::vector<int> filletEdgeIndices(const juce::String& id) const;
    bool setPlacement(const juce::String& id, double x, double y, double z, double rx, double ry, double rz, juce::String& error);
    bool setDimensions(const juce::String& id, double width, double depth, double height, double radius, juce::String& error);
    bool booleanOp(const juce::String& kind, const juce::String& first, const juce::String& second, juce::String& id, juce::String& error);
    bool rename(const juce::String& id, const juce::String& name, juce::String& error);
    bool setVisible(const juce::String& id, bool visible, juce::String& error);
    bool remove(const juce::String& id, juce::String& error);
    bool createInstance(const juce::String& featureId, const juce::String& name, juce::String& id, juce::String& error);
    bool setInstancePlacement(const juce::String& id, const CadFrame& placement, juce::String& error);
    bool removeInstance(const juce::String& id, juce::String& error);
    bool renameMechanical(const juce::String& id, const juce::String& name, juce::String& error);
    bool createJoint(const CadJointRequest& request, juce::String& id, juce::String& error);
    bool updateJoint(const juce::String& id, const CadJointRequest& request, juce::String& error);
    bool removeJoint(const juce::String& id, juce::String& error);
    bool setJointMapping(const CadSignalMapping& mapping, juce::String& error);
    bool removeJointMapping(const juce::String& jointId, juce::String& error);
    bool setInstanceGrounded(const juce::String& id, bool grounded, juce::String& error);
    bool setJointRole(const juce::String& jointId, const juce::String& role, double lockValue, bool enforceLimits, juce::String& error);
    bool createCoupling(const CadCoupling& coupling, juce::String& id, juce::String& error);
    bool updateCoupling(const juce::String& id, const CadCoupling& coupling, juce::String& error);
    bool removeCoupling(const juce::String& id, juce::String& error);
    // IK task definitions are authored data: one undoable transaction each. Targets without an id get one.
    bool createIkTask(const CadIkTask& task, juce::String& id, juce::String& error);
    bool updateIkTask(const juce::String& id, const CadIkTask& task, juce::String& error);
    bool removeIkTask(const juce::String& id, juce::String& error);
    // Physical materials (CadMaterials.h). Each call is one undoable document command; none changes geometry.
    bool createMaterial(const CadMaterial& definition, juce::String& id, juce::String& error);
    bool updateMaterial(const juce::String& id, const CadMaterial& definition, juce::String& error);   // id kept, revision advances
    bool removeMaterial(const juce::String& id, bool clearAssignments, juce::String& error);            // refused while assigned unless clearAssignments
    bool assignMaterial(const juce::String& featureId, const juce::String& materialId, juce::String& error);
    bool clearMaterialAssignment(const juce::String& featureId, juce::String& error);
    bool createStock(const CadSheetStock&, juce::String& id, juce::String& error);
    bool updateStock(const juce::String& id, const CadSheetStock&, juce::String& error);
    bool removeStock(const juce::String& id, juce::String& error);
    bool setDefaultStock(const juce::String& id, juce::String& error); // empty clears
    bool importMaterial(const CadMaterial&, const juce::String& featureId, juce::String& id, juce::String& error);
    bool importStock(const CadSheetStock&, const CadMaterial&, bool selectDefault, juce::String& id, juce::String& error);
    // Structural analysis cases (CadAnalysisCases.h): definitions only, one undoable command each. Create and update
    // refuse cases whose references do not resolve now; later breakage is reported by analysis::referenceProblems.
    bool createAnalysisCase(const CadAnalysisCase& definition, juce::String& id, juce::String& error);
    bool updateAnalysisCase(const juce::String& id, const CadAnalysisCase& definition, juce::String& error);   // id kept, revision advances
    bool removeAnalysisCase(const juce::String& id, juce::String& error);
    bool undo(juce::String& error);
    bool redo(juce::String& error);
    bool save(const juce::File& file, juce::String& error);
    bool load(const juce::File& file, juce::String& error);
    bool exportJson(const juce::File& file, juce::String& error) const;
    bool makeRepresentativeReference(const juce::String& featureId, CadPersistentReference& reference, juce::String& error);
    bool resolveReference(const CadPersistentReference& reference, juce::String& error) const;
    bool referenceWorld(const juce::String& instanceId, gp_Trsf& world, juce::String& error) const;

    // Fixtures may explicitly display a referenced hidden body without changing source visibility.
    std::shared_ptr<const CadRenderSnapshot> snapshot(bool includeHidden = false) const;
    // Maps a body's evaluated shape (which already carries its authored placement) onto an instance of it whose
    // frame is instanceWorld. Shared by the runtime pose and export so each placement is applied exactly once.
    gp_Trsf instanceShapeTransform(const juce::String& featureId, const gp_Trsf& instanceWorld) const;
    const std::vector<CadFeature>& features() const { return items; }
    const CadFeature* find(const juce::String& id) const;
    const CadSketch* findSketch(const juce::String& id) const;
    const std::vector<CadSketch>& sketchData() const { return sketches; }
    const CadMechanics& mechanicsData() const { return mechanics; }
    const CadMaterials& materialsData() const { return materials; }
    const CadSheetStocks& stocksData() const { return sheetStocks; }
    const CadAnalysisCases& analysisData() const { return analysisCases; }
    const std::optional<kit::CadCraftKitMetadata>& kitData() const { return kitMetadata; }
    bool setKitMetadata(const kit::CadCraftKitMetadata& meta, juce::String& error);
    juce::var componentData() const;
    const juce::var* componentFor(const juce::String& feature) const;
    bool insertComponent(const juce::var& record,const CadFrame& placement,juce::String& instance,juce::String& error);
    // Multi-part component: one definition and occurrence per part, the first (or joint parent) part grounded, and the
    // record's joint between parts, all in one undoable transaction. instance is the grounded part's occurrence.
    bool insertComponentAssembly(const juce::var& record,const CadFrame& placement,juce::String& instance,juce::String& error);
    bool updateCraftStock(const juce::String& feature, const juce::var& geometry, juce::String& error);
    bool addCraftCut(const juce::String& feature, const juce::var& cutSpec, juce::String& error);
    bool clearCraftCuts(const juce::String& feature, juce::String& error);
    bool alignInstances(const juce::String& targetInstance, const juce::String& sourceInstance, const juce::String& alignType, const juce::var& options, juce::String& error);
    bool createAssemblyGroup(const juce::String& name, const std::vector<juce::String>& instanceIds, juce::String& groupId, juce::String& error);
    bool addToAssemblyGroup(const juce::String& groupId, const std::vector<juce::String>& instanceIds, juce::String& error);
    bool removeFromAssemblyGroup(const juce::String& groupId, const std::vector<juce::String>& instanceIds, juce::String& error);
    bool moveAssemblyGroup(const juce::String& groupId, const CadFrame& deltaPlacement, juce::String& error);
    bool createBondedJoint(const CadBondedJoint& spec, juce::String& jointId, juce::String& error);
    bool updateBondedJoint(const juce::String& jointId, const CadBondedJoint& spec, juce::String& error);
    bool removeBondedJoint(const juce::String& jointId, juce::String& error);
    bool reevaluateBondedJoints(juce::String& error);
    bool moveBondedGroup(const juce::String& instanceId, const CadFrame& deltaPlacement, juce::String& error);
    juce::var adhesiveBom() const;
    juce::var generateCutList() const;
    std::uint64_t revision() const { return documentRevision; }
    // Changes on every New and Open (never reused within a process), unlike revision numbers that undo can restore.
    std::uint64_t documentInstance() const { return instanceId; }
    bool modified() const { return dirty; }
    void markSaved() { savedState = serialize(); dirty = false; }
    void setFile(const juce::File& file) { documentFile = file; }
    juce::File file() const { return documentFile; }

public:
    struct CadTopologyEvolution
    {
        juce::String featureId;
        std::vector<std::pair<TopoDS_Shape, TopoDS_Shape>> generated;
        std::vector<std::pair<TopoDS_Shape, TopoDS_Shape>> modified;
        std::vector<TopoDS_Shape> deleted;
    };

private:
    bool mutate(std::function<bool(juce::String&)> action, juce::String& error);
    bool rebuild(juce::String& error);
    TopoDS_Shape evaluate(const juce::String& id, juce::String& error, juce::StringArray& active, CadFaceOrigins* origins = nullptr);
    TopoDS_Shape place(const TopoDS_Shape& source, const CadFeature& feature, CadFaceOrigins* origins = nullptr) const;
    gp_Trsf placement(const CadFeature& feature) const;
    bool resolveSketchFrame(const CadSketch& sketch, CadSketchFrame& frame, juce::String& error, juce::StringArray& active);
    bool supportFace(const CadFeature& feature, const juce::String& token, TopoDS_Face& face, juce::String& error) const;
    juce::String serialize() const;
    bool deserialize(const juce::String& json, juce::String& error);
    juce::String serializeSketches() const;
    bool deserializeSketches(const juce::String& json, juce::String& error);
    bool restoreMechanicsFromOCAF(juce::String& error);
    bool restoreMaterialsFromOCAF(juce::String& error);
    bool restoreStocksFromOCAF(juce::String& error);
    bool restoreAuthoredFromOCAF(juce::String& error);
    bool restoreAnalysisFromOCAF(juce::String& error);
    bool restoreKitFromOCAF(juce::String& error);
    bool readComponentData(const juce::var&,juce::String& error);
    bool restoreComponentsFromOCAF(juce::String& error);
    void writeComponentsToOCAF();
    bool materialAssignmentsResolve(juce::String& error) const;
    bool loadFile(const juce::File& file, juce::String& error);
    bool jointFramesFromRequest(const CadJointRequest& request, CadJoint& joint, juce::String& error) const;
    CadSketch* mutableSketch(const juce::String& id);
    void registerSketchSymmetry(CadSketch&,const juce::StringArray&);
    bool updateSketchMirrors(CadSketch&,juce::String&) const;
    bool solveSketch(CadSketch& sketch, juce::String& error) const;
    bool validateSketch(const CadSketch& sketch, juce::String& error) const;
    bool sketchProfiles(const CadSketch& sketch, const CadSketchFrame& frame, std::vector<TopoDS_Face>& faces, juce::String& error, std::vector<std::pair<TopoDS_Shape, juce::String>>* edgeEntities = nullptr) const;
    TopoDS_Shape extrudeSketch(const CadSketch& sketch, const CadSketchFrame& frame, double depth, bool reverse, juce::String& error, CadFaceOrigins* origins = nullptr, const juce::String& owner = {}) const;
    TopoDS_Shape revolveSketch(const CadFeature&, const CadSketch&, const CadSketchFrame&, CadFaceOrigins&, juce::String& error);
    bool holeSupport(const juce::String& target, const juce::String& face, CadSketchFrame&, TopoDS_Face&, juce::String& error, juce::StringArray& active);
    TopoDS_Shape drillShape(const CadFeature&, const TopoDS_Shape& target, const CadFaceOrigins&, CadFaceOrigins&, juce::String& error, juce::StringArray& active);
    TopoDS_Shape replicateShape(const CadFeature&, CadFaceOrigins&, juce::String& error, juce::StringArray& active);
    bool replicationEdge(const CadFeature&, const juce::String& reference, gp_Pnt& origin, gp_Dir& direction, bool axis, juce::String& error) const;
    bool modelingEdge(const CadFeature&,const juce::String&,TopoDS_Edge&,juce::String& error) const;
    TopoDS_Wire modelingPath(const juce::var&,const CadSketchFrame* overrideFrame,juce::String&,juce::StringArray& active);
    bool modelingSection(const juce::String&,TopoDS_Face&,CadSketchFrame&,juce::String&,juce::StringArray& active);
    TopoDS_Shape evaluateModeling(const CadFeature&,CadFaceOrigins&,juce::String&,juce::StringArray& active,bool surfaceOutput=false);
    TopoDS_Shape evaluateSurface(const CadFeature&,CadFaceOrigins&,juce::String&,juce::StringArray& active);
    bool surfaceCutTool(const juce::var& reference,TopoDS_Shape& shape,CadFaceOrigins& names,juce::String& error,juce::StringArray& active);
    bool splitRegionAnchors(const juce::var& parameters,juce::Array<juce::var>& anchors,juce::String& error) const;
    bool modelingProfile(const juce::String&,TopoDS_Wire&,CadSketchFrame&,juce::String&,juce::StringArray&);
    juce::var gearPairRequest(const CadFeature& a, const CadFeature& b) const;
    bool constructionSource(const juce::String& id, gp_Trsf& frame, gp_Trsf& shapeTransform, const CadFeature*& feature, juce::String& why) const;
    CadResolvedRef resolveConstructionImpl(const CadConstructionRef&, juce::StringArray visiting) const;
    bool buildConstruction(const juce::var& definition, const CadConstructionRef* existing, CadConstructionRef& out, juce::String& error) const;
    bool applyRigidDelta(const juce::String& id, const gp_Trsf& delta, juce::String& error);
    bool restoreConstructionFromOCAF(juce::String& error);
    std::vector<CadConstructionRef> constructionRefs;
    bool applyGearPair(const gear::PairPlan&, const juce::String& existingA, const juce::String& existingB, juce::var& reply, juce::String& error);
    bool gearShaft(const CadFeature& gearFeature, const juce::String& instanceId, gp_Pnt& origin, gp_Dir& axis) const;
    bool surfaceInput(const juce::String&,const juce::String&,TopoDS_Shape&,CadFaceOrigins&,juce::String&,juce::StringArray&);
    static CadSketchFrame originPlaneFrame(const CadSketch& sketch);
    void initializeOCAF();
    bool syncOCAFFromItems(juce::String& error);
    // Reads the feature definitions back from OCAF (authoritative). When every definition equals one in
    // `evaluated` (same order), that evaluated geometry and its face names are reused instead of rebuilding.
    bool syncItemsFromOCAF(juce::String& error, const std::vector<CadFeature>* evaluated = nullptr);
    bool saveNative(const juce::File& file, juce::String& error);
    bool loadNative(const juce::File& file, juce::String& error);
    void invalidate();
    static juce::String newId(const char* prefix, int number);

    std::vector<CadFeature> items;
    std::vector<CadSketch> sketches;
    std::map<juce::String, CadSketchFrame> sketchFrames;   // derived on rebuild
    CadMechanics mechanics;
    CadMaterials materials;
    CadSheetStocks sheetStocks;
    CadAnalysisCases analysisCases;
    std::optional<kit::CadCraftKitMetadata> kitMetadata;
    std::vector<CadComponentDefinition> componentDefinitions;
    static constexpr int componentContainerTag=9;
    std::vector<CadPersistentReference> references;
    std::vector<CadTopologyEvolution> operationHistory;
    Handle(TDocStd_Application) ocafApplication;
    Handle(TDocStd_Document) ocafDocument;
    std::uint64_t documentRevision = 0;
    std::uint64_t instanceId = 0;
    int nextNumber = 1;
    juce::String savedState;
    bool dirty = false;
    juce::File documentFile;
    mutable std::shared_ptr<CadRenderSnapshot> cachedSnapshot;
    mutable std::uint64_t cachedRevision = 0;
    // Results of the rebuild pass in progress (feature id -> placed shape and face origins). Each feature is
    // evaluated once per pass however many features consume it; null outside rebuild().
    struct EvaluatedFeature { TopoDS_Shape shape; CadFaceOrigins origins; };
    std::map<juce::String, EvaluatedFeature>* evaluationPass = nullptr;
    // Evaluated features kept across rebuilds. An entry is reused only while its signature (the feature's exact
    // definition plus, recursively, its operands, the sketches it uses and face-sketch supports) is unchanged, so
    // any upstream edit invalidates every dependent. Derived data only; OCAF stays authoritative.
    struct CachedFeature { juce::String signature; TopoDS_Shape shape; CadFaceOrigins origins; std::vector<CadTopologyEvolution> history; };
    std::map<juce::String, CachedFeature> geometryCache;
    std::map<juce::String, juce::String>* signaturePass = nullptr;
    juce::String geometrySignature(const juce::String& id, juce::StringArray& visiting) const;
    juce::String sketchSignature(const juce::String& sketchId, juce::StringArray& visiting) const;
};

class CadCommandService
{
public:
    bool sketchSymmetryCommand(const juce::String& id,const juce::var& args,juce::String& error){return model.sketchSymmetryCommand(id,args,error);}
    juce::var sketchSymmetryState(const juce::String& id) const{return model.sketchSymmetryState(id);}
    bool setSketchReferences(const juce::String& sketchId,const juce::StringArray& ids,juce::String& error){return model.setSketchReferences(sketchId,ids,error);}
    CadDocument& document() { return model; }
    const CadDocument& document() const { return model; }
    bool newDocument();
    bool createBox(double w, double d, double h, juce::String& id, juce::String& error);
    bool createCylinder(double radius, double height, juce::String& id, juce::String& error);
    bool createSketch(const juce::String& plane, double offset, juce::String& id, juce::String& error) { return model.createSketch(plane, offset, id, error); }
    bool createFaceSketch(const juce::String& featureId, const juce::String& faceToken, juce::String& id, juce::String& error) { return model.createFaceSketch(featureId, faceToken, id, error); }
    bool listFaces(const juce::String& featureId, std::vector<CadFaceInfo>& faces, juce::String& error) const { return model.listFaces(featureId, faces, error); }
    bool describeFace(const juce::String& featureId, int faceIndex, CadFaceInfo& face, juce::String& error) const { return model.describeFace(featureId, faceIndex, face, error); }
    bool describeEdge(const juce::String& featureId, int edgeIndex, juce::String& description, juce::String& error) const { return model.describeEdge(featureId, edgeIndex, description, error); }
    // [{ index, name (persistent face token, or "" if none), planar, normal [x,y,z], centre [x,y,z] mm, area mm2 }]
    bool listFacesJson(const juce::String& featureId, juce::String& json, juce::String& error) const;
    bool addSketchLine(const juce::String& sketchId, double x1, double y1, double x2, double y2, juce::String& entityId, juce::String& error, const juce::String& firstConnection = {}, const juce::String& secondConnection = {}) { return model.addSketchLine(sketchId, x1, y1, x2, y2, entityId, error, firstConnection, secondConnection); }
    bool addSketchRectangle(const juce::String& sketchId, double x, double y, double width, double height, juce::String& groupId, juce::String& error) { return model.addSketchRectangle(sketchId, x, y, width, height, groupId, error); }
    bool addSketchCircle(const juce::String& sketchId, double x, double y, double radius, juce::String& entityId, juce::String& error) { return model.addSketchCircle(sketchId, x, y, radius, entityId, error); }
    bool deleteSketchEntities(const juce::String& sketchId, const juce::StringArray& entityIds, juce::String& error) { return model.deleteSketchEntities(sketchId, entityIds, error); }
    bool setSketchEntityDimension(const juce::String& sketchId, const juce::String& entityId, double value, juce::String& error) { return model.setSketchEntityDimension(sketchId, entityId, value, error); }
    bool setSketchEntityPosition(const juce::String& sketchId, const juce::String& entityId, double x, double y, juce::String& error) { return model.setSketchEntityPosition(sketchId, entityId, x, y, error); }
    bool addSketchConstraint(const juce::String& sketchId, const juce::String& type, const juce::String& firstEntity, const juce::String& secondEntity, double value, juce::String& constraintId, juce::String& error) { return model.addSketchConstraint(sketchId, type, firstEntity, secondEntity, value, constraintId, error); }
    bool editSketchEntity(const juce::String& sketchId, const juce::String& entityId, bool move, double x, double y, bool resize, double value, juce::String& error) { return model.editSketchEntity(sketchId, entityId, move, x, y, resize, value, error); }
    bool setSketchLineGeometry(const juce::String& sketchId, const juce::String& entityId, double x1, double y1, double x2, double y2, juce::String& error) { return model.setSketchLineGeometry(sketchId, entityId, x1, y1, x2, y2, error); }
    bool moveSketchPoint(const juce::String& sketchId, const juce::String& pointRef, double x, double y, juce::String& error) { return model.moveSketchPoint(sketchId, pointRef, x, y, error); }
    bool insertSketchVertex(const juce::String& sketchId, const juce::String& lineId, double x, double y, juce::String& newLineId, juce::StringArray& removed, juce::String& error) { return model.insertSketchVertex(sketchId, lineId, x, y, newLineId, removed, error); }
    bool addSketchBezier(const juce::String& sketchId, double x1, double y1, double cx1, double cy1, double cx2, double cy2, double x2, double y2, juce::String& entityId, juce::String& error, const juce::String& firstConnection = {}, const juce::String& secondConnection = {}) { return model.addSketchBezier(sketchId, x1, y1, cx1, cy1, cx2, cy2, x2, y2, entityId, error, firstConnection, secondConnection); }
    bool smoothSketchJoint(const juce::String& sketchId, const juce::String& bezierId, const juce::String& end, juce::String& error) { return model.smoothSketchJoint(sketchId, bezierId, end, error); }
    bool createExtrude(const juce::String& sketchId, double depth, bool reverse, juce::String& id, juce::String& error) { return model.createExtrude(sketchId, depth, reverse, id, error); }
    bool createRevolve(const juce::String& sketchId, const CadRevolveParameters& p, juce::String& id, juce::String& error) { return model.createRevolve(sketchId, p, id, error); }
    bool updateRevolve(const juce::String& id, const juce::String& sketchId, const CadRevolveParameters& p, juce::String& error) { return model.updateRevolve(id, sketchId, p, error); }
    juce::var revolveCommand(const juce::var& args);
    juce::var latheCommand(const juce::var& args) { return model.latheCommand(args); }
    juce::var chamferCommand(const juce::var& args) { return model.chamferCommand(args); }
    juce::var holeCommand(const juce::var& args) { return model.holeCommand(args); }
    juce::var replicationCommand(const juce::String& type, const juce::var& args) { return model.replicationCommand(type,args); }
    juce::var modelingCommand(const juce::String& type,const juce::var& args) { return model.modelingCommand(type,args); }
    juce::var gearCommand(const juce::var& args) { return model.gearCommand(args); }
    juce::var dinCommand(const juce::var& args) { return model.dinCommand(args); }
    juce::var tslotCommand(const juce::var& args) { return model.tslotCommand(args); }
    juce::var gearPairCommand(const juce::var& args) { return model.gearPairCommand(args); }
    juce::var referenceCommand(const juce::var& args) { return model.referenceCommand(args); }
    juce::var transformCommand(const juce::var& args) { return model.transformCommand(args); }
    bool createCut(const juce::String& sketchId, const juce::String& targetFeatureId, double depth, bool throughAll, bool reverse, juce::String& id, juce::String& error) { return model.createCut(sketchId, targetFeatureId, depth, throughAll, reverse, id, error); }
    bool createFillet(const juce::String& targetFeatureId, const std::vector<int>& edgeIndices, double radius, juce::String& id, juce::String& error) { return model.createFillet(targetFeatureId, edgeIndices, radius, id, error); }
    bool updateFillet(const juce::String& id, const juce::String& targetFeatureId, const std::vector<int>& edgeIndices, double radius, juce::String& error) { return model.updateFillet(id, targetFeatureId, edgeIndices, radius, error); }
    std::vector<int> filletEdgeIndices(const juce::String& id) const { return model.filletEdgeIndices(id); }
    bool setPlacement(const juce::String& id, double x, double y, double z, double rx, double ry, double rz, juce::String& error);
    bool setDimensions(const juce::String& id, double w, double d, double h, double radius, juce::String& error);
    bool booleanOp(const juce::String& kind, const juce::String& a, const juce::String& b, juce::String& id, juce::String& error);
    bool rename(const juce::String& id, const juce::String& name, juce::String& error) { return model.rename(id, name, error); }
    bool setVisible(const juce::String& id, bool value, juce::String& error) { return model.setVisible(id, value, error); }
    bool remove(const juce::String& id, juce::String& error) { return model.remove(id, error); }
    bool createInstance(const juce::String& featureId, const juce::String& name, juce::String& id, juce::String& error) { return model.createInstance(featureId, name, id, error); }
    bool setInstancePlacement(const juce::String& id, const CadFrame& placement, juce::String& error) { return model.setInstancePlacement(id, placement, error); }
    bool addCraftCut(const juce::String& feature, const juce::var& cutSpec, juce::String& error) { return model.addCraftCut(feature, cutSpec, error); }
    bool clearCraftCuts(const juce::String& feature, juce::String& error) { return model.clearCraftCuts(feature, error); }
    bool alignInstances(const juce::String& targetInstance, const juce::String& sourceInstance, const juce::String& alignType, const juce::var& options, juce::String& error) { return model.alignInstances(targetInstance, sourceInstance, alignType, options, error); }
    bool createAssemblyGroup(const juce::String& name, const std::vector<juce::String>& instanceIds, juce::String& groupId, juce::String& error) { return model.createAssemblyGroup(name, instanceIds, groupId, error); }
    bool addToAssemblyGroup(const juce::String& groupId, const std::vector<juce::String>& instanceIds, juce::String& error) { return model.addToAssemblyGroup(groupId, instanceIds, error); }
    bool removeFromAssemblyGroup(const juce::String& groupId, const std::vector<juce::String>& instanceIds, juce::String& error) { return model.removeFromAssemblyGroup(groupId, instanceIds, error); }
    bool moveAssemblyGroup(const juce::String& groupId, const CadFrame& deltaPlacement, juce::String& error) { return model.moveAssemblyGroup(groupId, deltaPlacement, error); }
    juce::var generateCutList() const { return model.generateCutList(); }
    bool createBondedJoint(const CadBondedJoint& spec, juce::String& jointId, juce::String& error) { return model.createBondedJoint(spec, jointId, error); }
    bool updateBondedJoint(const juce::String& jointId, const CadBondedJoint& spec, juce::String& error) { return model.updateBondedJoint(jointId, spec, error); }
    bool removeBondedJoint(const juce::String& jointId, juce::String& error) { return model.removeBondedJoint(jointId, error); }
    bool reevaluateBondedJoints(juce::String& error) { return model.reevaluateBondedJoints(error); }
    bool moveBondedGroup(const juce::String& instanceId, const CadFrame& deltaPlacement, juce::String& error) { return model.moveBondedGroup(instanceId, deltaPlacement, error); }
    juce::var adhesiveBom() const { return model.adhesiveBom(); }
    bool removeInstance(const juce::String& id, juce::String& error) { return model.removeInstance(id, error); }
    bool renameMechanical(const juce::String& id, const juce::String& name, juce::String& error) { return model.renameMechanical(id, name, error); }
    bool createJoint(const CadJointRequest& request, juce::String& id, juce::String& error) { return model.createJoint(request, id, error); }
    bool updateJoint(const juce::String& id, const CadJointRequest& request, juce::String& error) { return model.updateJoint(id, request, error); }
    bool removeJoint(const juce::String& id, juce::String& error) { return model.removeJoint(id, error); }
    bool setJointMapping(const CadSignalMapping& mapping, juce::String& error) { return model.setJointMapping(mapping, error); }
    bool removeJointMapping(const juce::String& jointId, juce::String& error) { return model.removeJointMapping(jointId, error); }
    bool setInstanceGrounded(const juce::String& id, bool grounded, juce::String& error) { return model.setInstanceGrounded(id, grounded, error); }
    bool setJointRole(const juce::String& jointId, const juce::String& role, double lockValue, bool enforceLimits, juce::String& error) { return model.setJointRole(jointId, role, lockValue, enforceLimits, error); }
    bool createCoupling(const CadCoupling& coupling, juce::String& id, juce::String& error) { return model.createCoupling(coupling, id, error); }
    bool updateCoupling(const juce::String& id, const CadCoupling& coupling, juce::String& error) { return model.updateCoupling(id, coupling, error); }
    bool removeCoupling(const juce::String& id, juce::String& error) { return model.removeCoupling(id, error); }
    bool createIkTask(const CadIkTask& task, juce::String& id, juce::String& error) { return model.createIkTask(task, id, error); }
    bool updateIkTask(const juce::String& id, const CadIkTask& task, juce::String& error) { return model.updateIkTask(id, task, error); }
    bool removeIkTask(const juce::String& id, juce::String& error) { return model.removeIkTask(id, error); }
    bool createMaterial(const CadMaterial& definition, juce::String& id, juce::String& error) { return model.createMaterial(definition, id, error); }
    bool updateMaterial(const juce::String& id, const CadMaterial& definition, juce::String& error) { return model.updateMaterial(id, definition, error); }
    bool removeMaterial(const juce::String& id, bool clearAssignments, juce::String& error) { return model.removeMaterial(id, clearAssignments, error); }
    bool assignMaterial(const juce::String& featureId, const juce::String& materialId, juce::String& error) { return model.assignMaterial(featureId, materialId, error); }
    bool clearMaterialAssignment(const juce::String& featureId, juce::String& error) { return model.clearMaterialAssignment(featureId, error); }
    // Material library and assignment tools (cad_material_list/create/update/delete/assign/clear) for the agent
    // and the local API: arguments use explicit unit names; the reply is { ok, error?, id?, ... }.
    juce::var materialCommand(const juce::String& tool, const juce::var& args);
    juce::var stockCommand(const juce::var& args);
    juce::var catalogCommand(const juce::var& args);
    juce::var componentCommand(const juce::var& args);
    void setComponentLibraryFile(const juce::File& file) { componentLibrary.setFile(file); }
    void setCatalogFile(const juce::File& file) { engineeringCatalog.setFile(file); }
    // Analysis-case tools (cad_analysis_list/create/update/delete); each listed case carries status and problems.
    juce::var analysisCommand(const juce::String& tool, const juce::var& args);
    bool undo(juce::String& error) { return model.undo(error); }
    bool redo(juce::String& error) { return model.redo(error); }
    bool makeRepresentativeReference(const juce::String& id, CadPersistentReference& reference, juce::String& error) { return model.makeRepresentativeReference(id, reference, error); }
    bool resolveReference(const CadPersistentReference& reference, juce::String& error) const { return model.resolveReference(reference, error); }
    bool save(const juce::File& file, juce::String& error) { return model.save(file, error); }
    bool load(const juce::File& file, juce::String& error) { return model.load(file, error); }
    std::shared_ptr<const CadRenderSnapshot> snapshot() const { return model.snapshot(); }
    // Manufacturing export (3MF, binary STL, STEP). planExport reads the document on the message thread and
    // copies the geometry; runExport works only on those copies, so it may run on a worker thread.
    bool planExport(const CadExportSettings& settings, CadExportPlan& plan, juce::String& error) const { return exporting::plan(model, settings, plan, error); }
    static CadExportResult runExport(const CadExportPlan& plan, const std::function<bool(double, const juce::String&)>& progress = {}) { return exporting::run(plan, progress); }

private:
    CadDocument model;
    CadEngineeringCatalog engineeringCatalog;
    CadComponentLibrary componentLibrary;
};
}
