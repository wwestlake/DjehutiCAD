#pragma once

#include <JuceHeader.h>
#include "CadDocument.h"
#include "CadMotionRuntime.h"

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <optional>

// DCAD side of the DEWB-DCAD motion interface (docs/DEWB_DCAD_MOTION_INTERFACE_V1.md).
// Transport: a JUCE InterprocessConnection named pipe carrying UTF-8 JSON messages. Pipe reading and
// parsing run on the connection thread, writes on a dedicated writer thread; validated motion states are
// handed to the message thread through a single latest-state slot, so obsolete frames never queue up.
// Every piece of queued work is stamped with the binding epoch, which advances whenever the session,
// binding, interface, profile or document is superseded; stale work is discarded before publication.
namespace djehuti::motion
{
inline constexpr int kProtocolVersion = 1;
inline constexpr std::uint32_t kMagicHeader = 0xf2b49e2c;    // JUCE InterprocessConnection default header word
inline constexpr int kMaxMessageBytes = 1024 * 1024;
inline constexpr int kMaxQueuedOutgoing = 64;
inline constexpr int kHeartbeatMs = 1000, kPeerTimeoutMs = 3000, kBindingTimeoutMs = 10000;
inline constexpr int kWriteStallMs = 3000;          // a single pipe write that takes longer means DEWB stopped reading
inline constexpr int kCommandTimeoutMs = 5000;      // no LifecycleResult (or accepted notice) within this time
inline constexpr int kCommandCompletionMs = 30000;  // completion time allowed after an "accepted" notice
inline constexpr int kShutdownWaitMs = 4000;        // bound for joining the pipe and writer threads
inline constexpr const char* kLifecycleCapability = "lifecycleControl.v1";

enum class LinkState { stopped, listening, connected, validating, awaitingSync, synchronized, streaming, paused, disconnected, error };
juce::String stateName(LinkState state);

struct SignalInfo { juce::String id, name, quantity, unit, kind; };

struct LinkSnapshot
{
    LinkState state = LinkState::stopped;
    juce::String pipeName, pipePath, peerName, simulationId, lifecycle, message, lastSequence, simTimePs;
    int severity = 0, acceptedProfileRevision = -1, timelineGeneration = 0;
    juce::StringArray problems, limitWarnings;
    std::vector<SignalInfo> signals;
    std::uint64_t serial = 0;
    juce::var mechanicalInterface;
    // Simulation lifecycle control
    bool lifecycleControl = false, lifecycleAvailable = false, commandPending = false, canRetry = false;
    juce::String lifecycleUnavailableReason, commandText, pendingCommand;
    int commandSeverity = 0;
    juce::String toJson() const;
};

// Latest link status as JSON for readers on other threads (agent, FRust); never blocks the link.
struct StatusBoard
{
    juce::String get() const { const std::lock_guard<std::mutex> guard(lock); return json; }
    void set(const juce::String& next) { const std::lock_guard<std::mutex> guard(lock); json = next; }
private:
    mutable std::mutex lock;
    juce::String json;
};

class MotionLink final : private juce::Timer, private juce::AsyncUpdater
{
public:
    MotionLink(std::shared_ptr<cad::CadCommandService> service, std::shared_ptr<cad::CadMotionRuntime> runtime);
    ~MotionLink() override;

    bool startListening(juce::String& error);
    void stopListening();
    void disconnectPeer();
    bool isListening() const;
    LinkSnapshot snapshot() const;
    juce::StringArray validateProfileAgainstSignals() const;
    std::shared_ptr<StatusBoard> statusBoard() const { return board; }

    // Simulation lifecycle requests: start, pause, resume, stop, reset, step, or "play" (start or resume by the
    // last confirmed lifecycle). DEWB stays authoritative: the pose and displayed state change only through
    // its state messages. stepDurationPs is used for "step" only.
    bool requestLifecycle(const juce::String& command, std::int64_t stepDurationPs, juce::String& error);
    bool retryLifecycle(juce::String& error);
    std::function<void()> onStatusChanged;

private:
    class Connection;
    class Writer;
    struct Contract
    {
        juce::String modelId, documentName;
        int interfaceRevision = -1, profileRevision = -1;
        std::uint64_t documentInstance = 0;
        std::vector<cad::CadSignalMapping> mappings;
        std::map<juce::String, cad::CadJoint> joints;
        juce::var interfaceMessage;
    };
    struct PendingState
    {
        bool valid = false, synchronize = false;
        std::map<juce::String, double> coordinates;
        juce::String simulationId, sequence;
        int generation = 0, profileRevision = 0;
        std::uint64_t epoch = 0;
        juce::uint32 receivedMs = 0;
    };
    struct PendingCommand
    {
        juce::String requestId, command;
        juce::var message;
        juce::uint32 sentMs = 0;
        bool accepted = false, timedOut = false;
        std::uint64_t epoch = 0;
    };

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void refreshContract();
    void publishPending();
    void handleIncoming(const juce::MemoryBlock& data);
    void handleMessage(const juce::var& message);
    void handleHello(const juce::var& message);
    void handleSignalInterface(const juce::var& message);
    void handleBindingAccept(const juce::var& message);
    void handleBindingReject(const juce::var& message);
    void handleState(const juce::var& message, bool synchronize);
    void handlePeerStatus(const juce::var& message);
    void handleLifecycleResult(const juce::var& message);
    void proposeBinding();
    juce::StringArray validateBinding(const Contract& contract, const std::vector<SignalInfo>& catalog, std::vector<juce::var>* errors) const;
    void send(const juce::var& message);
    void sendStatus(const juce::String& severity, const juce::String& code, const juce::String& text, bool rateLimited = false);
    void setState(LinkState next, const juce::String& message = {}, int severity = 0);
    void resetSession();
    void clearWatermarks();
    void invalidateBinding(const juce::String& reason);
    void failCommand(const juce::String& reason);
    bool lifecycleReady(juce::String& reason) const;
    void transportFailed(const juce::String& reason);
    void failConnection(const juce::String& reason);
    void restartPipe();
    void connectionEnded();
    void writeDiscoveryFile(bool present);

    std::shared_ptr<cad::CadCommandService> service;
    std::shared_ptr<cad::CadMotionRuntime> runtime;
    std::unique_ptr<Connection> connection;
    std::unique_ptr<Writer> writer;

    mutable std::recursive_mutex lock;
    LinkSnapshot status;
    Contract contract;
    std::vector<SignalInfo> catalog;
    juce::String signalInterfaceRevision;
    bool listening = false, peerPresent = false, helloReceived = false, bindingAccepted = false, stateApplied = false, peerLifecycle = false;
    int proposedProfileRevision = -1;
    juce::String acceptedSimulationId;
    // Ordering watermarks for the active simulation session. They survive same-session revalidation and are
    // cleared only by a fresh connection (Hello) or a SignalInterface with a different simulationId.
    juce::String watermarkSimulationId, lastSequenceText, lastSyncSequence;
    std::uint64_t lastSequence = 0;
    std::int64_t lastTick = -1;
    int lastMicrostep = 0;
    bool lastSyncAcked = false;
    std::uint64_t epoch = 1;
    juce::uint32 lastHeardMs = 0, proposalSentMs = 0, lastHeartbeatMs = 0, lastDisplayMs = 0;
    std::map<juce::String, juce::uint32> lastStatusSentMs;
    PendingState pending;
    std::optional<PendingCommand> command;
    bool statusDirty = false;
    std::atomic<bool> transportFailure { false };
    juce::String transportFailureReason;
    int publishDelayMs = 0;
    juce::String pipeName;
    std::uint64_t shownRevision = 0;
    std::shared_ptr<StatusBoard> board = std::make_shared<StatusBoard>();
};
}
