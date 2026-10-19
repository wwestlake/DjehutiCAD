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
#include "CadExport.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace djehuti::cad
{
// Construction origins of the faces of an evaluated shape: each face carries tokens "<featureId>/<role>" naming
// the feature that generated it and its role there (box "top", extrude "end", "side:<sketch entity>", ...).
// Tokens follow faces through Booleans and placements via OCCT's modified/deleted history; they are how a
// face is named persistently. Derived on every rebuild, never stored as geometry.
using CadFaceOrigins = std::vector<std::pair<TopoDS_Shape, juce::StringArray>>;

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
    TopoDS_Shape shape;
    CadFaceOrigins faceOrigins;   // derived with shape
};

struct CadSketchEntity
{
    juce::String id, type, groupId;
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0, radius = 1.0;
};

struct CadSketchConstraint
{
    juce::String id, type, firstEntity, secondEntity;
    double value = 0.0;
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
};

struct CadMesh
{
    juce::String featureId;
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
    bool addSketchLine(const juce::String& sketchId, double x1, double y1, double x2, double y2, juce::String& entityId, juce::String& error, const juce::String& firstConnection = {}, const juce::String& secondConnection = {});
    bool addSketchRectangle(const juce::String& sketchId, double x, double y, double width, double height, juce::String& groupId, juce::String& error);
    bool addSketchCircle(const juce::String& sketchId, double x, double y, double radius, juce::String& entityId, juce::String& error);
    bool setSketchEntityDimension(const juce::String& sketchId, const juce::String& entityId, double value, juce::String& error);
    bool setSketchEntityPosition(const juce::String& sketchId, const juce::String& entityId, double x, double y, juce::String& error);
    bool addSketchConstraint(const juce::String& sketchId, const juce::String& type, const juce::String& firstEntity, const juce::String& secondEntity, double value, juce::String& constraintId, juce::String& error);
    bool createExtrude(const juce::String& sketchId, double depth, bool reverse, juce::String& id, juce::String& error);
    bool createCut(const juce::String& sketchId, const juce::String& targetFeatureId, double depth, bool throughAll, bool reverse, juce::String& id, juce::String& error);
    bool createFillet(const juce::String& targetFeatureId, const std::vector<int>& edgeIndices, double radius, juce::String& id, juce::String& error);
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
    bool undo(juce::String& error);
    bool redo(juce::String& error);
    bool save(const juce::File& file, juce::String& error);
    bool load(const juce::File& file, juce::String& error);
    bool exportJson(const juce::File& file, juce::String& error) const;
    bool makeRepresentativeReference(const juce::String& featureId, CadPersistentReference& reference, juce::String& error);
    bool resolveReference(const CadPersistentReference& reference, juce::String& error) const;
    bool referenceWorld(const juce::String& instanceId, gp_Trsf& world, juce::String& error) const;

    std::shared_ptr<const CadRenderSnapshot> snapshot() const;
    // Maps a body's evaluated shape (which already carries its authored placement) onto an instance of it whose
    // frame is instanceWorld. Shared by the runtime pose and export so each placement is applied exactly once.
    gp_Trsf instanceShapeTransform(const juce::String& featureId, const gp_Trsf& instanceWorld) const;
    const std::vector<CadFeature>& features() const { return items; }
    const CadFeature* find(const juce::String& id) const;
    const CadSketch* findSketch(const juce::String& id) const;
    const std::vector<CadSketch>& sketchData() const { return sketches; }
    const CadMechanics& mechanicsData() const { return mechanics; }
    std::uint64_t revision() const { return documentRevision; }
    // Changes on every New and Open (never reused within a process), unlike revision numbers that undo can restore.
    std::uint64_t documentInstance() const { return instanceId; }
    bool modified() const { return dirty; }
    void markSaved() { dirty = false; }
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
    bool loadFile(const juce::File& file, juce::String& error);
    bool jointFramesFromRequest(const CadJointRequest& request, CadJoint& joint, juce::String& error) const;
    CadSketch* mutableSketch(const juce::String& id);
    bool solveSketch(CadSketch& sketch, juce::String& error) const;
    bool validateSketch(const CadSketch& sketch, juce::String& error) const;
    bool sketchProfiles(const CadSketch& sketch, const CadSketchFrame& frame, std::vector<TopoDS_Face>& faces, juce::String& error, std::vector<std::pair<TopoDS_Shape, juce::String>>* edgeEntities = nullptr) const;
    TopoDS_Shape extrudeSketch(const CadSketch& sketch, const CadSketchFrame& frame, double depth, bool reverse, juce::String& error, CadFaceOrigins* origins = nullptr, const juce::String& owner = {}) const;
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
    std::vector<CadPersistentReference> references;
    std::vector<CadTopologyEvolution> operationHistory;
    Handle(TDocStd_Application) ocafApplication;
    Handle(TDocStd_Document) ocafDocument;
    std::uint64_t documentRevision = 0;
    std::uint64_t instanceId = 0;
    int nextNumber = 1;
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
    bool setSketchEntityDimension(const juce::String& sketchId, const juce::String& entityId, double value, juce::String& error) { return model.setSketchEntityDimension(sketchId, entityId, value, error); }
    bool setSketchEntityPosition(const juce::String& sketchId, const juce::String& entityId, double x, double y, juce::String& error) { return model.setSketchEntityPosition(sketchId, entityId, x, y, error); }
    bool addSketchConstraint(const juce::String& sketchId, const juce::String& type, const juce::String& firstEntity, const juce::String& secondEntity, double value, juce::String& constraintId, juce::String& error) { return model.addSketchConstraint(sketchId, type, firstEntity, secondEntity, value, constraintId, error); }
    bool createExtrude(const juce::String& sketchId, double depth, bool reverse, juce::String& id, juce::String& error) { return model.createExtrude(sketchId, depth, reverse, id, error); }
    bool createCut(const juce::String& sketchId, const juce::String& targetFeatureId, double depth, bool throughAll, bool reverse, juce::String& id, juce::String& error) { return model.createCut(sketchId, targetFeatureId, depth, throughAll, reverse, id, error); }
    bool createFillet(const juce::String& targetFeatureId, const std::vector<int>& edgeIndices, double radius, juce::String& id, juce::String& error) { return model.createFillet(targetFeatureId, edgeIndices, radius, id, error); }
    bool setPlacement(const juce::String& id, double x, double y, double z, double rx, double ry, double rz, juce::String& error);
    bool setDimensions(const juce::String& id, double w, double d, double h, double radius, juce::String& error);
    bool booleanOp(const juce::String& kind, const juce::String& a, const juce::String& b, juce::String& id, juce::String& error);
    bool rename(const juce::String& id, const juce::String& name, juce::String& error) { return model.rename(id, name, error); }
    bool setVisible(const juce::String& id, bool value, juce::String& error) { return model.setVisible(id, value, error); }
    bool remove(const juce::String& id, juce::String& error) { return model.remove(id, error); }
    bool createInstance(const juce::String& featureId, const juce::String& name, juce::String& id, juce::String& error) { return model.createInstance(featureId, name, id, error); }
    bool setInstancePlacement(const juce::String& id, const CadFrame& placement, juce::String& error) { return model.setInstancePlacement(id, placement, error); }
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
};
}
