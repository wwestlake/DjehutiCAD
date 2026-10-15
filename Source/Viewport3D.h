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
    void prepareFrame(float sharedAngle, std::uint64_t serial);
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
    float yaw = 0.55f, pitch = 0.35f, distance = 4.0f, panX = 0.0f, panY = 0.0f;
    juce::Point<int> dragStart;
    bool glReady = false;
    juce::String glStatus = "Waiting for OpenGL context";
    std::array<GLsync, 3> fences {};
    std::array<GLuint, 3> frameBuffers {};
    DjehutiFramePacket lastFrame;
    bool hasFrame = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Viewport3D)
};

class ThreeDViewsPanel final : public juce::Component, private juce::Timer
{
public:
    ThreeDViewsPanel();
    ~ThreeDViewsPanel() override;
    bool addView();
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void closeView(int viewId);
    void publishFrames();
    juce::TextButton addButton { "New 3D View" }, pauseButton { "Pause" }, resetButton { "Reset Cameras" };
    class ViewTile;
    juce::OwnedArray<ViewTile> tiles;
    bool paused = false;
    float sharedAngle = 0.0f;
    std::uint64_t frameSerial = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreeDViewsPanel)
};
