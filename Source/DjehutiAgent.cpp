#include "DjehutiAgent.h"
#include "ProtectedCredentialStore.h"
#include <ai_provider/OpenAiProvider.h>

namespace
{
juce::String setting(juce::ApplicationProperties& p, const char* name, const char* fallback)
{
    return p.getUserSettings()->getValue(name, fallback);
}
std::string credentialName(const juce::String& provider, const juce::String& profile)
{
    const auto raw = provider + "/" + profile;
    juce::String safe;
    for (int i = 0; i < raw.length(); ++i)
    {
        const auto c = raw[i];
        safe << (juce::String("\\/:*?\"<>| ").containsChar(c) ? '_' : c);
    }
    return "agent/" + safe.toStdString();
}
std::vector<ai_provider::ToolDefinition> tools()
{
    const auto emptyObject = R"({"type":"object","properties":{},"additionalProperties":false})";
    const auto dimensions = R"({"type":"object","properties":{"width":{"type":"number"},"depth":{"type":"number"},"height":{"type":"number"}},"required":["width","depth","height"],"additionalProperties":false})";
    const auto featureDimensions = R"({"type":"object","properties":{"id":{"type":"string"},"width":{"type":"number"},"depth":{"type":"number"},"height":{"type":"number"},"radius":{"type":"number"}},"required":["id","width","depth","height"],"additionalProperties":false})";
    const auto cylinder = R"({"type":"object","properties":{"radius":{"type":"number"},"height":{"type":"number"}},"required":["radius","height"],"additionalProperties":false})";
    const auto placement = R"({"type":"object","properties":{"id":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"rx":{"type":"number"},"ry":{"type":"number"},"rz":{"type":"number"}},"required":["id","x","y","z"],"additionalProperties":false})";
    const auto booleanArgs = R"({"type":"object","properties":{"operation":{"type":"string","enum":["union","subtract","intersect"]},"first":{"type":"string"},"second":{"type":"string"}},"required":["operation","first","second"],"additionalProperties":false})";
    const auto sketch = R"({"type":"object","properties":{"plane":{"type":"string","enum":["XY","XZ","YZ"]},"offset":{"type":"number"}},"required":["plane"],"additionalProperties":false})";
    const auto sketchLine = R"({"type":"object","properties":{"sketch":{"type":"string"},"x1":{"type":"number"},"y1":{"type":"number"},"x2":{"type":"number"},"y2":{"type":"number"}},"required":["sketch","x1","y1","x2","y2"],"additionalProperties":false})";
    const auto sketchRectangle = R"({"type":"object","properties":{"sketch":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"width":{"type":"number"},"height":{"type":"number"}},"required":["sketch","x","y","width","height"],"additionalProperties":false})";
    const auto sketchCircle = R"({"type":"object","properties":{"sketch":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"radius":{"type":"number"}},"required":["sketch","x","y","radius"],"additionalProperties":false})";
    const auto sketchDimension = R"({"type":"object","properties":{"sketch":{"type":"string"},"entity":{"type":"string"},"value":{"type":"number"}},"required":["sketch","entity","value"],"additionalProperties":false})";
    const auto sketchPosition = R"({"type":"object","properties":{"sketch":{"type":"string"},"entity":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"}},"required":["sketch","entity","x","y"],"additionalProperties":false})";
    const auto sketchConstraint = R"({"type":"object","properties":{"sketch":{"type":"string"},"type":{"type":"string"},"first":{"type":"string"},"second":{"type":"string"},"value":{"type":"number"}},"required":["sketch","type","first"],"additionalProperties":false})";
    const auto extrude = R"({"type":"object","properties":{"sketch":{"type":"string"},"depth":{"type":"number"},"reverse":{"type":"boolean"}},"required":["sketch","depth"],"additionalProperties":false})";
    const auto instance = R"({"type":"object","properties":{"feature":{"type":"string"},"name":{"type":"string"}},"required":["feature"],"additionalProperties":false})";
    const auto joint = R"({"type":"object","properties":{"joint":{"type":"string"},"name":{"type":"string"},"type":{"type":"string","enum":["fixed","revolute","prismatic"]},"parent":{"type":"string"},"child":{"type":"string"},"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"axis":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"reference":{"type":"number"},"lower":{"type":"number"},"upper":{"type":"number"}},"required":["type","child","origin","axis"],"additionalProperties":false})";
    const auto mapping = R"({"type":"object","properties":{"joint":{"type":"string"},"signal":{"type":"string"},"quantity":{"type":"string","enum":["angle","angular_velocity","position","linear_velocity"]},"unit":{"type":"string"},"scale":{"type":"number"},"offset":{"type":"number"},"initialCoordinate":{"type":"number"}},"required":["joint","signal","quantity","unit"],"additionalProperties":false})";
    const auto preview = R"({"type":"object","properties":{"joint":{"type":"string"},"value":{"type":"number"},"reset":{"type":"boolean"}},"additionalProperties":false})";
    const auto lifecycle = R"({"type":"object","properties":{"command":{"type":"string","enum":["play","start","pause","resume","stop","reset","step","retry"]},"stepMs":{"type":"number"}},"required":["command"],"additionalProperties":false})";
    const auto listen = R"({"type":"object","properties":{"listen":{"type":"boolean"}},"required":["listen"],"additionalProperties":false})";
    const auto cut = R"({"type":"object","properties":{"sketch":{"type":"string"},"target":{"type":"string"},"depth":{"type":"number"},"throughAll":{"type":"boolean"},"reverse":{"type":"boolean"}},"required":["sketch","target"],"additionalProperties":false})";
    return {
        { "open_cad_workspace", "Open the CAD workspace.", emptyObject },
        { "open_frust_editor", "Open the FRust editor.", emptyObject },
        { "open_node_editor", "Open the node editor.", emptyObject },
        { "open_3d_views", "Open the 3D views workspace and add a view.", emptyObject },
        { "cad_create_box", "Create a dimensioned box in millimeters and return its managed feature id.", dimensions },
        { "cad_create_cylinder", "Create a cylinder in millimeters and return its managed feature id.", cylinder },
        { "cad_create_sketch", "Create an editable XY, XZ, or YZ planar sketch in millimeters.", sketch },
        { "cad_sketch_line", "Add a line entity to a managed sketch.", sketchLine },
        { "cad_sketch_rectangle", "Add a connected dimensioned rectangle to a managed sketch.", sketchRectangle },
        { "cad_sketch_circle", "Add a dimensioned circle to a managed sketch.", sketchCircle },
        { "cad_sketch_dimension", "Change a sketch line length or circle radius in millimeters.", sketchDimension },
        { "cad_sketch_position", "Move a sketch entity start point or circle center in millimeters.", sketchPosition },
        { "cad_sketch_constraint", "Add a supported sketch constraint: coincident, horizontal, vertical, fixed, length, distance, radius, or diameter.", sketchConstraint },
        { "cad_extrude", "Extrude a closed sketch profile into a real solid.", extrude },
        { "cad_cut", "Cut a closed sketch profile from an explicit solid target; support finite depth or through-all.", cut },
        { "cad_set_dimensions", "Change a managed CAD feature's dimensions in millimeters.", featureDimensions },
        { "cad_set_placement", "Set a feature's millimeter position and optional Euler rotations in degrees.", placement },
        { "cad_boolean", "Create a union, subtract, or intersect feature from two distinct solid body ids. For subtract, first is the target body to keep and second is the cutting tool. Both sources are hidden and stay in the feature history; undo restores them.", booleanArgs },
        { "cad_undo", "Undo the most recent CAD document command.", emptyObject },
        { "cad_create_instance", "Create a movable assembly instance of a solid body feature id. Instances keep their own stable id; the first instance appears where the body is modelled.", instance },
        { "cad_create_joint", "Create a joint between assembly instances. parent is an instance id or 'world'; origin is the joint origin in world millimetres and axis the world direction of the joint +Z motion axis at the reference pose. Revolute coordinates are radians, prismatic metres; lower/upper set engineering limits.", joint },
        { "cad_update_joint", "Update an existing joint (field 'joint') with the same fields as cad_create_joint; the child keeps its current reference position.", joint },
        { "cad_set_joint_mapping", "Map a DEWB signal to a joint in the connection profile: quantity angle/angular_velocity (revolute) or position/linear_velocity (prismatic), unit e.g. rad, deg, rev, rad/s, rpm, m, mm, m/s; scale is the gear ratio, offset applies to state signals, initialCoordinate to velocity signals.", mapping },
        { "cad_motion_preview", "Preview a joint coordinate (radians or metres) without changing the document, or reset all joints with reset=true. Not allowed while DEWB drives the joints.", preview },
        { "cad_motion_lifecycle", "Ask the connected DEWB simulator to change its run state: start, pause, resume, stop (end the run, keep the final state), reset (new timeline at the initial state), step (advance stepMs milliseconds of simulation time and stay paused), play (start or resume by the confirmed state), or retry (resend the unconfirmed request with the same id). DEWB must advertise lifecycle control; check cad_motion_status for the confirmed state and result.", lifecycle },
        { "cad_motion_listen", "Start (listen=true) or stop the DCAD motion endpoint that DEWB connects to.", listen },
        { "cad_motion_status", "Return the motion link status JSON: state, pipe path, DEWB simulation, accepted profile revision, timeline, sequence, signals, problems and the current mechanical interface with instance and joint ids.", emptyObject },
        { "cad_redo", "Redo the most recently undone CAD document command.", emptyObject }
    };
}
}

DjehutiAgentPanel::DjehutiAgentPanel(juce::ApplicationProperties& p,
                                     std::function<std::string(const std::string&, const std::string&, std::shared_ptr<DjehutiAgentRequestState>)> dispatch)
    : properties(p), dispatchTool(std::move(dispatch))
{
        history.push_back({ "system", "You are the DjehutiCAD engineering assistant. Be concise and only claim operations that are implemented. The current CAD document uses the existing command service and an OCAF-backed live document; bounded XY/XZ/YZ sketches, dimensional constraints, extrude, and cut are supported, while assemblies and advanced feature families remain future work. Use the CAD workspace, FRust editor, node editor, or 3D views when asked. Engineering changes are delivered on a feature branch, minimally checked, committed, merged to the repository's existing main branch, and pushed. Report completed CAD assignments to the architect chat, then stop; do not start another phase automatically." });
    transcript.setMultiLine(true); transcript.setReadOnly(true); transcript.setScrollbarsShown(true); transcript.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10151a)); transcript.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
    input.setMultiLine(false); send.onClick = [this] { sendMessage(); }; cancelButton.onClick = [this] { if (request != nullptr) request->cancelled = true; if (worker != nullptr && worker->joinable()) worker->detach(); worker.reset(); request.reset(); setBusy(false); appendTranscript("Agent", "Request cancelled."); };
    addAndMakeVisible(transcript); addAndMakeVisible(input); addAndMakeVisible(send); addAndMakeVisible(cancelButton);
    setBusy(false); appendTranscript("DjehutiCAD Agent", "BYOK agent ready. Configure a provider key in Settings > Agent Settings.");
}
DjehutiAgentPanel::~DjehutiAgentPanel()
{
    if (request != nullptr) { request->cancelled = true; request->alive = false; }
    if (worker != nullptr && worker->joinable()) worker->detach();
    worker.reset(); request.reset();
}
void DjehutiAgentPanel::setBusy(bool value) { busy = value; send.setEnabled(!value); cancelButton.setEnabled(value); input.setEnabled(!value); }
void DjehutiAgentPanel::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff11161b)); g.setColour(juce::Colour(0xffdce9ee)); g.setFont(juce::Font(20.0f, juce::Font::bold)); g.drawText("DjehutiCAD Agent", 24, 16, getWidth() - 48, 30, juce::Justification::left); }
void DjehutiAgentPanel::resized() { auto area = getLocalBounds().reduced(24); area.removeFromTop(42); auto bottom = area.removeFromBottom(34); cancelButton.setBounds(bottom.removeFromRight(90)); send.setBounds(bottom.removeFromRight(80)); input.setBounds(bottom.reduced(4, 2)); transcript.setBounds(area); }
void DjehutiAgentPanel::appendTranscript(const juce::String& speaker, const juce::String& text) { transcript.moveCaretToEnd(); transcript.insertTextAtCaret(speaker + ": " + text + "\n\n"); }

void DjehutiAgentPanel::sendMessage()
{
    const auto text = input.getText().trim(); if (text.isEmpty() || busy) return;
    const auto providerName = setting(properties, "agentProvider", "openai"); const auto profile = setting(properties, "agentProfile", "DjehutiCAD"); const auto model = setting(properties, "agentModel", "gpt-4o-mini");
    std::string key, error; if (!DjehutiCredentials::read(credentialName(providerName, profile), key, error)) { appendTranscript("Agent", juce::String(error)); return; }
    input.clear(); appendTranscript("You", text); history.push_back({ "user", text.toStdString() }); const auto messages = history; const auto dispatch = dispatchTool; const auto state = std::make_shared<DjehutiAgentRequestState>(); request = state; setBusy(true);
    const juce::Component::SafePointer<DjehutiAgentPanel> safe(this);
    worker = std::make_unique<std::thread>([safe, state, messages, key = std::move(key), model = model.toStdString(), dispatch] {
        ai_provider::ChatResponse response; auto round = messages;
        ai_provider::OpenAiProvider api(key, model);
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            if (state->cancelled) return;
            response = api.sendChat(round, tools());
            if (state->cancelled || !state->alive) return;
            if (!response.ok || response.toolCalls.empty()) break;
            round.push_back({ "assistant", response.content, response.toolCalls });
            for (const auto& call : response.toolCalls)
            {
                if (state->cancelled || !state->alive) return;
                const auto result = dispatch ? dispatch(call.name, call.argumentsJson, state) : "No workspace dispatcher is available.";
                if (state->cancelled || !state->alive) return;
                round.push_back({ "tool", result, {}, call.id });
            }
        }
        juce::MessageManager::callAsync([safe, state, response] {
            if (safe == nullptr || !state->alive || state->cancelled) return;
            safe->worker->detach(); safe->worker.reset(); safe->request.reset(); safe->setBusy(false);
            if (response.ok) { safe->history.push_back({ "assistant", response.content }); safe->appendTranscript("Agent", juce::String(response.content)); }
            else safe->appendTranscript("Agent", juce::String(response.errorMessage));
        });
    });
}

DjehutiAgentSettings::DjehutiAgentSettings(juce::ApplicationProperties& p, std::function<void()> saved) : properties(p), onSaved(std::move(saved))
{
    provider.addItem("OpenAI", 1); provider.setSelectedId(1); profile.setText(setting(p, "agentProfile", "DjehutiCAD")); model.setText(setting(p, "agentModel", "gpt-4o-mini")); key.setPasswordCharacter('*'); key.setTextToShowWhenEmpty("Enter a new key; existing key is retained when blank", juce::Colours::grey);
    providerLabel.setText("Provider", juce::dontSendNotification); profileLabel.setText("Profile", juce::dontSendNotification); modelLabel.setText("Model", juce::dontSendNotification); keyLabel.setText("API key", juce::dontSendNotification); status.setText("Keys use Windows user-scoped DPAPI and are never written to the settings file.", juce::dontSendNotification);
    saveButton.onClick = [this] { save(); }; removeButton.onClick = [this] { std::string e; DjehutiCredentials::remove(credentialName("openai", profile.getText().trim()), e); status.setText(e.empty() ? "Protected key removed." : juce::String(e), juce::dontSendNotification); }; cancelButton.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->closeButtonPressed(); };
    addAndMakeVisible(providerLabel); addAndMakeVisible(profileLabel); addAndMakeVisible(modelLabel); addAndMakeVisible(keyLabel); addAndMakeVisible(provider); addAndMakeVisible(profile); addAndMakeVisible(model); addAndMakeVisible(key); addAndMakeVisible(status); addAndMakeVisible(saveButton); addAndMakeVisible(removeButton); addAndMakeVisible(cancelButton);
}
void DjehutiAgentSettings::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff171b20)); }
void DjehutiAgentSettings::resized() { auto a = getLocalBounds().reduced(24); auto row = [&](juce::Label& l, juce::Component& c) { l.setBounds(a.removeFromTop(28)); c.setBounds(a.removeFromTop(32)); a.removeFromTop(10); }; row(providerLabel, provider); row(profileLabel, profile); row(modelLabel, model); row(keyLabel, key); status.setBounds(a.removeFromTop(42)); auto b = a.removeFromTop(34); cancelButton.setBounds(b.removeFromRight(80)); removeButton.setBounds(b.removeFromRight(110).reduced(4, 0)); saveButton.setBounds(b.removeFromRight(80).reduced(4, 0)); }
void DjehutiAgentSettings::save()
{
    const auto profileName = profile.getText().trim(); const auto value = key.getText().trim(); std::string error; if (!value.isEmpty() && !DjehutiCredentials::write(credentialName("openai", profileName), value.toStdString(), error)) { status.setText(error, juce::dontSendNotification); return; }
    auto* s = properties.getUserSettings(); s->setValue("agentProvider", "openai"); s->setValue("agentProfile", profileName); s->setValue("agentModel", model.getText().trim()); s->saveIfNeeded(); status.setText("Agent settings saved.", juce::dontSendNotification); if (onSaved) onSaved();
}
