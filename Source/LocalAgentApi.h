#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>

// DEWB-compatible localhost discovery and tool calls (loopback only, dynamic port, bearer token). Requests run
// one at a time, in order, on a single worker that hands each step to the JUCE message thread through the
// shared tool dispatcher; the listener never reads or mutates a live CAD document. A request is queued,
// running, then completed, failed or cancelled (cancel applies only before it starts).
class LocalAgentApi final : private juce::Thread
{
public:
    // keepWaiting() turns false when the API is shutting down; the handler must then stop waiting and return.
    using ToolHandler = std::function<juce::var(const juce::String&, const juce::var&, std::function<bool()> keepWaiting)>;
    LocalAgentApi(ToolHandler, juce::var catalogue);
    ~LocalAgentApi() override;
    bool start();
    void stop();
    static juce::File getDiscoveryFile();

private:
    struct State;
    void run() override;
    void handleConnection(juce::StreamingSocket&);
    bool writeJson(juce::StreamingSocket&, int, const juce::var&);
    juce::StreamingSocket listener;
    juce::String token;
    ToolHandler handler;
    juce::var catalogue;
    std::shared_ptr<State> state;
    std::unique_ptr<juce::ThreadPool> worker;
};
