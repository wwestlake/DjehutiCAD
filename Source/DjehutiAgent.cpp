#include "DjehutiAgent.h"
#include "ProtectedCredentialStore.h"
#include <ai_provider/OpenAiProvider.h>

namespace
{
constexpr auto kCredentialName = "agent-api-key";
juce::String setting(juce::ApplicationProperties& p, const char* name, const char* fallback)
{
    return p.getUserSettings()->getValue(name, fallback);
}
}

DjehutiAgentPanel::DjehutiAgentPanel(juce::ApplicationProperties& p) : properties(p)
{
    transcript.setMultiLine(true); transcript.setReadOnly(true); transcript.setScrollbarsShown(true);
    transcript.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10151a));
    input.setMultiLine(false); input.setReturnKeyStartsNewLine(false);
    send.onClick = [this] { sendMessage(); };
    addAndMakeVisible(transcript); addAndMakeVisible(input); addAndMakeVisible(send);
    appendTranscript("DjehutiCAD Agent", "BYOK agent ready. Configure a provider key in Settings > Agent Settings.");
}

DjehutiAgentPanel::~DjehutiAgentPanel()
{
    cancel = true;
    if (worker != nullptr && worker->joinable()) worker->detach();
}

void DjehutiAgentPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff11161b));
    g.setColour(juce::Colour(0xffdce9ee));
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.drawText("DjehutiCAD Agent", 24, 16, getWidth() - 48, 30, juce::Justification::left);
}

void DjehutiAgentPanel::resized()
{
    auto area = getLocalBounds().reduced(24); area.removeFromTop(42);
    auto bottom = area.removeFromBottom(34); send.setBounds(bottom.removeFromRight(90)); input.setBounds(bottom.reduced(0, 2));
    transcript.setBounds(area);
}

void DjehutiAgentPanel::appendTranscript(const juce::String& speaker, const juce::String& text)
{
    transcript.moveCaretToEnd(); transcript.insertTextAtCaret(speaker + ": " + text + "\n\n");
}

void DjehutiAgentPanel::sendMessage()
{
    const auto text = input.getText().trim();
    if (text.isEmpty() || worker != nullptr) return;
    input.clear(); appendTranscript("You", text);
    std::string key, error;
    if (!DjehutiCredentials::read(kCredentialName, key, error)) { appendTranscript("Agent", juce::String(error)); return; }
    const auto model = setting(properties, "agentModel", "gpt-4o-mini").toStdString();
    cancel = false;
    worker = std::make_unique<std::thread>([this, key = std::move(key), model, text] {
        ai_provider::OpenAiProvider provider(key, model);
        std::vector<ai_provider::ChatMessage> messages {
            { "system", "You are the DjehutiCAD engineering assistant. Be concise and distinguish implemented behavior from plans." },
            { "user", text.toStdString() }
        };
        const auto response = provider.sendChat(messages);
        juce::MessageManager::callAsync([this, response] {
            appendTranscript("Agent", response.ok ? juce::String(response.content) : juce::String(response.errorMessage));
            if (worker != nullptr && worker->joinable()) worker->detach();
            worker.reset();
        });
        if (cancel) return;
    });
}

DjehutiAgentSettings::DjehutiAgentSettings(juce::ApplicationProperties& p, std::function<void()> saved)
    : properties(p), onSaved(std::move(saved))
{
    provider.addItem("OpenAI", 1); provider.setSelectedId(1);
    profile.setText(setting(p, "agentProfile", "DjehutiCAD"));
    model.setText(setting(p, "agentModel", "gpt-4o-mini"));
    key.setPasswordCharacter('*'); key.setTextToShowWhenEmpty("Enter a new key; existing key is retained when blank", juce::Colours::grey);
    providerLabel.setText("Provider", juce::dontSendNotification); profileLabel.setText("Profile", juce::dontSendNotification);
    modelLabel.setText("Model", juce::dontSendNotification); keyLabel.setText("API key", juce::dontSendNotification);
    status.setText("Keys are stored with Windows user-scoped DPAPI and are never written to the settings file.", juce::dontSendNotification);
    saveButton.onClick = [this] { save(); }; removeButton.onClick = [this] { std::string e; DjehutiCredentials::remove(kCredentialName, e); status.setText(e.empty() ? "Protected key removed." : juce::String(e), juce::dontSendNotification); }; cancelButton.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->closeButtonPressed(); };
    addAndMakeVisible(providerLabel); addAndMakeVisible(profileLabel); addAndMakeVisible(modelLabel); addAndMakeVisible(keyLabel);
    addAndMakeVisible(provider); addAndMakeVisible(profile); addAndMakeVisible(model); addAndMakeVisible(key);
    addAndMakeVisible(status); addAndMakeVisible(saveButton); addAndMakeVisible(removeButton); addAndMakeVisible(cancelButton);
}

void DjehutiAgentSettings::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff171b20)); }
void DjehutiAgentSettings::resized()
{
    auto a = getLocalBounds().reduced(24); auto row = [&](juce::Label& l, juce::Component& c) { l.setBounds(a.removeFromTop(28)); c.setBounds(a.removeFromTop(32)); a.removeFromTop(10); };
    row(providerLabel, provider); row(profileLabel, profile); row(modelLabel, model); row(keyLabel, key);
    status.setBounds(a.removeFromTop(42)); auto buttons = a.removeFromTop(34); cancelButton.setBounds(buttons.removeFromRight(80)); removeButton.setBounds(buttons.removeFromRight(110).reduced(4, 0)); saveButton.setBounds(buttons.removeFromRight(80).reduced(4, 0));
}
void DjehutiAgentSettings::save()
{
    const auto value = key.getText().trim(); std::string error;
    if (!value.isEmpty() && !DjehutiCredentials::write(kCredentialName, value.toStdString(), error)) { status.setText(error, juce::dontSendNotification); return; }
    auto* s = properties.getUserSettings(); s->setValue("agentProvider", "openai"); s->setValue("agentProfile", profile.getText().trim()); s->setValue("agentModel", model.getText().trim()); s->saveIfNeeded();
    status.setText("Agent settings saved.", juce::dontSendNotification); if (onSaved) onSaved();
}
