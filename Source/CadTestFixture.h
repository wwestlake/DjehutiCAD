#pragma once
#include "CadDocument.h"
#include "CadMotionRuntime.h"

namespace djehuti::cad
{
// First-class fixture documents own their setup, never source CAD features. OCAF is authoritative for
// authored configuration and undo; versioned .djfixture JSON is the portable persistence boundary.
// Source geometry is resolved into private, read-only runtime documents and is never embedded in fixtures.
class CadTestFixture
{
public:
    explicit CadTestFixture(const juce::String& name);
    const juce::var& configuration() const { return config; }
    juce::String id() const { return config["id"].toString(); }
    juce::String name() const { return config["name"].toString(); }
    juce::File file() const { return savedFile; }
    bool modified() const { return dirty; }
    juce::var command(const juce::var&);
    juce::var inspect();
    juce::var validation();
    bool save(const juce::File&, bool overwrite, juce::String& error);
    bool open(const juce::File&, juce::String& error);
    std::shared_ptr<CadTestFixture> duplicate(const juce::String& name) const;
    std::shared_ptr<const CadRenderSnapshot> snapshot();
    std::vector<CadOverlayLine> overlays();
    juce::String faceToken(const juce::String& drawId, int faceIndex);
    juce::var facePoint(const juce::String& drawId, int faceIndex);
    juce::var targetChoices();
    juce::var materialFor(const juce::String& target,const juce::String& component={});
    static juce::var methods();
private:
    struct Resolved { std::shared_ptr<const CadDocument> document; juce::String digest, error; };
    struct Body { juce::String target, component, feature; std::shared_ptr<const CadDocument> source; gp_Trsf transform; };
    bool edit(const std::function<bool(juce::var&, juce::String&)>&, juce::String& error);
    bool checkStructure(const juce::var&, juce::String& error) const;
    bool resolve(const juce::var& source, Resolved&, juce::String& error);
    std::vector<Body> bodies();
    juce::var execute();
    juce::String inputStamp() const;
    void writeOCAF();
    bool restoreOCAF(juce::String&);
    juce::var config, result;
    Handle(TDocStd_Document) authored;
    std::map<juce::String, Resolved> sources;
    juce::File savedFile;
    juce::String savedDigest;
    bool dirty=true;
    std::uint64_t serial=1;
};

// The GUI and the existing agent/local API share this one command dispatcher (message thread only).
class CadFixtureService
{
public:
    juce::var command(const juce::var&);
    const std::vector<std::shared_ptr<CadTestFixture>>& documents() const { return fixtures; }
    std::shared_ptr<CadTestFixture> find(const juce::String& id) const;
private:
    std::vector<std::shared_ptr<CadTestFixture>> fixtures;
};
bool runCadFixtureCheck(juce::var& report);
}
