#include "FrustEngine.h"
#include "CadDocument.h"

#include <frust_plugin_host/FrustPluginHost.h>
#include <frust_plugin_host/FrustPluginHostSource.h>
#include <CompilerApi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>

namespace frust_engine
{
namespace
{
// The plugin host keeps its application identity and host-function table per
// process, so loads happen one at a time.
std::mutex& loadMutex()
{
    static std::mutex m;
    return m;
}

double millisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::string frustStringLiteral(const std::string& text)
{
    std::string literal = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            literal += '\\';
        literal += c;
    }
    return literal + "\"";
}

Diagnostic convert(const ::frust::Diagnostic& d, const std::string& unitName, int lineOffset)
{
    Diagnostic out;
    out.error = d.severity == ::frust::Diagnostic::Severity::Error;
    out.file = d.file.empty() && d.line > 0 ? unitName : d.file;
    out.line = d.line;
    if (out.file == unitName && out.line > lineOffset)
        out.line -= lineOffset;
    out.column = d.column;
    out.message = d.message;
    return out;
}

::frust::CompileRequest requestFor(const std::string& unitName, const std::string& source)
{
    ::frust::CompileRequest request;
    request.sources.push_back({ unitName, source });
    return request;
}

// Lines a running script logs; a script runs on one thread.
thread_local std::vector<std::string>* scriptLog = nullptr;

extern "C" std::int64_t djehuti_frust_log(const char* text)
{
    if (scriptLog != nullptr)
        std::cout << (text != nullptr ? text : "") << "\n";   // into the captured output, in order
    return 0;
}

// Math the generated audio DSP needs (Frust has no exp or log of its own).
extern "C" double djehuti_dsp_exp(double x) { return std::exp(x); }
extern "C" double djehuti_dsp_log(double x) { return std::log(x); }

// The Frust compiler emits a call to frust_dbg_tick(line, column) before every
// expression (its IDE debugger hook); without a definition no compiled unit
// links. This registration and the FRust runtime's own frust_dbg_tick (which
// calls g_frust_dbg_callback) both lead to the app's handler, the debugger;
// with none installed a tick does nothing.
std::atomic<TickHandler> tickHandler { nullptr };
std::mutex cadMutex;
std::weak_ptr<djehuti::cad::CadCommandService> cadService;
thread_local std::string cadReturn;
thread_local std::string cadError;

std::shared_ptr<djehuti::cad::CadCommandService> activeCadService()
{
    std::lock_guard<std::mutex> lock(cadMutex);
    return cadService.lock();
}

const char* returnCad(const juce::String& value)
{
    cadReturn = value.toStdString();
    return cadReturn.c_str();
}

void setCadError(const juce::String& error)
{
    cadError = error.toStdString();
}

extern "C" const char* djehuti_cad_create_box(double width, double depth, double height)
{
    if (auto service = activeCadService())
    {
        juce::String id, error;
        if (service->createBox(width, depth, height, id, error)) { setCadError({}); return returnCad(id); }
        setCadError(error);
    }
    else setCadError("No CAD document is attached.");
    return returnCad({});
}

extern "C" const char* djehuti_cad_create_cylinder(double radius, double height)
{
    if (auto service = activeCadService())
    {
        juce::String id, error;
        if (service->createCylinder(radius, height, id, error)) { setCadError({}); return returnCad(id); }
        setCadError(error);
    }
    else setCadError("No CAD document is attached.");
    return returnCad({});
}

extern "C" const char* djehuti_cad_create_sketch(const char* plane, double offset)
{
    if (auto service = activeCadService()) { juce::String id, error; if (service->createSketch(plane != nullptr ? plane : "XY", offset, id, error)) { setCadError({}); return returnCad(id); } setCadError(error); } else setCadError("No CAD document is attached."); return returnCad({});
}

extern "C" const char* djehuti_cad_sketch_line(const char* sketchId, double x1, double y1, double x2, double y2)
{
    if (auto service = activeCadService()) { juce::String id, error; if (service->addSketchLine(sketchId != nullptr ? sketchId : "", x1, y1, x2, y2, id, error)) { setCadError({}); return returnCad(id); } setCadError(error); } else setCadError("No CAD document is attached."); return returnCad({});
}

extern "C" const char* djehuti_cad_sketch_rectangle(const char* sketchId, double x, double y, double width, double height)
{
    if (auto service = activeCadService()) { juce::String id, error; if (service->addSketchRectangle(sketchId != nullptr ? sketchId : "", x, y, width, height, id, error)) { setCadError({}); return returnCad(id); } setCadError(error); } else setCadError("No CAD document is attached."); return returnCad({});
}

extern "C" const char* djehuti_cad_sketch_circle(const char* sketchId, double x, double y, double radius)
{
    if (auto service = activeCadService()) { juce::String id, error; if (service->addSketchCircle(sketchId != nullptr ? sketchId : "", x, y, radius, id, error)) { setCadError({}); return returnCad(id); } setCadError(error); } else setCadError("No CAD document is attached."); return returnCad({});
}

extern "C" std::int64_t djehuti_cad_sketch_dimension(const char* sketchId, const char* entityId, double value)
{
    if (auto service = activeCadService()) { juce::String error; const bool ok=service->setSketchEntityDimension(sketchId != nullptr ? sketchId : "", entityId != nullptr ? entityId : "", value, error); setCadError(error); return ok ? 1 : 0; } setCadError("No CAD document is attached."); return 0;
}

extern "C" std::int64_t djehuti_cad_sketch_position(const char* sketchId, const char* entityId, double x, double y)
{
    if (auto service = activeCadService()) { juce::String error; const bool ok = service->setSketchEntityPosition(sketchId != nullptr ? sketchId : "", entityId != nullptr ? entityId : "", x, y, error); setCadError(error); return ok ? 1 : 0; } setCadError("No CAD document is attached."); return 0;
}

extern "C" const char* djehuti_cad_sketch_constraint(const char* sketchId, const char* type, const char* first, const char* second, double value)
{
    if (auto service = activeCadService()) { juce::String id, error; if(service->addSketchConstraint(sketchId != nullptr ? sketchId : "", type != nullptr ? type : "", first != nullptr ? first : "", second != nullptr ? second : "", value, id, error)){setCadError({});return returnCad(id);}setCadError(error); } else setCadError("No CAD document is attached."); return returnCad({});
}

extern "C" const char* djehuti_cad_extrude(const char* sketchId, double depth, std::int64_t reverse)
{
    if (auto service = activeCadService()) { juce::String id, error; if(service->createExtrude(sketchId != nullptr ? sketchId : "", depth, reverse != 0, id, error)){setCadError({});return returnCad(id);}setCadError(error); } else setCadError("No CAD document is attached."); return returnCad({});
}

extern "C" const char* djehuti_cad_cut(const char* sketchId, const char* targetId, double depth, std::int64_t throughAll, std::int64_t reverse)
{
    if (auto service = activeCadService()) { juce::String id, error; if(service->createCut(sketchId != nullptr ? sketchId : "", targetId != nullptr ? targetId : "", depth, throughAll != 0, reverse != 0, id, error)){setCadError({});return returnCad(id);}setCadError(error); } else setCadError("No CAD document is attached."); return returnCad({});
}

extern "C" std::int64_t djehuti_cad_set_dimensions(const char* id, double width, double depth, double height, double radius)
{
    if (auto service = activeCadService())
    {
        juce::String error;
        const bool ok = service->setDimensions(id != nullptr ? id : "", width, depth, height, radius, error);
        setCadError(error);
        return ok ? 1 : 0;
    }
    setCadError("No CAD document is attached.");
    return 0;
}

extern "C" std::int64_t djehuti_cad_set_placement(const char* id, double x, double y, double z, double rx, double ry, double rz)
{
    if (auto service = activeCadService())
    {
        juce::String error;
        const bool ok = service->setPlacement(id != nullptr ? id : "", x, y, z, rx, ry, rz, error);
        setCadError(error);
        return ok ? 1 : 0;
    }
    setCadError("No CAD document is attached.");
    return 0;
}

extern "C" const char* djehuti_cad_boolean(const char* operation, const char* first, const char* second)
{
    if (auto service = activeCadService())
    {
        juce::String id, error;
        if (service->booleanOp(operation != nullptr ? operation : "", first != nullptr ? first : "", second != nullptr ? second : "", id, error)) { setCadError({}); return returnCad(id); }
        setCadError(error);
    }
    else setCadError("No CAD document is attached.");
    return returnCad({});
}

extern "C" const char* djehuti_cad_last_error()
{
    return cadError.c_str();
}

extern "C" void djehuti_frust_dbg_tick(int line, int column)
{
    if (auto* handler = tickHandler.load(std::memory_order_relaxed))
        handler(line, column);
}

// The script running on this thread, so a Stop from inside it can restore
// standard output and unload it (abandonRunningScript).
struct RunningScript
{
    FrustPluginHandle handle = nullptr;
    std::ostringstream* printed = nullptr;
    std::streambuf* consoleBuffer = nullptr;
};
thread_local RunningScript* runningScript = nullptr;

void ensureHostSetup()
{
    static std::once_flag once;
    std::call_once(once, [] {
        frust_plugin_host_set_application_identity("djehuti-electronics-lab");
        frust_plugin_register_host_function("frust_dbg_tick", reinterpret_cast<void*>(&djehuti_frust_dbg_tick));
        frust_plugin_register_host_function("djehuti_frust_log", reinterpret_cast<void*>(&djehuti_frust_log));
        frust_plugin_register_host_function("djehuti_dsp_exp", reinterpret_cast<void*>(&djehuti_dsp_exp));
        frust_plugin_register_host_function("djehuti_dsp_log", reinterpret_cast<void*>(&djehuti_dsp_log));
        frust_plugin_register_host_function("djehuti_cad_create_box", reinterpret_cast<void*>(&djehuti_cad_create_box));
        frust_plugin_register_host_function("djehuti_cad_create_cylinder", reinterpret_cast<void*>(&djehuti_cad_create_cylinder));
        frust_plugin_register_host_function("djehuti_cad_create_sketch", reinterpret_cast<void*>(&djehuti_cad_create_sketch));
        frust_plugin_register_host_function("djehuti_cad_sketch_line", reinterpret_cast<void*>(&djehuti_cad_sketch_line));
        frust_plugin_register_host_function("djehuti_cad_sketch_rectangle", reinterpret_cast<void*>(&djehuti_cad_sketch_rectangle));
        frust_plugin_register_host_function("djehuti_cad_sketch_circle", reinterpret_cast<void*>(&djehuti_cad_sketch_circle));
        frust_plugin_register_host_function("djehuti_cad_sketch_dimension", reinterpret_cast<void*>(&djehuti_cad_sketch_dimension));
        frust_plugin_register_host_function("djehuti_cad_sketch_position", reinterpret_cast<void*>(&djehuti_cad_sketch_position));
        frust_plugin_register_host_function("djehuti_cad_sketch_constraint", reinterpret_cast<void*>(&djehuti_cad_sketch_constraint));
        frust_plugin_register_host_function("djehuti_cad_extrude", reinterpret_cast<void*>(&djehuti_cad_extrude));
        frust_plugin_register_host_function("djehuti_cad_cut", reinterpret_cast<void*>(&djehuti_cad_cut));
        frust_plugin_register_host_function("djehuti_cad_set_dimensions", reinterpret_cast<void*>(&djehuti_cad_set_dimensions));
        frust_plugin_register_host_function("djehuti_cad_set_placement", reinterpret_cast<void*>(&djehuti_cad_set_placement));
        frust_plugin_register_host_function("djehuti_cad_boolean", reinterpret_cast<void*>(&djehuti_cad_boolean));
        frust_plugin_register_host_function("djehuti_cad_last_error", reinterpret_cast<void*>(&djehuti_cad_last_error));
    });
}

Result checkWithOffset(const std::string& unitName, const std::string& source, int lineOffset)
{
    Result result;
    auto request = requestFor(unitName, source);
    request.emitObject = false;
    const auto start = std::chrono::steady_clock::now();
    const auto compiled = ::frust::Compile(request);
    result.compileMs = millisecondsSince(start);
    for (const auto& d : compiled.diagnostics)
        result.diagnostics.push_back(convert(d, unitName, lineOffset));
    result.compiled = compiled.ok;
    result.ok = compiled.ok;
    return result;
}
}

std::string Diagnostic::text() const
{
    std::string s = file.empty() ? std::string("frust") : file;
    if (line > 0) s += ":" + std::to_string(line);
    if (column > 0) s += ":" + std::to_string(column);
    return s + ": " + (error ? "error: " : "warning: ") + message;
}

std::string Result::report() const
{
    std::string s;
    for (const auto& d : diagnostics)
        s += d.text() + "\n";
    if (!error.empty())
        s += error + "\n";
    return s;
}

std::string manifestLine(const std::string& name, const std::string& description, const std::vector<std::string>& hostFunctions)
{
    std::string id;
    for (const char c : name)
        id += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') ? c : '_';
    std::string required;
    for (const auto& fn : hostFunctions)
        required += std::string(required.empty() ? "" : ",") + "{\"name\":\"" + fn + "\"}";
    const std::string json = "{\"name\":\"" + (id.empty() ? std::string("unit") : id) + "\",\"version\":\"0.1.0\",\"description\":"
                           + frustStringLiteral(description)
                           + (required.empty() ? std::string() : ",\"requiredHostFunctions\":[" + required + "]") + "}";
    return "manifest " + frustStringLiteral(json) + ";\n";
}

Result check(const std::string& unitName, const std::string& source)
{
    return checkWithOffset(unitName, source, 0);
}

Engine::Engine()
{
    ensureHostSetup();
}

Engine::~Engine()
{
    unloadAll();
}

Result Engine::load(const std::string& key, const std::string& source)
{
    const auto unitName = key + ".frust";
    auto result = checkWithOffset(unitName, source, 0);
    if (!result.ok)
        return result;
    const auto start = std::chrono::steady_clock::now();
    FrustPluginHandle handle = nullptr;
    {
        std::lock_guard<std::mutex> loading(loadMutex());
        handle = frust_plugin_host::loadFromSource(requestFor(unitName, source));
        if (handle == nullptr)
        {
            const char* why = frust_plugin_last_error();
            result.error = why != nullptr && *why != '\0' ? why : "The Frust unit compiled but could not be loaded.";
        }
    }
    result.compileMs += millisecondsSince(start);
    if (handle == nullptr)
    {
        result.ok = false;
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (auto found = units.find(key); found != units.end())
        frust_plugin_unload(found->second);
    units[key] = handle;
    return result;
}

void* Engine::function(const std::string& key, const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = units.find(key);
    return found != units.end() ? frust_plugin_get_fn(found->second, name.c_str()) : nullptr;
}

void Engine::unload(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (auto found = units.find(key); found != units.end())
    {
        frust_plugin_unload(found->second);
        units.erase(found);
    }
}

void Engine::unloadAll()
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [key, handle] : units)
        frust_plugin_unload(handle);
    units.clear();
}

bool Engine::isLoaded(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return units.count(key) != 0;
}

void setTickHandler(TickHandler handler)
{
    tickHandler.store(handler);
}

std::string abandonRunningScript()
{
    auto* running = runningScript;
    if (running == nullptr)
        return {};
    runningScript = nullptr;
    scriptLog = nullptr;
    std::string printed;
    if (running->printed != nullptr)
    {
        std::cout.rdbuf(running->consoleBuffer);
        printed = running->printed->str();
    }
    if (running->handle != nullptr)
        frust_plugin_unload(running->handle);
    return printed;
}

Result runScript(const std::string& script)
{
    return runScript(script, RunOptions {});
}

Result runScript(const std::string& script, const RunOptions& options)
{
    ensureHostSetup();
    const std::string unitName = "script.frust";
    std::vector<std::string> hostFunctions { "djehuti_frust_log" };
    hostFunctions.insert(hostFunctions.end(), cadHostFunctions().begin(), cadHostFunctions().end());
    hostFunctions.insert(hostFunctions.end(), options.hostFunctions.begin(), options.hostFunctions.end());
    const std::string header = manifestLine("electronics_lab_script", "A script run in Djehuti Electronics Lab.", hostFunctions)
                             + "extern fn djehuti_frust_log(text: String) -> i64;\n"
                               "pub fn print_line(text: String) -> i64 = { djehuti_frust_log(text) }\n"
                             + options.prelude;
    const int headerLines = (int)std::count(header.begin(), header.end(), '\n');
    const auto source = header + script;

    auto result = checkWithOffset(unitName, source, headerLines);
    if (!result.ok)
        return result;

    FrustPluginHandle handle = nullptr;
    const auto start = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> loading(loadMutex());
        handle = frust_plugin_host::loadFromSource(requestFor(unitName, source));
        if (handle == nullptr)
        {
            const char* why = frust_plugin_last_error();
            result.error = why != nullptr && *why != '\0' ? why : "The script compiled but could not be loaded.";
        }
    }
    result.compileMs += millisecondsSince(start);
    if (handle == nullptr)
    {
        result.ok = false;
        return result;
    }

    using RunFn = const char* (*)();
    auto* run = reinterpret_cast<RunFn>(frust_plugin_get_fn(handle, "run"));
    if (run == nullptr)
    {
        result.ok = false;
        result.error = "The script has no `pub fn run() -> String`; define it, and what it returns is reported.";
        frust_plugin_unload(handle);
        return result;
    }
    // The FRust runtime prints (frust_print_str, which node programs' Print
    // nodes call) to standard output; while the script runs that is captured,
    // with its print_line lines in order, as the script's output.
    if (options.shouldRun != nullptr && !options.shouldRun())
    {
        frust_plugin_unload(handle);
        result.ok = false;
        result.error = "Stopped before the script ran.";
        return result;
    }
    std::vector<std::string> lines;
    std::ostringstream printed;
    if (options.beforeRun != nullptr)
        options.beforeRun(headerLines);
    RunningScript running;
    running.handle = handle;
    running.printed = &printed;
    running.consoleBuffer = std::cout.rdbuf(printed.rdbuf());
    runningScript = &running;
    scriptLog = &lines;
    const auto runStart = std::chrono::steady_clock::now();
    const char* returned = run();
    result.runMs = millisecondsSince(runStart);
    scriptLog = nullptr;
    runningScript = nullptr;
    std::cout.rdbuf(running.consoleBuffer);
    result.output += printed.str();
    if (returned != nullptr)
        result.output += returned;
    frust_plugin_unload(handle);
    result.ok = true;
    return result;
}

void setCadCommandService(std::shared_ptr<djehuti::cad::CadCommandService> service)
{
    std::lock_guard<std::mutex> lock(cadMutex);
    cadService = std::move(service);
}

std::string cadPrelude()
{
    return "extern fn djehuti_cad_create_box(width: f64, depth: f64, height: f64) -> String;\n"
           "extern fn djehuti_cad_create_cylinder(radius: f64, height: f64) -> String;\n"
           "extern fn djehuti_cad_create_sketch(plane: String, offset: f64) -> String;\n"
           "extern fn djehuti_cad_sketch_line(sketch: String, x1: f64, y1: f64, x2: f64, y2: f64) -> String;\n"
           "extern fn djehuti_cad_sketch_rectangle(sketch: String, x: f64, y: f64, width: f64, height: f64) -> String;\n"
           "extern fn djehuti_cad_sketch_circle(sketch: String, x: f64, y: f64, radius: f64) -> String;\n"
           "extern fn djehuti_cad_sketch_dimension(sketch: String, entity: String, value: f64) -> i64;\n"
           "extern fn djehuti_cad_sketch_position(sketch: String, entity: String, x: f64, y: f64) -> i64;\n"
           "extern fn djehuti_cad_sketch_constraint(sketch: String, type: String, first: String, second: String, value: f64) -> String;\n"
           "extern fn djehuti_cad_extrude(sketch: String, depth: f64, reverse: i64) -> String;\n"
           "extern fn djehuti_cad_cut(sketch: String, target: String, depth: f64, through_all: i64, reverse: i64) -> String;\n"
           "extern fn djehuti_cad_set_dimensions(id: String, width: f64, depth: f64, height: f64, radius: f64) -> i64;\n"
           "extern fn djehuti_cad_set_placement(id: String, x: f64, y: f64, z: f64, rx: f64, ry: f64, rz: f64) -> i64;\n"
           "extern fn djehuti_cad_boolean(operation: String, first: String, second: String) -> String;\n"
           "extern fn djehuti_cad_last_error() -> String;\n";
}

const std::vector<std::string>& cadHostFunctions()
{
    static const std::vector<std::string> names {
        "djehuti_cad_create_box", "djehuti_cad_create_cylinder", "djehuti_cad_create_sketch", "djehuti_cad_sketch_line", "djehuti_cad_sketch_rectangle", "djehuti_cad_sketch_circle", "djehuti_cad_sketch_dimension", "djehuti_cad_sketch_position", "djehuti_cad_sketch_constraint", "djehuti_cad_extrude", "djehuti_cad_cut", "djehuti_cad_set_dimensions",
        "djehuti_cad_set_placement", "djehuti_cad_boolean", "djehuti_cad_last_error"
    };
    return names;
}
}
