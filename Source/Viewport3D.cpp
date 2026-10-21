#include "Viewport3D.h"

#include <cmath>
#include <cstring>
using namespace juce::gl;

namespace
{
struct M4 { float v[16] {}; };
struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 scale(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalized(V3 a) { const auto l = juce::jmax(1.0e-6f, length(a)); return scale(a, 1.0f / l); }
M4 identity() { M4 m; m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1.0f; return m; }
M4 mul(const M4& a, const M4& b) { M4 r; for (int c = 0; c < 4; ++c) for (int row = 0; row < 4; ++row) for (int k = 0; k < 4; ++k) r.v[c * 4 + row] += a.v[k * 4 + row] * b.v[c * 4 + k]; return r; }
M4 perspective(float fov, float aspect, float nearZ, float farZ) { M4 m; const auto f = 1.0f / std::tan(fov * 0.5f); m.v[0] = f / aspect; m.v[5] = f; m.v[10] = (farZ + nearZ) / (nearZ - farZ); m.v[11] = -1.0f; m.v[14] = 2.0f * farZ * nearZ / (nearZ - farZ); return m; }
M4 camera(float yaw, float pitch, float distance, float targetX, float targetY, float targetZ, float panX, float panY)
{
    const auto cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    const V3 target {targetX, targetY, targetZ};
    // CAD coordinates are Z-up: yaw turns about world Z and pitch is elevation above XY.
    const V3 eye = add(target, {distance * cp * sy, -distance * cp * cy, distance * sp});
    const auto forward = normalized(sub(target, eye));
    const auto referenceUp = V3 {0.0f, 0.0f, 1.0f};
    const auto right = normalized(cross(forward, referenceUp));
    const auto up = cross(right, forward);
    M4 m = identity();
    m.v[0] = right.x; m.v[4] = right.y; m.v[8] = right.z;
    m.v[1] = up.x; m.v[5] = up.y; m.v[9] = up.z;
    m.v[2] = -forward.x; m.v[6] = -forward.y; m.v[10] = -forward.z;
    m.v[12] = -dot(right, eye) + panX;
    m.v[13] = -dot(up, eye) + panY;
    m.v[14] = dot(forward, eye);
    return m;
}
M4 projectionFor(int width, int height, float distance, float sceneRadius)
{
    const auto aspect = height > 0 ? juce::jmax(0.01f, (float) width / (float) height) : 1.0f;
    const auto depthRadius = juce::jmax(0.001f, sceneRadius);
    const auto nearZ = juce::jmax(0.001f, distance - depthRadius * 1.5f);
    const auto farZ = juce::jmax(nearZ + 1.0f, distance + depthRadius * 1.5f);
    return perspective(0.85f, aspect, nearZ, farZ);
}
M4 rotation(float a) { M4 m = identity(); m.v[0] = std::cos(a); m.v[2] = std::sin(a); m.v[8] = -std::sin(a); m.v[10] = std::cos(a); return m; }
}

bool runViewportCameraCheck(juce::String& detail)
{
    const auto close = [](float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; };
    const auto check = [&](bool ok, const char* message)
    {
        if (!ok) detail = message;
        return ok;
    };
    const std::array<std::array<float, 3>, 3> cases {{{0.785398163f, 0.615479709f, 200.0f}, {juce::MathConstants<float>::halfPi, 0.0f, 4.0f}, {0.30f, -0.20f, 12.0f}}};
    const V3 target {13.0f, -7.0f, 29.0f};
    for (const auto& item : cases)
    {
        const auto m = camera(item[0], item[1], item[2], target.x, target.y, target.z, 0.0f, 0.0f);
        const V3 right {m.v[0], m.v[4], m.v[8]}, up {m.v[1], m.v[5], m.v[9]}, back {m.v[2], m.v[6], m.v[10]};
        if (!check(close(length(right), 1.0f, 1.0e-4f) && close(length(up), 1.0f, 1.0e-4f) && close(length(back), 1.0f, 1.0e-4f), "Camera basis is not unit length.")) return false;
        if (!check(close(dot(right, up), 0.0f, 1.0e-4f) && close(dot(right, back), 0.0f, 1.0e-4f) && close(dot(up, back), 0.0f, 1.0e-4f), "Camera basis is not orthogonal.")) return false;
        const auto determinant = dot(right, cross(up, back));
        if (!check(close(determinant, 1.0f, 1.0e-4f), "Camera basis is reflected.")) return false;
        if (!check(close(right.z, 0.0f, 1.0e-4f) && up.z > 0.0f, "World Z must project vertically upward without camera roll.")) return false;
        const V3 targetInView {dot(right, target) + m.v[12], dot(up, target) + m.v[13], dot(back, target) + m.v[14]};
        if (!check(close(targetInView.x, 0.0f, 1.0e-3f) && close(targetInView.y, 0.0f, 1.0e-3f) && close(targetInView.z, -item[2], 1.0e-3f), "Camera target does not map to view center.")) return false;
        const V3 vector {1.25f, -2.0f, 0.75f};
        const V3 transformed {dot(right, vector), dot(up, vector), dot(back, vector)};
        if (!check(close(length(transformed), length(vector), 1.0e-4f), "Camera view changes world-space distance.")) return false;
    }
    detail = "Z-up/no-roll orientation, camera basis, determinant, target mapping, and distance preservation passed.";
    return true;
}

Viewport3D::Viewport3D(int number) : viewNumber(number)
{
    // Rendered on demand: prepareFrame requests a render only when something changed.
    setWantsKeyboardFocus(true);
    setOpaque(false); context.setRenderer(this); context.setContinuousRepainting(false); context.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1); context.attachTo(*this);
}
Viewport3D::~Viewport3D() { context.detach(); }
void Viewport3D::paint(juce::Graphics& g) { drawReferenceGuides(g); g.setColour(juce::Colour(0xffa9c8d2)); g.drawText(title.isNotEmpty() ? title : "View " + juce::String(viewNumber), 10, 8, juce::jmax(100, getWidth() - 20), 20, juce::Justification::left); g.setColour(juce::Colour(0xff8fa4ad)); g.setFont(juce::Font(10.0f)); g.drawFittedText(glStatus, 10, juce::jmax(24, getHeight() - 28), getWidth() - 20, 18, juce::Justification::left, 1); }
void Viewport3D::resized() { if (hasSceneBounds && !userCameraChanged && !fitPending) frameScene(); tryPendingFit(); markDirty(); }
void Viewport3D::timerCallback() {}
void Viewport3D::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragStart = pressPosition = e.position.roundToInt();
    draggingTarget = false;
    selectingRegion=selection!=nullptr && selection->tool()!=djehuti::cad::CadSelection::Tool::pointer && !e.mods.isRightButtonDown() && !e.mods.isShiftDown() && !(wantsBodyPick && wantsBodyPick());
    regionEnd=e.position;
    if(selectingRegion) { repaint(); return; }
    if (!targetDragEnabled || e.mods.isRightButtonDown() || e.mods.isShiftDown() || onTargetDragged == nullptr) return;
    const auto posed = poseSource ? poseSource() : pose;
    juce::Point<float> handle;
    if (posed == nullptr || !posed->dragHandle.active || !toScreen(posed->dragHandle.point, handle) || handle.getDistanceFrom(e.position) > 14.0f) return;
    draggingTarget = true;
    dragTargetId = posed->dragHandle.targetId;
    dragPlanePoint = posed->dragHandle.point; dragPlaneNormal = posed->dragHandle.normal;
}
void Viewport3D::mouseUp(const juce::MouseEvent& e)
{
    if (draggingTarget)
    {
        draggingTarget = false;
        std::array<float, 3> point {};
        if (dragPoint(e.position, point) && onTargetDragged) onTargetDragged(dragTargetId, point, true);
        return;
    }
    if(e.mods.isPopupMenu() && e.position.roundToInt().getDistanceFrom(pressPosition)<=3) { selectingRegion=false; showContextMenu(e); return; }
    if(selectingRegion)
    {
        selectingRegion=false;
        auto picked=pickRegion(pressPosition.toFloat(),e.position,selection->tool()==djehuti::cad::CadSelection::Tool::circle);
        if(e.mods.isCtrlDown()) { auto prior=selection->items(); for(const auto& p:picked) if(std::find(prior.begin(),prior.end(),p)==prior.end()) prior.push_back(p); picked=std::move(prior); }
        selection->setSelectedMany(std::move(picked)); markDirty(); repaint();
        if(onContextCommand) onContextCommand("inspectRegion",selection->selected());
        return;
    }
    if (e.mods.isRightButtonDown() || e.mods.isShiftDown() || e.position.roundToInt().getDistanceFrom(pressPosition) > 3) return;
    if(onMarkerPicked)
    {
        float nearest=10;juce::String marker;
        for(const auto& [id,point]:pickMarkers){juce::Point<float> screen;if(toScreen(point,screen)&&screen.getDistanceFrom(e.position)<nearest){nearest=screen.getDistanceFrom(e.position);marker=id;}}
        if(marker.isNotEmpty()){onMarkerPicked(marker);return;}
    }
    const auto picked=pick(e.position,selection!=nullptr && !(wantsBodyPick&&wantsBodyPick())?selection->mode():djehuti::cad::CadSelection::Mode::body);
    if(selection!=nullptr) selection->setSelected(picked);
    if(picked.face<0 && picked.edge<0)
    {
        if(onBodyPicked) onBodyPicked(picked.drawId);
        else if(onContextCommand) onContextCommand("inspect",picked);
    }
    markDirty();

}
// World ray (mm) through a component position, from the inverse of the current view-projection.
bool Viewport3D::rayThrough(juce::Point<float> position, std::array<float, 3>& origin, std::array<float, 3>& direction) const
{
    if (getWidth() <= 0 || getHeight() <= 0) return false;
    const auto vp = mul(projectionFor(getWidth(), getHeight(), distance, juce::jmax(sceneRadius, clipRadius)), camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY));
    // General 4x4 inverse (column-major), cofactor expansion.
    const float* a = vp.v; float inv[16];
    inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
    inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
    inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
    inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
    inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
    inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
    const auto d = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::abs(d) < 1.0e-20f) return false;
    const float ndcX = position.x / (float) getWidth() * 2.0f - 1.0f, ndcY = 1.0f - position.y / (float) getHeight() * 2.0f;
    auto unproject = [&](float z, std::array<float, 3>& out)
    {
        float w = 0.0f;
        for (int row = 0; row < 4; ++row)
        {
            const auto value = (inv[row] * ndcX + inv[4 + row] * ndcY + inv[8 + row] * z + inv[12 + row]) / d;
            if (row < 3) out[(size_t) row] = value; else w = value;
        }
        if (std::abs(w) < 1.0e-20f) return false;
        for (auto& c : out) c /= w;
        return true;
    };
    std::array<float, 3> nearPoint {}, farPoint {};
    if (!unproject(-1.0f, nearPoint) || !unproject(1.0f, farPoint)) return false;
    origin = nearPoint;
    for (size_t k = 0; k < 3; ++k) direction[k] = farPoint[k] - nearPoint[k];
    return true;
}
bool Viewport3D::toScreen(const std::array<float, 3>& p, juce::Point<float>& screen) const
{
    if (getWidth() <= 0 || getHeight() <= 0) return false;
    const auto vp = mul(projectionFor(getWidth(), getHeight(), distance, juce::jmax(sceneRadius, clipRadius)), camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY));
    float clip[4];
    for (int row = 0; row < 4; ++row) clip[row] = vp.v[row] * p[0] + vp.v[4 + row] * p[1] + vp.v[8 + row] * p[2] + vp.v[12 + row];
    if (clip[3] <= 1.0e-6f) return false;
    screen = { (clip[0] / clip[3] + 1.0f) * 0.5f * (float) getWidth(), (1.0f - clip[1] / clip[3]) * 0.5f * (float) getHeight() };
    return true;
}
// Intersection of the view ray with the drag plane captured at the press.
bool Viewport3D::dragPoint(juce::Point<float> position, std::array<float, 3>& point) const
{
    std::array<float, 3> o {}, dir {};
    if (!rayThrough(position, o, dir)) return false;
    const auto& n = dragPlaneNormal;
    const auto denom = dir[0] * n[0] + dir[1] * n[1] + dir[2] * n[2];
    if (std::abs(denom) < 1.0e-9f) return false;   // ray parallel to the plane
    const auto t = ((dragPlanePoint[0] - o[0]) * n[0] + (dragPlanePoint[1] - o[1]) * n[1] + (dragPlanePoint[2] - o[2]) * n[2]) / denom;
    if (t < 0.0f) return false;
    for (size_t k = 0; k < 3; ++k) point[k] = o[k] + t * dir[k];
    return true;
}
juce::String Viewport3D::pickBody(juce::Point<float> position) const { return pick(position, djehuti::cad::CadSelection::Mode::body).drawId; }
djehuti::cad::CadPick Viewport3D::pick(juce::Point<float> position, djehuti::cad::CadSelection::Mode mode) const
{
    using Mode = djehuti::cad::CadSelection::Mode;
    djehuti::cad::CadPick result;
    if (pickSnapshot == nullptr || getWidth() <= 0 || getHeight() <= 0) return result;
    const auto viewProjection = mul(projectionFor(getWidth(), getHeight(), distance, juce::jmax(sceneRadius, clipRadius)), camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY));
    const auto w = (float) getWidth(), h = (float) getHeight();
    float bestDepth = 2.0f;
    struct Draw { const djehuti::cad::CadMesh* mesh; M4 model; juce::String id; };
    std::vector<Draw> draws;
    for (const auto& mesh : pickSnapshot->meshes)
    {
        if (pose == nullptr || pose->instancedFeatures.count(mesh.featureId) == 0) draws.push_back({ &mesh, identity(), mesh.featureId });
        if (pose != nullptr)
            for (const auto& instance : pose->instances)
                if (instance.featureId == mesh.featureId) { M4 m; std::memcpy(m.v, instance.matrix.data(), sizeof(m.v)); draws.push_back({ &mesh, m, instance.instanceId }); }
    }
    for (const auto& draw : draws)
    {
        const auto& mesh = *draw.mesh;
        const auto mvp = mul(viewProjection, draw.model);
        const auto project = [&](std::uint32_t index, juce::Point<float>& screen, float& depth)
        {
            const auto* v = mesh.vertices.data() + index * 6u;
            float clip[4];
            for (int row = 0; row < 4; ++row) clip[row] = mvp.v[row] * v[0] + mvp.v[4 + row] * v[1] + mvp.v[8 + row] * v[2] + mvp.v[12 + row];
            if (clip[3] <= 1.0e-6f) return false;
            screen = { (clip[0] / clip[3] + 1.0f) * 0.5f * w, (1.0f - clip[1] / clip[3]) * 0.5f * h }; depth = clip[2] / clip[3];
            return true;
        };
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            juce::Point<float> a, b, c; float da = 0, db = 0, dc = 0;
            if (!project(mesh.indices[i], a, da) || !project(mesh.indices[i + 1], b, db) || !project(mesh.indices[i + 2], c, dc)) continue;
            const auto area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (std::abs(area) < 1.0e-6f) continue;
            const auto u = ((b.x - position.x) * (c.y - position.y) - (c.x - position.x) * (b.y - position.y)) / area;
            const auto v = ((c.x - position.x) * (a.y - position.y) - (a.x - position.x) * (c.y - position.y)) / area;
            const auto t = 1.0f - u - v;
            if (u < 0.0f || v < 0.0f || t < 0.0f) continue;
            const auto depth = u * da + v * db + t * dc;
            if (depth >= -1.0f && depth < bestDepth) { bestDepth = depth; result.drawId = draw.id; result.featureId = mesh.featureId; result.face = mesh.faceIndex; }
        }
    }
    if (mode == Mode::body) { result.face = -1; return result; }
    if (mode == Mode::face) return result;
    // Edge mode: the nearest edge within 7 px whose depth there is not behind the surface under the cursor.
    djehuti::cad::CadPick edgePick; float bestDistance = 7.0f;
    for (const auto& line : pickSnapshot->edges)
    {
        std::vector<std::pair<M4, juce::String>> placements;
        if (pose == nullptr || pose->instancedFeatures.count(line.featureId) == 0) placements.push_back({ identity(), line.featureId });
        if (pose != nullptr) for (const auto& instance : pose->instances) if (instance.featureId == line.featureId) { M4 m; std::memcpy(m.v, instance.matrix.data(), sizeof(m.v)); placements.push_back({ m, instance.instanceId }); }
        for (const auto& [model, id] : placements)
        {
            const auto mvp = mul(viewProjection, model);
            const auto project = [&](size_t i, juce::Point<float>& screen, float& depth)
            {
                const auto* v = line.points.data() + i * 3u; float clip[4];
                for (int row = 0; row < 4; ++row) clip[row] = mvp.v[row] * v[0] + mvp.v[4 + row] * v[1] + mvp.v[8 + row] * v[2] + mvp.v[12 + row];
                if (clip[3] <= 1.0e-6f) return false;
                screen = { (clip[0] / clip[3] + 1.0f) * 0.5f * w, (1.0f - clip[1] / clip[3]) * 0.5f * h }; depth = clip[2] / clip[3];
                return true;
            };
            for (size_t i = 0; i + 1 < line.points.size() / 3; ++i)
            {
                juce::Point<float> a, b; float da = 0, db = 0;
                if (!project(i, a, da) || !project(i + 1, b, db)) continue;
                const auto ab = b - a; const auto len2 = ab.x * ab.x + ab.y * ab.y;
                const auto t = len2 > 0.0f ? juce::jlimit(0.0f, 1.0f, ((position.x - a.x) * ab.x + (position.y - a.y) * ab.y) / len2) : 0.0f;
                const auto closest = a + ab * t; const auto distanceToEdge = closest.getDistanceFrom(position);
                const auto depth = da + (db - da) * t;
                if (distanceToEdge < bestDistance && (bestDepth > 1.5f || depth <= bestDepth + 0.002f)) { bestDistance = distanceToEdge; edgePick = { id, line.featureId, -1, line.edgeIndex }; }
            }
        }
    }
    return edgePick;
}
void Viewport3D::mouseMove(const juce::MouseEvent& e)
{
    if (selection == nullptr || selection->mode() == djehuti::cad::CadSelection::Mode::body) return;
    selection->setHover(pick(e.position, selection->mode()));
    if (onFrameRequested) onFrameRequested();
}
void Viewport3D::mouseExit(const juce::MouseEvent&) { if (selection != nullptr) selection->setHover({}); }
void Viewport3D::setInspectionLines(std::vector<djehuti::cad::CadOverlayLine> next) { { const juce::SpinLock::ScopedLockType lock(highlightLock); glInspection = std::move(next); } markDirty(); }
void Viewport3D::setHighlights(std::vector<std::pair<juce::String, juce::Colour>> next) { { const juce::SpinLock::ScopedLockType lock(highlightLock); if (highlights == next) return; highlights = std::move(next); } markDirty(); }
void Viewport3D::mouseDrag(const juce::MouseEvent& e)
{
    if(selectingRegion) { regionEnd=e.position; repaint(); return; }
    if (draggingTarget)
    {
        std::array<float, 3> point {};
        if (dragPoint(e.position, point) && onTargetDragged) onTargetDragged(dragTargetId, point, false);
        if (onFrameRequested) onFrameRequested();
        return;
    }
    const auto now = e.position.roundToInt(), d = now - dragStart; if (e.mods.isRightButtonDown() || e.mods.isShiftDown()) { panX += d.x * 0.01f; panY -= d.y * 0.01f; } else { yaw += d.x * 0.01f; pitch = juce::jlimit(-1.4f, 1.4f, pitch + d.y * 0.01f); } dragStart = now; cameraChanged(); }
void Viewport3D::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const auto minimumDistance = juce::jmax(0.001f, sceneRadius * 0.02f);
    const auto maximumDistance = juce::jmax(1000.0f, sceneRadius * 100000.0f);
    distance = juce::jlimit(minimumDistance, maximumDistance, distance * std::exp(-w.deltaY * 0.5f)); cameraChanged();
}

juce::var Viewport3D::cameraState() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("yawDeg", (double)juce::radiansToDegrees(yaw)); o->setProperty("pitchDeg", (double)juce::radiansToDegrees(pitch));
    o->setProperty("distance", distance); o->setProperty("panX", panX); o->setProperty("panY", panY);
    o->setProperty("targetX", targetX); o->setProperty("targetY", targetY); o->setProperty("targetZ", targetZ);
    o->setProperty("showing", isShowing()); return o;
}
bool Viewport3D::controlCamera(const juce::var& args, juce::String& error)
{
    const auto op = args.getProperty("operation", "set").toString();
    if (op == "fit") { fitAll(); return true; }
    if (op == "reset") { resetCamera(); return true; }
    if (op == "state") return true;
    if (op != "set") { error = "Camera operation must be set, fit, reset or state."; return false; }
    for (const auto* key : { "yawDeg", "pitchDeg", "distance", "panX", "panY" }) if (args.hasProperty(key))
    {
        const auto value = args[key];
        if (!(value.isDouble() || value.isInt() || value.isInt64()) || !std::isfinite((double)value) || std::abs((double)value) > 1.0e9)
        { error = juce::String(key) + " must be a finite camera value within +/- 1e9."; return false; }
    }
    const auto minimum = juce::jmax(0.001f, sceneRadius * 0.02f), maximum = juce::jmax(1000.0f, sceneRadius * 100000.0f);
    const auto nextDistance = (double)args.getProperty("distance", distance);
    const auto nextPitch = (double)args.getProperty("pitchDeg", juce::radiansToDegrees(pitch));
    if (nextDistance < minimum || nextDistance > maximum || std::abs(nextPitch) > juce::radiansToDegrees(1.4))
    { error = "Camera distance or elevation exceeds the same bounds used by mouse navigation."; return false; }
    yaw = (float)juce::degreesToRadians(std::remainder((double)args.getProperty("yawDeg", juce::radiansToDegrees(yaw)),360.0));
    pitch = (float)juce::degreesToRadians(nextPitch); distance = (float)nextDistance;
    panX = (float)(double)args.getProperty("panX", panX); panY = (float)(double)args.getProperty("panY", panY);
    cameraChanged(); markDirty(); return true;
}
void Viewport3D::resetCamera() { yaw = homeYaw; pitch = homePitch; panX = panY = 0.0f; userCameraChanged = false; if (hasSceneBounds) frameScene(); else { distance = homeDistance; targetX = targetY = targetZ = 0.0f; fitPending = true; } markDirty(); }
void Viewport3D::fitAll() { userCameraChanged = false; if (hasSceneBounds) frameScene(); else fitPending = true; markDirty(); }
void Viewport3D::requestFit(std::uint64_t documentInstance) { fitPending = true; fitDocument = documentInstance; userCameraChanged = false; tryPendingFit(); markDirty(); }
// The user moved the camera: their view wins over any fit still waiting for geometry.
void Viewport3D::cameraChanged() { userCameraChanged = true; fitPending = false; if (onFrameRequested) onFrameRequested(); }
void Viewport3D::markDirty() { renderDirty = true; if (onFrameRequested) onFrameRequested(); }
// A pending fit uses the posed bounds of the drawn geometry, so it waits for the view size and for a runtime
// pose computed from the same document state; an older pose would place instances where they are not drawn.
void Viewport3D::tryPendingFit()
{
    if (!fitPending || !hasSceneBounds || getWidth() <= 0 || getHeight() <= 0) return;
    if (fitDocument != 0 && boundsDocument != fitDocument) return;
    if (poseSource != nullptr && (pose == nullptr || pose->documentInstance != boundsDocument || pose->documentRevision != boundsRevision)) return;
    frameScene();
    fitPending = false;
    renderDirty = true;
}
void Viewport3D::updateSceneBounds(const std::shared_ptr<const djehuti::cad::CadRenderSnapshot>& snapshot)
{
    if (snapshot == nullptr || (snapshot->revision == boundsRevision && snapshot->documentInstance == boundsDocument)) return;
    boundsRevision = snapshot->revision;
    boundsDocument = snapshot->documentInstance;
    featureBounds.clear();
    for (const auto& mesh : snapshot->meshes)
        for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 6)
        {
            const float p[3] { mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2] };
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) continue;
            const bool fresh = featureBounds.find(mesh.featureId) == featureBounds.end();
            auto& b = featureBounds[mesh.featureId];
            for (int k = 0; k < 3; ++k) { b.min[k] = fresh ? p[k] : juce::jmin(b.min[k], p[k]); b.max[k] = fresh ? p[k] : juce::jmax(b.max[k], p[k]); }
        }
    updatePosedBounds();
}
// Posed scene bounds drive the initial framing and the clipping range; motion never refits the user's camera.
void Viewport3D::updatePosedBounds()
{
    bool found = false;
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f, maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
    std::vector<V3> corners;
    auto addBox = [&](const Bounds& b, const float* matrix)
    {
        for (int c = 0; c < 8; ++c)
        {
            const V3 local { (c & 1) ? b.max[0] : b.min[0], (c & 2) ? b.max[1] : b.min[1], (c & 4) ? b.max[2] : b.min[2] };
            const V3 p = matrix == nullptr ? local : V3 { matrix[0] * local.x + matrix[4] * local.y + matrix[8] * local.z + matrix[12], matrix[1] * local.x + matrix[5] * local.y + matrix[9] * local.z + matrix[13], matrix[2] * local.x + matrix[6] * local.y + matrix[10] * local.z + matrix[14] };
            corners.push_back(p);
            if (!found) { minX = maxX = p.x; minY = maxY = p.y; minZ = maxZ = p.z; found = true; }
            else { minX = juce::jmin(minX, p.x); minY = juce::jmin(minY, p.y); minZ = juce::jmin(minZ, p.z); maxX = juce::jmax(maxX, p.x); maxY = juce::jmax(maxY, p.y); maxZ = juce::jmax(maxZ, p.z); }
        }
    };
    for (const auto& [id, b] : featureBounds) if (pose == nullptr || pose->instancedFeatures.count(id) == 0) addBox(b, nullptr);
    if (pose != nullptr) for (const auto& instance : pose->instances) if (const auto b = featureBounds.find(instance.featureId); b != featureBounds.end()) addBox(b->second, instance.matrix.data());
    hasSceneBounds = found;
    if (found)
    {
        sceneCenterX = (minX + maxX) * 0.5f; sceneCenterY = (minY + maxY) * 0.5f; sceneCenterZ = (minZ + maxZ) * 0.5f;
        const auto dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
        sceneRadius = juce::jmax(0.001f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
        tryPendingFit();
    }
    else if (fitPending) { targetX = targetY = targetZ = 0.0f; sceneRadius = emptySceneRadius; distance = homeDistance; }   // empty document: millimetre-scale working area
    clipRadius = sceneRadius;
    for (const auto& c : corners) clipRadius = juce::jmax(clipRadius, length(sub(c, { targetX, targetY, targetZ })));
}
void Viewport3D::frameScene()
{
    const auto aspect = getHeight() > 0 ? juce::jmax(0.01f, (float) getWidth() / (float) getHeight()) : 1.0f;
    const auto verticalHalfFov = 0.85f * 0.5f;
    const auto horizontalHalfFov = std::atan(std::tan(verticalHalfFov) * aspect);
    const auto limitingHalfFov = juce::jmin(verticalHalfFov, horizontalHalfFov);
    const auto margin = juce::jmax(0.001f, sceneRadius) * 1.25f;
    targetX = sceneCenterX; targetY = sceneCenterY; targetZ = sceneCenterZ;
    distance = juce::jmax(0.001f, margin / std::tan(limitingHalfFov));
    panX = panY = 0.0f;
}
void Viewport3D::setSnapshot(std::shared_ptr<const djehuti::cad::CadRenderSnapshot> next) { pickSnapshot = next; updateSceneBounds(next); pendingSnapshot = std::move(next); markDirty(); }
juce::String Viewport3D::diagnostics() const { return glStatus; }
bool Viewport3D::prepareFrame(float sharedAngle, std::uint64_t serial)
{
    // Hidden views (other tab, minimised window) do no work and catch up when shown again.
    if (!isShowing()) { wasShowing = false; return false; }
    if (!wasShowing) { wasShowing = true; renderDirty = true; }
    // JUCE creates the GL context inside a requested render and waits for the next request if it cannot yet;
    // keep requesting until the context is ready, then render only on change.
    if (!glReady) { renderDirty = true; context.triggerRepaint(); return false; }
    if (poseSource != nullptr)
        if (auto latest = poseSource(); latest != pose)
        {
            pose = std::move(latest);
            updatePosedBounds();
            { const juce::SpinLock::ScopedLockType lock(highlightLock); glPose = pose; }
            renderDirty = true;
        }
    if (selection != nullptr && selection->serial() != shownSelectionSerial)
    {
        shownSelectionSerial = selection->serial();
        { const juce::SpinLock::ScopedLockType lock(highlightLock); glHover = selection->hover(); glSelected = selection->selected(); glChosen=selection->items(); }
        renderDirty = true;
    }
    tryPendingFit();
    const auto p = projectionFor(getWidth(), getHeight(), distance, juce::jmax(sceneRadius, clipRadius)); const auto v = camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY); const auto m = identity(); juce::ignoreUnused(sharedAngle);
    std::array<float, 32> cameraState {};
    std::memcpy(cameraState.data(), p.v, sizeof(p.v)); std::memcpy(cameraState.data() + 16, v.v, sizeof(v.v));
    if (!renderDirty.exchange(false) && hasPublished && cameraState == lastCamera) return false;   // nothing changed: no frame, no render
    updateReferenceGuides();
    DjehutiFramePacket packet; std::memcpy(packet.model.v, m.v, sizeof(m.v)); std::memcpy(packet.view.v, v.v, sizeof(v.v)); std::memcpy(packet.projection.v, p.v, sizeof(p.v)); packet.serial = serial; packet.angle = sharedAngle;
    if (frames.publish(packet)) { lastCamera = cameraState; hasPublished = true; }
    else renderDirty = true;   // every slot is still in flight: the render below retires fences, then retry next tick
    context.triggerRepaint();
    return true;
}

void Viewport3D::newOpenGLContextCreated()
{
    const char* vertex = "#version 410 core\nlayout(location=0) in vec3 position; layout(location=1) in vec3 normal; layout(std140) uniform FrameData { mat4 uMVP; mat4 uModel; }; uniform mat4 uInstance; out vec3 n; void main(){ n=mat3(uModel)*mat3(uInstance)*normal; gl_Position=uMVP*uInstance*vec4(position,1.0); }";
    const char* fragment = "#version 410 core\nin vec3 n; uniform vec3 uColour; out vec4 colour; void main(){ vec3 l=normalize(vec3(0.4,0.8,0.6)); float d=max(dot(normalize(n),l),0.0); colour=vec4(uColour*(0.25+0.75*d),1.0); }";
    shader = std::make_unique<juce::OpenGLShaderProgram>(context);
    if (!shader->addVertexShader(vertex) || !shader->addFragmentShader(fragment) || !shader->link()) { glStatus = "Shader error: " + shader->getLastError(); juce::Logger::writeToLog(glStatus); shader.reset(); repaint(); return; }
    positionAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "position"); normalAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "normal");
    glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW); glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW); glGenBuffers(3, frameBuffers.data());
    for (auto b : frameBuffers) { glBindBuffer(GL_UNIFORM_BUFFER, b); glBufferData(GL_UNIFORM_BUFFER, sizeof(DjehutiMat4) * 2, nullptr, GL_STREAM_DRAW); }
    const auto block = glGetUniformBlockIndex(shader->getProgramID(), "FrameData"); if (block != GL_INVALID_INDEX) glUniformBlockBinding(shader->getProgramID(), block, 0);
    glEnableVertexAttribArray(positionAttribute->attributeID); glVertexAttribPointer(positionAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr); glEnableVertexAttribArray(normalAttribute->attributeID); glVertexAttribPointer(normalAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float))); glBindVertexArray(0); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
    // Joint axis indications share the shader: same vertex layout, drawn as lines.
    glGenVertexArrays(1, &axisVao); glBindVertexArray(axisVao); glGenBuffers(1, &axisVbo); glBindBuffer(GL_ARRAY_BUFFER, axisVbo); glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(positionAttribute->attributeID); glVertexAttribPointer(positionAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr); glEnableVertexAttribArray(normalAttribute->attributeID); glVertexAttribPointer(normalAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float))); glBindVertexArray(0);
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION)); const auto* shading = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)); glStatus = juce::String("OpenGL ") + (version != nullptr ? version : "version unavailable") + " / GLSL " + (shading != nullptr ? shading : "unavailable") + " / 3 UBO frames"; glReady = true; renderDirty = true;   // a new context needs a fresh frame
}
void Viewport3D::renderOpenGL()
{
    if (!glReady || shader == nullptr || getWidth() <= 0 || getHeight() <= 0) return;
    for (int i = 0; i < 3; ++i) if (fences[static_cast<size_t>(i)] != nullptr && glClientWaitSync(fences[static_cast<size_t>(i)], 0, 0) != GL_TIMEOUT_EXPIRED) { glDeleteSync(fences[static_cast<size_t>(i)]); fences[static_cast<size_t>(i)] = nullptr; frames.complete(i); }
    if (pendingSnapshot != nullptr && (pendingSnapshot->revision != uploadedGeometryRevision || pendingSnapshot->documentInstance != uploadedGeometryDocument))
    {
        std::vector<float> vertices;
        std::vector<std::uint32_t> indices;
        meshRanges.clear();
        for (const auto& mesh : pendingSnapshot->meshes)
        {
            meshRanges.push_back({ mesh.featureId, static_cast<GLsizei>(indices.size()), static_cast<GLsizei>(mesh.indices.size()), mesh.faceIndex });
            const auto base = static_cast<std::uint32_t>(vertices.size() / 6);
            vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            for (const auto index : mesh.indices) indices.push_back(base + index);
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_DYNAMIC_DRAW);
        geometryIndexCount = static_cast<GLsizei>(indices.size());
        uploadedGeometryRevision = pendingSnapshot->revision; uploadedGeometryDocument=pendingSnapshot->documentInstance;
    }
    DjehutiFramePacket packet; int slot = -1; if (frames.consumeNewest(packet, slot)) { lastFrame = packet; hasFrame = true; lastSlot = slot; }
    if (!hasFrame) return;
    // JUCE paints the component overlay after renderOpenGL() and leaves GL_DEPTH_TEST disabled, so restore the
    // 3D pipeline state every frame; otherwise parts are layered by draw order instead of depth while orbiting.
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); glDisable(GL_BLEND);
    const auto scale = static_cast<float>(context.getRenderingScale()); glViewport(0, 0, juce::roundToInt(scale * (float) getWidth()), juce::roundToInt(scale * (float) getHeight())); glClearColor(0.035f, 0.055f, 0.075f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto activeSlot = static_cast<size_t>(lastSlot); M4 projection, viewMatrix, model; std::memcpy(projection.v, lastFrame.projection.v, sizeof(projection.v)); std::memcpy(viewMatrix.v, lastFrame.view.v, sizeof(viewMatrix.v)); std::memcpy(model.v, lastFrame.model.v, sizeof(model.v)); const auto finalMvp = mul(projection, mul(viewMatrix, model));
    if (slot >= 0)
    {
        DjehutiMat4 matrices[2]; std::memcpy(matrices[0].v, finalMvp.v, sizeof(finalMvp.v)); matrices[1] = lastFrame.model;
        glBindBuffer(GL_UNIFORM_BUFFER, frameBuffers[activeSlot]); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(matrices), matrices);
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameBuffers[activeSlot]); shader->use();
    if (geometryIndexCount > 0)
    {
        std::vector<std::pair<juce::String, juce::Colour>> colours;
        std::shared_ptr<const djehuti::cad::CadPoseSnapshot> posed;
        djehuti::cad::CadPick hoverPick, selectedPick;
        std::vector<djehuti::cad::CadPick> pickedItems;
        { const juce::SpinLock::ScopedLockType lock(highlightLock); colours = highlights; posed = glPose; hoverPick = glHover; selectedPick = glSelected; pickedItems=glChosen; }
        const auto geometry = pendingSnapshot;
        const auto colourOf = [&](const juce::String& id) { juce::Colour colour(0xff2e9ec7); for (const auto& item : colours) if (item.first == id) colour = item.second; for(const auto& p:pickedItems) if(p.drawId==id && p.face<0 && p.edge<0) colour=juce::Colour(0xffff4fd8); return colour; };
        const auto draw = [&](const MeshRange& range, const float* matrix, const juce::String& id)
        {
            const auto colour = colourOf(id);
            shader->setUniformMat4("uInstance", matrix, 1, GL_FALSE);
            shader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
            glDrawElements(GL_TRIANGLES, range.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<size_t>(range.first) * sizeof(std::uint32_t)));
        };
        const auto unit = identity();
        glBindVertexArray(vao);
        for (const auto& range : meshRanges)
        {
            // A body used by assembly instances is drawn only at its instance poses, never also at its authored position.
            if (posed == nullptr || posed->instancedFeatures.count(range.featureId) == 0) draw(range, unit.v, range.featureId);
            if (posed != nullptr) for (const auto& instance : posed->instances) if (instance.featureId == range.featureId) draw(range, instance.matrix.data(), instance.instanceId);
        }
        glBindVertexArray(0);
        // Picked faces and edges: hover in light cyan, the selection in magenta, with the same instance transform as
        // the body they belong to (only the instance that was hit).
        const auto matrixFor = [&](const djehuti::cad::CadPick& p, M4& m)
        {
            m = identity();
            if (posed == nullptr || posed->instancedFeatures.count(p.featureId) == 0) return p.drawId == p.featureId;
            for (const auto& instance : posed->instances) if (instance.instanceId == p.drawId) { std::memcpy(m.v, instance.matrix.data(), sizeof(m.v)); return true; }
            return false;
        };
        const auto drawLines = [&](const std::vector<float>& points, bool strip, const float* matrix, juce::Colour colour)
        {
            if (points.size() < 6 || axisVao == 0) return;
            std::vector<float> data;
            const float light[3] { 0.4f / 1.0770f, 0.8f / 1.0770f, 0.6f / 1.0770f };
            for (size_t i = 0; i + 2 < points.size(); i += 3) { data.insert(data.end(), { points[i], points[i + 1], points[i + 2] }); data.insert(data.end(), { light[0], light[1], light[2] }); }
            glBindBuffer(GL_ARRAY_BUFFER, axisVbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
            glDisable(GL_DEPTH_TEST);
            glBindVertexArray(axisVao);
            shader->setUniformMat4("uInstance", matrix, 1, GL_FALSE);
            shader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
            glDrawArrays(strip ? GL_LINE_STRIP : GL_LINES, 0, static_cast<GLsizei>(points.size() / 3));
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        };
        std::vector<std::pair<djehuti::cad::CadPick,juce::Colour>> pickColours {{hoverPick,juce::Colour(0xff9be7ff)}};
        for(const auto& p:pickedItems) pickColours.push_back({p,juce::Colour(0xffff4fd8)});
        for (const auto& [p, colour] : pickColours)
        {
            M4 m;
            if (!p.valid() || !matrixFor(p, m)) continue;
            if (p.face >= 0)
            {
                glDepthFunc(GL_LEQUAL); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f);
                glBindVertexArray(vao);
                shader->setUniformMat4("uInstance", m.v, 1, GL_FALSE);
                shader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
                for (const auto& range : meshRanges)
                    if (range.featureId == p.featureId && range.faceIndex == p.face)
                        glDrawElements(GL_TRIANGLES, range.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<size_t>(range.first) * sizeof(std::uint32_t)));
                glBindVertexArray(0);
                glDisable(GL_POLYGON_OFFSET_FILL); glDepthFunc(GL_LESS);
            }
            if (p.edge >= 0 && geometry != nullptr)
                for (const auto& line : geometry->edges) if (line.featureId == p.featureId && line.edgeIndex == p.edge) drawLines(line.points, true, m.v, colour);
        }
        // Visible sketches in their resolved 3-D plane (pairs of points per segment).
        if (geometry != nullptr) for (const auto& outline : geometry->sketchOutlines) drawLines(outline.points, false, unit.v, juce::Colour(0xffe8d27a));
        if (posed != nullptr && !posed->overlay.empty() && axisVao != 0)
        {
            // IK desired/achieved markers and the drag plane, computed by the runtime from the drawn pose.
            std::vector<float> lines;
            const float light[3] { 0.4f / 1.0770f, 0.8f / 1.0770f, 0.6f / 1.0770f };
            for (const auto& line : posed->overlay)
                for (const auto* p : { &line.a, &line.b }) { lines.insert(lines.end(), { (*p)[0], (*p)[1], (*p)[2] }); lines.insert(lines.end(), { light[0], light[1], light[2] }); }
            glBindBuffer(GL_ARRAY_BUFFER, axisVbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(float)), lines.data(), GL_DYNAMIC_DRAW);
            glDisable(GL_DEPTH_TEST);
            glBindVertexArray(axisVao);
            shader->setUniformMat4("uInstance", unit.v, 1, GL_FALSE);
            for (size_t i = 0; i < posed->overlay.size(); ++i)
            {
                const juce::Colour colour(posed->overlay[i].argb);
                shader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
                glDrawArrays(GL_LINES, static_cast<GLint>(i * 2), 2);
            }
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        }
        // Interference regions from the last inspection, visible through the parts that enclose them.
        std::vector<djehuti::cad::CadOverlayLine> inspection;
        { const juce::SpinLock::ScopedLockType lock(highlightLock); inspection = glInspection; }
        if (!inspection.empty() && axisVao != 0)
        {
            std::vector<float> lines;
            const float light[3] { 0.4f / 1.0770f, 0.8f / 1.0770f, 0.6f / 1.0770f };
            for (const auto& line : inspection)
                for (const auto* p : { &line.a, &line.b }) { lines.insert(lines.end(), { (*p)[0], (*p)[1], (*p)[2] }); lines.insert(lines.end(), { light[0], light[1], light[2] }); }
            glBindBuffer(GL_ARRAY_BUFFER, axisVbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(float)), lines.data(), GL_DYNAMIC_DRAW);
            glDisable(GL_DEPTH_TEST);
            glBindVertexArray(axisVao);
            shader->setUniformMat4("uInstance", unit.v, 1, GL_FALSE);
            const juce::Colour colour(inspection.front().argb);
            shader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(inspection.size() * 2));
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        }
    }
    // References use the same shader/depth buffer as the solid, without contributing to model bounds or picking.
    std::vector<djehuti::cad::CadOverlayLine> guides;
    { const juce::SpinLock::ScopedLockType lock(highlightLock); guides=glGuides; }
    std::map<juce::uint32,std::vector<float>> batches;
    for(const auto& line:guides) for(const auto& p:{line.a,line.b}) { auto& v=batches[line.argb]; v.insert(v.end(),{p[0],p[1],p[2],.3714f,.7428f,.5571f}); }
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); glBindVertexArray(axisVao);
    const auto unit=identity(); shader->setUniformMat4("uInstance",unit.v,1,GL_FALSE);
    for(const auto& [argb,vertices]:batches)
    {
        const juce::Colour colour(argb); shader->setUniform("uColour",colour.getFloatRed(),colour.getFloatGreen(),colour.getFloatBlue());
        glBindBuffer(GL_ARRAY_BUFFER,axisVbo); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES,0,(GLsizei)(vertices.size()/6));
    }
    glBindVertexArray(0); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
    if (slot >= 0) fences[activeSlot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}
void Viewport3D::drawCube(const float*) {}
void Viewport3D::releaseGL() { uploadedGeometryRevision=0; uploadedGeometryDocument=0; for (auto& f : fences) if (f != nullptr) { glDeleteSync(f); f = nullptr; } glDeleteBuffers(3, frameBuffers.data()); if (vao != 0) glDeleteVertexArrays(1, &vao); if (vbo != 0) glDeleteBuffers(1, &vbo); if (ibo != 0) glDeleteBuffers(1, &ibo); if (axisVao != 0) glDeleteVertexArrays(1, &axisVao); if (axisVbo != 0) glDeleteBuffers(1, &axisVbo); frameBuffers.fill(0); vao = vbo = ibo = axisVao = axisVbo = 0; frames.reset(); lastSlot = -1; hasFrame = false; renderDirty = true; uploadedGeometryRevision = 0; geometryIndexCount = 0; meshRanges.clear(); shader.reset(); positionAttribute.reset(); normalAttribute.reset(); glReady = false; }
void Viewport3D::openGLContextClosing() { releaseGL(); }

class ThreeDViewsPanel::ViewTile final : public juce::Component
{
public:
    ViewTile(int viewId, std::function<void(int)> close) : identifier(viewId), view(viewId), closeCallback(std::move(close)) { closeButton.setButtonText("x"); closeButton.onClick = [this] { closeCallback(identifier); }; addAndMakeVisible(view); addAndMakeVisible(closeButton); }
    int id() const { return identifier; }
    Viewport3D& viewport() { return view; }
    void resized() override { closeButton.setBounds(getWidth() - 28, 4, 24, 22); view.setBounds(0, 0, getWidth(), getHeight()); }
private: int identifier; Viewport3D view; juce::TextButton closeButton; std::function<void(int)> closeCallback;
};

ThreeDViewsPanel::ThreeDViewsPanel(std::shared_ptr<djehuti::cad::CadCommandService> service) : cadService(std::move(service))
{
    addAndMakeVisible(workspaceMenu);
    workspaceMenu.sections = [this] { return std::vector<WorkspaceMenu::Section>{ {"Views",{{1,"New 3D view",tiles.size()<6},{2,"Fit all views"},{3,"Reset cameras"}}}, {"Reference guides",{{10,"Toggle floor in all views"},{11,"Toggle world planes in all views"},{12,"Toggle selected local planes in all views"},{13,"Toggle joint frames and limits in all views"}}} }; };
    workspaceMenu.invoke = [this](int id)
    {
        if(id==1) addView(); else if(id==2) { for(auto* tile:tiles) tile->viewport().fitAll(); }
        else if(id==3) { for(auto* tile:tiles) tile->viewport().resetCamera(); }
        else if(id>=10 && id<=13) { const auto key=juce::StringArray{"floor","world","local","joints"}[id-10]; const bool enabled=tiles.size()>0 && tiles[0]->viewport().guideEnabled(key); for(auto* tile:tiles) if(tile->viewport().guideEnabled(key)==enabled) tile->viewport().toggleGuide(key); }
    };
    addAndMakeVisible(addButton); addAndMakeVisible(pauseButton); addAndMakeVisible(resetButton); addAndMakeVisible(fitButton); addButton.onClick = [this] { addView(); }; pauseButton.onClick = [this] { paused = !paused; pauseButton.setButtonText(paused ? "Resume" : "Pause"); }; resetButton.onClick = [this] { for (auto* tile : tiles) tile->viewport().resetCamera(); }; fitButton.onClick = [this] { for (auto* tile : tiles) tile->viewport().fitAll(); }; addView(); addView(); addView(); startTimerHz(60);
}
ThreeDViewsPanel::~ThreeDViewsPanel() { stopTimer(); }
void ThreeDViewsPanel::setSelection(std::shared_ptr<djehuti::cad::CadSelection> next) { selection = std::move(next); for (auto* tile : tiles) tile->viewport().setSelection(selection); }
void ThreeDViewsPanel::setPoseSource(std::function<std::shared_ptr<const djehuti::cad::CadPoseSnapshot>()> source) { poseSource = std::move(source); for (auto* tile : tiles) tile->viewport().setPoseSource(poseSource); }
bool ThreeDViewsPanel::addView() { if (tiles.size() >= 6) { addButton.setEnabled(false); return false; } static int nextId = 1; tiles.add(new ViewTile(nextId++, [this](int id) { closeView(id); })); tiles.getLast()->viewport().setPoseSource(poseSource); tiles.getLast()->viewport().onFrameRequested = [this] { wakeFrames(); }; tiles.getLast()->viewport().setSelection(selection); tiles.getLast()->viewport().onContextCommand=contextCommand; if(tiles.size()>1) tiles.getLast()->viewport().restoreViewState(tiles[0]->viewport().viewState()); addAndMakeVisible(tiles.getLast()); if (currentSnapshot != nullptr) tiles.getLast()->viewport().setSnapshot(currentSnapshot); addButton.setEnabled(tiles.size() < 6); resized(); return true; }
void ThreeDViewsPanel::closeView(int id) { if (tiles.size() <= 1) return; for (int i = tiles.size(); --i >= 0;) if (tiles[i]->id() == id) { tiles.remove(i); break; } addButton.setEnabled(tiles.size() < 6); resized(); }
// Views frame a newly opened or created document once; otherwise every camera stays as the user left it.
// Frames are published (and rendered) only for views whose camera, pose, geometry or visibility changed.
bool ThreeDViewsPanel::publishFrames()
{
    bool changed = false;
    if (cadService != nullptr)
    {
        const auto& document = cadService->document();
        if (document.documentInstance() != shownDocument)
        {
            shownDocument = document.documentInstance();
            for (auto* tile : tiles) tile->viewport().requestFit(shownDocument);
            shownRevision = ~std::uint64_t(0);
        }
        if (document.revision() != shownRevision)
        {
            currentSnapshot = cadService->snapshot(); shownRevision = currentSnapshot != nullptr ? currentSnapshot->revision : 0;
            for (auto* tile : tiles) tile->viewport().setSnapshot(currentSnapshot);
            changed = true;
        }
    }
    ++frameSerial;
    for (auto* tile : tiles) changed = tile->viewport().prepareFrame(sharedAngle, frameSerial) || changed;
    return changed;
}
void ThreeDViewsPanel::timerCallback()
{
    if (publishFrames()) wakeFrames();
    else if (fastFrames && ++idleFrameTicks >= 60) { fastFrames = false; startTimerHz(10); }
}
void ThreeDViewsPanel::wakeFrames()
{
    idleFrameTicks = 0;
    if (!fastFrames) { fastFrames = true; startTimerHz(60); }
}
void ThreeDViewsPanel::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff171b20)); }
void ThreeDViewsPanel::resized() { auto area = getLocalBounds().reduced(6); workspaceMenu.setBounds(area.removeFromTop(30)); addButton.setVisible(false); pauseButton.setVisible(false); resetButton.setVisible(false); fitButton.setVisible(false); const int columns = tiles.size() > 2 ? 2 : 1, rows = (tiles.size() + columns - 1) / columns, w = juce::jmax(1, area.getWidth() / columns), h = juce::jmax(1, area.getHeight() / rows); for (int i = 0; i < tiles.size(); ++i) tiles[i]->setBounds(area.getX() + (i % columns) * w + 4, area.getY() + (i / columns) * h + 4, w - 8, h - 8); }

void ThreeDViewsPanel::setContextCommand(std::function<void(const juce::String&,const djehuti::cad::CadPick&)> callback)
{ contextCommand=std::move(callback); for(auto* tile:tiles) tile->viewport().onContextCommand=contextCommand; }
juce::String ThreeDViewsPanel::viewState() const
{ juce::Array<juce::var> states; for(auto* tile:tiles) states.add(tile->viewport().viewState()); return juce::JSON::toString(states); }
void ThreeDViewsPanel::restoreViewState(const juce::String& json)
{
    const auto data=juce::JSON::parse(json);
    if(const auto* states=data.getArray(); states!=nullptr && !states->isEmpty())
    {
        const int count=juce::jlimit(1,6,states->size());
        while(tiles.size()>count) closeView(tiles.getLast()->id());
        while(tiles.size()<count) addView();
        for(int i=0;i<count;++i) tiles[i]->viewport().restoreViewState((*states)[i]);
    }
}

bool Viewport3D::keyPressed(const juce::KeyPress& key)
{
    if(key==juce::KeyPress::escapeKey) { selectingRegion=false; if(selection!=nullptr) selection->setTool(djehuti::cad::CadSelection::Tool::pointer); markDirty(); repaint(); return true; }
    if(key.getTextCharacter()=='f' && !key.getModifiers().isAnyModifierKeyDown()) { fitAll(); return true; }
    return false;
}


juce::var ThreeDViewsPanel::controlViewCamera(int view, const juce::var& args)
{
    juce::String error;
    auto* result = new juce::DynamicObject();
    if (!juce::isPositiveAndBelow(view, tiles.size())) error = "Unknown 3D view index.";
    else { tiles[view]->viewport().controlCamera(args, error); result->setProperty("camera", tiles[view]->viewport().cameraState()); }
    result->setProperty("ok", error.isEmpty()); if (error.isNotEmpty()) result->setProperty("error", error); return result;
}
