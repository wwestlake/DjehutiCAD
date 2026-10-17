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
    std::uint64_t revision() const { return documentRevision; }
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
    std::vector<CadPersistentReference> references;
    std::vector<CadTopologyEvolution> operationHistory;
    Handle(TDocStd_Application) ocafApplication;
    Handle(TDocStd_Document) ocafDocument;
    std::uint64_t documentRevision = 0;
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
