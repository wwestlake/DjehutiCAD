#include "CadComponents.h"
#include "CadDocument.h"
#include "EngineeringSpecificationChecks.h"
#include <STEPControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>

namespace djehuti::cad
{
bool runCadManufacturerCheck(juce::var& report,const juce::File& root)
{
    using namespace components;juce::var semantic;bool passed=engineering::checks(root.getChildFile("manufacturer-reference-library.json"),semantic);juce::Array<juce::var> rows;
    const auto check=[&](bool ok,const char* name,const juce::var& detail=juce::var()){auto r=object();put(r,"name",name);put(r,"passed",ok);put(r,"detail",detail);rows.add(r);passed&=ok;};
    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/manufacturer-check");folder.createDirectory();
    juce::var library;juce::String error;check(engineering::loadLibrary(root.getChildFile("manufacturer-reference-library.json"),library,error),"Load reference library",error);
    const int solidCounts[]{1,12,1};const double volumes[]{2117.92,9342.31,427.522};int index=0;
    if(library["records"].isArray())for(const auto& original:*library["records"].getArray())
    {
        auto record=clone(original);juce::File source;
        for(const auto& s:*engineering::specification(record)["sources"].getArray()){const auto p=s["path"].toString();if(p.endsWith(".step")||p.endsWith(".stp"))source=root.getChildFile(p);}
        const auto hash=engineering::fileHash(source);const bool imported=attachModel(record,source,error);check(imported,"Import original manufacturer STEP",imported?record["geometry"]["import"]:juce::var(error));
        if(!imported){++index;continue;}
        auto geometry=shape(record,error);GProp_GProps props;BRepGProp::VolumeProperties(geometry,props);TopTools_IndexedMapOfShape solids;TopExp::MapShapes(geometry,TopAbs_SOLID,solids);
        check(BRepCheck_Analyzer(geometry).IsValid()&&solids.Extent()==solidCounts[index]&&std::abs(props.Mass()-volumes[index])<.02,"Valid topology, distinct solids and independent diagnostic volume agree",props.Mass());
        CadDocument document;juce::String instance;const auto placement=mech::frameFromEulerDegrees(30,-20,10,0,0,90);
        const bool inserted=document.insertComponent(record,placement,instance,error);check(inserted,"Insert original geometry through OCAF",error);
        if(inserted)
        {
            const auto feature=document.mechanicsData().instances.front().featureId;const auto file=folder.getChildFile(record["id"].toString()+".djo");
            check(document.undo(error)&&document.features().empty()&&document.redo(error),"Native OCAF undo/redo retains manufacturer snapshot",error);
            check(document.save(file,error),"Save native document",error);CadDocument reopened;const bool loaded=reopened.load(file,error);check(loaded,"Reopen native document",error);
            if(loaded&&reopened.find(feature))
            {
                const auto* snapshot=reopened.componentFor(feature);GProp_GProps after;BRepGProp::VolumeProperties(reopened.find(feature)->shape,after);
                const auto& occurrence=reopened.mechanicsData().instances.front();
                const auto qError=[&] { const double a=std::sqrt(std::pow(occurrence.placement.qw-placement.qw,2)+std::pow(occurrence.placement.qx-placement.qx,2)+std::pow(occurrence.placement.qy-placement.qy,2)+std::pow(occurrence.placement.qz-placement.qz,2)); const double b=std::sqrt(std::pow(occurrence.placement.qw+placement.qw,2)+std::pow(occurrence.placement.qx+placement.qx,2)+std::pow(occurrence.placement.qy+placement.qy,2)+std::pow(occurrence.placement.qz+placement.qz,2)); return std::min(a,b); }();
                check(snapshot&&juce::JSON::toString((*snapshot)["engineering"])==juce::JSON::toString(record["engineering"]),"Reopen preserves structured engineering metadata");
                const auto beforeImport=record["geometry"]["import"], afterImport=(*snapshot)["geometry"]["import"];
                check(snapshot&&afterImport["solidCount"]==beforeImport["solidCount"]&&afterImport["faceCount"]==beforeImport["faceCount"]&&afterImport["sourceLengthUnits"]==beforeImport["sourceLengthUnits"]&&afterImport["assemblyNodes"].size()==beforeImport["assemblyNodes"].size()&&(*snapshot)["geometry"]["sourceSha256"]==record["geometry"]["sourceSha256"],"Reopen preserves source hierarchy and import metadata");
                check(std::abs(after.Mass()-props.Mass())<1e-6,"Reopen preserves imported volume");
                check(std::abs(occurrence.placement.x-placement.x)<1e-9&&std::abs(occurrence.placement.y-placement.y)<1e-9&&std::abs(occurrence.placement.z-placement.z)<1e-9,"Reopen preserves occurrence translation");
                check(qError<1e-9,"Reopen preserves occurrence rotation");
            }
        }
        check(engineering::fileHash(source)==hash,"Manufacturer original is unchanged");++index;
    }
    // A unit-sensitive placed compound checks scale and orientation independently of the large originals.
    auto synthetic=clone(library["records"][0]);BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    const auto box=BRepPrimAPI_MakeBox(1.0,.5,.25).Shape();builder.Add(compound,box);gp_Trsf offset;offset.SetTranslation(gp_Vec(2.0,0,0));builder.Add(compound,box.Moved(TopLoc_Location(offset)));
    STEPControl_Writer writer;const juce::String oldUnit=Interface_Static::CVal("write.step.unit");Interface_Static::SetCVal("write.step.unit","INCH");writer.Transfer(compound,STEPControl_AsIs);
    const auto inch=folder.getChildFile("placed-inch.step");const auto written=writer.Write(inch.getFullPathName().toRawUTF8());Interface_Static::SetCVal("write.step.unit",oldUnit.toRawUTF8());
    const bool imported=written==IFSelect_RetDone&&attachModel(synthetic,inch,error);check(imported,"Import synthetic inch-unit placed compound",error);
    if(imported){const auto s=shape(synthetic,error);GProp_GProps p;BRepGProp::VolumeProperties(s,p);Bnd_Box b;BRepBndLib::AddOptimal(s,b,false,false);double x0,y0,z0,x1,y1,z1;b.Get(x0,y0,z0,x1,y1,z1);check(std::abs(p.Mass()-2*25.4*.5*25.4*.25*25.4)<1e-6&&std::abs(x1-x0-76.2)<1e-5&&std::abs(y1-y0-12.7)<1e-5&&std::abs(z1-z0-6.35)<1e-5&&juce::JSON::toString(synthetic["geometry"]["import"]["sourceLengthUnits"]).containsIgnoreCase("inch"),"Inches normalize to mm; separate solid placements and axes remain intact",synthetic["geometry"]["import"]);}
    const auto before=juce::JSON::toString(synthetic);const auto bad=folder.getChildFile("invalid.step");bad.replaceWithText("not a STEP file");check(!attachModel(synthetic,bad,error)&&before==juce::JSON::toString(synthetic),"Failed transfer leaves original record unchanged");check(!attachModel(synthetic,folder.getChildFile("unsupported.igs"),error)&&error.contains("TKDEIGES"),"IGES limitation reports the missing toolkit explicitly");
    report=object();put(report,"passed",passed);put(report,"engineering",semantic);put(report,"geometry",rows);put(report,"folder",folder.getFullPathName());return passed;
}
}
