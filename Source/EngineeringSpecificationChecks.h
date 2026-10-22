#pragma once
#include "EngineeringSpecification.h"
namespace djehuti::engineering
{
inline bool checks(const juce::File& libraryFile,juce::var& report)
{
    juce::Array<juce::var> rows;bool passed=true;
    const auto check=[&](bool ok,const char* name){auto r=object();put(r,"name",name);put(r,"passed",ok);rows.add(r);passed&=ok;};
    juce::var library;juce::String error;check(loadLibrary(libraryFile,library,error),"Portable library validates");
    if(library["records"].size()!=3){report=object();put(report,"error",error);return false;}
    for(const auto& r:*library["records"].getArray())
    {
        check(verifyAssets(r,libraryFile.getParentDirectory(),error),"Original assets match their SHA-256 references");
        const auto reopened=juce::JSON::parse(juce::JSON::toString(r));check(juce::JSON::toString(reopened)==juce::JSON::toString(r)&&validate(reopened,error),"Portable JSON roundtrip retains complete claim and source semantics");
    }
    const auto motor=library["records"][0];const auto interpreted=interpret(motor);
    const auto find=[](const juce::var& array,const char* id){for(const auto& p:*array.getArray())if(p["id"].toString()==id)return p;return juce::var();};
    check(std::abs((double)find(interpreted["claims"],"stall_torque")["siValue"]-.05295591)<1e-10,"Torque conversion uses kgf mm, not kgf cm");
    check(std::abs((double)find(interpreted["claims"],"effective_stall_resistance")["siValue"]-8.955223880597)<1e-10,"Dimensional voltage/current derivation independently agrees");
    check(find(interpreted["claims"],"curve_speed_intercept")["readiness"].toString()=="needs_review"&&find(interpreted["claims"],"winding_inductance")["value"].isVoid()&&!(bool)interpreted["modelReadiness"][0]["allListedParametersAvailable"],"Conflicting nominal/fit values and missing dynamic parameters prevent false completeness");
    auto bad=juce::JSON::parse(juce::JSON::toString(motor));auto p=bad["properties"][0];put(p,"unit","A");check(!validate(bad,error),"Reject dimensionally incompatible units");
    bad=juce::JSON::parse(juce::JSON::toString(motor));p=bad["properties"][0];put(p,"variant","encoder version");check(!validate(bad,error),"Reject claims from another manufacturer variant");
    bad=juce::JSON::parse(juce::JSON::toString(motor));p=find(bad["properties"],"winding_inductance");put(p,"value",0);check(!validate(bad,error),"Unknown is not silently replaced by zero");
    bad=juce::JSON::parse(juce::JSON::toString(motor));p=find(bad["properties"],"effective_stall_resistance");put(p,"value",9.5);check(!validate(bad,error),"Reject a mismatched derived coefficient");
    bad=juce::JSON::parse(juce::JSON::toString(motor));p=find(bad["properties"],"winding_inductance");put(p,"provenance","assumed");put(p,"value",.001);check(validate(bad,error)&&find(interpret(bad)["claims"],"winding_inductance")["readiness"].toString()=="assumed","Explicit assumptions remain distinct from manufacturer data");put(p,"verification","reviewed");check(!validate(bad,error),"Assumptions cannot claim verified manufacturer status");
    bad=juce::JSON::parse(juce::JSON::toString(motor));p=specification(bad)["sources"][0];put(p,"sha256",juce::String::repeatedString("0",64));check(!verifyAssets(bad,libraryFile.getParentDirectory(),error),"Changed asset hashes are detected");
    check(std::abs(unit("capacitance","uF")->scale-1e-6)<1e-15,"Electrical engineering prefixes retain SI dimensions");
    report=object();put(report,"passed",passed);put(report,"checks",rows);return passed;
}
}
