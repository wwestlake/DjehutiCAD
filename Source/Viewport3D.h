#pragma once

#include <JuceHeader.h>
#include "FrameResourceRing.h"
#include "CadDocument.h"

bool runViewportCameraCheck(juce::String& detail);

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
    void setSnapshot(std::shared_ptr<const djehuti::cad::CadRenderSnapshot> next);
    void fitAll();
    juce::String diagnostics() const;

private:
    void timerCallback() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void releaseGL();
    void drawCube(const float* mvp);
    void updateSceneBounds(const std::shared_ptr<const djehuti::cad::CadRenderSnapshot>& snapshot);
    void frameScene();

    const int viewNumber;
    juce::OpenGLContext context;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> positionAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> normalAttribute;
    GLuint vao = 0, vbo = 0, ibo = 0;
    DjehutiFrameResourceRing frames;
    float yaw = 0.55f, pitch = 0.35f, distance = 4.0f, panX = 0.0f, panY = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
    float sceneCenterX = 0.0f, sceneCenterY = 0.0f, sceneCenterZ = 0.0f, sceneRadius = 1.0f;
    bool hasSceneBounds = false, hasFramedInitialScene = false, userCameraChanged = false;
    std::uint64_t boundsRevision = 0;
    juce::Point<int> dragStart;
    bool glReady = false;
    juce::String glStatus = "Waiting for OpenGL context";
    std::array<GLsync, 3> fences {};
    std::array<GLuint, 3> frameBuffers {};
    DjehutiFramePacket lastFrame;
    bool hasFrame = false;
    int lastSlot = -1;
    std::shared_ptr<const djehuti::cad::CadRenderSnapshot> pendingSnapshot;
    std::uint64_t uploadedGeometryRevision = 0;
    GLsizei geometryIndexCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Viewport3D)
};

class ThreeDViewsPanel final : public juce::Component, private juce::Timer
{
public:
    explicit ThreeDViewsPanel(std::shared_ptr<djehuti::cad::CadCommandService> service);
    ~ThreeDViewsPanel() override;
    bool addView();
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void closeView(int viewId);
    void publishFrames();
    juce::TextButton addButton { "New 3D View" }, pauseButton { "Pause" }, resetButton { "Reset Cameras" }, fitButton { "Fit All" };
    class ViewTile;
    juce::OwnedArray<ViewTile> tiles;
    bool paused = false;
    float sharedAngle = 0.0f;
    std::uint64_t frameSerial = 0;
    std::shared_ptr<djehuti::cad::CadCommandService> cadService;
    std::shared_ptr<const djehuti::cad::CadRenderSnapshot> currentSnapshot;
    std::uint64_t shownRevision = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreeDViewsPanel)
};
