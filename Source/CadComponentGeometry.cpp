#include "CadComponents.h"
#include "EngineeringSpecification.h"
#include "CadCraftStock.h"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <STEPControl_Reader.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#include <windows.h>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <map>

namespace djehuti::cad::components
{
namespace
{
juce::String display(const juce::var& value)
{if(value.isDouble()||value.isInt()||value.isInt64()){std::ostringstream stream;stream<<std::setprecision(10)<<(double)value;return juce::String(stream.str());}return value.toString();}

TopoDS_Shape buildDrawingReconstruction(const juce::String& id)
{
    if(id=="bearing_ezo_623zz")
        return BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(5.0,4.0).Shape(),BRepPrimAPI_MakeCylinder(1.5,4.0).Shape()).Shape();
    if(id=="bearing_ezo_625zz")
        return BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(8.0,5.0).Shape(),BRepPrimAPI_MakeCylinder(2.5,5.0).Shape()).Shape();
    if(id=="leadscrew_lsn_s_mb_320")
    {
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        builder.Add(compound,BRepPrimAPI_MakeCylinder(4.0,320.0).Shape());
        gp_Ax2 nutAxis(gp_Pnt(0,0,20.0),gp_Dir(0,0,1));
        TopoDS_Shape nut=BRepAlgoAPI_Fuse(BRepPrimAPI_MakeCylinder(nutAxis,6.35,19.05).Shape(),BRepPrimAPI_MakeCylinder(nutAxis,12.7,3.81).Shape()).Shape();
        nut=BRepAlgoAPI_Cut(nut,BRepPrimAPI_MakeCylinder(nutAxis,4.0,19.05).Shape()).Shape();
        for(int i=0;i<3;++i)
        {
            const double angle=i*2.0*juce::MathConstants<double>::pi/3.0;
            gp_Ax2 holeAxis(gp_Pnt(9.525*std::cos(angle),9.525*std::sin(angle),20.0),gp_Dir(0,0,1));
            nut=BRepAlgoAPI_Cut(nut,BRepPrimAPI_MakeCylinder(holeAxis,1.78,3.81).Shape()).Shape();
        }
        builder.Add(compound,nut);return compound;
    }
    if(id=="servo_feetech_fs90r_v1")
    {
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(-11.25,-6.05,0.0),gp_Pnt(11.25,6.05,23.4)).Shape());
        TopoDS_Shape leftTab=BRepPrimAPI_MakeBox(gp_Pnt(-16.15,-6.05,16.7),gp_Pnt(-11.25,6.05,19.2)).Shape();
        leftTab=BRepAlgoAPI_Cut(leftTab,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-13.85,0.0,16.7),gp_Dir(0,0,1)),1.1,2.5).Shape()).Shape();
        TopoDS_Shape rightTab=BRepPrimAPI_MakeBox(gp_Pnt(11.25,-6.05,16.7),gp_Pnt(16.15,6.05,19.2)).Shape();
        rightTab=BRepAlgoAPI_Cut(rightTab,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(13.85,0.0,16.7),gp_Dir(0,0,1)),1.1,2.5).Shape()).Shape();
        builder.Add(compound,leftTab);builder.Add(compound,rightTab);
        builder.Add(compound,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(5.2,0.0,23.4),gp_Dir(0,0,1)),4.35,4.3).Shape());
        builder.Add(compound,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(5.2,0.0,27.7),gp_Dir(0,0,1)),2.43,3.0).Shape());
        return compound;
    }
    if(id=="switch_omron_d2f_01f"||id=="switch_omron_d2f_01fl")
    {
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        TopoDS_Shape body=BRepPrimAPI_MakeBox(gp_Pnt(-6.4,-2.9,0.0),gp_Pnt(6.4,2.9,6.5)).Shape();
        for(double x:{-3.25,3.25})
            body=BRepAlgoAPI_Cut(body,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,-2.9,2.5),gp_Dir(0,1,0)),1.0,5.8).Shape()).Shape();
        builder.Add(compound,body);
        builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(-3.25,-0.7,6.5),gp_Pnt(-1.85,0.7,7.7)).Shape());
        if(id=="switch_omron_d2f_01fl")
            builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(-6.0,-2.0,7.7),gp_Pnt(6.5,2.0,8.1)).Shape());
        return compound;
    }
    if(id=="pcb_sparkfun_sen_22301")
    {
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(0.0,0.0,0.0),gp_Pnt(25.4,25.4,1.6)).Shape());
        builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(11.2,11.45,1.6),gp_Pnt(14.2,13.95,2.43)).Shape());
        return compound;
    }
    return {};
}
}
juce::File resolveAssetFile(const juce::var& record,const juce::String& relativePath)
{
    if(relativePath.isEmpty())return {};
    const auto explicitRoot=record["engineeringAssetRoot"].toString();
    if(juce::File::isAbsolutePath(explicitRoot))
    {
        const auto candidate=juce::File(explicitRoot).getChildFile(relativePath);
        if(candidate.existsAsFile())return candidate;
    }
    for(const auto& base:{juce::File::getCurrentWorkingDirectory().getChildFile("Resources"),
                          juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory()})
    {
        const auto candidate=base.getChildFile(relativePath);
        if(candidate.existsAsFile())return candidate;
    }
    return {};
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
    if(!engineering::validate(record,error))return false;
    const auto g=record["geometry"];const auto kind=g["kind"].toString();
    if(kind=="craftStock")return craft::validateGeometry(g,error);
    if(kind=="none")return true;
    if(kind=="brep")
    {
        if(g["data"].toString().isNotEmpty())return g["data"].toString().getNumBytesAsUTF8()<=32*1024*1024?true:fail("Missing or oversized component BRep.");
        if(g["drawingReconstruction"].toString().isNotEmpty())return !buildDrawingReconstruction(g["drawingReconstruction"].toString()).IsNull()?true:fail("Unknown manufacturer drawing reconstruction.");
        if(g["sourcePath"].toString().isNotEmpty()&&g["sourceSha256"].toString().length()==64&&g["sourceSha256"].toString().containsOnly("0123456789abcdef"))return true;
        return fail("Missing or oversized component BRep.");
    }
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
    static auto& validShapeCache=*new std::map<juce::String,TopoDS_Shape>();
    try
    {
        TopoDS_Shape result;juce::String cacheKey;
        if(kind=="craftStock")result=craft::shape(g,error);
        else if(kind=="envelope")result=BRepPrimAPI_MakeBox((double)g["widthMm"],(double)g["heightMm"],(double)g["lengthMm"]).Shape();
        else if(kind=="cylinder")result=BRepPrimAPI_MakeCylinder((double)g["diameterMm"]/2,(double)g["lengthMm"]).Shape();
        else if(kind=="brep")
        {
            if(g["data"].toString().isNotEmpty())
            {
                cacheKey="data:"+digest(g["data"].toString());
                if(auto it=validShapeCache.find(cacheKey);it!=validShapeCache.end())return it->second;
                std::istringstream input(g["data"].toString().toStdString());BRep_Builder builder;BRepTools::Read(result,input,builder);
            }
            else if(g["drawingReconstruction"].toString().isNotEmpty())
            {
                cacheKey="recon:"+g["drawingReconstruction"].toString();
                if(auto it=validShapeCache.find(cacheKey);it!=validShapeCache.end())return it->second;
                result=buildDrawingReconstruction(g["drawingReconstruction"].toString());
            }
            else if(g["sourcePath"].toString().isNotEmpty())
            {
                cacheKey="step:"+g["sourceSha256"].toString();
                if(auto it=validShapeCache.find(cacheKey);it!=validShapeCache.end())return it->second;
                const auto asset=resolveAssetFile(record,g["sourcePath"].toString());
                if(!asset.existsAsFile()){error="Missing associated manufacturer CAD source: "+g["sourcePath"].toString();return {};}
                auto temp=clone(record);if(!attachModel(temp,asset,error))return {};
                std::istringstream input(temp["geometry"]["data"].toString().toStdString());BRep_Builder builder;BRepTools::Read(result,input,builder);
            }
        }
        else {error="This catalog record lacks usable dimensions or an attached CAD model.";return {};}
        if(result.IsNull()||!TopExp_Explorer(result,TopAbs_SOLID).More()||!BRepCheck_Analyzer(result).IsValid()){error="Component representation must contain valid OCCT solid geometry.";return {};}
        if(cacheKey.isNotEmpty())validShapeCache[cacheKey]=result;
        return result;
    }
    catch(const Standard_Failure& failure){error="Cannot build component geometry: "+juce::String(failure.GetMessageString());return {};}
}
juce::String encodeShape(const TopoDS_Shape& shape){std::ostringstream stream;BRepTools::Write(shape,stream);return juce::String::fromUTF8(stream.str().c_str());}
juce::String details(const juce::var& record)
{
    juce::String text=record["name"].toString()+"\n"+record["origin"].toString()+" / "+record["category"].toString()+"\nManufacturer: "+record.getProperty("manufacturer","Unknown").toString()+"\nModel / part: "+record.getProperty("model","Unknown").toString()+" / "+record.getProperty("partNumber","Unknown").toString()+"\n"+record["description"].toString();
    text+="\nReview status: "+record.getProperty("reviewStatus","User component").toString()+"\nCAD representation: "+record["geometry"]["label"].toString()+"\nPublished mass: "+(record.hasProperty("massKg")?record["massKg"].toString()+" kg":"Unknown")+"\nCentre of gravity: Unknown unless calculated from an explicitly assigned homogeneous material and actual geometry.";
    if(record.hasProperty("material"))
    {text+="\nExisting material: "+record["material"]["name"].toString();for(const auto pair:{std::pair<const char*,const char*>{"density","Density"},{"youngsModulus","Young's modulus"},{"poissonsRatio","Poisson's ratio"},{"yieldStrength","Yield strength"}}){const auto property=record["material"]["properties"][pair.first];text+="\n"+juce::String(pair.second)+": "+(property.isObject()?property["value"].toString()+" "+property["unit"].toString():juce::String("Unknown"));}}
    if(auto* fields=record["properties"].getArray())for(const auto& field:*fields)
    {text+="\n"+field["label"].toString()+": "+(field["value"].isVoid()||(field["value"].isString()&&field["value"].toString().trim().isEmpty())?juce::String("Unknown"):display(field["value"]));if(field.hasProperty("unit"))text+=" "+field["unit"].toString();if(field.hasProperty("confidence"))text+=" ("+field["confidence"].toString()+")";if(field.hasProperty("note")&&field["note"].toString().isNotEmpty())text+=" — "+field["note"].toString();if(field.hasProperty("sourceUrl"))text+="\n  Source: "+field["sourceUrl"].toString();if(field.hasProperty("siValue"))text+="  ["+display(field["siValue"])+" "+field["siUnit"].toString()+"]";if(field.hasProperty("formula"))text+=" (workbook formula: "+field["formula"].toString()+")";}
    text+="\nSource workbook: "+record["provenance"]["workbook"].toString()+" / "+record["provenance"]["sheet"].toString()+" / row "+record["provenance"]["row"].toString()+"\nSource: "+record["sourceUrl"].toString();
    if(auto* refs=record["provenance"]["references"].getArray())for(const auto& ref:*refs){text+="\nSource register: "+ref["sheet"].toString();for(const auto& field:*ref["properties"].getArray())text+="\n  "+field["label"].toString()+": "+field["value"].toString();}
    if(!engineering::specification(record).isVoid())text+="\nStructured engineering specifications, SI conversions and model readiness:\n"+juce::JSON::toString(engineering::interpret(record));
    text+="\nEngineering use: catalog specifications and simplified envelopes do not establish material homogeneity, mounting detail, mass distribution, joints or simulation validity.";return text;
}
}
