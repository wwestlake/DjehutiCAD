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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace djehuti::cad
{
struct CadFeature
{
    juce::String id, name, type;
    juce::String firstOperand, secondOperand;
    juce::String sketchId, targetFeatureId;
    double width = 10.0, depth = 10.0, height = 10.0, radius = 5.0;
    bool reverse = false, throughAll = false;
    double x = 0.0, y = 0.0, z = 0.0, rx = 0.0, ry = 0.0, rz = 0.0;
    bool visible = true;
    TopoDS_Shape shape;
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
    juce::String id, plane = "XY";
    double offset = 0.0;
    std::vector<CadSketchEntity> entities;
    std::vector<CadSketchConstraint> constraints;
};

struct CadMesh
{
    juce::String featureId;
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
};

struct CadRenderSnapshot
{
    std::uint64_t revision = 0;
    std::vector<CadMesh> meshes;
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
    bool addSketchLine(const juce::String& sketchId, double x1, double y1, double x2, double y2, juce::String& entityId, juce::String& error, const juce::String& firstConnection = {}, const juce::String& secondConnection = {});
    bool addSketchRectangle(const juce::String& sketchId, double x, double y, double width, double height, juce::String& groupId, juce::String& error);
    bool addSketchCircle(const juce::String& sketchId, double x, double y, double radius, juce::String& entityId, juce::String& error);
    bool setSketchEntityDimension(const juce::String& sketchId, const juce::String& entityId, double value, juce::String& error);
    bool setSketchEntityPosition(const juce::String& sketchId, const juce::String& entityId, double x, double y, juce::String& error);
    bool addSketchConstraint(const juce::String& sketchId, const juce::String& type, const juce::String& firstEntity, const juce::String& secondEntity, double value, juce::String& constraintId, juce::String& error);
    bool createExtrude(const juce::String& sketchId, double depth, bool reverse, juce::String& id, juce::String& error);
    bool createCut(const juce::String& sketchId, const juce::String& targetFeatureId, double depth, bool throughAll, bool reverse, juce::String& id, juce::String& error);
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

    std::shared_ptr<const CadRenderSnapshot> snapshot() const;
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
    TopoDS_Shape evaluate(const juce::String& id, juce::String& error, juce::StringArray& active);
    TopoDS_Shape place(const TopoDS_Shape& source, const CadFeature& feature) const;
    juce::String serialize() const;
    bool deserialize(const juce::String& json, juce::String& error);
    juce::String serializeSketches() const;
    bool deserializeSketches(const juce::String& json, juce::String& error);
    bool restoreMechanicsFromOCAF(juce::String& error);
    bool loadFile(const juce::File& file, juce::String& error);
    bool referenceWorld(const juce::String& instanceId, gp_Trsf& world, juce::String& error) const;
    bool jointFramesFromRequest(const CadJointRequest& request, CadJoint& joint, juce::String& error) const;
    CadSketch* mutableSketch(const juce::String& id);
    bool solveSketch(CadSketch& sketch, juce::String& error) const;
    bool validateSketch(const CadSketch& sketch, juce::String& error) const;
    bool sketchProfiles(const CadSketch& sketch, std::vector<TopoDS_Face>& faces, juce::String& error) const;
    TopoDS_Shape extrudeSketch(const CadSketch& sketch, double depth, bool reverse, juce::String& error) const;
    void initializeOCAF();
    bool syncOCAFFromItems(juce::String& error);
    bool syncItemsFromOCAF(juce::String& error);
    bool saveNative(const juce::File& file, juce::String& error);
    bool loadNative(const juce::File& file, juce::String& error);
    void invalidate();
    static juce::String newId(const char* prefix, int number);

    std::vector<CadFeature> items;
    std::vector<CadSketch> sketches;
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
    bool addSketchLine(const juce::String& sketchId, double x1, double y1, double x2, double y2, juce::String& entityId, juce::String& error, const juce::String& firstConnection = {}, const juce::String& secondConnection = {}) { return model.addSketchLine(sketchId, x1, y1, x2, y2, entityId, error, firstConnection, secondConnection); }
    bool addSketchRectangle(const juce::String& sketchId, double x, double y, double width, double height, juce::String& groupId, juce::String& error) { return model.addSketchRectangle(sketchId, x, y, width, height, groupId, error); }
    bool addSketchCircle(const juce::String& sketchId, double x, double y, double radius, juce::String& entityId, juce::String& error) { return model.addSketchCircle(sketchId, x, y, radius, entityId, error); }
    bool setSketchEntityDimension(const juce::String& sketchId, const juce::String& entityId, double value, juce::String& error) { return model.setSketchEntityDimension(sketchId, entityId, value, error); }
    bool setSketchEntityPosition(const juce::String& sketchId, const juce::String& entityId, double x, double y, juce::String& error) { return model.setSketchEntityPosition(sketchId, entityId, x, y, error); }
    bool addSketchConstraint(const juce::String& sketchId, const juce::String& type, const juce::String& firstEntity, const juce::String& secondEntity, double value, juce::String& constraintId, juce::String& error) { return model.addSketchConstraint(sketchId, type, firstEntity, secondEntity, value, constraintId, error); }
    bool createExtrude(const juce::String& sketchId, double depth, bool reverse, juce::String& id, juce::String& error) { return model.createExtrude(sketchId, depth, reverse, id, error); }
    bool createCut(const juce::String& sketchId, const juce::String& targetFeatureId, double depth, bool throughAll, bool reverse, juce::String& id, juce::String& error) { return model.createCut(sketchId, targetFeatureId, depth, throughAll, reverse, id, error); }
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

private:
    CadDocument model;
};
}
