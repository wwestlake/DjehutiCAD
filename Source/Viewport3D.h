#pragma once

#include <JuceHeader.h>
#include "WorkspaceMenu.h"
#include "FrameResourceRing.h"
#include "CadDocument.h"
#include "CadMotionRuntime.h"
#include "CadSelection.h"
#include "CadTooltipWindow.h"

#include <atomic>

bool runViewportCameraCheck(juce::String& detail);
bool runViewportRangeCheck(juce::var& report);
bool runViewportInteractionCheck(juce::String& detail);

class Viewport3D final : public juce::Component,
                         private juce::OpenGLRenderer,
                         private juce::Timer
{
public:
    explicit Viewport3D(int viewNumber);
    ~Viewport3D() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override;
    void focusLost(FocusChangeType) override;
    std::function<bool(const juce::KeyPress&)> onWorkspaceKey;
    std::function<bool()> wantsEdgePick;
    std::function<juce::String()> edgeTarget;
    void setFlyMode(bool);
    bool isFlyMode() const {return flyMode;}
    void moveFly(float forward,float right,float up);
    void lookFly(float yawDelta,float pitchDelta);
    bool setOrbitPoint(const std::array<float,3>&);
    void chooseOrbitPoint();
    bool isChoosingOrbitPoint() const {return orbitPointPicking;}
    juce::Rectangle<int> contextMenuAnchor(juce::Point<int> screen) const;
    std::vector<djehuti::cad::CadPick> allPicks() const;

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
    std::function<void(const juce::String&, const djehuti::cad::CadPick&)> onContextCommand;
    bool guideEnabled(const juce::String& name) const;
    void toggleGuide(const juce::String& name);
    juce::var viewState() const;
    void restoreViewState(const juce::var& state);
    void resetCamera();
    juce::var cameraState() const;
    bool controlCamera(const juce::var&, juce::String& error);
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
    bool fitSelection();
    juce::String diagnostics() const;
    void setTitle(const juce::String& text) { title = text; repaint(); }
    void setHighlights(std::vector<std::pair<juce::String, juce::Colour>> next);
    // World-mm lines drawn on top of the model (interference regions); empty clears them. Never affects picking or bounds.
    void setInspectionLines(std::vector<djehuti::cad::CadOverlayLine> next);
    juce::String pickBody(juce::Point<float> position) const;
    void setPoseSource(std::function<std::shared_ptr<const djehuti::cad::CadPoseSnapshot>()> source) { poseSource = std::move(source); }
    std::function<void(const juce::String& featureId)> onBodyPicked;
    std::function<bool()> faceSnapEnabled;
    std::function<bool(djehuti::cad::CadPick&)> refineFaceSnap;
    bool faceSnapMarker=false;juce::Point<float> faceSnapScreen;
    // True while the owner needs whole bodies (a command choosing its target): clicks pick bodies in any mode.
    std::function<bool()> wantsBodyPick;
    std::function<bool()> wantsSketchPick; // Enabled by the sketch-driven Virtual Lathe selector only.
    // IK target dragging. Only while enabled, and only when a press lands on the published drag handle, does a
    // drag move the target within its drag plane; every other press keeps orbit, pan and picking unchanged.
    void setTargetDragEnabled(bool enabled) { targetDragEnabled = enabled; if (!enabled) draggingTarget = false; }
    std::function<void(const juce::String& targetId, const std::array<float, 3>& point, bool finished)> onTargetDragged;
    // Optional fixture equipment picking; empty in ordinary CAD views. Uses the same camera projection.
    void setPickMarkers(std::vector<std::pair<juce::String,std::array<float,3>>> markers) { pickMarkers=std::move(markers); }
    std::function<void(const juce::String&)> onMarkerPicked;
    void markDirty();
    // Move/rotate manipulator for one body or assembly instance. The viewport draws and hit-tests it and previews
    // the drag without touching the document; the owner applies the finished amount as one document command.
    struct Manipulator
    {
        bool active = false, local = false;
        juce::String drawId, label;
        std::array<float, 3> pivot {};
        std::array<std::array<float, 3>, 3> axes { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
    };
    struct Manipulation { bool rotate = false; int axis = 0; double amount = 0.0; };   // mm along, or radians about, axes[axis] through pivot
    void setManipulator(const Manipulator& next);
    const Manipulator& manipulator() const { return manip; }
    bool isManipulating() const { return manipAxis >= 0; }
    std::function<bool(const Manipulation&)> onManipulated;   // true: committed (the preview clears when the new geometry arrives)
    juce::var manipulatorState() const;
    juce::Point<float> manipulatorHandle(int axis, bool rotate, float fraction) const;   // screen point on an arrow or ring
    // Standard named views (front, back, left, right, top, bottom, isometric) about the current orbit target.
    bool setStandardView(const juce::String& name);

private:
    friend bool runViewportInteractionCheck(juce::String&);
    friend bool runWorkspaceNavigationCheck(juce::var&);
    friend bool runViewportRangeCheck(juce::var&);
    friend bool runPlacementWorkflowCheck(juce::var&);
    std::array<float,2> clippingPlanes() const;
    std::array<float,2> navigationRange() const;
    void renderFloor(const DjehutiFramePacket&);
    void createFloorRenderer();
    bool captureViewport(const juce::File&,juce::String&);
    std::vector<std::array<std::array<float,3>,8>> posedBoxes;
    std::unique_ptr<juce::OpenGLShaderProgram> floorShader,rangeShader;
    std::atomic<bool> floorReady{false};
    GLuint floorVao=0, floorQueries[3]{};
    int floorQueryCursor=0;
    bool floorQueryPending[3]{};
    std::atomic<std::uint64_t> floorGpuNs{0}, renderedFrames{0}, geometryUploads{0};
    void drawReferenceGuides(juce::Graphics&);
    void updateReferenceGuides();
    std::vector<djehuti::cad::CadOverlayLine> glGuides;
    std::vector<djehuti::cad::CadOverlayLine> glInspection;
    float gridSpacing=10.0f;
    std::vector<djehuti::cad::CadPick> pickRegion(juce::Point<float> start, juce::Point<float> end, bool circle) const;
    void showContextMenu(const juce::MouseEvent&);
    bool selectingRegion = false, flyMode=false, orbitPointPicking=false;
    double flyTick=0;
    juce::Point<float> regionEnd;
    bool showFloor = true, showWorld = false, showLocal = false, showJoints = false;
    bool rayThrough(juce::Point<float> position, std::array<float, 3>& origin, std::array<float, 3>& direction) const;
    bool toScreen(const std::array<float, 3>& point, juce::Point<float>& screen) const;
    bool dragPoint(juce::Point<float> position, std::array<float, 3>& point) const;
    bool targetDragEnabled = false, draggingTarget = false;
    juce::String dragTargetId;
    std::array<float, 3> dragPlanePoint {}, dragPlaneNormal {};
    std::vector<std::pair<juce::String,std::array<float,3>>> pickMarkers;
    Manipulator manip;
    int manipAxis = -1, previewClearFrames = -1;
    bool manipRotate = false, orbitAnchored = false;
    double manipStartParam = 0.0, manipAmount = 0.0;
    std::array<float, 3> manipStartVector {};
    juce::Point<float> manipMouse, manipPress;
    bool glPreviewActive = false; juce::String glPreviewId; std::array<float, 16> glPreviewMatrix {};   // guarded by highlightLock
    static constexpr float maxPitch = 1.5697f;   // just short of straight up/down: top and bottom views
    float manipulatorLength() const;
    std::array<float, 3> eyePosition() const;
    bool hitManipulator(juce::Point<float> position, int& axis, bool& rotate) const;
    bool axisParameter(juce::Point<float> position, int axis, double& t) const;
    bool ringVector(juce::Point<float> position, int axis, std::array<float, 3>& v) const;
    void updateManipulation(juce::Point<float> position, bool snap);
    void setPreview(bool active);
    void drawManipulator(juce::Graphics&);
    void anchorOrbit(juce::Point<float> position);

    void timerCallback() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void releaseGL();
    void drawCube(const float* mvp);
    void updateSceneBounds(const std::shared_ptr<const djehuti::cad::CadRenderSnapshot>& snapshot);
    void frameScene();
    void updatePosedBounds();
    void tryPendingFit(); /*
    void markDirty();
*/ void cameraChanged();

    const int viewNumber;
    juce::OpenGLContext context;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> positionAttribute;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> normalAttribute;
    GLuint vao = 0, vbo = 0, ibo = 0, axisVao = 0, axisVbo = 0;
    DjehutiFrameResourceRing frames;
    static constexpr float homeYaw = 0.785398163f, homePitch = 0.615479709f;
    static constexpr float homeDistance = 200.0f, emptySceneRadius = 50.0f;
    float yaw = homeYaw, pitch = homePitch, distance = homeDistance, panX = 0.0f, panY = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
    float sceneCenterX = 0.0f, sceneCenterY = 0.0f, sceneCenterZ = 0.0f, sceneRadius = emptySceneRadius;
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
    float clipRadius = emptySceneRadius;
    juce::SpinLock highlightLock;
    std::vector<std::pair<juce::String, juce::Colour>> highlights;
    struct MeshRange { juce::String featureId; GLsizei first = 0, count = 0; int faceIndex = -1; bool doubleSided = false; };
    std::shared_ptr<djehuti::cad::CadSelection> selection;
    std::uint64_t shownSelectionSerial = 0;
    std::vector<djehuti::cad::CadPick> glChosen;
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
    std::uint64_t uploadedGeometryRevision = 0, uploadedGeometryDocument = 0;
    GLsizei geometryIndexCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Viewport3D)
};

class ThreeDViewsPanel final : public juce::Component, private juce::Timer
{
public:
    explicit ThreeDViewsPanel(std::shared_ptr<djehuti::cad::CadCommandService> service);
    ~ThreeDViewsPanel() override;
    void setContextCommand(std::function<void(const juce::String&,const djehuti::cad::CadPick&)> callback);
    juce::String viewState() const;
    void restoreViewState(const juce::String&);
    bool addView();
    juce::var controlViewCamera(int view, const juce::var&);
    WorkspaceMenu workspaceMenu;
    void setPoseSource(std::function<std::shared_ptr<const djehuti::cad::CadPoseSnapshot>()> source);
    void setSelection(std::shared_ptr<djehuti::cad::CadSelection> next);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    CadTooltipWindow tooltipWindow{*this};
    std::function<void(const juce::String&,const djehuti::cad::CadPick&)> contextCommand;
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
