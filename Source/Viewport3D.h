#pragma once

#include <JuceHeader.h>
#include "FrameResourceRing.h"
#include "CadDocument.h"
#include "CadMotionRuntime.h"
#include "CadSelection.h"

#include <atomic>

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
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    // Shared body/face/edge selection: in Face and Edge mode, hover highlights and a click (not a drag) selects.
    void setSelection(std::shared_ptr<djehuti::cad::CadSelection> next) { selection = std::move(next); markDirty(); }
    // Visible geometry under a view position, using the drawn transforms (assembly instances at their pose).
    djehuti::cad::CadPick pick(juce::Point<float> position, djehuti::cad::CadSelection::Mode mode) const;
    void resetCamera();
    // Publishes a frame and requests one render only when the camera, pose, geometry, highlights or visibility
    // changed; returns true if it did. Hidden and minimised views do nothing.
    bool prepareFrame(float sharedAngle, std::uint64_t serial);
    // Frame the visible geometry once it and a matching runtime pose are available (new view, New, Open).
    // documentInstance names the document to frame, so geometry still shown from the previous one is not used.
    void requestFit(std::uint64_t documentInstance = 0);
    // Called when this view needs frames soon (camera input, new geometry); owners speed up their frame timer.
    std::function<void()> onFrameRequested;
    void setSnapshot(std::shared_ptr<const djehuti::cad::CadRenderSnapshot> next);
    void fitAll();
    juce::String diagnostics() const;
    void setTitle(const juce::String& text) { title = text; repaint(); }
    void setHighlights(std::vector<std::pair<juce::String, juce::Colour>> next);
    juce::String pickBody(juce::Point<float> position) const;
    void setPoseSource(std::function<std::shared_ptr<const djehuti::cad::CadPoseSnapshot>()> source) { poseSource = std::move(source); }
    std::function<void(const juce::String& featureId)> onBodyPicked;
    // True while the owner needs whole bodies (a command choosing its target): clicks pick bodies in any mode.
    std::function<bool()> wantsBodyPick;
    // IK target dragging. Only while enabled, and only when a press lands on the published drag handle, does a
    // drag move the target within its drag plane; every other press keeps orbit, pan and picking unchanged.
    void setTargetDragEnabled(bool enabled) { targetDragEnabled = enabled; if (!enabled) draggingTarget = false; }
    std::function<void(const juce::String& targetId, const std::array<float, 3>& point, bool finished)> onTargetDragged;

private:
    bool rayThrough(juce::Point<float> position, std::array<float, 3>& origin, std::array<float, 3>& direction) const;
    bool toScreen(const std::array<float, 3>& point, juce::Point<float>& screen) const;
    bool dragPoint(juce::Point<float> position, std::array<float, 3>& point) const;
    bool targetDragEnabled = false, draggingTarget = false;
    juce::String dragTargetId;
    std::array<float, 3> dragPlanePoint {}, dragPlaneNormal {};

    void timerCallback() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void releaseGL();
    void drawCube(const float* mvp);
    void updateSceneBounds(const std::shared_ptr<const djehuti::cad::CadRenderSnapshot>& snapshot);
    void frameScene();
    void updatePosedBounds();
    void tryPendingFit();
    void markDirty();
    void cameraChanged();

    const int viewNumber;
    juce::OpenGLContext context;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> positionAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> normalAttribute;
    GLuint vao = 0, vbo = 0, ibo = 0, axisVao = 0, axisVbo = 0;
    DjehutiFrameResourceRing frames;
    float yaw = 0.55f, pitch = 0.35f, distance = 4.0f, panX = 0.0f, panY = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
    float sceneCenterX = 0.0f, sceneCenterY = 0.0f, sceneCenterZ = 0.0f, sceneRadius = 1.0f;
    bool hasSceneBounds = false, fitPending = true, userCameraChanged = false;
    std::uint64_t boundsRevision = 0, boundsDocument = 0, fitDocument = 0;
    std::atomic<bool> renderDirty { true };   // set from the GL thread too (context recreation)
    std::array<float, 32> lastCamera {};       // projection and view of the last published frame
    bool hasPublished = false, wasShowing = false;
    juce::Point<int> dragStart, pressPosition;
    juce::String title;
    std::shared_ptr<const djehuti::cad::CadRenderSnapshot> pickSnapshot;
    std::function<std::shared_ptr<const djehuti::cad::CadPoseSnapshot>()> poseSource;
    std::shared_ptr<const djehuti::cad::CadPoseSnapshot> pose, glPose;
    struct Bounds { float min[3] {}, max[3] {}; };
    std::map<juce::String, Bounds> featureBounds;
    float clipRadius = 1.0f;
    juce::SpinLock highlightLock;
    std::vector<std::pair<juce::String, juce::Colour>> highlights;
    struct MeshRange { juce::String featureId; GLsizei first = 0, count = 0; int faceIndex = -1; };
    std::shared_ptr<djehuti::cad::CadSelection> selection;
    std::uint64_t shownSelectionSerial = 0;
    djehuti::cad::CadPick glHover, glSelected;   // guarded by highlightLock
    std::vector<MeshRange> meshRanges;
    std::atomic<bool> glReady { false };   // written on the GL thread
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
    void setPoseSource(std::function<std::shared_ptr<const djehuti::cad::CadPoseSnapshot>()> source);
    void setSelection(std::shared_ptr<djehuti::cad::CadSelection> next);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void closeView(int viewId);
    bool publishFrames();
    void wakeFrames();
    int idleFrameTicks = 0;
    bool fastFrames = true;
    std::uint64_t shownDocument = 0;
    juce::TextButton addButton { "New 3D View" }, pauseButton { "Pause" }, resetButton { "Reset Cameras" }, fitButton { "Fit All" };
    class ViewTile;
    juce::OwnedArray<ViewTile> tiles;
    bool paused = false;
    float sharedAngle = 0.0f;
    std::uint64_t frameSerial = 0;
    std::shared_ptr<djehuti::cad::CadCommandService> cadService;
    std::shared_ptr<const djehuti::cad::CadRenderSnapshot> currentSnapshot;
    std::uint64_t shownRevision = 0;
    std::function<std::shared_ptr<const djehuti::cad::CadPoseSnapshot>()> poseSource;
    std::shared_ptr<djehuti::cad::CadSelection> selection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreeDViewsPanel)
};
