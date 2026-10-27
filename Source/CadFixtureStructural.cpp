#include "CadTestFixture.h"
#include "CadCantilever.h"
#include "CadFixtureGeometry.h"
#include "CadEngineeringUnits.h"
#include <cmath>

namespace djehuti::cad
{
namespace
{
juce::var object(){return new juce::DynamicObject;}
void put(juce::var& v,const juce::Identifier& key,const juce::var& value){v.getDynamicObject()->setProperty(key,value);}
juce::var xyz(const gp_XYZ& p){return juce::Array<juce::var>{p.X(),p.Y(),p.Z()};}
gp_Pnt point(const juce::var& p){return {(double)p[0],(double)p[1],(double)p[2]};}
gp_Vec vector(const juce::var& p){return {(double)p[0],(double)p[1],(double)p[2]};}
int axis(const juce::var& v){return juce::StringArray{"X","Y","Z"}.indexOf(v.toString());}
gp_Vec unit(int i){return {i==0?1.0:0.0,i==1?1.0:0.0,i==2?1.0:0.0};}
}
bool CadTestFixture::executeCantilever(juce::var&,juce::Array<juce::var>& rows,juce::Array<juce::var>& materials,juce::String& error)
{
    const auto fail=[&](const juce::String& reason){error=reason;return false;};
    if(config["models"].size()!=1||config["models"][0]["kind"].toString()!="part")
        return fail("Cantilever analysis supports exactly one part occurrence; assemblies and multiple bodies are unsupported.");
    const auto parts=bodies();if(parts.size()!=1)return fail("Select one supported solid beam.");
    const auto& body=parts.front();const auto* feature=body.source->find(body.feature);
    if(!feature||feature->type!="box")return fail("The initial cantilever backend supports an unmodified parametric Box with rectangular section only.");
    const auto options=config["settings"]["options"];
    const int beamAxis=axis(options["beamAxis"]), bendingAxis=axis(options["bendingAxis"]);
    if(beamAxis<0||bendingAxis<0||beamAxis==bendingAxis)return fail("Define distinct beamAxis and bendingAxis (X, Y or Z in the source feature's local frame).");
    if((bool)options["includeSelfWeight"])return fail("Self-weight loading is unsupported by the end-force cantilever backend.");
    const double scale=(double)options.getProperty("visualizationScale",1.0);
    if(!std::isfinite(scale)||scale<=0||scale>1.0e6)return fail("Deformation visualization scale must be > 0 and <= 1000000.");
    const juce::var *support=nullptr,*load=nullptr;
    for(const auto& e:*config["equipment"].getArray())
    {
        const auto kind=e["kind"].toString();
        if(kind=="fixed"||kind=="support")
        {
            if(support)return fail("Cantilever analysis requires exactly one fixed support.");support=&e;
            if(kind=="support")
            {if(e["fixed"].size()!=6)return fail("The cantilever support must restrain all six degrees of freedom.");}
        }
        else if(kind=="force"){if(load)return fail("Cantilever analysis requires exactly one free-end point force.");load=&e;}
        else if(!juce::StringArray{"displacement","reaction","stress","strain","point","safetyFactor"}.contains(kind))
            return fail("Unsupported cantilever equipment: "+kind+". Only one fixed end and one transverse end force are supported.");
    }
    if(!support||!load)return fail("Add one fixed support and one transverse point force at opposite end-face centres.");
    if((*support)["face"].toString().isEmpty()||(*load)["face"].toString().isEmpty())
        return fail("The fixed support and end force must be attached to persistent end faces.");
    const double dimensions[]{feature->width,feature->depth,feature->height};
    const int transverseAxis=3-beamAxis-bendingAxis;
    const auto localToSource=fixturegeom::featurePlacement(*feature), sourceToLocal=localToSource.Inverted();
    auto root=point((*support)["pointMm"]).Transformed(sourceToLocal);
    auto tip=point((*load)["pointMm"]).Transformed(sourceToLocal);
    constexpr double tol=fixturegeom::attachmentToleranceMm;
    const bool minEnd=std::abs(root.Coord(beamAxis+1))<=tol;
    const bool maxEnd=std::abs(root.Coord(beamAxis+1)-dimensions[beamAxis])<=tol;
    if(!minEnd&&!maxEnd)return fail("Fixed support must be at a beam end-face centre.");
    for(int i=0;i<3;++i)
    {
        const double expectedRoot=i==beamAxis?(minEnd?0:dimensions[i]):dimensions[i]/2;
        const double expectedTip=i==beamAxis?(minEnd?dimensions[i]:0):dimensions[i]/2;
        if(std::abs(root.Coord(i+1)-expectedRoot)>tol||std::abs(tip.Coord(i+1)-expectedTip)>tol)
            return fail("Support and force must be at opposite end-face centres; eccentric loads and torsion are unsupported.");
    }
    const gp_Vec along=unit(beamAxis)*(minEnd?1:-1), bending=unit(bendingAxis), transverse=bending.Crossed(along);
    const auto direction=vector((*load)["direction"]).Transformed(sourceToLocal);
    const double signedForce=(double)(*load)["magnitude"]*direction.Dot(transverse);
    if(std::abs(std::abs(direction.Dot(transverse))-1)>1.0e-8)
        return fail("Force must be transverse and in the plane defined by the beam and explicit bending axes (source feature local axes).");
    CadMaterial material;const auto effective=materialFor(body.target);
    if(!effective.isObject()||!matl::materialFromJson(effective,material,error)||material.model!="isotropic"||!material.youngsModulusMPa)
        return fail("Assign an isotropic material or fixture substitution with a positive Young's modulus.");
    const beam::Input input{units::mmToM(dimensions[beamAxis]),units::mmToM(dimensions[bendingAxis]),
                            units::mmToM(dimensions[transverseAxis]),units::mpaToPa(*material.youngsModulusMPa),signedForce};
    const auto solved=beam::solve(input);if(!solved.valid)return fail(juce::String(solved.error));
    if(material.yieldStrengthMPa&&solved.maximumStressPa>=units::mpaToPa(*material.yieldStrengthMPa))
        return fail("Calculated stress reaches/exceeds the material yield strength; elastic analysis is unsupported for this load.");
    const auto localToWorld=body.transform*localToSource;
    root.Transform(localToWorld);tip.Transform(localToWorld);
    const auto worldAxis=along.Transformed(localToWorld),worldTransverse=transverse.Transformed(localToWorld),worldBending=bending.Transformed(localToWorld);
    auto row=object(),section=object(),model=object();put(row,"target",body.target);put(row,"name",feature->name);put(row,"frame","fixtureWorld");
    put(row,"solver","eulerBernoulliRectangularCantilever/v1");put(row,"support",(*support)["id"]);put(row,"load",(*load)["id"]);
    put(section,"type","rectangle");put(section,"lengthM",input.lengthM);put(section,"breadthM",input.breadthM);put(section,"depthM",input.depthM);
    put(section,"areaM2",solved.areaM2);put(section,"secondMomentM4",solved.secondMomentM4);put(section,"extremeFibreM",solved.extremeFibreM);put(row,"section",section);
    put(model,"rootMm",xyz(root.XYZ()));put(model,"endMm",xyz(tip.XYZ()));put(model,"axisWorld",xyz(worldAxis.XYZ()));
    put(model,"transverseWorld",xyz(worldTransverse.XYZ()));put(model,"bendingAxisWorld",xyz(worldBending.XYZ()));put(row,"beam",model);
    put(row,"youngsModulusPa",input.youngsModulusPa);put(row,"forceN",input.forceN);put(row,"visualizationScale",scale);
    put(row,"tipDisplacementM",xyz((worldTransverse*solved.tipDisplacementM).XYZ()));put(row,"tipDisplacementMagnitudeM",std::abs(solved.tipDisplacementM));
    put(row,"reactionForceN",xyz((worldTransverse*solved.reactionN).XYZ()));put(row,"reactionMomentNm",xyz((worldBending*solved.reactionMomentNm).XYZ()));
    put(row,"maximumBendingStressPa",solved.maximumStressPa);put(row,"maximumElasticStrain",solved.maximumStrain);
    put(row,"elasticLimitChecked",material.yieldStrengthMPa.has_value());put(row,"selfWeightIncluded",false);
    juce::Array<juce::var> stations;
    for(const auto& station:solved.stations)
    {
        auto s=object();put(s,"xM",station.xM);put(s,"displacementM",station.displacementM);put(s,"slopeRad",station.slopeRad);
        put(s,"bendingMomentNm",station.momentNm);put(s,"extremeStressPa",station.extremeStressPa);put(s,"extremeStrain",station.extremeStrain);
        const auto reference=root.Translated(worldAxis*units::mToMm(station.xM));
        put(s,"referenceMm",xyz(reference.XYZ()));put(s,"displacementWorldM",xyz((worldTransverse*station.displacementM).XYZ()));stations.add(s);
    }
    put(row,"stations",stations);rows.add(row);materials.add(effective);return true;
}
juce::var CadTestFixture::cantileverMeasurement(const juce::var& e,const juce::var& row)
{
    auto request=juce::JSON::parse(juce::JSON::toString(e));const auto kind=e["kind"].toString();
    put(request,"status","unsupported");put(request,"reason","This measurement is not supported by the rectangular cantilever model.");
    const auto complete=[&](const juce::var& value,const char* unit,const char* reason)
    {put(request,"status","completed");put(request,"value",value);put(request,"unit",unit);put(request,"reason",reason);};
    const auto parts=bodies();if(parts.size()!=1||e["target"].toString()!=row["target"].toString())return request;
    const auto p=point(e["pointMm"]).Transformed(parts[0].transform), root=point(row["beam"]["rootMm"]);
    const auto offset=gp_Vec(root,p),t=vector(row["beam"]["axisWorld"]),v=vector(row["beam"]["transverseWorld"]),b=vector(row["beam"]["bendingAxisWorld"]);
    const double xMm=offset.Dot(t),length=(double)row["section"]["lengthM"],x=units::mmToM(xMm);
    const double tol=fixturegeom::attachmentToleranceMm;
    if(xMm < -tol||xMm>units::mToMm(length)+tol)return request;
    if(kind=="point"){complete(xyz(p.XYZ()),"mm","Undeformed reference point in fixture-world coordinates.");return request;}
    if(kind=="reaction")
    {
        if(p.Distance(root)>tol){put(request,"reason","Reaction probes must be at the fixed-end centre.");return request;}
        auto reaction=object();put(reaction,"forceN",row["reactionForceN"]);put(reaction,"momentNm",row["reactionMomentNm"]);
        complete(reaction,"N; N*m","Fixed-end force and moment in fixture-world coordinates.");return request;
    }
    const beam::Input input{length,(double)row["section"]["breadthM"],(double)row["section"]["depthM"],(double)row["youngsModulusPa"],(double)row["forceN"]};
    const auto s=beam::evaluate(input,juce::jlimit(0.0,length,x));
    if(kind=="displacement")
    {
        if(std::abs(offset.Dot(v))>tol||std::abs(offset.Dot(b))>tol){put(request,"reason","Displacement probes must lie on the beam neutral axis (section centre).");return request;}
        complete(xyz((v*s.displacementM).XYZ()),"m","Calculated neutral-axis translation; Euler-Bernoulli small-deflection solution.");
    }
    else if(kind=="stress"||kind=="strain")
    {
        // Report the bending stress at the actual fibre, never the maximum stress under a misleading point label.
        const double y=units::mmToM(offset.Dot(v)),z=units::mmToM(offset.Dot(b));
        if(std::abs(y)>input.depthM/2+units::mmToM(tol)||std::abs(z)>input.breadthM/2+units::mmToM(tol))return request;
        const double stress=-s.momentNm*y/(double)row["section"]["secondMomentM4"];
        complete(kind=="stress"?stress:stress/input.youngsModulusPa,kind=="stress"?"Pa":"1","Signed longitudinal bending response at the configured fibre; no local clamp stress concentration.");
    }
    return request;
}
}
