#include "LocalAgentApi.h"
#include <map>
#include <mutex>
#include <string>

namespace
{
juce::var failure(const juce::String& message)
{
    auto* o = new juce::DynamicObject(); o->setProperty("ok", false); o->setProperty("error", message); return o;
}
constexpr size_t maxBytes = 1024 * 1024;
}

struct LocalAgentApi::State
{
    struct Request
    {
        juce::String id, name, arguments, status { "queued" };
        juce::var result;
        double durationMs = 0;
        juce::WaitableEvent done { true };
    };
    std::mutex mutex;
    bool active = true;
    std::map<juce::String, std::shared_ptr<Request>> requests;
    juce::StringArray order;
};

LocalAgentApi::LocalAgentApi(ToolHandler h, juce::var c)
    : juce::Thread("DjehutiCAD Local Agent API"), handler(std::move(h)), catalogue(std::move(c)), state(std::make_shared<State>()) {}
LocalAgentApi::~LocalAgentApi() { stop(); }
juce::File LocalAgentApi::getDiscoveryFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiCAD").getChildFile("agent-api.json");
}
bool LocalAgentApi::start()
{
    if (isThreadRunning()) return true;
    if (!listener.createListener(0, "127.0.0.1")) return false;
    state = std::make_shared<State>();
    token = juce::Uuid().toString() + juce::Uuid().toString();
    auto* o = new juce::DynamicObject();
    o->setProperty("schema", "djehuti-cad-agent-api"); o->setProperty("version", 1);
    o->setProperty("baseUrl", "http://127.0.0.1:" + juce::String(listener.getBoundPort()));
    o->setProperty("token", token); o->setProperty("transport", "http");
    o->setProperty("executable", juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());
    o->setProperty("instance", juce::Uuid().toString());
    auto file = getDiscoveryFile();
    if (!file.getParentDirectory().createDirectory().wasOk() || !file.replaceWithText(juce::JSON::toString(juce::var(o))))
    { listener.close(); return false; }
    worker = std::make_unique<juce::ThreadPool>(1);   // one request at a time, in submission order
    startThread(); return true;
}
void LocalAgentApi::stop()
{
    { std::lock_guard lock(state->mutex); state->active = false; }
    signalThreadShouldExit(); listener.close();
    stopThread(3000); // Every socket wait below is bounded and observes threadShouldExit().
    // A running request stops waiting for the UI once state->active is false (see keepWaiting), so this cannot
    // deadlock when stop() is called on the message thread; queued requests are dropped.
    if (worker != nullptr) { worker->removeAllJobs(true, 10000); worker.reset(); }
    auto file = getDiscoveryFile();
    if (token.isNotEmpty() && juce::JSON::parse(file.loadFileAsString()).getProperty("token", {}).toString() == token)
        file.deleteFile(); // Another process's discovery file is never removed.
}
void LocalAgentApi::run()
{
    while (!threadShouldExit())
    {
        if (listener.waitUntilReady(true, 100) <= 0) continue;
        std::unique_ptr<juce::StreamingSocket> socket(listener.waitForNextConnection());
        if (socket && !threadShouldExit()) handleConnection(*socket);
    }
}
bool LocalAgentApi::writeJson(juce::StreamingSocket& socket, int status, const juce::var& body)
{
    const auto json = juce::JSON::toString(body, true);
    const juce::String reason = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 401 ? "Unauthorized" : status == 404 ? "Not Found" : status == 409 ? "Conflict" : status == 429 ? "Too Many Requests" : "Bad Request";
    const auto response = "HTTP/1.1 " + juce::String(status) + " " + reason + "\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
        + juce::String(json.getNumBytesAsUTF8()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + json;
    const auto data = response.toUTF8(); const int total = (int) data.sizeInBytes() - 1;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
    for (int sent = 0; sent < total && !threadShouldExit();)
    {
        if (juce::Time::getMillisecondCounterHiRes() > deadline) return false;
        if (socket.waitUntilReady(false, 50) <= 0) continue;
        const auto n = socket.write(data.getAddress() + sent, juce::jmin(4096, total - sent));
        if (n <= 0) return false;
        sent += n;
        if (sent == total) return true;
    }
    return false;
}
void LocalAgentApi::handleConnection(juce::StreamingSocket& socket)
{
    std::string bytes; size_t headerEnd = std::string::npos, expected = 0;
    juce::String method, path, authorization, origin;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
    while (!threadShouldExit() && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (socket.waitUntilReady(true, 50) <= 0) continue;
        char buffer[4096]; const auto n = socket.read(buffer, sizeof(buffer), false);
        if (n <= 0) return;
        bytes.append(buffer, (size_t)n);
        if (bytes.size() > maxBytes) { writeJson(socket, 400, failure("Request exceeds 1 MiB.")); return; }
        if (headerEnd == std::string::npos)
        {
            headerEnd = bytes.find("\r\n\r\n");
            if (headerEnd == std::string::npos) { if (bytes.size() > 16384) break; continue; }
            if (headerEnd > 16384) break;
            auto lines = juce::StringArray::fromLines(juce::String::fromUTF8(bytes.data(), (int)headerEnd));
            auto first = juce::StringArray::fromTokens(lines[0], " ", "");
            if (first.size() != 3) break;
            method = first[0]; path = first[1];
            size_t contentLength = 0; bool hasLength = false, bad = false;
            for (int i = 1; i < lines.size(); ++i)
            {
                const auto key = lines[i].upToFirstOccurrenceOf(":", false, false).trim().toLowerCase();
                const auto value = lines[i].fromFirstOccurrenceOf(":", false, false).trim();
                if (key == "authorization") authorization = value;
                else if (key == "origin") origin = value;
                else if (key == "transfer-encoding") bad = true;
                else if (key == "content-length")
                {
                    if (hasLength || value.isEmpty() || value.length() > 7 || !value.containsOnly("0123456789")) bad = true;
                    hasLength = true; contentLength = (size_t)value.getLargeIntValue();
                }
            }
            expected = headerEnd + 4 + contentLength;
            if (bad || expected > maxBytes || (method == "POST" && !hasLength))
            { writeJson(socket, 400, failure("Invalid length or unsupported transfer encoding.")); return; }
            // Native clients only. Do not enable CORS or accept browser-origin requests.
            if (origin.isNotEmpty() || authorization != "Bearer " + token) { writeJson(socket, 401, failure("A valid local bearer token is required; browser origins are not accepted.")); return; }
        }
        if (headerEnd != std::string::npos && bytes.size() >= expected) break;
    }
    if (threadShouldExit()) return;
    if (headerEnd == std::string::npos || expected == 0 || bytes.size() != expected || authorization != "Bearer " + token)
    { writeJson(socket, 400, failure("Invalid or incomplete HTTP request.")); return; }

    if (method == "GET" && path == "/v1/status")
    {
        auto* o = new juce::DynamicObject(); o->setProperty("status", "ready"); o->setProperty("service", "DjehutiCAD Agent API");
        o->setProperty("version", 1); writeJson(socket, 200, o); return;
    }
    if (method == "GET" && path == "/v1/tools") { writeJson(socket, 200, catalogue); return; }
    std::shared_ptr<State::Request> request;
    if (method == "POST" && path.startsWith("/v1/requests/") && path.endsWith("/cancel"))
    {
        // Only work that has not started can be cancelled: a started step runs to completion on the UI thread.
        std::lock_guard lock(state->mutex);
        auto found = state->requests.find(path.substring(13).dropLastCharacters(7));
        if (found == state->requests.end()) { writeJson(socket, 404, failure("Unknown or expired request id.")); return; }
        request = found->second;
        if (request->status == "queued")
        {
            auto* result = new juce::DynamicObject(); result->setProperty("ok", false); result->setProperty("cancelled", true);
            result->setProperty("error", "Cancelled before it started; nothing was changed.");
            request->result = result; request->status = "cancelled"; request->done.signal();
        }
        else
        {
            const auto reply = failure(request->status == "running" ? "The request is already running; a started step finishes atomically on the UI thread and cannot be cancelled."
                                                                    : "The request has already finished.");
            reply.getDynamicObject()->setProperty("requestId", request->id); reply.getDynamicObject()->setProperty("status", request->status);
            writeJson(socket, 409, reply); return;
        }
    }
    else if (method == "GET" && path.startsWith("/v1/requests/"))
    {
        std::lock_guard lock(state->mutex);
        auto found = state->requests.find(path.substring(13));
        if (found != state->requests.end()) request = found->second;
    }
    else if ((method == "POST" && path == "/v1/tools/call") || (method == "GET" && path == "/v1/state"))
    {
        juce::var parsed;
        if (method == "GET") { auto* o = new juce::DynamicObject(); o->setProperty("name", "cad_ui"); o->setProperty("arguments", juce::var(new juce::DynamicObject())); parsed = o; }
        else if (!juce::JSON::parse(juce::String::fromUTF8(bytes.data() + headerEnd + 4, (int)(expected - headerEnd - 4)), parsed).wasOk())
        { writeJson(socket, 400, failure("Invalid JSON.")); return; }
        const auto name = parsed.getProperty("name", {}).toString();
        auto args = parsed.getProperty("arguments", juce::var(new juce::DynamicObject()));
        bool known = false;
        if (const auto* list = catalogue.getArray()) for (const auto& t : *list) if (t.getProperty("name", {}).toString() == name) known = true;
        if (!parsed.isObject() || !args.isObject() || !known) { writeJson(socket, 400, failure("Expected a registered tool name and an arguments object. GET /v1/tools lists tools.")); return; }
        auto id = parsed.getProperty("requestId", {}).toString();
        if (id.isEmpty()) id = juce::Uuid().toString();
        if (id.length() > 128 || !id.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")) { writeJson(socket, 400, failure("Invalid requestId.")); return; }
        const auto arguments = juce::JSON::toString(args, true);
        bool submit = false;
        {
            std::lock_guard lock(state->mutex);
            auto found = state->requests.find(id);
            if (found != state->requests.end())
            {
                request = found->second;
                if (request->name != name || request->arguments != arguments) { writeJson(socket, 409, failure("requestId already belongs to different arguments.")); return; }
            }
            else
            {
                int pending = 0;
                for (const auto& entry : state->requests) if (entry.second->status == "queued" || entry.second->status == "running") ++pending;
                if (pending >= 16) { writeJson(socket, 429, failure("Command queue is full.")); return; }
                // Retain at most 256 results; never discard queued or running work.
                for (int i = 0; state->requests.size() >= 256 && i < state->order.size();)
                {
                    auto old = state->requests.find(state->order[i]);
                    if (old != state->requests.end() && old->second->status != "queued" && old->second->status != "running") { state->requests.erase(old); state->order.remove(i); }
                    else ++i;
                }
                request = std::make_shared<State::Request>(); request->id = id; request->name = name; request->arguments = arguments;
                state->requests[id] = request; state->order.add(id); submit = true;
            }
        }
        if (submit)
        {
            auto shared = state; auto call = handler;
            worker->addJob([shared, request, call, args]
            {
                { std::lock_guard lock(shared->mutex); if (!shared->active || request->status != "queued") return; request->status = "running"; }
                const auto started = juce::Time::getMillisecondCounterHiRes();
                const auto keepWaiting = [shared] { std::lock_guard lock(shared->mutex); return shared->active; };
                juce::var result;
                try { result = call(request->name, args, keepWaiting); }
                catch (const std::exception& e) { result = failure(e.what()); }
                catch (...) { result = failure("CAD command threw an exception."); }
                if (!result.isObject()) result = failure("CAD command returned no structured result.");
                { std::lock_guard lock(shared->mutex); request->result = result; request->status = (bool)result.getProperty("ok", false) ? "completed" : "failed"; request->durationMs = juce::Time::getMillisecondCounterHiRes() - started; }
                request->done.signal();
            });
        }
        request->done.wait(1500); // Slower operations answer 202 and remain queryable by id; never time out and silently replay them.
    }
    if (!request) { writeJson(socket, 404, failure("Unknown endpoint or expired request id.")); return; }
    auto* reply = new juce::DynamicObject(); int code = 202;
    {
        std::lock_guard lock(state->mutex);
        reply->setProperty("requestId", request->id); reply->setProperty("tool", request->name); reply->setProperty("status", request->status);
        reply->setProperty("durationMs", request->durationMs);
        if (request->status == "completed" || request->status == "failed" || request->status == "cancelled") { reply->setProperty("result", request->result); code = request->status == "failed" ? 409 : 200; }
    }
    writeJson(socket, code, reply);
}
