#pragma once
#include <JuceHeader.h>
#include <TopoDS_Shape.hxx>

namespace djehuti::cad
{
class CadDocument;
struct CadComponentDefinition
{
    juce::String featureId;
    juce::var record; // Catalog snapshot; craft stock dimensions are explicitly editable through CadDocument.
};
namespace components
{
juce::var object();
juce::var clone(const juce::var&);
void put(juce::var&,const juce::Identifier&,const juce::var&);
juce::String digest(const juce::String&);
bool validate(const juce::var&,juce::String&);
TopoDS_Shape shape(const juce::var&,juce::String&);
juce::String encodeShape(const TopoDS_Shape&);
juce::File resolveAssetFile(const juce::var& record,const juce::String& relativePath);
bool attachModel(juce::var&,const juce::File&,juce::String&);
bool readWorkbook(const juce::File&,juce::Array<juce::var>& records,juce::var& report,juce::String& error);
juce::String details(const juce::var&);
}

// Application catalog is independent of document authority. Insertion snapshots a definition into OCAF.
// Research workbooks are import sources; the runtime catalog and attached BReps are entirely local JSON.
class CadComponentLibrary
{
public:
    static juce::File defaultFile();
    void setFile(const juce::File&);
    bool ensureLoaded(juce::String&);
    const juce::Array<juce::var>& records() const { return entries; }
    const juce::var* find(const juce::String&) const;
    juce::var command(const juce::var&,CadDocument&);
    bool importRecords(const juce::Array<juce::var>&,bool replace,juce::var& report,juce::String& error);
    bool exportFile(const juce::File&,juce::String&) const;
private:
    bool save(const juce::Array<juce::var>&,juce::String&);
    juce::File file;
    juce::String diskText;
    bool loaded=false;
    juce::Array<juce::var> entries;
};
bool runCadManufacturerCheck(juce::var&,const juce::File& resourceRoot);
bool runCadComponentCheck(juce::var&,const juce::File& researchRoot);
}
