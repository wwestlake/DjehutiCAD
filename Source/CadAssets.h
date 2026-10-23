#pragma once
#include <JuceHeader.h>
namespace djehuti::cad
{
class CadDocument;
// Personal collection metadata wraps ordinary engineering JSON; no second geometry model.
class CadAssetLibrary
{
public:
    explicit CadAssetLibrary(juce::File file = {});
    juce::var command(const juce::var&,CadDocument&);
private:
    bool load(juce::String&);
    bool store(const juce::var&,juce::String&);
    juce::File file;
    juce::var data;
    juce::String disk;
};
bool runCadAssetCheck(juce::var&);
}
