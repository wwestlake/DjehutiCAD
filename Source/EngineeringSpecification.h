#pragma once
#include <juce_core/juce_core.h>
#include <windows.h>
#include <bcrypt.h>
#include <cmath>
#include <map>
#include <set>

// Mirrored in DEWB. Optional extension of the existing component record, not a new document.
namespace djehuti::engineering
{
inline juce::var object() { return new juce::DynamicObject; }
inline void put(juce::var& v, const juce::Identifier& k, const juce::var& x) { v.getDynamicObject()->setProperty(k,x); }
inline bool number(const juce::var& v) { return (v.isDouble()||v.isInt()||v.isInt64())&&std::isfinite((double)v); }
struct Unit { const char* quantity; const char* symbol; const char* si; double scale; };
inline const Unit* unit(const juce::String& quantity,const juce::String& symbol)
{
    static const Unit units[]{
        {"length","mm","m",.001},{"length","m","m",1},{"length","in","m",.0254},
        {"mass","g","kg",.001},{"mass","kg","kg",1},
        {"voltage","V","V",1},{"current","A","A",1},{"current","mA","A",.001},
        {"resistance","ohm","ohm",1},{"inductance","H","H",1},{"inductance","mH","H",.001},
        {"capacitance","F","F",1},{"capacitance","uF","F",.000001},
        {"power","W","W",1},{"force","N","N",1},{"torque","N m","N m",1},
        {"torque","kgf cm","N m",.0980665},{"torque","kgf mm","N m",.00980665},
        {"angularSpeed","rpm","rad/s",.10471975511965977},{"angularSpeed","rad/s","rad/s",1},
        {"speed","mm/s","m/s",.001},{"speed","m/s","m/s",1},
        {"time","s","s",1},{"time","ms","s",.001},{"time","us","s",.000001},
        {"frequency","Hz","Hz",1},{"ratio","1","1",1},{"ratio","%","1",.01},
        {"inertia","kg m2","kg m2",1},{"stress","Pa","Pa",1},{"stress","MPa","Pa",1e6},
        {"density","kg/m3","kg/m3",1}};
    for(const auto& u:units) if(quantity==u.quantity&&symbol==u.symbol)return &u;
    return nullptr;
}
inline juce::String fileHash(const juce::File& file)
{
    juce::MemoryBlock bytes;if(file.getSize()>128*1024*1024||!file.loadFileAsData(bytes))return {};
    BCRYPT_ALG_HANDLE algorithm=nullptr;unsigned char result[32]{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    const auto status=BCryptHash(algorithm,nullptr,0,(PUCHAR)bytes.getData(),(ULONG)bytes.getSize(),result,sizeof(result));
    BCryptCloseAlgorithmProvider(algorithm,0);
    return status<0?juce::String():juce::String::toHexString(result,sizeof(result),0);
}
inline juce::var specification(const juce::var& record) { return record["engineering"]["specification"]; }
inline bool validate(const juce::var& record,juce::String& error)
{
    const auto s=specification(record);if(s.isVoid())return true; // Legacy engineering metadata is unchanged.
    const auto fail=[&](const juce::String& why){error="Engineering specification: "+why;return false;};
    if(!s.isObject()||(int)s["schemaVersion"]!=1)return fail("unsupported schemaVersion.");
    if(record["id"].toString().isEmpty()||record["manufacturer"].toString().isEmpty()||record["partNumber"].toString().isEmpty()||s["variant"].toString().isEmpty())return fail("manufacturer, exact part number, component ID and variant are required.");
    if(!s["sources"].isArray()||!s["curves"].isArray()||!s["models"].isArray()||!record["properties"].isArray())return fail("sources, curves, models and properties must be lists.");
    std::set<juce::String> sources,curves;
    for(const auto& source:*s["sources"].getArray())
    {
        const auto id=source["id"].toString(),path=source["path"].toString(),hash=source["sha256"].toString();
        if(id.isEmpty()||!sources.insert(id).second||source["originalFilename"].toString().isEmpty()||source["revision"].toString().isEmpty()||!source["url"].toString().startsWith("https://"))return fail("source identity, revision, original filename and HTTPS URL are required.");
        if(path.isEmpty()||juce::File::isAbsolutePath(path)||path.contains("..")||hash.length()!=64||!hash.containsOnly("0123456789abcdef"))return fail("source needs a safe relative asset path and SHA-256.");
    }
    const auto validRefs=[&](const juce::var& refs){if(!refs.isArray()||refs.size()==0)return false;for(const auto& id:*refs.getArray())if(!sources.count(id.toString()))return false;return true;};
    std::map<juce::String,juce::var> claims;
    for(const auto& p:*record["properties"].getArray())
    {
        const auto id=p["id"].toString(),kind=p["provenance"].toString(),q=p["quantity"].toString();
        if(id.isEmpty()||!claims.emplace(id,p).second||p["variant"]!=s["variant"])return fail("claim IDs must be unique and variants must match the record.");
        if(!juce::StringArray{"manufacturer_published","manufacturer_cad_derived","third_party_measured","analytically_derived","assumed","unknown"}.contains(kind))return fail(id+": invalid provenance.");
        if(!juce::StringArray{"reviewed","unverified","conflicted"}.contains(p["verification"].toString())||!p["conditions"].isObject())return fail(id+": verification and explicit conditions are required.");
        if(kind=="unknown") { if(!p["value"].isVoid()||p["verification"].toString()=="reviewed")return fail(id+": unknown values must be null and unverified."); }
        else if(q=="text") {if(!p["value"].isString()||p["value"].toString().isEmpty()||p["unit"].toString().isNotEmpty())return fail(id+": invalid textual specification.");}
        else if(!number(p["value"])||!unit(q,p["unit"].toString()))return fail(id+": nonnumeric value or incompatible/unsupported unit.");
        if(kind!="unknown"&&kind!="assumed"&&kind!="analytically_derived"&&!validRefs(p["sourceRefs"]))return fail(id+": original source reference required.");
        if(kind=="assumed"&&p["verification"].toString()=="reviewed")return fail(id+": assumptions cannot be verified specifications.");
        if(p.hasProperty("tolerance")&&(!number(p["tolerance"])||(double)p["tolerance"]<0||q=="text"||kind=="unknown"))return fail(id+": tolerance must be nonnegative in the claim unit.");
        for(const auto& condition:p["conditions"].getDynamicObject()->getProperties())
        {
            const auto& v=condition.value;
            if(v.isString())continue;
            if(!v.isObject()||!number(v["value"])||!unit(v["quantity"].toString(),v["unit"].toString()))return fail(id+": condition must be text or a typed quantity.");
        }
        if(kind=="unknown"&&q!="text"&&!unit(q,p["unit"].toString()))return fail(id+": unknown parameter still needs a quantity and unit.");
    }
    for(const auto& entry:claims)
    {
        const auto p=entry.second;
        if(p["verification"].toString()=="conflicted")
        {if(!p["conflictsWith"].isArray()||p["conflictsWith"].size()==0)return fail(entry.first+": discrepancy references required.");for(const auto& id:*p["conflictsWith"].getArray())if(id.toString()==entry.first||!claims.count(id.toString()))return fail(entry.first+": invalid discrepancy reference.");}
        if(p["provenance"].toString()!="analytically_derived")continue;
        // Deliberately bounded dimensional derivation; additional operations require explicit implementation.
        const auto d=p["derivation"];const auto a=claims.find(d["numerator"].toString()),b=claims.find(d["denominator"].toString());
        if(d["operation"].toString()!="voltage/current"||a==claims.end()||b==claims.end()||d["assumptions"].toString().isEmpty())return fail(entry.first+": unsupported or incomplete derivation.");
        const auto usable=[](const juce::var& x){return x["provenance"].toString()=="manufacturer_published"&&x["verification"].toString()=="reviewed"&&number(x["value"]);};
        // var object equality is identity-based, so compare the retained condition records explicitly.
        const auto conditions=juce::JSON::toString(a->second["conditions"]);
        if(!usable(a->second)||!usable(b->second)||a->second["quantity"].toString()!="voltage"||b->second["quantity"].toString()!="current"||p["quantity"].toString()!="resistance"||conditions!=juce::JSON::toString(b->second["conditions"])||conditions!=juce::JSON::toString(p["conditions"]))return fail(entry.first+": incompatible, unverified or differently conditioned derivation inputs/output.");
        const auto av=(double)a->second["value"]*unit("voltage",a->second["unit"].toString())->scale,bv=(double)b->second["value"]*unit("current",b->second["unit"].toString())->scale;
        if(bv==0||std::abs((double)p["value"]*unit("resistance",p["unit"].toString())->scale-av/bv)>1e-8*juce::jmax(1.0,std::abs(av/bv)))return fail(entry.first+": derived value disagrees with dimensional calculation.");
    }
    for(const auto& c:*s["curves"].getArray())
    {
        if(c["id"].toString().isEmpty()||!curves.insert(c["id"].toString()).second||!validRefs(c["sourceRefs"])||!c["conditions"].isObject()||c["variant"]!=s["variant"]||!c["axes"].isArray()||c["axes"].size()<2)return fail("invalid curve identity, provenance, conditions or axes.");
        for(const auto& axis:*c["axes"].getArray())if(!unit(axis["quantity"].toString(),axis["unit"].toString()))return fail("unsupported curve axis quantity/unit.");
        if(c.hasProperty("points"))
        {if(!c["points"].isArray()||c["digitization"].toString().isEmpty())return fail("digitized points need method/uncertainty notes.");for(const auto& row:*c["points"].getArray()){if(!row.isArray()||row.size()!=c["axes"].size())return fail("curve point dimensions disagree with axes.");for(const auto& v:*row.getArray())if(!number(v))return fail("nonfinite curve point.");}}
    }
    std::set<juce::String> models;
    for(const auto& m:*s["models"].getArray())
    {
        if(m["id"].toString().isEmpty()||!models.insert(m["id"].toString()).second||m["scope"].toString().isEmpty()||!m["parameters"].isArray()||!m["curveRefs"].isArray())return fail("model needs identity, scope, parameters and curveRefs.");
        for(const auto& id:*m["parameters"].getArray())if(!claims.count(id.toString()))return fail("model references missing parameter.");
        for(const auto& id:*m["curveRefs"].getArray())if(!curves.count(id.toString()))return fail("model references missing original curve.");
    }
    return true;
}
inline juce::var interpret(const juce::var& record)
{
    auto result=object();juce::String error;const bool ok=validate(record,error)&&!specification(record).isVoid();put(result,"ok",ok);
    if(!ok){put(result,"error",error.isEmpty()?"No structured engineering specification.":error);return result;}
    put(result,"componentId",record["id"]);put(result,"specification",specification(record));juce::Array<juce::var> claims,models;
    std::map<juce::String,juce::String> readiness;
    for(const auto& p:*record["properties"].getArray())
    {
        auto c=juce::JSON::parse(juce::JSON::toString(p));const auto kind=p["provenance"].toString();
        const juce::String status=kind=="unknown"?"unknown":kind=="assumed"?"assumed":p["verification"].toString()!="reviewed"?"needs_review":kind=="analytically_derived"?"derived":"available";
        readiness[p["id"].toString()]=status;put(c,"readiness",status);
        if(number(p["value"]))if(const auto* u=unit(p["quantity"].toString(),p["unit"].toString())){put(c,"siValue",(double)p["value"]*u->scale);put(c,"siUnit",u->si);}
        claims.add(c);
    }
    for(const auto& m:*specification(record)["models"].getArray())
    {
        auto v=object();put(v,"id",m["id"]);put(v,"scope",m["scope"]);juce::Array<juce::var> params;bool complete=m["parameters"].size()>0;
        for(const auto& id:*m["parameters"].getArray()){auto p=object();const auto status=readiness[id.toString()];put(p,"id",id);put(p,"status",status);params.add(p);complete&=status=="available"||status=="derived";}
        put(v,"parameters",params);put(v,"allListedParametersAvailable",complete);put(v,"solverValidated",false);models.add(v);
    }
    put(result,"claims",claims);put(result,"modelReadiness",models);return result;
}
inline bool verifyAssets(const juce::var& record,const juce::File& root,juce::String& error)
{
    if(!validate(record,error)||specification(record).isVoid())return false;
    for(const auto& s:*specification(record)["sources"].getArray())
    {const auto file=root.getChildFile(s["path"].toString());if(!file.isAChildOf(root)||!file.existsAsFile()||fileHash(file)!=s["sha256"].toString()){error="Missing or changed original asset: "+file.getFullPathName();return false;}}
    return true;
}
inline bool loadLibrary(const juce::File& file,juce::var& library,juce::String& error)
{
    if(!file.existsAsFile()||file.getSize()>64*1024*1024){error="Choose an existing portable component library below 64 MB.";return false;}
    const auto parsed=juce::JSON::parse(file);std::set<juce::String> ids;
    if(parsed["format"].toString()!="djehuti-component-library"||(int)parsed["schemaVersion"]!=1||!parsed["records"].isArray()){error="Unsupported portable component library.";return false;}
    for(const auto& r:*parsed["records"].getArray())if(specification(r).isVoid()||!validate(r,error)||!ids.insert(r["id"].toString()).second){if(error.isEmpty())error="Missing structured specification or duplicate component ID.";return false;}
    library=parsed;return true;
}
}
