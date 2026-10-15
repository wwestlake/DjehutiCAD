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
    auto safe = (provider + "/" + profile).replaceCharacters("\\/:*?\"<>| ", "______________");
    return "agent/" + safe.toStdString();
}
std::vector<ai_provider::ToolDefinition> tools()
{
    const auto emptyObject = R"({"type":"object","properties":{},"additionalProperties":false})";
    return {
        { "open_cad_workspace", "Open the CAD workspace.", emptyObject },
        { "open_frust_editor", "Open the FRust editor.", emptyObject },
        { "open_node_editor", "Open the node editor.", emptyObject },
        { "open_3d_views", "Open the 3D views workspace and add a view.", emptyObject }
    };
}
}

DjehutiAgentPanel::DjehutiAgentPanel(juce::ApplicationProperties& p,
                                     std::function<std::string(const std::string&, const std::string&)> dispatch)
    : properties(p), dispatchTool(std::move(dispatch))
{
    history.push_back({ "system", "You are the DjehutiCAD engineering assistant. Be concise. Only claim operations that are implemented. You can open the CAD workspace, FRust editor, node editor, or 3D views when asked." });
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
    input.clear(); appendTranscript("You", text); history.push_back({ "user", text.toStdString() }); const auto messages = history; const auto dispatch = dispatchTool; const auto state = std::make_shared<RequestState>(); request = state; setBusy(true);
    const juce::Component::SafePointer<DjehutiAgentPanel> safe(this);
    worker = std::make_unique<std::thread>([safe, state, messages, key = std::move(key), model = model.toStdString(), dispatch] {
        ai_provider::ChatResponse response; auto round = messages;
        ai_provider::OpenAiProvider api(key, model);
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            if (state->cancelled) return;
            response = api.sendChat(round, tools()); if (!response.ok || response.toolCalls.empty()) break;
            round.push_back({ "assistant", response.content, response.toolCalls });
            for (const auto& call : response.toolCalls) round.push_back({ "tool", dispatch ? dispatch(call.name, call.argumentsJson) : "No workspace dispatcher is available.", {}, call.id });
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
