#pragma once

#include <JuceHeader.h>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
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
    double width = 10.0, depth = 10.0, height = 10.0, radius = 5.0;
    double x = 0.0, y = 0.0, z = 0.0, rx = 0.0, ry = 0.0, rz = 0.0;
    bool visible = true;
    TopoDS_Shape shape;
};

struct CadMesh
{
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
};

struct CadRenderSnapshot
{
    std::uint64_t revision = 0;
    std::vector<CadMesh> meshes;
};

class CadDocument
{
public:
    CadDocument();

    void clear();
    bool createBox(double width, double depth, double height, juce::String& id, juce::String& error);
    bool createCylinder(double radius, double height, juce::String& id, juce::String& error);
    bool setPlacement(const juce::String& id, double x, double y, double z, double rx, double ry, double rz, juce::String& error);
    bool setDimensions(const juce::String& id, double width, double depth, double height, double radius, juce::String& error);
    bool booleanOp(const juce::String& kind, const juce::String& first, const juce::String& second, juce::String& id, juce::String& error);
    bool rename(const juce::String& id, const juce::String& name, juce::String& error);
    bool setVisible(const juce::String& id, bool visible, juce::String& error);
    bool remove(const juce::String& id, juce::String& error);
    bool undo(juce::String& error);
    bool redo(juce::String& error);
    bool save(const juce::File& file, juce::String& error) const;
    bool load(const juce::File& file, juce::String& error);

    std::shared_ptr<const CadRenderSnapshot> snapshot() const;
    const std::vector<CadFeature>& features() const { return items; }
    const CadFeature* find(const juce::String& id) const;
    std::uint64_t revision() const { return documentRevision; }
    bool modified() const { return dirty; }
    void markSaved() { dirty = false; }
    void setFile(const juce::File& file) { documentFile = file; }
    juce::File file() const { return documentFile; }

private:
    struct State { juce::String json; };
    bool mutate(std::function<bool(juce::String&)> action, juce::String& error);
    bool rebuild(juce::String& error);
    TopoDS_Shape evaluate(const juce::String& id, juce::String& error, juce::StringArray& active);
    TopoDS_Shape place(const TopoDS_Shape& source, const CadFeature& feature) const;
    juce::String serialize() const;
    bool deserialize(const juce::String& json, juce::String& error);
    void rememberUndo();
    void invalidate();
    static juce::String newId(const char* prefix, int number);

    std::vector<CadFeature> items;
    std::vector<State> undoStack, redoStack;
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
    bool setPlacement(const juce::String& id, double x, double y, double z, double rx, double ry, double rz, juce::String& error);
    bool setDimensions(const juce::String& id, double w, double d, double h, double radius, juce::String& error);
    bool booleanOp(const juce::String& kind, const juce::String& a, const juce::String& b, juce::String& id, juce::String& error);
    bool rename(const juce::String& id, const juce::String& name, juce::String& error) { return model.rename(id, name, error); }
    bool setVisible(const juce::String& id, bool value, juce::String& error) { return model.setVisible(id, value, error); }
    bool remove(const juce::String& id, juce::String& error) { return model.remove(id, error); }
    bool undo(juce::String& error) { return model.undo(error); }
    bool redo(juce::String& error) { return model.redo(error); }
    bool save(const juce::File& file, juce::String& error) { return model.save(file, error); }
    bool load(const juce::File& file, juce::String& error) { return model.load(file, error); }
    std::shared_ptr<const CadRenderSnapshot> snapshot() const { return model.snapshot(); }

private:
    CadDocument model;
};
}
