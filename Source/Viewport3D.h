#pragma once

#include <JuceHeader.h>
#include "FrameResourceRing.h"

class Viewport3D final : public juce::Component,
                         private juce::OpenGLRenderer,
                         private juce::Timer
{
public:
    explicit Viewport3D(int viewNumber);
    ~Viewport3D() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resetCamera();
    void setPaused(bool shouldPause);
    juce::String diagnostics() const;

private:
    void timerCallback() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void releaseGL();
    void drawCube(const float* mvp);

    const int viewNumber;
    juce::OpenGLContext context;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> positionAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> normalAttribute;
    GLuint vao = 0, vbo = 0, ibo = 0;
    DjehutiFrameResourceRing frames;
    float angle = 0.0f, yaw = 0.55f, pitch = 0.35f, distance = 4.0f;
    juce::Point<int> dragStart;
    bool paused = false, glReady = false;
    juce::String glStatus = "Waiting for OpenGL context";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Viewport3D)
};

class ThreeDViewsPanel final : public juce::Component
{
public:
    ThreeDViewsPanel();
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void addView();
    juce::TextButton addButton { "New 3D View" }, pauseButton { "Pause" }, resetButton { "Reset Cameras" };
    juce::OwnedArray<Viewport3D> views;
    bool paused = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreeDViewsPanel)
};
