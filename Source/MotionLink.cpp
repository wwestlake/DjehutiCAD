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

// 64-bit wire values (ticks, sequences) are decimal strings so JSON number precision never truncates them.
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
                                             { "problems", problemList }, { "limitWarnings", warningList }, { "mechanicalInterface", mechanicalInterface } }), true);
}

//==============================================================================
class MotionLink::Connection final : public juce::InterprocessConnection
{
public:
    explicit Connection(MotionLink& ownerIn) : juce::InterprocessConnection(false, kMagicHeader), owner(ownerIn) {}
    ~Connection() override { disconnect(-1, Notify::no); }
    void connectionMade() override {}
    void connectionLost() override { owner.connectionEnded(); }
    void messageReceived(const juce::MemoryBlock& message) override { owner.handleIncoming(message); }
private:
    MotionLink& owner;
};

// Writes leave the GUI and connection threads; the queue is bounded so a stalled peer cannot grow memory.
class MotionLink::Writer final : public juce::Thread
{
public:
    explicit Writer(Connection& c) : juce::Thread("DCAD motion writer"), connection(c) {}
    ~Writer() override { stopThread(4000); }
    bool push(const juce::String& text)
    {
        const std::lock_guard<std::mutex> guard(queueLock);
        if ((int) queue.size() >= kMaxQueuedOutgoing) return false;
        queue.push_back(text);
        wake.signal();
        return true;
    }
    void run() override
    {
        while (!threadShouldExit())
        {
            wake.wait(100);
            for (;;)
            {
                juce::String next;
                {
                    const std::lock_guard<std::mutex> guard(queueLock);
                    if (queue.empty()) break;
                    next = queue.front(); queue.pop_front();
                }
                if (threadShouldExit()) return;
                connection.sendMessage(juce::MemoryBlock(next.toRawUTF8(), next.getNumBytesAsUTF8()));
            }
        }
    }
private:
    Connection& connection;
    std::mutex queueLock;
    std::deque<juce::String> queue;
    juce::WaitableEvent wake;
};

//==============================================================================
MotionLink::MotionLink(std::shared_ptr<cad::CadCommandService> s, std::shared_ptr<cad::CadMotionRuntime> r) : service(std::move(s)), runtime(std::move(r))
{
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

LinkSnapshot MotionLink::snapshot() const { const Lock guard(lock); return status; }

bool MotionLink::startListening(juce::String& error)
{
    if (isListening()) return true;
    juce::Random random;
    const auto token = juce::String::toHexString(random.nextInt64()).paddedLeft('0', 16).substring(0, 12);
    const auto name = "DjehutiCAD.Motion." + juce::String(_getpid()) + "." + token;
    auto created = std::make_unique<Connection>(*this);
    if (!created->createPipe(name, -1, true)) { error = "Could not create the named pipe " + name + "."; return false; }
    connection = std::move(created);
    writer = std::make_unique<Writer>(*connection);
    writer->startThread();
    {
        const Lock guard(lock);
        pipeName = name;
        listening = true;
        resetSession();
        status.pipeName = name;
        status.pipePath = "\\\\.\\pipe\\" + name;
        setState(LinkState::listening, "Waiting for DEWB to connect to " + status.pipePath + ".");
    }
    writeDiscoveryFile(true);
    return true;
}

void MotionLink::stopListening()
{
    {
        const Lock guard(lock);
        if (!listening && connection == nullptr) return;
        listening = false;
    }
    if (writer != nullptr) writer->signalThreadShouldExit();
    if (connection != nullptr) connection->disconnect(-1, juce::InterprocessConnection::Notify::no);
    writer.reset();
    connection.reset();
    writeDiscoveryFile(false);
    const Lock guard(lock);
    resetSession();
    status.pipeName.clear(); status.pipePath.clear();
    setState(LinkState::stopped, "Motion link stopped. The last pose is held until you preview or reset.");
}

// Closing the pipe instance breaks the client's handle; a fresh instance is created for the next connection.
void MotionLink::disconnectPeer()
{
    if (!isListening()) return;
    if (writer != nullptr) writer->signalThreadShouldExit();
    if (connection != nullptr) connection->disconnect(-1, juce::InterprocessConnection::Notify::no);
    writer.reset();
    if (connection != nullptr && connection->createPipe(pipeName, -1, false))
    {
        writer = std::make_unique<Writer>(*connection);
        writer->startThread();
    }
    const Lock guard(lock);
    resetSession();
    setState(LinkState::disconnected, "Disconnected from DEWB; holding the last pose. Listening again on " + status.pipePath + ".");
}

void MotionLink::connectionEnded() { const Lock guard(lock); statusDirty = true; }

void MotionLink::resetSession()
{
    peerPresent = helloReceived = bindingAccepted = stateApplied = false;
    proposedProfileRevision = -1;
    catalog.clear(); signalInterfaceRevision.clear();
    acceptedSimulationId.clear(); lastSequenceText.clear();
    lastSequence = 0; lastTick = -1; lastMicrostep = 0;
    pending = {};
    status.peerName.clear(); status.simulationId.clear(); status.lifecycle.clear(); status.lastSequence.clear(); status.simTimePs.clear();
    status.acceptedProfileRevision = -1; status.timelineGeneration = 0;
    status.signals.clear(); status.limitWarnings.clear();
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
    if (writer == nullptr) return;
    if (!writer->push(juce::JSON::toString(message, true)))
    {
        status.problems.add("Outgoing message queue overflowed; DEWB is not reading. Disconnect and reconnect.");
        setState(LinkState::error, "DEWB stopped reading messages; the outgoing queue is full.", 2);
    }
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
    if (service->document().revision() != shownRevision) refreshContract();
    bool restart = false;
    {
        const Lock guard(lock);
        const auto now = nowMs();
        restart = listening && connection != nullptr && !connection->isConnected();
        if (peerPresent && now - lastHeardMs > (juce::uint32) kPeerTimeoutMs)
        {
            resetSession();
            setState(LinkState::disconnected, "No message from DEWB for " + juce::String(kPeerTimeoutMs / 1000) + " s; holding the last pose. Reconnect to resume.", 1);
        }
        if (peerPresent && proposedProfileRevision >= 0 && !bindingAccepted && now - proposalSentMs > (juce::uint32) kBindingTimeoutMs)
        {
            proposedProfileRevision = -1;
            sendStatus("error", "bindingTimeout", "DEWB did not answer the binding proposal.");
            setState(LinkState::error, "DEWB did not accept or reject the binding proposal within " + juce::String(kBindingTimeoutMs / 1000) + " s.", 2);
        }
        if (peerPresent && now - lastHeartbeatMs > (juce::uint32) kHeartbeatMs) { lastHeartbeatMs = now; sendStatus("info", "heartbeat", stateName(status.state)); }
    }
    if (restart)
    {
        // The pipe's read thread ended (client error or a malformed header); serve a fresh pipe instance.
        if (writer != nullptr) writer->signalThreadShouldExit();
        connection->disconnect(-1, juce::InterprocessConnection::Notify::no);
        writer.reset();
        if (connection->createPipe(pipeName, -1, false)) { writer = std::make_unique<Writer>(*connection); writer->startThread(); }
        const Lock guard(lock);
        const bool hadPeer = peerPresent;
        resetSession();
        if (hadPeer) setState(LinkState::disconnected, "The DEWB connection ended; holding the last pose. Listening again.", 1);
    }
    handleAsyncUpdate();
}

void MotionLink::handleAsyncUpdate()
{
    PendingState publish;
    bool simulationOwns = false, notify = false;
    {
        const Lock guard(lock);
        publish = pending; pending = {};
        simulationOwns = peerPresent && helloReceived;
        notify = statusDirty; statusDirty = false;
    }
    runtime->setDriver(simulationOwns ? cad::CadMotionRuntime::Driver::simulation : cad::CadMotionRuntime::Driver::preview);
    if (publish.valid)
    {
        runtime->applySimulationCoordinates(publish.coordinates);
        if (publish.synchronize)
        {
            const Lock guard(lock);
            if (bindingAccepted && publish.simulationId == acceptedSimulationId && publish.generation == status.timelineGeneration && publish.sequence == lastSequenceText)
            {
                stateApplied = true;
                send(makeObject({ { "type", "StateApplied" }, { "simulationId", publish.simulationId }, { "timelineGeneration", publish.generation },
                                  { "profileRevision", publish.profileRevision }, { "sequence", publish.sequence } }));
                setState(LinkState::synchronized, "Synchronized with DEWB; the pose shows the simulation state.");
                notify = true;
            }
        }
    }
    board->set(snapshot().toJson());
    if (notify && onStatusChanged) onStatusChanged();
}

//==============================================================================
// The mechanical interface and profile are rebuilt from the document on the message thread; any change to
// what DEWB validated invalidates the binding and starts revalidation.
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
    const bool changed = next.modelId != contract.modelId || next.interfaceRevision != contract.interfaceRevision || next.profileRevision != contract.profileRevision;
    contract = std::move(next);
    if (changed && helloReceived)
    {
        bindingAccepted = stateApplied = false;
        proposedProfileRevision = -1;
        status.acceptedProfileRevision = -1;
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
    if (!listening) return;
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
    // Every Hello starts a new session: earlier bindings, timelines and synchronization never carry over.
    resetSession();
    status.problems.clear();
    peerPresent = helloReceived = true;
    lastHeartbeatMs = nowMs();
    status.peerName = message.getProperty("application", "DEWB").toString() + " " + message.getProperty("applicationVersion", "").toString();
    juce::Array<juce::var> capabilities { "mechanicalInterface.v1", "bindingProposal.v1", "stateFrame.v1", "heartbeat.v1" };
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
    catalog = next;
    signalInterfaceRevision = message.getProperty("signalInterfaceRevision", "").toString();
    // A new signal interface always requires a fresh proposal, acceptance and synchronization.
    bindingAccepted = stateApplied = false;
    status.acceptedProfileRevision = -1;
    status.simulationId = simulationId;
    status.signals = next;
    status.peerName = status.peerName.isNotEmpty() ? status.peerName : juce::String("DEWB");
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
    bindingAccepted = true; stateApplied = false;
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
    proposedProfileRevision = -1; bindingAccepted = false;
    status.problems = reasons;
    setState(LinkState::error, "DEWB rejected the connection profile.", 2);
}

void MotionLink::handleState(const juce::var& message, bool synchronize)
{
    const juce::String kind = synchronize ? "SynchronizeState" : "StateFrame";
    auto reject = [&](const juce::String& code, const juce::String& text) { sendStatus("error", code, kind + " rejected: " + text, true); status.message = kind + " rejected: " + text; ++status.serial; statusDirty = true; triggerAsyncUpdate(); };
    if (!bindingAccepted) { reject("notBound", "no accepted binding profile."); return; }
    if (!synchronize && !stateApplied) { reject("synchronizeRequired", "send SynchronizeState and wait for StateApplied before streaming."); return; }
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
    if (synchronize && stateApplied && generation < status.timelineGeneration) { reject("staleTimeline", "timelineGeneration is older than the current timeline."); return; }
    if (!synchronize)
    {
        if (generation < status.timelineGeneration) { reject("staleTimeline", "frame belongs to a superseded timeline."); return; }
        if (generation > status.timelineGeneration) { reject("synchronizeRequired", "a new timeline must begin with SynchronizeState."); return; }
        if ((std::uint64_t) sequence <= lastSequence) { reject("staleSequence", "sequence must increase within a timeline."); return; }
        if (tick < lastTick || (tick == lastTick && microstep < lastMicrostep)) { reject("timeRegression", "(simTimePs, microstep) moved backwards within a timeline."); return; }
    }

    // Validate the whole frame before anything is published.
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
    lastSequence = (std::uint64_t) sequence; lastSequenceText = message.getProperty("sequence", "").toString();
    lastTick = tick; lastMicrostep = microstep;
    status.lastSequence = lastSequenceText; status.simTimePs = message.getProperty("simTimePs", "").toString(); status.lifecycle = lifecycle;
    if (synchronize) stateApplied = false;
    pending.valid = true;
    pending.synchronize = synchronize || (pending.valid && pending.synchronize && pending.generation == generation);
    pending.coordinates = coordinates; pending.simulationId = simulationId; pending.sequence = lastSequenceText;
    pending.generation = generation; pending.profileRevision = status.acceptedProfileRevision;
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
        resetSession();
        setState(LinkState::disconnected, "DEWB closed the session; holding the last pose.", 1);
        return;
    }
    const auto lifecycle = message.getProperty("state", "").toString();
    if (code == "heartbeat" || lifecycle.isEmpty()) return;
    if (stateApplied && (lifecycle == "paused" || lifecycle == "stopped")) { status.lifecycle = lifecycle; setState(LinkState::paused, lifecycle == "paused" ? "Simulation paused." : "Simulation stopped."); }
    else if (lifecycle == "faulted") { status.lifecycle = lifecycle; setState(LinkState::error, "DEWB reports a fault: " + message.getProperty("message", "").toString(), 2); }
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
