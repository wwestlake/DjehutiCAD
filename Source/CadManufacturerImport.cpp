#include "CadComponents.h"
#include "EngineeringSpecification.h"
#include <STEPCAFControl_Reader.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Tool.hxx>
#include <TColStd_SequenceOfAsciiString.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <sstream>

namespace djehuti::cad::components
{
namespace
{
juce::String labelId(const TDF_Label& label){TCollection_AsciiString id;TDF_Tool::Entry(label,id);return id.ToCString();}
void describe(const TDF_Label& occurrence,const juce::String& parent,juce::Array<juce::var>& nodes,int depth=0)
{
    if(depth>128)throw Standard_Failure("Assembly nesting exceeds 128 levels.");
    TDF_Label definition=occurrence;XCAFDoc_ShapeTool::GetReferredShape(occurrence,definition);
    if(definition.IsNull())definition=occurrence;
    auto node=object();const auto path=parent+"/"+labelId(occurrence);put(node,"path",path);put(node,"parent",parent);put(node,"definition",labelId(definition));
    Handle(TDataStd_Name) name;if(definition.FindAttribute(TDataStd_Name::GetID(),name))put(node,"name",TCollection_AsciiString(name->Get(),'?').ToCString());
    const auto t=XCAFDoc_ShapeTool::GetLocation(occurrence).Transformation();juce::Array<juce::var> matrix;
    for(int r=1;r<=3;++r)for(int c=1;c<=4;++c)matrix.add(t.Value(r,c));put(node,"localTransform3x4",matrix);
    put(node,"assembly",XCAFDoc_ShapeTool::IsAssembly(definition));nodes.add(node);
    TDF_LabelSequence children;XCAFDoc_ShapeTool::GetComponents(definition,children);
    for(int i=1;i<=children.Length();++i)describe(children.Value(i),path,nodes,depth+1);
}
}
bool attachModel(juce::var& record,const juce::File& file,juce::String& error)
{
    if(file.hasFileExtension("iges;igs")){error="IGES is not enabled in this OCCT build (TKDEIGES is absent). Supply the manufacturer's STEP file; no geometry has been changed.";return false;}
    if(!file.hasFileExtension("step;stp;brep")){error="Supported attachments: STEP (.step/.stp) and millimetre BRep (.brep).";return false;}
    if(!file.existsAsFile()||file.getSize()>32*1024*1024){error="Choose a readable local CAD model of at most 32 MB.";return false;}
    const auto sourceHash=engineering::fileHash(file);
    if(sourceHash.isEmpty()){error="Cannot read/hash the CAD source. Check file access and retry.";return false;}
    juce::String stage="read";const auto start=juce::Time::getMillisecondCounterHiRes();auto info=object();juce::Array<juce::var> timings;
    const auto mark=[&](const char* completed){auto t=object();put(t,"stage",completed);put(t,"elapsedSeconds",(juce::Time::getMillisecondCounterHiRes()-start)/1000);timings.add(t);juce::Logger::writeToLog("Manufacturer import: "+juce::String(completed)+" "+juce::String((juce::Time::getMillisecondCounterHiRes()-start)/1000,2)+" s");};
    try
    {
        TopoDS_Shape result;juce::Array<juce::var> nodes,units;
        if(file.hasFileExtension("step;stp"))
        {
            // Stream input supports Unicode paths without relocating the assembly beside a temporary file.
            // External-file assemblies are explicitly rejected below; they must be exported self-contained.
            std::istringstream input(file.loadFileAsString().toStdString());STEPCAFControl_Reader reader;
            if(reader.ReadStream("manufacturer.step",input)!=IFSelect_RetDone){error="STEP read failed: check the original file for truncation or unsupported syntax.";return false;}
            reader.ChangeReader().SetSystemLengthUnit(1.0); // OCCT local length unit: millimetres.
            TColStd_SequenceOfAsciiString lengths,angles,solids;reader.ChangeReader().FileUnits(lengths,angles,solids);
            for(int i=1;i<=lengths.Length();++i)units.add(lengths.Value(i).ToCString());
            const auto roots=reader.NbRootsForTransfer();put(info,"transferRoots",roots);mark("read");stage="transfer";
            Handle(TDocStd_Document) temporary=new TDocStd_Document("BinXCAF");
            if(roots<1||!reader.Transfer(temporary)||reader.Reader().NbShapes()<roots){error="STEP transfer did not produce every root. Export a self-contained solid STEP assembly and retry.";return false;}
            if(!reader.ExternFiles().IsEmpty()){error="This STEP assembly references external files. Export a self-contained STEP assembly; partial geometry was not attached.";return false;}
            auto shapes=XCAFDoc_DocumentTool::ShapeTool(temporary->Main());TDF_LabelSequence free;shapes->GetFreeShapes(free);
            if(free.Length()==1)result=shapes->GetShape(free.Value(1));
            else {BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);for(int i=1;i<=free.Length();++i)builder.Add(compound,shapes->GetShape(free.Value(i)));result=compound;}
            for(int i=1;i<=free.Length();++i)describe(free.Value(i),{},nodes);
            mark("transfer");
        }
        else {std::istringstream input(file.loadFileAsString().toStdString());BRep_Builder builder;BRepTools::Read(result,input,builder);units.add("mm (BRep input contract)");mark("read");}
        stage="validation";
        if(result.IsNull()||!TopExp_Explorer(result,TopAbs_SOLID).More()||!BRepCheck_Analyzer(result).IsValid()){error="Imported topology is empty, has no solids, or fails OCCT geometric validity. Repair/export valid solids in the source CAD tool.";return false;}
        TopTools_IndexedMapOfShape solids,faces;TopExp::MapShapes(result,TopAbs_SOLID,solids);TopExp::MapShapes(result,TopAbs_FACE,faces);
        put(info,"solidCount",solids.Extent());put(info,"faceCount",faces.Extent());put(info,"sourceLengthUnits",units);put(info,"documentLengthUnit","mm");
        put(info,"assemblyNodes",nodes);put(info,"assemblySemantics","Placed compound and source hierarchy retained; imported children are not native parametric assembly occurrences.");
        Bnd_Box box;BRepBndLib::Add(result,box,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        put(info,"conservativeBoundsMm",juce::Array<juce::var>{x0,y0,z0,x1,y1,z1});mark("validation");stage="snapshot";
        auto g=object();put(g,"kind","brep");put(g,"level","manufacturer");put(g,"label","Manufacturer solid geometry with original placement (mm)");put(g,"data",encodeShape(result));put(g,"sourceFile",file.getFileName());put(g,"sourceSha256",sourceHash);mark("snapshot");put(info,"stages",timings);put(g,"import",info);
        if(engineering::fileHash(file)!=sourceHash){error="CAD source changed during import. Retry with a stable local copy.";return false;}
        auto candidate=clone(record);put(candidate,"geometry",g);
        if(!validate(candidate,error)){error="Imported snapshot rejected: "+error;return false;}
        record=candidate;return true; // Commit only after complete transfer, validation and snapshot limits.
    }
    catch(const Standard_Failure& f){error="Manufacturer import failed during "+stage+": "+juce::String(f.GetMessageString());return false;}
    catch(const std::exception& f){error="Manufacturer import failed during "+stage+": "+juce::String(f.what());return false;}
}
}
