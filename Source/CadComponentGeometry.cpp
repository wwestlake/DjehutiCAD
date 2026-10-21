#include "CadComponents.h"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <STEPControl_Reader.hxx>
#include <windows.h>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace djehuti::cad::components
{
namespace
{
juce::String display(const juce::var& value)
{if(value.isDouble()||value.isInt()||value.isInt64()){std::ostringstream stream;stream<<std::setprecision(10)<<(double)value;return juce::String(stream.str());}return value.toString();}
}
juce::var object(){return new juce::DynamicObject;}
juce::var clone(const juce::var& v){return juce::JSON::parse(juce::JSON::toString(v));}
void put(juce::var& v,const juce::Identifier& k,const juce::var& value){v.getDynamicObject()->setProperty(k,value);}
juce::String digest(const juce::String& text)
{
    BCRYPT_ALG_HANDLE algorithm=nullptr;unsigned char output[32]{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    const auto status=BCryptHash(algorithm,nullptr,0,(PUCHAR)text.toRawUTF8(),(ULONG)text.getNumBytesAsUTF8(),output,sizeof(output));BCryptCloseAlgorithmProvider(algorithm,0);
    return status<0?juce::String():juce::String::toHexString(output,sizeof(output),0);
}
bool validate(const juce::var& record,juce::String& error)
{
    const auto fail=[&](const char* reason){error=reason;return false;};
    if(!record.isObject()||record["id"].toString().isEmpty()||record["name"].toString().trim().isEmpty()||record["category"].toString().trim().isEmpty()||(int)record["revision"]<1)return fail("A component needs an identity, name, category and positive revision.");
    if(!juce::StringArray{"commercial","custom"}.contains(record["origin"].toString()))return fail("Component origin must be commercial or custom.");
    if(!record["properties"].isArray())return fail("Component properties must be a typed property list.");
    if(record.hasProperty("massKg")&&(!(record["massKg"].isDouble()||record["massKg"].isInt()||record["massKg"].isInt64())||!std::isfinite((double)record["massKg"])||(double)record["massKg"]<=0))return fail("Published component mass must be a finite positive kg value.");
    const auto g=record["geometry"];const auto kind=g["kind"].toString();
    if(kind=="none")return true;
    if(kind=="brep")return g["data"].toString().isNotEmpty()&&g["data"].toString().getNumBytesAsUTF8()<=32*1024*1024?true:fail("Missing or oversized component BRep.");
    if(kind!="envelope"&&kind!="cylinder")return fail("Unknown component geometry representation.");
    const std::vector<const char*> dimensions=kind=="cylinder"?std::vector<const char*>{"diameterMm","lengthMm"}:std::vector<const char*>{"widthMm","heightMm","lengthMm"};
    for(const auto* key:dimensions)
    {const auto n=g[key];if(!(n.isDouble()||n.isInt()||n.isInt64())||!std::isfinite((double)n)||(double)n<=1e-4||(double)n>1e7)return fail("Geometry dimensions must be documented finite positive millimetres within the modeling range.");}
    return true;
}
TopoDS_Shape shape(const juce::var& record,juce::String& error)
{
    if(!validate(record,error))return {};
    const auto g=record["geometry"];const auto kind=g["kind"].toString();
    try
    {
        TopoDS_Shape result;
        if(kind=="envelope")result=BRepPrimAPI_MakeBox((double)g["widthMm"],(double)g["heightMm"],(double)g["lengthMm"]).Shape();
        else if(kind=="cylinder")result=BRepPrimAPI_MakeCylinder((double)g["diameterMm"]/2,(double)g["lengthMm"]).Shape();
        else if(kind=="brep"){std::istringstream input(g["data"].toString().toStdString());BRep_Builder builder;BRepTools::Read(result,input,builder);}
        else {error="This catalog record lacks usable dimensions or an attached CAD model.";return {};}
        if(result.IsNull()||!TopExp_Explorer(result,TopAbs_SOLID).More()||!BRepCheck_Analyzer(result).IsValid()){error="Component representation must contain valid OCCT solid geometry.";return {};}
        return result;
    }
    catch(const Standard_Failure& failure){error="Cannot build component geometry: "+juce::String(failure.GetMessageString());return {};}
}
juce::String encodeShape(const TopoDS_Shape& shape){std::ostringstream stream;BRepTools::Write(shape,stream);return juce::String::fromUTF8(stream.str().c_str());}
bool attachModel(juce::var& record,const juce::File& file,juce::String& error)
{
    if(!file.existsAsFile()||file.getSize()>32*1024*1024){error="Choose a local STEP or BRep model, no larger than 32 MB.";return false;}
    try
    {
        TopoDS_Shape result;
        if(file.hasFileExtension("step;stp"))
        {
            // Reader honors STEP length units and converts to OCCT millimetres. No remote downloads or
            // file-path dependencies survive attachment; the exact transferred shape is snapshotted.
            juce::TemporaryFile staged(".step");if(!file.copyFileTo(staged.getFile())){error="Cannot stage STEP model.";return false;}
            STEPControl_Reader reader;if(reader.ReadFile(staged.getFile().getFullPathName().toRawUTF8())!=IFSelect_RetDone||reader.TransferRoots()==0){error="STEP transfer failed.";return false;}result=reader.OneShape();
        }
        else if(file.hasFileExtension("brep")){std::istringstream input(file.loadFileAsString().toStdString());BRep_Builder builder;BRepTools::Read(result,input,builder);}
        else {error="Supported local CAD attachments are .step, .stp and millimetre .brep files.";return false;}
        if(result.IsNull()||!TopExp_Explorer(result,TopAbs_SOLID).More()||!BRepCheck_Analyzer(result).IsValid()){error="Attached model has no valid solid geometry.";return false;}
        auto g=object();put(g,"kind","brep");put(g,"level","manufacturer");put(g,"label","Attached manufacturer CAD (solid BRep snapshot)");put(g,"data",encodeShape(result));put(g,"sourceFile",file.getFileName());put(record,"geometry",g);return validate(record,error);
    }
    catch(const Standard_Failure& f){error=juce::String(f.GetMessageString());return false;}
}
juce::String details(const juce::var& record)
{
    juce::String text=record["name"].toString()+"\n"+record["origin"].toString()+" / "+record["category"].toString()+"\nManufacturer: "+record.getProperty("manufacturer","Unknown").toString()+"\nModel / part: "+record.getProperty("model","Unknown").toString()+" / "+record.getProperty("partNumber","Unknown").toString()+"\n"+record["description"].toString();
    text+="\nReview status: "+record.getProperty("reviewStatus","User component").toString()+"\nCAD representation: "+record["geometry"]["label"].toString()+"\nPublished mass: "+(record.hasProperty("massKg")?record["massKg"].toString()+" kg":"Unknown")+"\nCentre of gravity: Unknown unless calculated from an explicitly assigned homogeneous material and actual geometry.";
    if(record.hasProperty("material"))
    {text+="\nExisting material: "+record["material"]["name"].toString();for(const auto pair:{std::pair<const char*,const char*>{"density","Density"},{"youngsModulus","Young's modulus"},{"poissonsRatio","Poisson's ratio"},{"yieldStrength","Yield strength"}}){const auto property=record["material"]["properties"][pair.first];text+="\n"+juce::String(pair.second)+": "+(property.isObject()?property["value"].toString()+" "+property["unit"].toString():juce::String("Unknown"));}}
    if(auto* fields=record["properties"].getArray())for(const auto& field:*fields)
    {text+="\n"+field["label"].toString()+": "+(field["value"].isVoid()||(field["value"].isString()&&field["value"].toString().trim().isEmpty())?juce::String("Unknown"):display(field["value"]));if(field.hasProperty("siValue"))text+="  ["+display(field["siValue"])+" "+field["siUnit"].toString()+"]";if(field.hasProperty("formula"))text+=" (workbook formula: "+field["formula"].toString()+")";}
    text+="\nSource workbook: "+record["provenance"]["workbook"].toString()+" / "+record["provenance"]["sheet"].toString()+" / row "+record["provenance"]["row"].toString()+"\nSource: "+record["sourceUrl"].toString();
    if(auto* refs=record["provenance"]["references"].getArray())for(const auto& ref:*refs){text+="\nSource register: "+ref["sheet"].toString();for(const auto& field:*ref["properties"].getArray())text+="\n  "+field["label"].toString()+": "+field["value"].toString();}
    text+="\nEngineering use: catalog specifications and simplified envelopes do not establish material homogeneity, mounting detail, mass distribution, joints or simulation validity.";return text;
}
}
