#pragma once

#include <JuceHeader.h>
#include <memory>
#include <thread>

class DjehutiAgentPanel final : public juce::Component
{
public:
    explicit DjehutiAgentPanel(juce::ApplicationProperties& properties);
    ~DjehutiAgentPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void sendMessage();
    void appendTranscript(const juce::String& speaker, const juce::String& text);
    juce::ApplicationProperties& properties;
    juce::TextEditor transcript;
    juce::TextEditor input;
    juce::TextButton send { "Send" };
    std::unique_ptr<std::thread> worker;
    std::atomic_bool cancel { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DjehutiAgentPanel)
};

class DjehutiAgentSettings final : public juce::Component
{
public:
    DjehutiAgentSettings(juce::ApplicationProperties& properties, std::function<void()> onSaved);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void save();
    juce::ApplicationProperties& properties;
    std::function<void()> onSaved;
    juce::Label providerLabel, profileLabel, modelLabel, keyLabel, status;
    juce::ComboBox provider;
    juce::TextEditor profile, model, key;
    juce::TextButton saveButton { "Save" }, removeButton { "Remove key" }, cancelButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DjehutiAgentSettings)
};
