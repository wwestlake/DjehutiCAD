#include "CadTestFixture.h"
#include "CadGeometricProperties.h"
#include "CadEngineeringUnits.h"
#include "CadKinematics.h"
#include "CadFixtureGeometry.h"
#include <TDataStd_AsciiString.hxx>
#include <TDocStd_Application.hxx>
#include <BinDrivers.hxx>
#include <windows.h>
#include <bcrypt.h>
#include <cmath>
#include <set>

namespace djehuti::cad
{
namespace
{
juce::var object() { return new juce::DynamicObject(); }
juce::var copy(const juce::var& v) { return juce::JSON::parse(juce::JSON::toString(v)); }
void put(juce::var& v, const juce::Identifier& key, const juce::var& value) { v.getDynamicObject()->setProperty(key,value); }
juce::var reply(bool ok, const juce::String& error={}) { auto v=object();put(v,"ok",ok);if(!ok)put(v,"error",error);return v; }
juce::String uid(const char* prefix) { return juce::String(prefix)+juce::Uuid().toDashedString(); }
juce::String hash(const void* data, size_t size)
{
    BCRYPT_ALG_HANDLE algorithm=nullptr; unsigned char output[32]{};
    if(size>0xffffffffu || BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    const auto status=BCryptHash(algorithm,nullptr,0,(PUCHAR)data,(ULONG)size,output,sizeof(output));
    BCryptCloseAlgorithmProvider(algorithm,0);
    return status<0?juce::String():juce::String::toHexString(output,sizeof(output),0);
}
juce::String hashText(const juce::String& text) { return hash(text.toRawUTF8(),text.getNumBytesAsUTF8()); }
juce::String hashFile(const juce::File& file) { juce::MemoryBlock data;if(!file.loadFileAsData(data))return {};return hash(data.getData(),data.getSize()); }
juce::var frame(double x=0,double y=0,double z=0,double rx=0,double ry=0,double rz=0)
{ auto v=object();put(v,"x",x);put(v,"y",y);put(v,"z",z);put(v,"rx",rx);put(v,"ry",ry);put(v,"rz",rz);return v; }
gp_Trsf transform(const juce::var& v) { return mech::frameFromEulerDegrees((double)v["x"],(double)v["y"],(double)v["z"],(double)v["rx"],(double)v["ry"],(double)v["rz"]).toTrsf(); }
juce::var vec(double x,double y,double z) { return juce::Array<juce::var>{x,y,z}; }
bool numeric(const juce::var& v) { return (v.isInt()||v.isInt64()||v.isDouble())&&std::isfinite((double)v); }
bool vector(const juce::var& v) { if(!v.isArray()||v.size()!=3)return false;for(const auto& n:*v.getArray())if(!numeric(n))return false;return true; }
juce::var* entry(juce::var& v,const juce::String& key,const juce::String& id)
{ if(auto* a=v[juce::Identifier(key)].getArray())for(auto& item:*a)if(item["id"].toString()==id)return &item;return nullptr; }
const juce::var* entry(const juce::var& v,const juce::String& key,const juce::String& id)
{ if(auto* a=v[juce::Identifier(key)].getArray())for(const auto& item:*a)if(item["id"].toString()==id)return &item;return nullptr; }
juce::String drawId(const juce::String& target,const juce::String& component) { return target+"|"+component; }
juce::StringArray kinds() { return {"fixed","support","force","moment","pressure","distributedForce","displacement","reaction","stress","strain","safetyFactor","point","jointLock"}; }
gp_Pnt equipmentPoint(const juce::var& e){return {(double)e["pointMm"][0],(double)e["pointMm"][1],(double)e["pointMm"][2]};}
bool attachment(const CadDocument& doc,const juce::String& feature,const juce::var& item,juce::String& error)
{
    if(item["attachmentIssue"].toString().isNotEmpty()){error=item["attachmentIssue"].toString();return false;}
    TopoDS_Face face;gp_Pnt centre;bool planar=false;
    if(!fixturegeom::face(doc,feature,item["face"].toString(),face,centre,planar,error))return false;
    if(!fixturegeom::onFace(face,equipmentPoint(item))){error="Attachment point is off its referenced trimmed face; correct its position.";return false;}
    return true;
}
}
CadTestFixture::CadTestFixture(const juce::String& name)
{
    Handle(TDocStd_Application) app=new TDocStd_Application;BinDrivers::DefineFormat(app);app->NewDocument(TCollection_ExtendedString("BinOcaf"),authored);authored->SetUndoLimit(100);
    config=object();put(config,"id",uid("fixture-"));put(config,"name",name);put(config,"revision",1);
    for(const auto* key:{"sources","models","equipment"})put(config,key,juce::Array<juce::var>());
    auto settings=object();put(settings,"method","geometricProperties");put(settings,"gravity",vec(0,0,-units::kStandardGravity));put(settings,"options",object());put(config,"settings",settings);writeOCAF();
}
void CadTestFixture::writeOCAF() { const auto text=juce::JSON::toString(config);TDataStd_AsciiString::Set(authored->Main().FindChild(1,true),TCollection_AsciiString(text.toRawUTF8())); }
bool CadTestFixture::restoreOCAF(juce::String& error)
{
    Handle(TDataStd_AsciiString) text;if(!authored->Main().FindChild(1,false).FindAttribute(TDataStd_AsciiString::GetID(),text)){error="Missing fixture configuration.";return false;}
    auto next=juce::JSON::parse(juce::String::fromUTF8(text->Get().ToCString()));if(!checkStructure(next,error))return false;config=next;++serial;dirty=true;return true;
}
bool CadTestFixture::checkStructure(const juce::var& c,juce::String& error) const
{
    const auto fail=[&](const juce::String& e){error=e;return false;};
    if(!c.isObject()||!c["id"].toString().startsWith("fixture-")||c["name"].toString().trim().isEmpty()||(int)c["revision"]<1)return fail("A fixture needs an identity, name and positive revision.");
    for(const auto* key:{"sources","models","equipment"})if(!c[key].isArray())return fail(juce::String(key)+" must be an array.");
    std::set<juce::String> ids;
    for(const auto* key:{"sources","models","equipment"})for(const auto& v:*c[key].getArray())if(!v.isObject()||v["id"].toString().isEmpty()||!ids.insert(v["id"].toString()).second)return fail("Fixture entries need unique identities.");
    for(const auto& s:*c["sources"].getArray())if(!juce::File::isAbsolutePath(s["path"].toString())||s["digest"].toString().length()!=64||s["modelId"].toString().isEmpty())return fail("A source needs an absolute path, SHA-256 revision and model identity.");
    for(const auto& m:*c["models"].getArray())
    {
        if(!entry(c,"sources",m["source"].toString()))return fail("Model refers to a missing fixture source.");
        if(m["kind"].toString()!="part"&&m["kind"].toString()!="assembly")return fail("Model kind must be part or assembly.");
        if(m["kind"].toString()=="part"&&m["feature"].toString().isEmpty())return fail("Select a source solid feature.");
        if(!m["placement"].isObject())return fail("Model placement must be an object (mm and degrees).");
        for(const auto* axis:{"x","y","z","rx","ry","rz"})if(!numeric(m["placement"][axis])||std::abs((double)m["placement"][axis])>1e9)return fail("Placement values must be finite numbers within +/- 1e9 (mm and degrees).");
        if(m.hasProperty("material")){CadMaterial material;if(!matl::materialFromJson(m["material"],material,error)||!matl::validateMaterial(material,error))return false;if(!material.id.startsWith("mat-"))return fail("Substitutions require a complete material record with a stable material ID.");}
    }
    if(!c["settings"].isObject()||!c["settings"]["options"].isObject()||!vector(c["settings"]["gravity"]))return fail("Settings need options and a finite gravity vector in m/s^2.");
    const auto availableMethods=methods();bool known=false;for(const auto& method:*availableMethods.getArray())known|=method["id"]==c["settings"]["method"];if(!known)return fail("Unknown analysis method.");
    std::set<juce::String> lockedJoints;
    for(const auto& e:*c["equipment"].getArray())
    {
        if(!kinds().contains(e["kind"].toString()))return fail("Unknown equipment kind.");
        const auto* m=entry(c,"models",e["target"].toString());if(!m)return fail("Equipment target is not a fixture occurrence.");
        if((*m)["kind"].toString()=="assembly"&&e["component"].toString().isEmpty())return fail("Select an assembly component instance.");
        if(!vector(e["pointMm"])||!vector(e["direction"]))return fail("Equipment point and direction need three finite numbers.");
        if(e.hasProperty("anchor")&&e["anchor"].toString()!="point"&&e["anchor"].toString()!="faceCentre")return fail("Attachment anchor must be point or faceCentre.");
        const auto kind=e["kind"].toString();
        if(juce::StringArray{"force","moment","pressure","distributedForce"}.contains(kind))
        {
            if(!numeric(e["magnitude"])||(double)e["magnitude"]<=0)return fail("Load magnitude must be finite and positive.");
            double norm=0;for(const auto& n:*e["direction"].getArray())norm+=(double)n*(double)n;
            if(std::abs(norm-1)>1e-8)return fail("Load direction must be a unit vector.");
            const auto expected=kind=="moment"?"N*m":kind=="pressure"?"Pa":"N";
            if(e["unit"].toString()!=expected)return fail("Stored load units must be "+juce::String(expected)+".");
        }
        if((kind=="pressure"||kind=="distributedForce")&&e["face"].toString().isEmpty())return fail("Distributed loads require a persistent face reference.");
        if(kind=="support")
        { if(!e["fixed"].isArray()||e["fixed"].size()==0)return fail("Select at least one constrained degree of freedom.");std::set<juce::String> seen;for(const auto& d:*e["fixed"].getArray())if(!analysis::degreesOfFreedom().contains(d.toString())||!seen.insert(d.toString()).second)return fail("Invalid or repeated support degree of freedom."); }
        if(kind=="jointLock"&&((*m)["kind"].toString()!="assembly"||e["joint"].toString().isEmpty()||!numeric(e["coordinate"])))return fail("Joint locks need an assembly joint and a finite coordinate (rad or m).");
        if(kind=="jointLock"&&!lockedJoints.insert(e["target"].toString()+"|"+e["joint"].toString()).second)return fail("Only one fixture lock may be assigned to a joint in an occurrence.");
    }
    return true;
}
bool CadTestFixture::edit(const std::function<bool(juce::var&,juce::String&)>& action,juce::String& error)
{
    auto next=copy(config);if(!action(next,error))return false;put(next,"revision",(int)config["revision"]+1);if(!checkStructure(next,error))return false;
    authored->NewCommand();config=next;writeOCAF();authored->CommitCommand();dirty=true;++serial;return true;
}
bool CadTestFixture::resolve(const juce::var& source,Resolved& out,juce::String& error)
{
    const juce::File file(source["path"].toString());const auto digest=hashFile(file);if(digest.isEmpty()){error="Source unavailable: "+file.getFullPathName();return false;}
    if(out.document&&out.digest==digest)return true;
    // CadDocument's native loader stages beside its input; stage in TEMP so a read-only source folder works.
    juce::TemporaryFile staged(file.hasFileExtension("djo")?".djo":".json");
    auto document=std::make_shared<CadDocument>();
    if(!file.copyFileTo(staged.getFile())||!document->load(staged.getFile(),error))return false;
    if(hashFile(file)!=digest){error="Source changed while being read. Retry validation.";return false;}
    out.document=document;out.digest=digest;out.error.clear();++serial;return true;
}
std::vector<CadTestFixture::Body> CadTestFixture::bodies()
{
    std::vector<Body> result;
    for(const auto& model:*config["models"].getArray())
    {
        const auto key=model["source"].toString();const auto* reference=entry(config,"sources",key);if(!reference)continue;
        auto& resolved=sources[key];juce::String ignored;if(!resolve(*reference,resolved,ignored))continue;
        const auto& d=*resolved.document;const auto placement=transform(model["placement"]);
        if(model["kind"].toString()=="part") {if(d.find(model["feature"].toString()))result.push_back({model["id"].toString(),{},model["feature"].toString(),resolved.document,placement});}
        else for(const auto& instance:d.mechanicsData().instances)if(d.find(instance.featureId))result.push_back({model["id"].toString(),instance.id,instance.featureId,resolved.document,placement*d.instanceShapeTransform(instance.featureId,instance.placement.toTrsf())});
    }
    return result;
}
juce::var CadTestFixture::validation()
{
    juce::StringArray problems;
    for(const auto& reference:*config["sources"].getArray())
    {
        auto& resolved=sources[reference["id"].toString()];juce::String error;
        if(!resolve(reference,resolved,error)){resolved.error=error;problems.add(error);continue;}
        if(resolved.digest!=reference["digest"].toString())problems.add("Source revision changed: "+reference["path"].toString()+". Review and accept the source update.");
        if(resolved.document->mechanicsData().modelId!=reference["modelId"].toString())problems.add("Source model identity changed: "+reference["path"].toString());
    }
    const auto parts=bodies();
    if(config["models"].size()==0)problems.add("Add at least one source part or assembly.");
    for(const auto& model:*config["models"].getArray())
    {
        bool found=false;for(const auto& b:parts)if(b.target==model["id"].toString()){const auto* f=b.source->find(b.feature);found|=f&&!f->shape.IsNull()&&TopExp_Explorer(f->shape,TopAbs_SOLID).More();}
        if(!found)problems.add("Missing solid or assembly instances: "+model["id"].toString());
        const auto runtime=sources.find(model["source"].toString());
        if(model["kind"].toString()=="assembly"&&runtime!=sources.end()&&runtime->second.document)
            for(const auto& instance:runtime->second.document->mechanicsData().instances){const auto* feature=runtime->second.document->find(instance.featureId);if(!feature||feature->shape.IsNull()||!TopExp_Explorer(feature->shape,TopAbs_SOLID).More())problems.add("Missing assembly component geometry: "+instance.id);}
    }
    for(const auto& item:*config["equipment"].getArray())
    {
        const Body* body=nullptr;for(const auto& b:parts)if(b.target==item["target"].toString()&&b.component==item["component"].toString()){body=&b;break;}
        if(!body){problems.add("Invalid equipment target: "+item["id"].toString());continue;}
        if(item["face"].toString().isNotEmpty())
        {juce::String error;if(!attachment(*body->source,body->feature,item,error))problems.add(item["id"].toString()+": "+error);}
        if(item["kind"].toString()=="jointLock")
        {
            const auto* joint=body->source->mechanicsData().findJoint(item["joint"].toString());
            if(!joint)problems.add("Missing source joint: "+item["id"].toString());
            else if(joint->type=="fixed")problems.add("Fixed joints have no adjustable coordinate: "+item["id"].toString());
            else if(joint->childInstance!=body->component&&joint->parentInstance!=body->component)problems.add("Joint lock target is not connected to this joint: "+item["id"].toString());
        }
    }
    auto v=object();put(v,"valid",problems.isEmpty());put(v,"problems",juce::var(problems));return v;
}
juce::String CadTestFixture::inputStamp() const { return hashText(juce::JSON::toString(config)); }
juce::var CadTestFixture::methods()
{
    juce::Array<juce::var> list;
    for(const auto* id:{"geometricProperties","massProperties","kinematics","linearStatic","modal","thermal","dynamics"})
    {auto v=object();put(v,"id",id);const bool available=juce::StringArray{"geometricProperties","massProperties","kinematics","linearStatic"}.contains(id);put(v,"available",available);put(v,"description",juce::String(id)=="geometricProperties"?"OCCT volume, centroid and geometric inertia; no response to loads":juce::String(id)=="massProperties"?"Density-scaled mass, centroid and self-weight; no support reactions":juce::String(id)=="kinematics"?"Existing joint/loop solver at reference coordinates and fixture joint locks; no forces or dynamics":juce::String(id)=="linearStatic"?"Rectangular Box cantilever only: one fixed end, one transverse end force, explicit local beam/bending axes; no gravity or general FEA":"No validated backend installed; setup and measurement requests are retained");list.add(v);}
    return list;
}
juce::var CadTestFixture::execute()
{
    auto run=object();put(run,"id",uid("run-"));put(run,"configuration",copy(config));put(run,"inputStamp",inputStamp());put(run,"createdUtc",juce::Time::getCurrentTime().toISO8601(true));
    const auto checked=validation();put(run,"validation",checked);juce::Array<juce::var> rows,materials,measurements;
    const auto method=config["settings"]["method"].toString();put(run,"method",method);
    juce::String status="completed",error;
    if(!(bool)checked["valid"]){status="invalid";error="Resolve model validation problems before executing.";}
    else if(!juce::StringArray{"geometricProperties","massProperties","kinematics","linearStatic"}.contains(method)){status="unsupported";error="No validated "+method+" backend is installed.";}
    else if(method=="linearStatic")
    {if(!executeCantilever(run,rows,materials,error))status="unsupported";}
    else if(method=="kinematics")
    {
        for(const auto& model:*config["models"].getArray())
        {
            if(model["kind"].toString()!="assembly"){status="unsupported";error="Kinematics requires assembly occurrences.";break;}
            auto mechanics=sources[model["source"].toString()].document->mechanicsData();
            for(const auto& e:*config["equipment"].getArray())if(e["target"]==model["id"]&&e["kind"].toString()=="jointLock")for(auto& joint:mechanics.joints)if(joint.id==e["joint"].toString()){joint.role="locked";joint.lockValue=(double)e["coordinate"];}
            const auto solved=kin::solve(mechanics,{});if(!solved.valid){status="error";error=solved.message;break;}
            auto row=solved.toJson();put(row,"target",model["id"]);put(row,"name",model.getProperty("name","Assembly"));put(row,"fixturePlacement",model["placement"]);rows.add(row);
        }
    }
    else
    {
        double totalMass=0,cx=0,cy=0,cz=0;
        for(const auto& b:bodies())
        {
            const auto properties=geomprops::ofShape(b.source->find(b.feature)->shape,b.transform);
            if(!properties.ok()){status="error";error=properties.message;break;}
            auto row=properties.toJson();put(row,"frame","fixtureWorld");put(row,"target",b.target);put(row,"component",b.component);
            const auto* model=entry(config,"models",b.target);juce::String label=(*model)["name"].toString();if(label.isEmpty())label=b.source->find(b.feature)->name;if(const auto* instance=b.source->mechanicsData().findInstance(b.component))label+=" / "+instance->name;put(row,"name",label);CadMaterial material;bool hasMaterial=false,substituted=false;
            if(model->hasProperty("material")){hasMaterial=matl::materialFromJson((*model)["material"],material,error);substituted=true;}
            else if(const auto* assignment=b.source->materialsData().assignmentFor(b.feature))if(const auto* m=b.source->materialsData().find(assignment->materialId)){material=*m;hasMaterial=true;}
            if(hasMaterial){auto used=matl::materialToJson(material);put(used,"target",b.target);put(used,"component",b.component);put(used,"substituted",substituted);materials.add(used);}
            if(method=="massProperties")
            {
                if(!hasMaterial||!material.densityKgPerM3){status="error";error="Assign source material or a fixture substitution with density: "+b.target;break;}
                const double mass=units::massKg(*material.densityKgPerM3,properties.volumeMm3);if(!std::isfinite(mass)||mass<=0){status="error";error="Mass is outside the supported numerical range.";break;}put(row,"massKg",mass);put(row,"substituted",substituted);
                juce::Array<juce::var> inertia;for(const auto& axis:properties.inertiaMm5){juce::Array<juce::var> values;for(const auto value:axis)values.add(units::mm5ToM5(value)*(*material.densityKgPerM3));inertia.add(values);}put(row,"inertiaKgM2",inertia);
                const auto g=config["settings"]["gravity"];put(row,"selfWeightN",vec(mass*(double)g[0],mass*(double)g[1],mass*(double)g[2]));
                totalMass+=mass;cx+=mass*properties.centroidMm.X();cy+=mass*properties.centroidMm.Y();cz+=mass*properties.centroidMm.Z();
            }
            rows.add(row);
        }
        if(status=="completed"&&method=="massProperties"&&totalMass>0){if(!std::isfinite(totalMass)||!std::isfinite(cx)||!std::isfinite(cy)||!std::isfinite(cz)){status="error";error="Mass totals are outside the supported numerical range.";}else{put(run,"totalMassKg",totalMass);put(run,"centroidMm",vec(cx/totalMass,cy/totalMass,cz/totalMass));}}
    }
    if(status=="completed")
    {
        const auto finalValidation=validation();
        if(!(bool)finalValidation["valid"]){status="invalid";error="Source changed during execution. Review the update and rerun.";put(run,"validation",finalValidation);run.getDynamicObject()->removeProperty("totalMassKg");run.getDynamicObject()->removeProperty("centroidMm");}
    }
    for(const auto& e:*config["equipment"].getArray())if(juce::StringArray{"displacement","reaction","stress","strain","safetyFactor","point"}.contains(e["kind"].toString()))
    {
        auto request=copy(e);put(request,"status","unsupported");put(request,"reason","This method does not calculate the requested physical response.");
        if(status=="completed"&&method=="linearStatic")request=cantileverMeasurement(e,rows[0]);
        if(status=="completed"&&e["kind"].toString()=="point"&&method!="kinematics")for(const auto& b:bodies())if(b.target==e["target"].toString()&&b.component==e["component"].toString())
        {gp_Pnt point((double)e["pointMm"][0],(double)e["pointMm"][1],(double)e["pointMm"][2]);point.Transform(b.transform);put(request,"status","completed");put(request,"value",vec(point.X(),point.Y(),point.Z()));put(request,"unit","mm");put(request,"reason","Reference point in fixture-world coordinates, not a displacement response.");}
        measurements.add(request);
    }
    put(run,"status",status);if(error.isNotEmpty())put(run,"message",error);
    // Failed runs never publish partial numerical tables as usable results.
    if(status=="completed"){put(run,"rows",rows);put(run,"materials",materials);}put(run,"measurements",measurements);
    put(run,"scope",method=="linearStatic"?"Euler-Bernoulli rectangular prismatic beam; static transverse end force, ideal fixed clamp, homogeneous linear elasticity, small displacement, length/section >= 10. No shear deformation, torsion, self-weight, contact or local clamp stress concentrations. Extreme stresses/strains are section envelopes; point probes report their configured fibre. Magenta curve: calculated neutral axis at the labeled visualization scale; solid geometry is undeformed.":"Reference configuration. Geometric/mass/kinematic methods do not compute structural response.");
    result=run;dirty=true;return run;
}
juce::var CadTestFixture::inspect()
{
    auto v=reply(true);put(v,"configuration",copy(config));put(v,"validation",validation());put(v,"methods",methods());put(v,"file",savedFile.getFullPathName());put(v,"modified",dirty);
    if(result.isObject()){auto shown=copy(result);put(shown,"stale",result["inputStamp"].toString()!=inputStamp()||!(bool)v["validation"]["valid"]);put(v,"result",shown);}return v;
}
bool CadTestFixture::save(const juce::File& file,bool overwrite,juce::String& error)
{
    if(!file.hasFileExtension("djfixture")){error="Fixture files use the .djfixture extension.";return false;}
    for(const auto& source:*config["sources"].getArray())if(juce::File(source["path"].toString())==file){error="A fixture cannot overwrite its source model.";return false;}
    if(file.existsAsFile()&&file!=savedFile&&!overwrite){error="Destination exists; explicit overwrite is required.";return false;}
    if(file==savedFile&&file.existsAsFile()&&!overwrite&&hashFile(file)!=savedDigest){error="Fixture changed on disk. Save to another file or explicitly approve replacing it.";return false;}
    auto envelope=object();put(envelope,"format","djehuti-test-fixture");put(envelope,"schemaVersion",1);put(envelope,"configuration",config);if(result.isObject())put(envelope,"result",result);
    juce::TemporaryFile temp(file);if(!temp.getFile().replaceWithText(juce::JSON::toString(envelope))||!temp.overwriteTargetFileWithTemporary()){error="Unable to save fixture: "+file.getFullPathName();return false;}savedFile=file;savedDigest=hashFile(file);dirty=false;return true;
}
bool CadTestFixture::open(const juce::File& file,juce::String& error)
{
    const auto root=juce::JSON::parse(file.loadFileAsString());if(root["format"].toString()!="djehuti-test-fixture"||(int)root["schemaVersion"]!=1){error="Unsupported or malformed fixture file.";return false;}
    if(!checkStructure(root["configuration"],error))return false;config=copy(root["configuration"]);result=copy(root["result"]);writeOCAF();authored->ClearUndos();authored->ClearRedos();savedFile=file;savedDigest=hashFile(file);sources.clear();dirty=false;++serial;return true;
}
std::shared_ptr<CadTestFixture> CadTestFixture::duplicate(const juce::String& name) const
{auto next=std::make_shared<CadTestFixture>(name);next->config=copy(config);put(next->config,"id",uid("fixture-"));put(next->config,"name",name);put(next->config,"revision",1);next->result=juce::var();next->writeOCAF();return next;}
juce::var CadTestFixture::command(const juce::var& args)
{
    const auto action=args.getProperty("action","inspect").toString();juce::String error,id;
    if(action=="inspect"||action=="validate")return inspect();
    if(action=="run"){auto v=reply(true);put(v,"result",execute());return v;}
    if(action=="undo"||action=="redo"){const bool ok=action=="undo"?authored->Undo():authored->Redo();if(!ok)return reply(false,"Nothing to "+action+".");return restoreOCAF(error)?inspect():reply(false,error);}
    const auto ok=edit([&](juce::var& c,juce::String& e)
    {
        if(action=="rename"){put(c,"name",args["name"].toString().trim());return true;}
        if(action=="settings"){auto settings=copy(c["settings"]);for(const auto& p:args["definition"].getDynamicObject()->getProperties())put(settings,p.name,p.value);put(c,"settings",settings);return true;}
        if(action=="addSource")
        {
            const auto path=args["path"].toString();if(!juce::File::isAbsolutePath(path)){e="Select an absolute CAD file path.";return false;}
            auto source=object();put(source,"id",uid("source-"));put(source,"path",juce::File(path).getFullPathName());Resolved resolved;if(!resolve(source,resolved,e))return false;
            put(source,"digest",resolved.digest);put(source,"modelId",resolved.document->mechanicsData().modelId);put(source,"cadRevision",(juce::int64)resolved.document->revision());id=source["id"].toString();c["sources"].getArray()->add(source);sources[id]=resolved;return true;
        }
        if(action=="acceptSource"||action=="relocateSource")
        {
            auto* source=entry(c,"sources",args["source"].toString());if(!source){e="Unknown fixture source.";return false;}
            if(action=="relocateSource"){if(!juce::File::isAbsolutePath(args["path"].toString())){e="Choose an absolute source path.";return false;}put(*source,"path",args["path"]);}
            Resolved resolved;if(!resolve(*source,resolved,e))return false;
            if(resolved.document->mechanicsData().modelId!=(*source)["modelId"].toString()){e="Replacement source has a different model identity.";return false;}
            // Remap only an explicitly authored face-centre anchor on one surviving planar face. Arbitrary
            // points are preserved if still on the trimmed face; no nearest-face or nearest-body retargeting.
            for(auto& item:*c["equipment"].getArray())
            {
                const auto* model=entry(c,"models",item["target"].toString());
                if(!model||(*model)["source"]!=(*source)["id"]||item["face"].toString().isEmpty())continue;
                juce::String feature=(*model)["feature"].toString(),reason;
                if((*model)["kind"].toString()=="assembly")
                {const auto* instance=resolved.document->mechanicsData().findInstance(item["component"].toString());feature=instance?instance->featureId:juce::String();}
                TopoDS_Face face;gp_Pnt centre;bool planar=false;
                item.getDynamicObject()->removeProperty("attachmentIssue");
                if(fixturegeom::face(*resolved.document,feature,item["face"].toString(),face,centre,planar,reason))
                {
                    if(item["anchor"].toString()=="faceCentre")
                    {if(planar&&fixturegeom::onFace(face,centre))put(item,"pointMm",vec(centre.X(),centre.Y(),centre.Z()));else reason="Face-centre anchor is not unambiguous on the revised planar trimmed face; reattach manually.";}
                    else if(!fixturegeom::onFace(face,equipmentPoint(item)))reason="Stored point is off the revised face; reattach manually.";
                }
                if(reason.isNotEmpty())put(item,"attachmentIssue",reason);
            }
            put(*source,"digest",resolved.digest);put(*source,"cadRevision",(juce::int64)resolved.document->revision());sources[(*source)["id"].toString()]=resolved;return true;
        }
        if(action=="addModel")
        {auto m=copy(args["definition"]);if(!m.isObject()){e="Model definition required.";return false;}id=uid("occurrence-");put(m,"id",id);if(!m.hasProperty("placement"))put(m,"placement",frame());c["models"].getArray()->add(m);return true;}
        if(action=="updateModel"||action=="substituteMaterial")
        {
            auto* m=entry(c,"models",args["model"].toString());if(!m){e="Unknown fixture occurrence.";return false;}
            if(action=="substituteMaterial"){if(args["material"].isVoid())m->getDynamicObject()->removeProperty("material");else put(*m,"material",args["material"]);}
            else {const auto d=args["definition"];for(const auto* key:{"name","placement"})if(d.hasProperty(key))put(*m,key,d[key]);}return true;
        }
        if(action=="upsertEquipment")
        {
            auto item=copy(args["definition"]);if(!item.isObject()){e="Equipment definition required.";return false;}id=item["id"].toString();if(id.isEmpty())id=uid("equipment-");put(item,"id",id);
            if(!item.hasProperty("pointMm"))put(item,"pointMm",vec(0,0,0));if(!item.hasProperty("direction"))put(item,"direction",vec(0,0,-1));
            item.getDynamicObject()->removeProperty("attachmentIssue");
            // Numeric edits away from the picked centre turn the anchor into an exact point. This prevents
            // later source acceptance from undoing an intentional user position.
            if(item["anchor"].toString()=="faceCentre"&&vector(item["pointMm"]))
            {
                bool centreMatches=false;for(const auto& b:bodies())if(b.target==item["target"].toString()&&b.component==item["component"].toString())
                {TopoDS_Face face;gp_Pnt centre;bool planar=false;juce::String reason;centreMatches=fixturegeom::face(*b.source,b.feature,item["face"].toString(),face,centre,planar,reason)&&planar&&fixturegeom::onFace(face,centre)&&centre.Distance(equipmentPoint(item))<=fixturegeom::attachmentToleranceMm;}
                if(!centreMatches)put(item,"anchor","point");
            }
            const auto kind=item["kind"].toString();if(juce::StringArray{"force","moment","pressure","distributedForce"}.contains(kind))
            {double si=0;const auto quantity=kind=="moment"?"moment":kind=="pressure"?"stress":"force";if(!numeric(item["magnitude"])||!units::toSI(quantity,(double)item["magnitude"],item["unit"].toString(),si,e))return false;put(item,"magnitude",si);put(item,"unit",units::siUnit(quantity));}
            if(auto* old=entry(c,"equipment",id))*old=item;else c["equipment"].getArray()->add(item);return true;
        }
        if(action=="removeEquipment"||action=="removeModel"||action=="removeSource")
        {
            const auto key=action=="removeEquipment"?"equipment":action=="removeModel"?"models":"sources";const auto item=args["item"].toString();auto* a=c[key].getArray();bool found=false;for(int i=a->size();--i>=0;)if((*a)[i]["id"].toString()==item){a->remove(i);found=true;}if(!found)e="Unknown fixture entry.";return found;
        }
        e="Unknown fixture action: "+action;return false;
    },error);
    auto v=reply(ok,error);if(ok){put(v,"id",id);put(v,"configuration",copy(config));}return v;
}
juce::var CadTestFixture::targetChoices()
{
    juce::Array<juce::var> list;
    for(const auto& source:*config["sources"].getArray())
    {auto& runtime=sources[source["id"].toString()];juce::String error;if(!resolve(source,runtime,error))continue;auto v=copy(source);juce::Array<juce::var> features,joints;for(const auto& f:runtime.document->features())if(!f.shape.IsNull()&&TopExp_Explorer(f.shape,TopAbs_SOLID).More()){auto choice=object();put(choice,"id",f.id);put(choice,"name",f.name);features.add(choice);}put(v,"features",features);put(v,"mechanics",mech::toJson(runtime.document->mechanicsData()));list.add(v);}
    return list;
}
juce::var CadTestFixture::materialFor(const juce::String& target,const juce::String& component)
{
    for(const auto& b:bodies())if(b.target==target&&b.component==component)
    {
        const auto* model=entry(config,"models",target);if(model&&model->hasProperty("material")){auto material=copy((*model)["material"]);put(material,"substituted",true);return material;}
        if(const auto* assignment=b.source->materialsData().assignmentFor(b.feature))if(const auto* m=b.source->materialsData().find(assignment->materialId)){auto material=matl::materialToJson(*m);put(material,"substituted",false);return material;}
    }
    return {};
}
std::shared_ptr<const CadRenderSnapshot> CadTestFixture::snapshot()
{
    auto rendered=std::make_shared<CadRenderSnapshot>();rendered->revision=serial;rendered->documentInstance=(std::uint64_t)id().hashCode64();
    std::map<const CadDocument*,std::shared_ptr<const CadRenderSnapshot>> sourceSnapshots;
    for(const auto& b:bodies())
    {
        auto& source=sourceSnapshots[b.source.get()];if(!source)source=b.source->snapshot(true);const auto drawn=drawId(b.target,b.component);
        for(const auto& mesh:source->meshes)if(mesh.featureId==b.feature)
        {auto next=mesh;next.featureId=drawn;for(size_t i=0;i+5<next.vertices.size();i+=6){gp_Pnt p(next.vertices[i],next.vertices[i+1],next.vertices[i+2]);p.Transform(b.transform);gp_Vec n(next.vertices[i+3],next.vertices[i+4],next.vertices[i+5]);n.Transform(b.transform);next.vertices[i]=(float)p.X();next.vertices[i+1]=(float)p.Y();next.vertices[i+2]=(float)p.Z();next.vertices[i+3]=(float)n.X();next.vertices[i+4]=(float)n.Y();next.vertices[i+5]=(float)n.Z();}rendered->meshes.push_back(std::move(next));}
        for(const auto& edge:source->edges)if(edge.featureId==b.feature){auto next=edge;next.featureId=drawn;for(size_t i=0;i+2<next.points.size();i+=3){gp_Pnt p(next.points[i],next.points[i+1],next.points[i+2]);p.Transform(b.transform);next.points[i]=(float)p.X();next.points[i+1]=(float)p.Y();next.points[i+2]=(float)p.Z();}rendered->edges.push_back(std::move(next));}
    }
    return rendered;
}
juce::String CadTestFixture::faceToken(const juce::String& id,int index)
{for(const auto& b:bodies())if(drawId(b.target,b.component)==id){CadFaceInfo face;juce::String error;if(b.source->describeFace(b.feature,index,face,error))return face.token;}return {};}
juce::var CadTestFixture::facePoint(const juce::String& id,int index)
{for(const auto& b:bodies())if(drawId(b.target,b.component)==id){CadFaceInfo face;juce::String error;if(b.source->describeFace(b.feature,index,face,error))return vec(face.centre.X(),face.centre.Y(),face.centre.Z());}return {};}
std::vector<CadOverlayLine> CadTestFixture::overlays()
{
    std::vector<CadOverlayLine> lines;
    const auto parts=bodies();
    for(const auto& e:*config["equipment"].getArray())for(const auto& b:parts)if(b.target==e["target"].toString()&&b.component==e["component"].toString())
    {
        // Points and directions are in source-document coordinates, carried with the fixture occurrence.
        gp_Pnt p((double)e["pointMm"][0],(double)e["pointMm"][1],(double)e["pointMm"][2]);p.Transform(b.transform);
        juce::String reason;const bool valid=e["face"].toString().isEmpty()||attachment(*b.source,b.feature,e,reason);
        const auto kind=e["kind"].toString();const juce::uint32 colour=!valid?0xffff4444:(kind=="fixed"||kind=="support")?0xff44ddaa:juce::StringArray{"force","moment","pressure","distributedForce"}.contains(kind)?0xffffaa44:0xff55bbff;
        const auto add=[&](const gp_Pnt& a,const gp_Pnt& z){lines.push_back({{(float)a.X(),(float)a.Y(),(float)a.Z()},{(float)z.X(),(float)z.Y(),(float)z.Z()},colour});};
        for(int axis=0;axis<3;++axis){gp_Vec d(axis==0?3:0,axis==1?3:0,axis==2?3:0);add(p.Translated(-d),p.Translated(d));}
        if(juce::StringArray{"force","moment","pressure","distributedForce"}.contains(kind)){gp_Vec d((double)e["direction"][0],(double)e["direction"][1],(double)e["direction"][2]);d.Transform(b.transform);add(p,p.Translated(d*20));}
    }
    if(result["status"].toString()=="completed"&&result["method"].toString()=="linearStatic"&&result["inputStamp"].toString()==inputStamp()&&(bool)validation()["valid"])
        for(const auto& row:*result["rows"].getArray())
        {
            const double scale=(double)row["visualizationScale"];const auto stations=row["stations"];
            const auto deformed=[&](int i){std::array<float,3> p{};for(int k=0;k<3;++k)p[k]=(float)((double)stations[i]["referenceMm"][k]+1000*scale*(double)stations[i]["displacementWorldM"][k]);return p;};
            for(int i=1;i<stations.size();++i)lines.push_back({deformed(i-1),deformed(i),0xffff66dd});
        }
    return lines;
}
std::shared_ptr<const CadPoseSnapshot> CadTestFixture::dragPose(const juce::String& target,const juce::String& plane)
{
    auto pose=std::make_shared<CadPoseSnapshot>();pose->documentInstance=(std::uint64_t)id().hashCode64();pose->documentRevision=serial;pose->serial=serial;
    const auto* model=entry(config,"models",target);if(!model)return pose;
    const auto p=(*model)["placement"];pose->dragHandle={true,target,{(float)p["x"],(float)p["y"],(float)p["z"]},{plane=="YZ"?1.0f:0.0f,plane=="XZ"?1.0f:0.0f,plane=="XY"?1.0f:0.0f}};
    // Visible handle cross, using existing overlay rendering; rotations remain the numeric placement controls.
    for(int axis=0;axis<3;++axis){auto a=pose->dragHandle.point,b=a;a[axis]-=5;b[axis]+=5;pose->overlay.push_back({a,b,0xffffffff});}
    return pose;
}
std::vector<std::pair<juce::String,std::array<float,3>>> CadTestFixture::equipmentMarkers()
{
    std::vector<std::pair<juce::String,std::array<float,3>>> markers;const auto parts=bodies();
    for(const auto& e:*config["equipment"].getArray())for(const auto& b:parts)if(b.target==e["target"].toString()&&b.component==e["component"].toString())
    {const auto p=equipmentPoint(e).Transformed(b.transform);markers.push_back({e["id"].toString(),{(float)p.X(),(float)p.Y(),(float)p.Z()}});}
    return markers;
}
std::shared_ptr<CadTestFixture> CadFixtureService::find(const juce::String& id) const {for(const auto& fixture:fixtures)if(fixture->id()==id)return fixture;return {};}
juce::var CadFixtureService::command(const juce::var& args)
{
    const auto action=args.getProperty("action","list").toString();juce::String error;
    if(action=="list"){auto v=reply(true);juce::Array<juce::var> list;for(const auto& f:fixtures){auto item=object();put(item,"id",f->id());put(item,"name",f->name());put(item,"file",f->file().getFullPathName());put(item,"modified",f->modified());list.add(item);}put(v,"fixtures",list);return v;}
    if(action=="create"||action=="open")
    {
        auto fixture=std::make_shared<CadTestFixture>(args.getProperty("name","Test fixture").toString());
        if(action=="open"){if(!juce::File::isAbsolutePath(args["path"].toString()))return reply(false,"An absolute fixture path is required.");if(!fixture->open(juce::File(args["path"].toString()),error))return reply(false,error);if(auto existing=find(fixture->id())){if(existing->file()==fixture->file()){auto v=existing->inspect();put(v,"id",existing->id());return v;}return reply(false,"Another open fixture has this identity. Duplicate it to create an independent setup.");}}
        if(fixture->name().trim().isEmpty())return reply(false,"A fixture name is required.");fixtures.push_back(fixture);auto v=fixture->inspect();put(v,"id",fixture->id());return v;
    }
    auto fixture=find(args["fixture"].toString());if(!fixture)return reply(false,"Unknown fixture identity.");
    if(action=="duplicate"){auto next=fixture->duplicate(args.getProperty("name",fixture->name()+" copy").toString());if(next->name().trim().isEmpty())return reply(false,"A fixture name is required.");fixtures.push_back(next);auto v=next->inspect();put(v,"id",next->id());return v;}
    if(action=="save")
    {const auto path=args.getProperty("path",fixture->file().getFullPathName()).toString();if(!juce::File::isAbsolutePath(path))return reply(false,"Choose an absolute fixture path.");for(const auto& other:fixtures)if(other!=fixture&&other->file()==juce::File(path))return reply(false,"Another open fixture owns that path.");return fixture->save(juce::File(path),(bool)args.getProperty("overwrite",false),error)?fixture->inspect():reply(false,error);}
    if(action=="close"||action=="delete")
    {
        if(action=="close"&&fixture->modified()&&!(bool)args.getProperty("discard",false))return reply(false,"Save fixture changes or explicitly discard them before closing.");
        if(action=="delete"){if(!(bool)args.getProperty("confirm",false))return reply(false,"Deleting a fixture requires confirm=true.");if(fixture->file().existsAsFile()&&!fixture->file().deleteFile())return reply(false,"Unable to delete fixture file.");}
        std::erase(fixtures,fixture);return reply(true);
    }
    if(action=="settings"&&!args["definition"].isObject())return reply(false,"Settings definition must be an object.");
    if(action=="choices"){auto v=reply(true);put(v,"sources",fixture->targetChoices());return v;}
    return fixture->command(args);
}
}
