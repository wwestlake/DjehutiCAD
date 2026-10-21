#include "CadCantilever.h"
#include "CadTestFixture.h"
#include "CadFixtureGeometry.h"
#include <cmath>
#include <limits>

namespace djehuti::cad
{
bool runCadCantileverCheck(juce::var& report)
{
    const auto object=[](){return juce::var(new juce::DynamicObject);};
    const auto put=[](juce::var& v,const juce::Identifier& key,const juce::var& value){v.getDynamicObject()->setProperty(key,value);};
    const auto closeTo=[](double a,double b){return std::abs(a-b)<=1.0e-10*std::max(1.0e-12,std::abs(b));};
    juce::Array<juce::var> checks,benchmarks;bool passed=true;
    const auto check=[&](bool ok,const juce::String& label,const juce::var& detail=juce::var())
    {auto c=object();put(c,"check",label);put(c,"passed",ok);if(!detail.isVoid())put(c,"detail",detail);checks.add(c);passed&=ok;};
    // Independent hand-calculated benchmarks in N, mm, MPa, mm^4. Production solver uses SI.
    struct Benchmark{double lengthMm,breadthMm,depthMm,forceN,eMPa,tipMm,stressMPa;};
    for(const auto b:{Benchmark{400,20,10,100,200000,6.4,120},Benchmark{800,20,10,25,200000,12.8,60},Benchmark{400,30,20,200,70000,3.0476190476190476,40}})
    {
        const beam::Input input{b.lengthMm/1000,b.breadthMm/1000,b.depthMm/1000,b.eMPa*1e6,b.forceN};
        const auto r=beam::solve(input);auto v=object();put(v,"lengthMm",b.lengthMm);put(v,"breadthMm",b.breadthMm);put(v,"depthMm",b.depthMm);put(v,"forceN",b.forceN);
        put(v,"expectedTipMm",b.tipMm);put(v,"calculatedTipMm",r.tipDisplacementM*1000);put(v,"expectedStressMPa",b.stressMPa);put(v,"calculatedStressMPa",r.maximumStressPa/1e6);benchmarks.add(v);
        check(r.valid&&closeTo(r.tipDisplacementM*1000,b.tipMm)&&closeTo(r.maximumStressPa/1e6,b.stressMPa),"Independent cantilever benchmark",v);
        check(closeTo(r.reactionN,-b.forceN)&&closeTo(r.reactionMomentNm,-b.forceN*b.lengthMm/1000),"Force and moment equilibrium");
        check(closeTo(r.maximumStrain,b.stressMPa/b.eMPa)&&closeTo(r.areaM2,b.breadthMm*b.depthMm*1e-6),"Section area and elastic strain use coherent units");
        check(r.stations.size()==41&&r.stations.front().displacementM==0&&r.stations.back().momentNm==0&&closeTo(r.stations[20].momentNm,b.forceN*b.lengthMm/2000)&&closeTo(r.stations[20].displacementM*1000,b.tipMm*5/16),"Calculated shape and bending-moment distribution satisfy end/interior conditions");
    }
    const beam::Input base{0.4,0.02,0.01,200e9,10};const auto reference=beam::solve(base);
    auto varied=base;varied.lengthM*=2;check(closeTo(beam::solve(varied).tipDisplacementM,8*reference.tipDisplacementM)&&closeTo(beam::solve(varied).maximumStressPa,2*reference.maximumStressPa),"Length scaling: displacement L^3, stress L");
    varied=base;varied.forceN*=3;check(closeTo(beam::solve(varied).tipDisplacementM,3*reference.tipDisplacementM)&&closeTo(beam::solve(varied).maximumStressPa,3*reference.maximumStressPa),"Linear load scaling");
    varied=base;varied.depthM*=2;check(closeTo(beam::solve(varied).tipDisplacementM,reference.tipDisplacementM/8)&&closeTo(beam::solve(varied).maximumStressPa,reference.maximumStressPa/4),"Section depth scaling: displacement h^-3, stress h^-2");
    varied=base;varied.forceN=-base.forceN;check(closeTo(beam::solve(varied).tipDisplacementM,-reference.tipDisplacementM),"Load sign reverses deflection");
    for(int kind=0;kind<6;++kind){varied=base;if(kind==0)varied.lengthM=0;if(kind==1)varied.depthM=-1;if(kind==2)varied.youngsModulusPa=0;if(kind==3)varied.forceN=std::numeric_limits<double>::infinity();if(kind==4)varied.lengthM=0.1;if(kind==5)varied.forceN=1e9;check(!beam::solve(varied).valid,"Reject invalid dimensions/modulus/load or unsupported slenderness/deflection: "+juce::String(kind));}

    const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("build/cantilever-check").getChildFile(juce::Uuid().toDashedString());folder.createDirectory();
    CadCommandService cad;cad.setCatalogFile(folder.getChildFile("catalog.json"));juce::String box,error;cad.createBox(400,20,10,box,error);
    auto m=object();put(m,"name","Benchmark steel");put(m,"youngsModulusMPa",200000);put(m,"densityKgPerM3",7850);put(m,"poissonsRatio",0.3);put(m,"yieldStrengthMPa",250);
    const auto material=cad.materialCommand("cad_material_create",m)["id"].toString();m=object();put(m,"feature",box);put(m,"material",material);cad.materialCommand("cad_material_assign",m);
    const auto sourceFile=folder.getChildFile("cantilever.djo");check(cad.save(sourceFile,error),"Save independent beam source",error);juce::MemoryBlock sourceBytes;sourceFile.loadFileAsData(sourceBytes);const auto revision=cad.document().revision();
    CadTestFixture fixture("Cantilever benchmark");
    const auto command=[&](const char* action,juce::var args=juce::var(new juce::DynamicObject)){put(args,"action",action);return fixture.command(args);};
    auto a=object();put(a,"path",sourceFile.getFullPathName());const auto source=command("addSource",a)["id"].toString();
    a=object();auto d=object();put(d,"source",source);put(d,"kind","part");put(d,"feature",box);put(d,"name","Beam");put(a,"definition",d);const auto target=command("addModel",a)["id"].toString();
    const auto settings=[&](const juce::String& beamAxis="X",const juce::String& bendingAxis="Y")
    {auto options=object(),s=object(),args=object();put(options,"beamAxis",beamAxis);put(options,"bendingAxis",bendingAxis);put(options,"visualizationScale",5.0);put(s,"method","linearStatic");put(s,"options",options);put(args,"definition",s);return command("settings",args);};settings();
    const auto equipment=[&](const juce::String& kind,const juce::String& face,const juce::var& point,const juce::String& anchor="faceCentre")
    {auto e=object(),args=object();put(e,"kind",kind);put(e,"target",target);put(e,"face",face);put(e,"pointMm",point);put(e,"anchor",anchor);put(e,"direction",juce::Array<juce::var>{0,0,-1});if(kind=="force"){put(e,"magnitude",100);put(e,"unit","N");}put(args,"definition",e);return command("upsertEquipment",args);};
    std::vector<CadFaceInfo> faces;cad.document().listFaces(box,faces,error);juce::String rootFace,tipFace;for(const auto& f:faces){if(std::abs(f.centre.X())<1e-8)rootFace=f.token;if(std::abs(f.centre.X()-400)<1e-8)tipFace=f.token;}
    const auto support=equipment("fixed",rootFace,juce::Array<juce::var>{0,10,5})["id"].toString();const auto load=equipment("force",tipFace,juce::Array<juce::var>{400,10,5})["id"].toString();
    equipment("displacement",tipFace,juce::Array<juce::var>{400,10,5});equipment("reaction",rootFace,juce::Array<juce::var>{0,10,5});equipment("stress",rootFace,juce::Array<juce::var>{0,10,10},"point");equipment("strain",rootFace,juce::Array<juce::var>{0,10,10},"point");equipment("safetyFactor",rootFace,juce::Array<juce::var>{0,10,5});
    auto run=command("run")["result"];check(run["status"].toString()=="completed"&&closeTo((double)run["rows"][0]["tipDisplacementMagnitudeM"],0.0064)&&closeTo((double)run["rows"][0]["section"]["secondMomentM4"],1.6666666666666667e-9),"Fixture resolves geometry, material and section inertia (not volume inertia)",run);
    check(run["measurements"][0]["status"].toString()=="completed"&&closeTo((double)run["measurements"][0]["value"][2],-0.0064)&&run["measurements"][1]["status"].toString()=="completed"&&closeTo((double)run["measurements"][2]["value"],120e6)&&closeTo((double)run["measurements"][3]["value"],0.0006)&&run["measurements"][4]["status"].toString()=="unsupported","Displacement/reaction/fibre stress/strain probes execute; safety factor stays unsupported",run["measurements"]);
    const auto lines=fixture.overlays();int deformationLines=0;for(const auto& line:lines)deformationLines+=line.argb==0xffff66dd;check(deformationLines==40,"Viewport overlay is the calculated 41-station deformation");
    const auto fixtureFile=folder.getChildFile("cantilever.djfixture");check(fixture.save(fixtureFile,false,error),"Save stamped structural result",error);CadTestFixture reopened("Reopen");check(reopened.open(fixtureFile,error)&&!(bool)reopened.inspect()["result"]["stale"]&&reopened.overlays().size()==lines.size(),"Reopen preserves current numerical results and calculated overlay",error);
    juce::MemoryBlock after;sourceFile.loadFileAsData(after);check(after==sourceBytes&&cad.document().revision()==revision,"Structural calculations and fixture persistence preserve authoritative source bytes/history");
    const auto update=[&](const juce::String& id,const std::function<void(juce::var&)>& edit)
    {for(const auto& old:*fixture.configuration()["equipment"].getArray())if(old["id"].toString()==id){auto e=juce::JSON::parse(juce::JSON::toString(old));edit(e);auto args=object();put(args,"definition",e);return command("upsertEquipment",args);}return juce::var();};
    update(load,[&](juce::var& e){put(e,"magnitude",0.1);put(e,"unit","kN");});run=command("run")["result"];check(run["status"].toString()=="completed"&&closeTo((double)run["rows"][0]["tipDisplacementMagnitudeM"],0.0064),"kN input is converted once to N");
    update(load,[&](juce::var& e){put(e,"direction",juce::Array<juce::var>{1,0,0});});check(command("run")["result"]["status"].toString()=="unsupported","Reject axial load in transverse cantilever model");command("undo");
    settings("X","X");check(command("run")["result"]["status"].toString()=="unsupported","Reject coincident beam/bending axes");settings();
    update(load,[&](juce::var& e){put(e,"magnitude",1000);});check(command("run")["result"]["status"].toString()=="unsupported","Reject non-small/yielding response");command("undo");
    const auto extraForce=equipment("force",tipFace,juce::Array<juce::var>{400,10,5})["id"].toString();check(command("run")["result"]["status"].toString()=="unsupported","Reject multiple end forces rather than silently dropping a load");a=object();put(a,"item",extraForce);command("removeEquipment",a);
    a=object();d=object();auto options=object();put(options,"beamAxis","X");put(options,"bendingAxis","Y");put(options,"includeSelfWeight",true);put(d,"options",options);put(a,"definition",d);command("settings",a);check(command("run")["result"]["status"].toString()=="unsupported","Explicit self-weight request is rejected");command("undo");
    update(support,[&](juce::var& e){put(e,"kind","support");put(e,"fixed",juce::Array<juce::var>{"ux","uy","uz"});});check(command("run")["result"]["status"].toString()=="unsupported","Reject non-clamped support");command("undo");
    a=object();put(a,"item",load);command("removeEquipment",a);check(command("run")["result"]["status"].toString()=="unsupported","Reject missing end force");command("undo");
    auto unknown=matl::materialToJson(*cad.document().materialsData().find(material));put(unknown,"properties",object());a=object();put(a,"model",target);put(a,"material",unknown);auto substitution=command("substituteMaterial",a);check(!(bool)substitution["ok"],"Material boundary rejects a record missing Young's modulus",substitution);
    a=object();put(a,"feature",box);cad.materialCommand("cad_material_clear",a);cad.save(sourceFile,error);a=object();put(a,"source",source);command("acceptSource",a);check(command("run")["result"]["status"].toString()=="unsupported","Unassigned effective material cannot produce a stiffness result");
    a=object();put(a,"feature",box);put(a,"material",material);cad.materialCommand("cad_material_assign",a);cad.save(sourceFile,error);a=object();put(a,"source",source);command("acceptSource",a);
    auto softer=matl::materialToJson(*cad.document().materialsData().find(material));auto modulus=softer["properties"]["youngsModulus"];put(modulus,"value",100000);a=object();put(a,"model",target);put(a,"material",softer);command("substituteMaterial",a);run=command("run")["result"];check(run["status"].toString()=="completed"&&closeTo((double)run["rows"][0]["tipDisplacementMagnitudeM"],0.0128)&&(bool)run["materials"][0]["substituted"],"Fixture material substitution affects stiffness without editing source");command("undo");
    // A centre anchor moves with its named face; an exact fibre point must not be guessed onto a new face.
    update(load,[&](juce::var& e){put(e,"anchor","point");});
    cad.setDimensions(box,500,20,10,0,error);cad.save(sourceFile,error);check(!(bool)fixture.validation()["valid"]&&(bool)fixture.inspect()["result"]["stale"],"Source changes stale results before acceptance");
    a=object();put(a,"source",source);check((bool)command("acceptSource",a)["ok"],"Accept same-model source revision");
    bool exactInvalid=false,centreRemapped=false;for(const auto& e:*fixture.configuration()["equipment"].getArray()){if(e["id"].toString()==load)exactInvalid=e["attachmentIssue"].toString().isNotEmpty()&&(double)e["pointMm"][0]==400;if(e["kind"].toString()=="displacement")centreRemapped=(double)e["pointMm"][0]==500;}
    check(exactInvalid&&centreRemapped&&command("run")["result"]["status"].toString()=="invalid","Acceptance remaps explicit face-centre anchors, retains invalid exact points and blocks structural calculation");
    update(load,[&](juce::var& e){put(e,"pointMm",juce::Array<juce::var>{500,10,5});put(e,"anchor","faceCentre");});check((bool)fixture.validation()["valid"]&&command("run")["result"]["status"].toString()=="completed","Manual reattachment clears the issue and enables valid rerun");
    update(load,[&](juce::var& e){put(e,"pointMm",juce::Array<juce::var>{500,100,5});});check(!(bool)fixture.validation()["valid"]&&command("run")["result"]["status"].toString()=="invalid","Point outside a trimmed face is invalid even when on the infinite face plane");command("undo");
    update(load,[&](juce::var& e){put(e,"face","deleted-or-split-face");});a=object();put(a,"source",source);command("acceptSource",a);check(!(bool)fixture.validation()["valid"],"Missing/ambiguous face is never retargeted to unrelated geometry");command("undo");command("undo");
    update(support,[&](juce::var& e){put(e,"face",tipFace);put(e,"pointMm",juce::Array<juce::var>{500,10,5});put(e,"anchor","faceCentre");});
    update(load,[&](juce::var& e){put(e,"face",rootFace);put(e,"pointMm",juce::Array<juce::var>{0,10,5});put(e,"anchor","faceCentre");});run=command("run")["result"];
    check(run["status"].toString()=="completed"&&closeTo((double)run["rows"][0]["tipDisplacementM"][2],-0.0125)&&closeTo((double)run["rows"][0]["reactionMomentNm"][1],50),"Either end may be fixed; reaction moment sign follows the physical beam direction",run);
    // Transform covariance: source geometry and occurrence can both be rotated and translated.
    cad.setPlacement(box,10,20,30,25,15,35,error);cad.save(sourceFile,error);a=object();put(a,"source",source);command("acceptSource",a);
    // Exact stress fibre probes intentionally need correction after a source placement change; remove them for this check.
    juce::StringArray remove;for(const auto& e:*fixture.configuration()["equipment"].getArray())if(e["kind"].toString()=="stress"||e["kind"].toString()=="strain")remove.add(e["id"].toString());for(const auto& id:remove){a=object();put(a,"item",id);command("removeEquipment",a);}
    const auto rotated=fixturegeom::featurePlacement(*cad.document().find(box));const auto force=gp_Vec(0,0,-1).Transformed(rotated);update(load,[&](juce::var& e){put(e,"direction",juce::Array<juce::var>{force.X(),force.Y(),force.Z()});});
    a=object();d=object();put(d,"placement",juce::JSON::parse(R"({"x":42,"y":-8,"z":19,"rx":12,"ry":-20,"rz":17})"));put(a,"model",target);put(a,"definition",d);command("updateModel",a);run=command("run")["result"];
    double forceResidual=0,momentResidual=0;if(run["status"].toString()=="completed")
    {const auto row=run["rows"][0];const auto f=[&](const juce::var& v){return gp_Vec((double)v[0],(double)v[1],(double)v[2]);};const auto applied=f(row["beam"]["transverseWorld"])*(double)row["forceN"];forceResidual=(applied+f(row["reactionForceN"])).Magnitude();momentResidual=((f(row["beam"]["axisWorld"])*(double)row["section"]["lengthM"]).Crossed(applied)+f(row["reactionMomentNm"])).Magnitude();}
    check(run["status"].toString()=="completed"&&forceResidual<1e-9&&momentResidual<1e-9,"Rotated source plus fixture frames preserve reaction equilibrium",run);
    auto root=object();put(root,"passed",passed);put(root,"checks",checks);put(root,"benchmarks",benchmarks);put(root,"folder",folder.getFullPathName());report=root;return passed;
}
}
