#include "MotionLink.h"

#include <process.h>

#include <cmath>
#include <limits>
#include <set>

namespace djehuti::motion
{
namespace
{
using Lock = std::lock_guard<std::recursive_mutex>;

juce::var makeObject(std::initializer_list<std::pair<const char*, juce::var>> fields)
{
    auto object = std::make_unique<juce::DynamicObject>();
    for (const auto& [name, value] : fields) object->setProperty(name, value);
    return juce::var(object.release());
}

juce::uint32 nowMs() { return juce::Time::getMillisecondCounter(); }

// 64-bit wire values (ticks, sequences, durations) are decimal strings so JSON number precision never truncates them.
bool parseDecimal(const juce::var& value, std::int64_t& out)
{
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.isEmpty() || text.length() > 19 || !text.containsOnly("0123456789")) return false;
    if (text.length() == 19 && text.compare("9223372036854775807") > 0) return false;
    out = text.getLargeIntValue();
    return true;
}

bool isNumber(const juce::var& value) { return (value.isDouble() || value.isInt() || value.isInt64()) && std::isfinite((double) value); }
bool isLifecycleCommand(const juce::String& c) { return c == "start" || c == "pause" || c == "resume" || c == "stop" || c == "reset" || c == "step"; }
juce::String commandLabel(const juce::String& c) { return c == "step" ? "Single Step" : c.substring(0, 1).toUpperCase() + c.substring(1); }
}

juce::String stateName(LinkState state)
{
    switch (state)
    {
        case LinkState::stopped: return "Stopped";
        case LinkState::listening: return "Listening";
        case LinkState::connected: return "Connected";
        case LinkState::validating: return "Validating";
        case LinkState::awaitingSync: return "Awaiting synchronization";
        case LinkState::synchronized: return "Synchronized";
        case LinkState::streaming: return "Streaming";
        case LinkState::paused: return "Paused";
        case LinkState::disconnected: return "Disconnected";
        case LinkState::error: return "Error";
    }
    return "Unknown";
}

juce::String LinkSnapshot::toJson() const
{
    juce::Array<juce::var> signalList, problemList, warningList;
    for (const auto& s : signals) signalList.add(makeObject({ { "id", s.id }, { "name", s.name }, { "quantity", s.quantity }, { "unit", s.unit }, { "kind", s.kind } }));
    for (const auto& p : problems) problemList.add(p);
    for (const auto& w : limitWarnings) warningList.add(w);
    return juce::JSON::toString(makeObject({ { "state", stateName(state) }, { "pipeName", pipeName }, { "pipePath", pipePath }, { "peer", peerName },
                                             { "simulationId", simulationId }, { "lifecycle", lifecycle }, { "message", message },
                                             { "acceptedProfileRevision", acceptedProfileRevision }, { "timelineGeneration", timelineGeneration },
                                             { "lastSequence", lastSequence }, { "simTimePs", simTimePs }, { "signals", signalList },
                                             { "problems", problemList }, { "limitWarnings", warningList },
                                             { "lifecycleControl", makeObject({ { "peerSupports", lifecycleControl }, { "available", lifecycleAvailable },
                                                                                { "unavailableReason", lifecycleUnavailableReason }, { "pendingCommand", pendingCommand },
                                                                                { "canRetry", canRetry }, { "lastResult", commandText } }) },
                                             { "mechanicalInterface", mechanicalInterface } }), true);
}

//==============================================================================
class MotionLink::Connection final : public juce::InterprocessConnection
{
public:
    explicit Connection(MotionLink& ownerIn) : juce::InterprocessConnection(false, kMagicHeader), owner(ownerIn) {}
    ~Connection() override { disconnect(kShutdownWaitMs, Notify::no); }
    void connectionMade() override {}
    void connectionLost() override { owner.connectionEnded(); }
    void messageReceived(const juce::MemoryBlock& message) override { owner.handleIncoming(message); }
private:
    MotionLink& owner;
};

// Writes leave the GUI and connection threads. The queue is bounded, a failed write is reported, and the
// time a write has been in progress is visible so the link can cancel a write to a peer that stopped reading.
class MotionLink::Writer final : public juce::Thread
{
public:
    Writer(MotionLink& ownerIn, Connection& c) : juce::Thread("DCAD motion writer"), owner(ownerIn), connection(c) {}
    ~Writer() override { stop(); stopThread(kShutdownWaitMs); }
    bool push(const juce::String& text)
    {
        {
            const std::lock_guard<std::mutex> guard(queueLock);
            if ((int) queue.size() >= kMaxQueuedOutgoing) return false;
            queue.push_back(text);
        }
        wake.signal();
        return true;
    }
    void stop() { signalThreadShouldExit(); wake.signal(); }
    juce::uint32 writeStartedMs() const { return started.load(); }
    void run() override
    {
        while (!threadShouldExit())
        {
            wake.wait(-1);
            for (;;)
            {
                juce::String next;
                {
                    const std::lock_guard<std::mutex> guard(queueLock);
                    if (queue.empty() || threadShouldExit()) break;
                    next = queue.front(); queue.pop_front();
                }
                started = juce::jmax<juce::uint32>(1, nowMs());
                const bool ok = connection.sendMessage(juce::MemoryBlock(next.toRawUTF8(), next.getNumBytesAsUTF8()));
                started = 0;
                if (!ok && !threadShouldExit())
                {
                    { const std::lock_guard<std::mutex> guard(queueLock); queue.clear(); }
                    owner.transportFailed("A write to DEWB failed; the pipe connection is no longer usable.");
                    break;
                }
            }
        }
    }
private:
    MotionLink& owner;
    Connection& connection;
    std::mutex queueLock;
    std::deque<juce::String> queue;
    juce::WaitableEvent wake;
    std::atomic<juce::uint32> started { 0 };
};

//==============================================================================
MotionLink::MotionLink(std::shared_ptr<cad::CadCommandService> s, std::shared_ptr<cad::CadMotionRuntime> r) : service(std::move(s)), runtime(std::move(r))
{
    // Test hook for the pending-state boundary: delays publication so invalidation can be exercised deterministically.
    publishDelayMs = juce::jlimit(0, 10000, juce::SystemStats::getEnvironmentVariable("DJEHUTI_MOTION_TEST_PUBLISH_DELAY_MS", "0").getIntValue());
    refreshContract();
    startTimerHz(10);
}

MotionLink::~MotionLink()
{
    stopTimer();
    stopListening();
    cancelPendingUpdate();
}

bool MotionLink::isListening() const { const Lock guard(lock); return listening; }

LinkSnapshot MotionLink::snapshot() const
{
    const Lock guard(lock);
    auto copy = status;
    juce::String reason;
    copy.lifecycleControl = peerLifecycle;
    copy.lifecycleAvailable = lifecycleReady(reason);
    copy.lifecycleUnavailableReason = reason;
    copy.commandPending = command.has_value() && !command->timedOut;
    copy.canRetry = command.has_value() && command->timedOut;
    copy.pendingCommand = command.has_value() ? command->command : juce::String();
    return copy;
}

bool MotionLink::startListening(juce::String& error)
{
    if (isListening()) return true;
    juce::Random random;
    const auto token = juce::String::toHexString(random.nextInt64()).paddedLeft('0', 16).substring(0, 12);
    const auto name = "DjehutiCAD.Motion." + juce::String(_getpid()) + "." + token;
    auto created = std::make_unique<Connection>(*this);
    if (!created->createPipe(name, -1, true)) { error = "Could not create the named pipe " + name + "."; return false; }
    connection = std::move(created);
    writer = std::make_unique<Writer>(*this, *connection);
    writer->startThread();
    {
        const Lock guard(lock);
        pipeName = name;
        listening = true;
        ++epoch;
        resetSession();
        clearWatermarks();
        status.pipeName = name;
        status.pipePath = "\\\\.\\pipe\\" + name;
        setState(LinkState::listening, "Waiting for DEWB to connect to " + status.pipePath + ".");
    }
    writeDiscoveryFile(true);
    return true;
}

// Pipe reads wait without a deadline so an idle listener stays healthy; closing the pipe cancels any pending read
// or write, and both threads are joined with a bounded wait.
void MotionLink::stopListening()
{
    {
        const Lock guard(lock);
        if (!listening && connection == nullptr) return;
        listening = false;
    }
    if (writer != nullptr) writer->stop();
    if (connection != nullptr) connection->disconnect(kShutdownWaitMs, juce::InterprocessConnection::Notify::no);
    writer.reset();
    connection.reset();
    writeDiscoveryFile(false);
    const Lock guard(lock);
    ++epoch;
    resetSession();
    clearWatermarks();
    failCommand("the motion link stopped listening.");
    status.pipeName.clear(); status.pipePath.clear();
    setState(LinkState::stopped, "Motion link stopped. The last pose is held until you preview or reset.");
}

void MotionLink::restartPipe()
{
    if (writer != nullptr) writer->stop();
    if (connection != nullptr) connection->disconnect(kShutdownWaitMs, juce::InterprocessConnection::Notify::no);
    writer.reset();
    transportFailure = false;
    if (connection != nullptr && connection->createPipe(pipeName, -1, false))
    {
        writer = std::make_unique<Writer>(*this, *connection);
        writer->startThread();
    }
}

// Closing the pipe instance breaks the client's handle; a fresh instance is created for the next connection.
void MotionLink::disconnectPeer()
{
    if (!isListening()) return;
    restartPipe();
    const Lock guard(lock);
    ++epoch;
    resetSession();
    clearWatermarks();
    failCommand("DEWB was disconnected.");
    setState(LinkState::disconnected, "Disconnected from DEWB; holding the last pose. Listening again on " + status.pipePath + ".");
}

void MotionLink::connectionEnded() { const Lock guard(lock); statusDirty = true; }

// Called from the writer thread (or when the queue is full); the connection is replaced on the message thread.
void MotionLink::transportFailed(const juce::String& reason)
{
    {
        const Lock guard(lock);
        if (transportFailure) return;
        transportFailureReason = reason;
    }
    transportFailure = true;
    triggerAsyncUpdate();
}

void MotionLink::failConnection(const juce::String& reason)
{
    if (!isListening()) return;
    restartPipe();
    const Lock guard(lock);
    ++epoch;
    resetSession();
    clearWatermarks();
    failCommand("the connection failed.");
    status.problems.add(reason);
    setState(LinkState::disconnected, reason + " The connection was closed and the last pose is held; DEWB can reconnect and revalidate.", 2);
}

void MotionLink::resetSession()
{
    peerPresent = helloReceived = bindingAccepted = stateApplied = peerLifecycle = false;
    proposedProfileRevision = -1;
    catalog.clear(); signalInterfaceRevision.clear();
    acceptedSimulationId.clear();
    pending = {};
    status.peerName.clear(); status.simulationId.clear(); status.lifecycle.clear();
    status.acceptedProfileRevision = -1;
    status.signals.clear(); status.limitWarnings.clear();
}

void MotionLink::clearWatermarks()
{
    watermarkSimulationId.clear(); lastSequenceText.clear(); lastSyncSequence.clear();
    lastSequence = 0; lastTick = -1; lastMicrostep = 0; lastSyncAcked = false;
    status.timelineGeneration = 0; status.lastSequence.clear(); status.simTimePs.clear();
}

// Supersedes the binding: queued states and the outstanding command belong to it and can never publish later,
// even if undo/redo restores the same revision numbers.
void MotionLink::invalidateBinding(const juce::String& reason)
{
    ++epoch;
    bindingAccepted = stateApplied = lastSyncAcked = false;
    proposedProfileRevision = -1;
    status.acceptedProfileRevision = -1;
    pending = {};
    failCommand(reason);
}

void MotionLink::failCommand(const juce::String& reason)
{
    if (!command.has_value()) return;
    status.commandText = commandLabel(command->command) + " was not confirmed: " + reason;
    status.commandSeverity = 2;
    ++status.serial;
    command.reset();
}

void MotionLink::setState(LinkState next, const juce::String& message, int severity)
{
    status.state = next; status.message = message; status.severity = severity;
    ++status.serial;
    statusDirty = true;
    triggerAsyncUpdate();
}

void MotionLink::send(const juce::var& message)
{
    if (writer == nullptr || transportFailure) return;
    if (!writer->push(juce::JSON::toString(message, true)))
        transportFailed("DEWB stopped reading: " + juce::String(kMaxQueuedOutgoing) + " outgoing messages are waiting.");
}

void MotionLink::sendStatus(const juce::String& severity, const juce::String& code, const juce::String& text, bool rateLimited)
{
    if (rateLimited)
    {
        const auto now = nowMs();
        if (const auto last = lastStatusSentMs.find(code); last != lastStatusSentMs.end() && now - last->second < 1000) return;
        lastStatusSentMs[code] = now;
    }
    send(makeObject({ { "type", "LinkStatus" }, { "state", stateName(status.state) }, { "severity", severity }, { "code", code }, { "message", text },
                      { "profileRevision", contract.profileRevision }, { "lastSequence", lastSequenceText } }));
}

//==============================================================================
void MotionLink::timerCallback()
{
    bool restart = false;
    juce::String stallReason;
    {
        const Lock guard(lock);
        const auto now = nowMs();
        restart = listening && connection != nullptr && !connection->isConnected();
        if (writer != nullptr && writer->writeStartedMs() != 0 && now - writer->writeStartedMs() > (juce::uint32) kWriteStallMs)
            stallReason = "DEWB stopped reading: a write did not complete within " + juce::String(kWriteStallMs / 1000) + " s.";
        if (peerPresent && now - lastHeardMs > (juce::uint32) kPeerTimeoutMs)
        {
            ++epoch;
            resetSession();
            clearWatermarks();
            failCommand("DEWB went silent.");
            setState(LinkState::disconnected, "No message from DEWB for " + juce::String(kPeerTimeoutMs / 1000) + " s; holding the last pose. Reconnect to resume.", 1);
        }
        if (peerPresent && proposedProfileRevision >= 0 && !bindingAccepted && now - proposalSentMs > (juce::uint32) kBindingTimeoutMs)
        {
            proposedProfileRevision = -1;
            sendStatus("error", "bindingTimeout", "DEWB did not answer the binding proposal.");
            setState(LinkState::error, "DEWB did not accept or reject the binding proposal within " + juce::String(kBindingTimeoutMs / 1000) + " s.", 2);
        }
        if (command.has_value() && !command->timedOut && now - command->sentMs > (juce::uint32) (command->accepted ? kCommandCompletionMs : kCommandTimeoutMs))
        {
            command->timedOut = true;
            status.commandText = "No result from DEWB for " + commandLabel(command->command) + (command->accepted ? " after it was accepted" : "") + ". The displayed state is the last state DEWB confirmed. Retry resends the same request.";
            status.commandSeverity = 1;
            ++status.serial;
            statusDirty = true;
        }
        if (peerPresent && now - lastHeartbeatMs > (juce::uint32) kHeartbeatMs) { lastHeartbeatMs = now; sendStatus("info", "heartbeat", stateName(status.state)); }
    }
    if (stallReason.isNotEmpty()) failConnection(stallReason);
    else if (restart)
    {
        // The pipe's read thread ended (client error or a malformed header); serve a fresh pipe instance.
        restartPipe();
        const Lock guard(lock);
        const bool hadPeer = peerPresent;
        ++epoch;
        resetSession();
        clearWatermarks();
        failCommand("the connection ended.");
        if (hadPeer) setState(LinkState::disconnected, "The DEWB connection ended; holding the last pose. Listening again.", 1);
    }
    handleAsyncUpdate();
}

void MotionLink::handleAsyncUpdate()
{
    // Contract changes are detected before publication so a state from a superseded document or profile never applies.
    if (service->document().revision() != shownRevision) refreshContract();
    if (transportFailure)
    {
        juce::String reason;
        { const Lock guard(lock); reason = transportFailureReason; }
        failConnection(reason);
    }
    publishPending();
    bool simulationOwns = false, notify = false;
    {
        const Lock guard(lock);
        simulationOwns = peerPresent && helloReceived;
        notify = statusDirty; statusDirty = false;
    }
    runtime->setDriver(simulationOwns ? cad::CadMotionRuntime::Driver::simulation : cad::CadMotionRuntime::Driver::preview);
    board->set(snapshot().toJson());
    if (notify && onStatusChanged) onStatusChanged();
}

// Validity is rechecked and the pose applied under the link lock, so invalidation on the connection thread
// cannot interleave between the check and publication.
void MotionLink::publishPending()
{
    const Lock guard(lock);
    if (!pending.valid) return;
    if (publishDelayMs > 0 && nowMs() - pending.receivedMs < (juce::uint32) publishDelayMs) return;
    const auto work = pending;
    pending = {};
    const bool current = work.epoch == epoch && bindingAccepted && work.simulationId == acceptedSimulationId
                      && work.profileRevision == status.acceptedProfileRevision && work.generation == status.timelineGeneration;
    if (!current) return;
    if (work.synchronize && work.sequence != lastSyncSequence) return;
    runtime->setDriver(cad::CadMotionRuntime::Driver::simulation);
    runtime->applySimulationCoordinates(work.coordinates);
    if (work.synchronize)
    {
        stateApplied = lastSyncAcked = true;
        send(makeObject({ { "type", "StateApplied" }, { "simulationId", work.simulationId }, { "timelineGeneration", work.generation },
                          { "profileRevision", work.profileRevision }, { "sequence", work.sequence } }));
        setState(LinkState::synchronized, "Synchronized with DEWB; the pose shows the simulation state.");
    }
}

//==============================================================================
// The mechanical interface and profile are rebuilt from the document on the message thread; any change to
// what DEWB validated (including a replaced document) invalidates the binding and starts revalidation.
void MotionLink::refreshContract()
{
    const auto& document = service->document();
    shownRevision = document.revision();
    const auto& model = document.mechanicsData();
    Contract next;
    next.modelId = model.modelId;
    next.documentName = document.file() == juce::File() ? juce::String("Untitled") : document.file().getFileName();
    next.interfaceRevision = model.interfaceRevision;
    next.profileRevision = model.profileRevision;
    next.documentInstance = document.documentInstance();
    next.mappings = model.mappings;
    juce::Array<juce::var> instances, joints;
    for (const auto& instance : model.instances) instances.add(makeObject({ { "id", instance.id }, { "name", instance.name }, { "featureId", instance.featureId } }));
    for (const auto& joint : model.joints)
    {
        next.joints[joint.id] = joint;
        juce::var coordinate;
        juce::Array<juce::var> accepted;
        if (joint.type != "fixed")
        {
            coordinate = makeObject({ { "quantity", cad::mech::coordinateQuantity(joint.type) }, { "unit", cad::mech::coordinateUnit(joint.type) }, { "reference", joint.reference },
                                      { "limits", joint.hasLimits ? makeObject({ { "lower", joint.lower }, { "upper", joint.upper } }) : juce::var() } });
            accepted.add(joint.type == "revolute" ? "angle" : "position");
            accepted.add(joint.type == "revolute" ? "angular_velocity" : "linear_velocity");
        }
        joints.add(makeObject({ { "id", joint.id }, { "name", joint.name }, { "type", joint.type },
                                { "parent", joint.parentInstance.isEmpty() ? juce::String("world") : joint.parentInstance }, { "child", joint.childInstance },
                                { "parentToJoint", cad::mech::frameToJson(joint.parentToJoint, 0.001) },
                                { "jointToChildAtReference", cad::mech::frameToJson(joint.jointToChild, 0.001) },
                                { "coordinate", coordinate }, { "acceptedSourceQuantities", accepted } }));
    }
    next.interfaceMessage = makeObject({ { "type", "MechanicalInterface" }, { "modelId", next.modelId }, { "documentName", next.documentName },
                                         { "interfaceRevision", next.interfaceRevision }, { "profileRevision", next.profileRevision },
                                         { "lengthUnit", "m" }, { "angleUnit", "rad" }, { "rotationConvention", "hamilton-wxyz-active" }, { "motionAxis", "+Z" },
                                         { "instances", instances }, { "joints", joints } });

    const Lock guard(lock);
    status.mechanicalInterface = next.interfaceMessage;
    const bool changed = next.modelId != contract.modelId || next.interfaceRevision != contract.interfaceRevision
                      || next.profileRevision != contract.profileRevision || next.documentInstance != contract.documentInstance;
    contract = std::move(next);
    if (!changed) return;
    invalidateBinding("the CAD mechanical interface or connection profile changed.");
    if (helloReceived)
    {
        sendStatus("warning", "revalidationRequired", "The DCAD mechanical interface or connection profile changed; the previous binding is no longer valid.");
        send(contract.interfaceMessage);
        if (!catalog.empty()) proposeBinding();
        else setState(LinkState::validating, "Mechanical interface changed; waiting for the DEWB signal interface.", 1);
    }
}

juce::StringArray MotionLink::validateProfileAgainstSignals() const
{
    const Lock guard(lock);
    if (catalog.empty()) return {};
    return validateBinding(contract, catalog, nullptr);
}

juce::StringArray MotionLink::validateBinding(const Contract& c, const std::vector<SignalInfo>& signals, std::vector<juce::var>* errors) const
{
    juce::StringArray problems;
    auto fail = [&](const juce::String& code, const juce::String& joint, const juce::String& signal, const juce::String& text)
    {
        problems.add(text);
        if (errors != nullptr) errors->push_back(makeObject({ { "code", code }, { "joint", joint }, { "signal", signal }, { "message", text } }));
    };
    if (c.mappings.empty()) fail("emptyProfile", {}, {}, "The connection profile has no signal mappings; map at least one joint.");
    for (const auto& m : c.mappings)
    {
        const auto joint = c.joints.find(m.jointId);
        if (joint == c.joints.end()) { fail("unknownJoint", m.jointId, m.signalId, "Mapping refers to unknown joint " + m.jointId + "."); continue; }
        const auto& j = joint->second;
        if (!cad::mech::quantityDrivesJoint(m.sourceQuantity, j.type)) { fail("quantityMismatch", m.jointId, m.signalId, j.name + ": " + m.sourceQuantity + " cannot drive a " + j.type + " joint."); continue; }
        const SignalInfo* signal = nullptr;
        for (const auto& s : signals) if (s.id == m.signalId) signal = &s;
        if (signal == nullptr) { fail("unknownSignal", m.jointId, m.signalId, j.name + ": DEWB does not provide signal '" + m.signalId + "'."); continue; }
        if (signal->quantity != m.sourceQuantity) { fail("quantityMismatch", m.jointId, m.signalId, j.name + ": signal '" + m.signalId + "' is " + signal->quantity + ", but the profile expects " + m.sourceQuantity + "."); continue; }
        if (signal->unit != m.sourceUnit) { fail("unitMismatch", m.jointId, m.signalId, j.name + ": signal '" + m.signalId + "' is in " + signal->unit + ", but the profile states " + m.sourceUnit + "."); continue; }
        const auto expectedKind = cad::mech::isRateQuantity(signal->quantity) ? "rate" : "state";
        if (signal->kind != expectedKind) fail("quantityMismatch", m.jointId, m.signalId, j.name + ": signal '" + m.signalId + "' is declared as a " + signal->kind + " output but " + signal->quantity + " is a " + expectedKind + " quantity.");
    }
    return problems;
}

void MotionLink::proposeBinding()
{
    std::vector<juce::var> errors;
    const auto problems = validateBinding(contract, catalog, &errors);
    status.problems = problems;
    if (!problems.isEmpty())
    {
        proposedProfileRevision = -1;
        juce::Array<juce::var> list; for (const auto& e : errors) list.add(e);
        send(makeObject({ { "type", "LinkStatus" }, { "state", "Error" }, { "severity", "error" }, { "code", "profileIncompatible" },
                          { "message", "DCAD cannot propose the connection profile: " + problems.joinIntoString(" ") }, { "errors", list }, { "profileRevision", contract.profileRevision } }));
        setState(LinkState::error, "The connection profile does not match the DEWB signal interface.", 2);
        return;
    }
    juce::Array<juce::var> mappings;
    for (const auto& m : contract.mappings)
    {
        const auto& joint = contract.joints.at(m.jointId);
        double factor = 1.0; cad::mech::unitToSI(m.sourceQuantity, m.sourceUnit, factor);
        const bool rate = cad::mech::isRateQuantity(m.sourceQuantity);
        mappings.add(makeObject({ { "joint", m.jointId }, { "signal", m.signalId }, { "sourceQuantity", m.sourceQuantity }, { "sourceUnit", m.sourceUnit },
                                  { "jointQuantity", cad::mech::coordinateQuantity(joint.type) }, { "jointUnit", cad::mech::coordinateUnit(joint.type) },
                                  { "unitToSI", factor }, { "scale", m.scale }, { "offset", rate ? 0.0 : m.offset },
                                  { "initialCoordinate", rate ? juce::var(m.initialCoordinate) : juce::var() } }));
    }
    proposedProfileRevision = contract.profileRevision;
    proposalSentMs = nowMs();
    send(makeObject({ { "type", "BindingProposal" }, { "modelId", contract.modelId }, { "interfaceRevision", contract.interfaceRevision },
                      { "profileRevision", contract.profileRevision }, { "simulationId", status.simulationId }, { "signalInterfaceRevision", signalInterfaceRevision },
                      { "mappings", mappings } }));
    setState(LinkState::validating, "Binding proposal sent; waiting for DEWB to accept profile revision " + juce::String(contract.profileRevision) + ".");
}

//==============================================================================
void MotionLink::handleIncoming(const juce::MemoryBlock& data)
{
    const Lock guard(lock);
    if (!listening || transportFailure) return;
    if ((int) data.getSize() > kMaxMessageBytes) { sendStatus("error", "messageTooLarge", "Messages are limited to " + juce::String(kMaxMessageBytes) + " bytes.", true); return; }
    const auto message = juce::JSON::parse(juce::String::fromUTF8((const char*) data.getData(), (int) data.getSize()));
    if (!message.isObject() || !message.getProperty("type", {}).isString()) { sendStatus("error", "invalidMessage", "Each message must be a JSON object with a string 'type'.", true); return; }
    lastHeardMs = nowMs();
    handleMessage(message);
}

void MotionLink::handleMessage(const juce::var& message)
{
    const auto type = message.getProperty("type", "").toString();
    if (type == "Hello") { handleHello(message); return; }
    if (!helloReceived) { sendStatus("error", "helloRequired", "Send Hello before any other message.", true); return; }
    if (type == "SignalInterface") handleSignalInterface(message);
    else if (type == "BindingAccept") handleBindingAccept(message);
    else if (type == "BindingReject") handleBindingReject(message);
    else if (type == "SynchronizeState") handleState(message, true);
    else if (type == "StateFrame") handleState(message, false);
    else if (type == "LinkStatus") handlePeerStatus(message);
    else if (type == "LifecycleResult") handleLifecycleResult(message);
    else sendStatus("error", "unknownMessageType", "Unknown message type '" + type + "'.", true);
}

void MotionLink::handleHello(const juce::var& message)
{
    if (message.getProperty("protocol", "").toString() != "djehuti.motion" || (int) message.getProperty("protocolVersion", 0) != kProtocolVersion || message.getProperty("role", "").toString() != "dewb")
    {
        sendStatus("error", "unsupportedProtocol", "DCAD speaks protocol 'djehuti.motion' version " + juce::String(kProtocolVersion) + " and expects role 'dewb'.");
        setState(LinkState::error, "A peer connected with an unsupported protocol or role.", 2);
        return;
    }
    // Every Hello starts a fresh connection session: bindings, timelines, watermarks and commands never carry over.
    ++epoch;
    resetSession();
    clearWatermarks();
    failCommand("DEWB started a new session.");
    status.problems.clear();
    peerPresent = helloReceived = true;
    lastHeartbeatMs = nowMs();
    status.peerName = message.getProperty("application", "DEWB").toString() + " " + message.getProperty("applicationVersion", "").toString();
    if (const auto* capabilities = message.getProperty("capabilities", {}).getArray()) peerLifecycle = capabilities->contains(juce::var(kLifecycleCapability));
    juce::Array<juce::var> capabilities { "mechanicalInterface.v1", "bindingProposal.v1", "stateFrame.v1", "heartbeat.v1", kLifecycleCapability };
    send(makeObject({ { "type", "Hello" }, { "protocol", "djehuti.motion" }, { "protocolVersion", kProtocolVersion }, { "role", "dcad" },
                      { "application", "DjehutiCAD" }, { "applicationVersion", "0.1.0" }, { "capabilities", capabilities } }));
    send(contract.interfaceMessage);
    setState(LinkState::connected, "DEWB connected; waiting for its signal interface.");
}

void MotionLink::handleSignalInterface(const juce::var& message)
{
    const auto* list = message.getProperty("signals", {}).getArray();
    const auto simulationId = message.getProperty("simulationId", "").toString();
    if (list == nullptr || simulationId.isEmpty()) { sendStatus("error", "invalidMessage", "SignalInterface needs 'simulationId' and a 'signals' array."); return; }
    std::vector<SignalInfo> next;
    std::set<juce::String> ids;
    for (const auto& v : *list)
    {
        SignalInfo s { v.getProperty("id", "").toString(), v.getProperty("name", "").toString(), v.getProperty("quantity", "").toString(), v.getProperty("unit", "").toString(), v.getProperty("kind", "").toString() };
        double factor = 0.0;
        if (s.id.isEmpty() || !ids.insert(s.id).second) { sendStatus("error", "invalidMessage", "Signal ids must be present and unique."); return; }
        if (!cad::mech::unitToSI(s.quantity, s.unit, factor) || (s.kind != "state" && s.kind != "rate")) { sendStatus("error", "invalidMessage", "Signal '" + s.id + "' has an unsupported quantity, unit or kind."); return; }
        next.push_back(s);
    }
    // A new signal interface always needs a fresh proposal, acceptance and synchronization. A different
    // simulationId replaces the simulation session, so its ordering history is discarded too.
    invalidateBinding("DEWB changed its signal interface.");
    if (simulationId != watermarkSimulationId) clearWatermarks();
    watermarkSimulationId = simulationId;
    catalog = next;
    signalInterfaceRevision = message.getProperty("signalInterfaceRevision", "").toString();
    status.simulationId = simulationId;
    status.signals = next;
    proposeBinding();
}

void MotionLink::handleBindingAccept(const juce::var& message)
{
    if (proposedProfileRevision < 0 || (int) message.getProperty("profileRevision", -1) != proposedProfileRevision || (int) message.getProperty("interfaceRevision", -1) != contract.interfaceRevision
        || message.getProperty("modelId", "").toString() != contract.modelId || message.getProperty("simulationId", "").toString() != status.simulationId)
    {
        sendStatus("error", "staleProfile", "BindingAccept must echo the modelId, interfaceRevision, profileRevision and simulationId of the outstanding BindingProposal.");
        return;
    }
    ++epoch;   // the new binding gets its own identity; nothing queued under an earlier binding can publish
    bindingAccepted = true; stateApplied = false; lastSyncAcked = false;
    pending = {};
    acceptedSimulationId = status.simulationId;
    status.acceptedProfileRevision = proposedProfileRevision;
    status.problems.clear();
    setState(LinkState::awaitingSync, "Binding accepted (profile revision " + juce::String(proposedProfileRevision) + "); waiting for SynchronizeState.");
}

void MotionLink::handleBindingReject(const juce::var& message)
{
    juce::StringArray reasons;
    if (const auto* list = message.getProperty("errors", {}).getArray())
        for (const auto& e : *list) reasons.add(e.getProperty("message", e.getProperty("code", "rejected")).toString());
    if (reasons.isEmpty()) reasons.add("DEWB rejected the profile without details.");
    invalidateBinding("DEWB rejected the binding.");
    status.problems = reasons;
    setState(LinkState::error, "DEWB rejected the connection profile.", 2);
}

// Ordering rules (spec section 6): a timeline older than the current one is always rejected; a newer timeline
// may only begin with SynchronizeState; within a timeline every state, including a resynchronization, must
// advance the sequence and must not move (simTimePs, microstep) backwards. A repeated SynchronizeState is
// never reapplied: if it was already published under the current binding, DCAD repeats its StateApplied.
void MotionLink::handleState(const juce::var& message, bool synchronize)
{
    const juce::String kind = synchronize ? "SynchronizeState" : "StateFrame";
    auto reject = [&](const juce::String& code, const juce::String& text) { sendStatus("error", code, kind + " rejected: " + text, true); status.message = kind + " rejected: " + text; ++status.serial; statusDirty = true; triggerAsyncUpdate(); };
    if (!bindingAccepted) { reject("notBound", "no accepted binding profile."); return; }
    const auto simulationId = message.getProperty("simulationId", "").toString();
    if (simulationId != acceptedSimulationId) { reject("wrongSimulation", "simulationId '" + simulationId + "' is not the bound simulation."); return; }
    if ((int) message.getProperty("profileRevision", -1) != status.acceptedProfileRevision) { reject("staleProfile", "profileRevision does not match the accepted profile."); return; }
    const auto generationValue = message.getProperty("timelineGeneration", {});
    if (!generationValue.isInt() && !generationValue.isInt64()) { reject("invalidMessage", "timelineGeneration must be an integer."); return; }
    const auto generation = (int) generationValue;
    if (generation < 1) { reject("invalidMessage", "timelineGeneration starts at 1."); return; }
    std::int64_t sequence = 0, tick = 0;
    if (!parseDecimal(message.getProperty("sequence", {}), sequence)) { reject("invalidMessage", "sequence must be a decimal string."); return; }
    if (!parseDecimal(message.getProperty("simTimePs", {}), tick)) { reject("invalidMessage", "simTimePs must be a decimal string of picosecond ticks."); return; }
    const auto microstep = (int) message.getProperty("microstep", 0);
    if (microstep < 0) { reject("invalidMessage", "microstep must not be negative."); return; }
    const auto lifecycle = message.getProperty("lifecycle", "").toString();
    if (lifecycle != "running" && lifecycle != "paused" && lifecycle != "stopped" && lifecycle != "faulted") { reject("invalidMessage", "lifecycle must be running, paused, stopped or faulted."); return; }
    const auto sequenceText = message.getProperty("sequence", "").toString();
    const bool sameTimeline = status.timelineGeneration > 0 && generation == status.timelineGeneration;

    if (status.timelineGeneration > 0 && generation < status.timelineGeneration) { reject("staleTimeline", "timeline " + juce::String(generation) + " was superseded by timeline " + juce::String(status.timelineGeneration) + "."); return; }
    if (synchronize && sameTimeline && sequenceText == lastSyncSequence)
    {
        if (lastSyncAcked)
            send(makeObject({ { "type", "StateApplied" }, { "simulationId", simulationId }, { "timelineGeneration", generation },
                              { "profileRevision", status.acceptedProfileRevision }, { "sequence", sequenceText }, { "duplicate", true } }));
        return;   // duplicate synchronization: acknowledged again if already published, never reapplied
    }
    if (!synchronize)
    {
        if (!stateApplied) { reject("synchronizeRequired", "send SynchronizeState and wait for StateApplied before streaming."); return; }
        if (!sameTimeline) { reject("synchronizeRequired", "a new timeline must begin with SynchronizeState."); return; }
    }
    if (sameTimeline)
    {
        if ((std::uint64_t) sequence <= lastSequence) { reject("staleSequence", "sequence " + sequenceText + " is not newer than " + lastSequenceText + " in this timeline."); return; }
        if (tick < lastTick || (tick == lastTick && microstep < lastMicrostep)) { reject("timeRegression", "(simTimePs, microstep) moved backwards within the timeline; a rewind needs a new timeline."); return; }
    }

    // Validate the whole state before anything is queued for publication.
    std::set<juce::String> driven;
    for (const auto& m : contract.mappings) driven.insert(m.jointId);
    std::map<juce::String, double> coordinates;
    const auto* joints = message.getProperty("joints", {}).getArray();
    if (joints == nullptr) { reject("invalidMessage", "joints must be an array."); return; }
    for (const auto& entry : *joints)
    {
        const auto id = entry.getProperty("joint", "").toString();
        if (driven.count(id) == 0) { reject("unknownJoint", "joint '" + id + "' is not driven by the accepted profile."); return; }
        if (coordinates.count(id) != 0) { reject("invalidMessage", "joint '" + id + "' appears twice."); return; }
        const auto coordinate = entry.getProperty("coordinate", {});
        if (!isNumber(coordinate)) { reject("nonFiniteValue", "joint '" + id + "' needs a finite numeric coordinate."); return; }
        if (entry.hasProperty("velocity") && !isNumber(entry.getProperty("velocity", {}))) { reject("nonFiniteValue", "joint '" + id + "' velocity must be finite when present."); return; }
        coordinates[id] = (double) coordinate;
    }
    for (const auto& id : driven) if (coordinates.count(id) == 0) { reject("missingJoint", "driven joint '" + id + "' has no coordinate."); return; }

    // Limit violations are reported, never clamped: DEWB's value is authoritative.
    juce::StringArray warnings;
    for (const auto& [id, value] : coordinates)
        if (const auto& joint = contract.joints.at(id); joint.hasLimits && (value < joint.lower || value > joint.upper))
            warnings.add(joint.name + " at " + juce::String(value, 4) + " " + cad::mech::coordinateUnit(joint.type) + " is outside [" + juce::String(joint.lower, 4) + ", " + juce::String(joint.upper, 4) + "].");
    status.limitWarnings = warnings;
    if (!warnings.isEmpty()) sendStatus("warning", "limitExceeded", warnings.joinIntoString(" "), true);

    status.timelineGeneration = generation;
    lastSequence = (std::uint64_t) sequence; lastSequenceText = sequenceText;
    lastTick = tick; lastMicrostep = microstep;
    if (synchronize) { lastSyncSequence = sequenceText; lastSyncAcked = false; stateApplied = false; }
    status.lastSequence = lastSequenceText; status.simTimePs = message.getProperty("simTimePs", "").toString(); status.lifecycle = lifecycle;
    if (nowMs() - lastDisplayMs > 500) { lastDisplayMs = nowMs(); ++status.serial; statusDirty = true; }   // throttled display refresh while streaming
    pending.synchronize = synchronize || (pending.valid && pending.synchronize && pending.epoch == epoch && pending.generation == generation);
    pending.valid = true;
    pending.coordinates = coordinates; pending.simulationId = simulationId; pending.sequence = pending.synchronize ? lastSyncSequence : sequenceText;
    pending.generation = generation; pending.profileRevision = status.acceptedProfileRevision; pending.epoch = epoch; pending.receivedMs = nowMs();
    if (!synchronize)
    {
        if (lifecycle == "faulted") setState(LinkState::error, "DEWB reports a simulation fault; showing the last reported state.", 2);
        else if (lifecycle == "running") { if (status.state != LinkState::streaming) setState(LinkState::streaming, "Streaming DEWB motion."); }
        else setState(LinkState::paused, lifecycle == "stopped" ? "Simulation stopped; holding its final state." : "Simulation paused.");
    }
    triggerAsyncUpdate();
}

void MotionLink::handlePeerStatus(const juce::var& message)
{
    const auto code = message.getProperty("code", "").toString();
    if (code == "goodbye")
    {
        ++epoch;
        resetSession();
        clearWatermarks();
        failCommand("DEWB closed the session.");
        setState(LinkState::disconnected, "DEWB closed the session; holding the last pose.", 1);
    }
}

//==============================================================================
bool MotionLink::lifecycleReady(juce::String& reason) const
{
    if (!peerPresent || !helloReceived) { reason = "DEWB is not connected."; return false; }
    if (!peerLifecycle) { reason = "DEWB did not advertise lifecycle control (" + juce::String(kLifecycleCapability) + "); motion still streams, but simulation controls are unavailable."; return false; }
    if (!bindingAccepted) { reason = "The connection profile is not bound yet."; return false; }
    if (!stateApplied) { reason = "Waiting for the synchronized simulation state."; return false; }
    return true;
}

bool MotionLink::requestLifecycle(const juce::String& requested, std::int64_t stepDurationPs, juce::String& error)
{
    const Lock guard(lock);
    if (!lifecycleReady(error)) return false;
    if (command.has_value() && !command->timedOut) { error = commandLabel(command->command) + " is still waiting for DEWB."; return false; }
    auto name = requested.toLowerCase().trim();
    if (name == "play") name = status.lifecycle == "paused" ? "resume" : "start";
    if (!isLifecycleCommand(name)) { error = "Unknown simulation command '" + requested + "'."; return false; }
    if (name == "step" && stepDurationPs <= 0) { error = "The step interval must be greater than zero."; return false; }
    PendingCommand next;
    next.requestId = juce::Uuid().toDashedString();
    next.command = name;
    next.epoch = epoch;
    next.message = makeObject({ { "type", "LifecycleCommand" }, { "requestId", next.requestId }, { "modelId", contract.modelId }, { "simulationId", acceptedSimulationId },
                                { "profileRevision", status.acceptedProfileRevision }, { "timelineGeneration", status.timelineGeneration }, { "command", name },
                                { "stepDurationPs", name == "step" ? juce::var(juce::String(stepDurationPs)) : juce::var() } });
    next.sentMs = nowMs();
    send(next.message);
    command = next;
    status.commandText = commandLabel(name) + (name == "step" ? " (" + juce::String((double) stepDurationPs / 1.0e9, 3) + " ms)" : juce::String()) + " sent; waiting for DEWB.";
    status.commandSeverity = 0;
    ++status.serial;
    statusDirty = true;
    triggerAsyncUpdate();
    return true;
}

// A retry resends the identical request (same requestId) so DEWB can deduplicate it.
bool MotionLink::retryLifecycle(juce::String& error)
{
    const Lock guard(lock);
    if (!command.has_value() || !command->timedOut) { error = "There is no unconfirmed command to retry."; return false; }
    if (command->epoch != epoch || !lifecycleReady(error)) { if (error.isEmpty()) error = "The binding changed; issue a new command."; command.reset(); return false; }
    command->timedOut = false; command->accepted = false; command->sentMs = nowMs();
    send(command->message);
    status.commandText = commandLabel(command->command) + " resent with the same request id; waiting for DEWB.";
    status.commandSeverity = 0;
    ++status.serial;
    statusDirty = true;
    triggerAsyncUpdate();
    return true;
}

// Results report command completion only; the pose and the displayed lifecycle stay governed by state messages.
void MotionLink::handleLifecycleResult(const juce::var& message)
{
    const auto requestId = message.getProperty("requestId", "").toString();
    if (!command.has_value() || requestId != command->requestId) { sendStatus("warning", "unknownRequest", "LifecycleResult for '" + requestId + "' does not match an outstanding DCAD request.", true); return; }
    if (message.getProperty("simulationId", "").toString() != acceptedSimulationId) { sendStatus("warning", "wrongSimulation", "LifecycleResult is for a different simulation.", true); return; }
    const auto outcome = message.getProperty("outcome", "").toString();
    const auto label = commandLabel(command->command);
    const auto time = juce::String(message.getProperty("simTimePs", "0").toString().getLargeIntValue() / 1.0e12, 6);
    if (outcome == "accepted")
    {
        command->accepted = true; command->sentMs = nowMs();
        status.commandText = label + " accepted by DEWB; waiting for completion.";
        status.commandSeverity = 0;
    }
    else if (outcome == "applied")
    {
        status.commandText = label + " applied: " + message.getProperty("lifecycle", "").toString() + ", timeline " + message.getProperty("timelineGeneration", "").toString() + ", t = " + time + " s.";
        status.commandSeverity = 0;
        command.reset();
    }
    else if (outcome == "rejected")
    {
        status.commandText = label + " rejected (" + message.getProperty("code", "rejected").toString() + "): " + message.getProperty("message", "").toString();
        status.commandSeverity = 2;
        command.reset();
    }
    else { sendStatus("error", "invalidMessage", "LifecycleResult outcome must be accepted, applied or rejected.", true); return; }
    ++status.serial;
    statusDirty = true;
    triggerAsyncUpdate();
}

void MotionLink::writeDiscoveryFile(bool present)
{
    const auto folder = juce::File::getSpecialLocation(juce::File::windowsLocalAppData).getChildFile("DjehutiCAD").getChildFile("MotionEndpoints");
    juce::String name, modelId, documentName;
    { const Lock guard(lock); name = pipeName; modelId = contract.modelId; documentName = contract.documentName; }
    if (name.isEmpty()) return;
    const auto file = folder.getChildFile(name + ".json");
    if (!present) { file.deleteFile(); return; }
    folder.createDirectory();
    file.replaceWithText(juce::JSON::toString(makeObject({ { "protocol", "djehuti.motion" }, { "protocolVersion", kProtocolVersion }, { "pipeName", name },
                                                           { "pipePath", "\\\\.\\pipe\\" + name }, { "processId", (int) _getpid() }, { "modelId", modelId },
                                                           { "documentName", documentName }, { "startedUtc", juce::Time::getCurrentTime().toISO8601(true) } }), false));
}
}
