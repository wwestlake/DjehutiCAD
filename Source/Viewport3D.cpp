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
    const V3 eye = add(target, {distance * cp * sy, distance * sp, distance * cp * cy});
    const auto forward = normalized(sub(target, eye));
    const auto referenceUp = V3 {0.0f, 1.0f, 0.0f};
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
    const std::array<std::array<float, 3>, 3> cases {{{0.55f, 0.35f, 4.0f}, {juce::MathConstants<float>::halfPi, 0.0f, 4.0f}, {0.30f, -0.20f, 12.0f}}};
    const V3 target {13.0f, -7.0f, 29.0f};
    for (const auto& item : cases)
    {
        const auto m = camera(item[0], item[1], item[2], target.x, target.y, target.z, 0.0f, 0.0f);
        const V3 right {m.v[0], m.v[4], m.v[8]}, up {m.v[1], m.v[5], m.v[9]}, back {m.v[2], m.v[6], m.v[10]};
        if (!check(close(length(right), 1.0f, 1.0e-4f) && close(length(up), 1.0f, 1.0e-4f) && close(length(back), 1.0f, 1.0e-4f), "Camera basis is not unit length.")) return false;
        if (!check(close(dot(right, up), 0.0f, 1.0e-4f) && close(dot(right, back), 0.0f, 1.0e-4f) && close(dot(up, back), 0.0f, 1.0e-4f), "Camera basis is not orthogonal.")) return false;
        const auto determinant = dot(right, cross(up, back));
        if (!check(close(determinant, 1.0f, 1.0e-4f), "Camera basis is reflected.")) return false;
        const V3 targetInView {dot(right, target) + m.v[12], dot(up, target) + m.v[13], dot(back, target) + m.v[14]};
        if (!check(close(targetInView.x, 0.0f, 1.0e-3f) && close(targetInView.y, 0.0f, 1.0e-3f) && close(targetInView.z, -item[2], 1.0e-3f), "Camera target does not map to view center.")) return false;
        const V3 vector {1.25f, -2.0f, 0.75f};
        const V3 transformed {dot(right, vector), dot(up, vector), dot(back, vector)};
        if (!check(close(length(transformed), length(vector), 1.0e-4f), "Camera view changes world-space distance.")) return false;
    }
    detail = "Camera basis, determinant, target mapping, and distance preservation passed.";
    return true;
}

Viewport3D::Viewport3D(int number) : viewNumber(number)
{
    setOpaque(false); context.setRenderer(this); context.setContinuousRepainting(true); context.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1); context.attachTo(*this);
}
Viewport3D::~Viewport3D() { context.detach(); }
void Viewport3D::paint(juce::Graphics& g) { g.setColour(juce::Colour(0xffa9c8d2)); g.drawText(title.isNotEmpty() ? title : "View " + juce::String(viewNumber), 10, 8, juce::jmax(100, getWidth() - 20), 20, juce::Justification::left); g.setColour(juce::Colour(0xff8fa4ad)); g.setFont(juce::Font(10.0f)); g.drawFittedText(glStatus, 10, juce::jmax(24, getHeight() - 28), getWidth() - 20, 18, juce::Justification::left, 1); }
void Viewport3D::resized() { if (hasSceneBounds && !userCameraChanged) frameScene(); }
void Viewport3D::timerCallback() {}
void Viewport3D::mouseDown(const juce::MouseEvent& e) { dragStart = pressPosition = e.position.roundToInt(); }
void Viewport3D::mouseUp(const juce::MouseEvent& e)
{
    // A click without dragging picks a body; any drag stays pure camera navigation.
    if (e.mods.isRightButtonDown() || e.mods.isShiftDown() || e.position.roundToInt().getDistanceFrom(pressPosition) > 3 || onBodyPicked == nullptr) return;
    onBodyPicked(pickBody(e.position));
}
juce::String Viewport3D::pickBody(juce::Point<float> position) const
{
    if (pickSnapshot == nullptr || getWidth() <= 0 || getHeight() <= 0) return {};
    const auto viewProjection = mul(projectionFor(getWidth(), getHeight(), distance, juce::jmax(sceneRadius, clipRadius)), camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY));
    const auto w = (float) getWidth(), h = (float) getHeight();
    juce::String best; float bestDepth = 2.0f;
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
            if (depth >= -1.0f && depth < bestDepth) { bestDepth = depth; best = draw.id; }
        }
    }
    return best;
}
void Viewport3D::setHighlights(std::vector<std::pair<juce::String, juce::Colour>> next) { const juce::SpinLock::ScopedLockType lock(highlightLock); highlights = std::move(next); }
void Viewport3D::mouseDrag(const juce::MouseEvent& e) { const auto now = e.position.roundToInt(), d = now - dragStart; if (e.mods.isRightButtonDown() || e.mods.isShiftDown()) { panX += d.x * 0.01f; panY -= d.y * 0.01f; } else { yaw += d.x * 0.01f; pitch = juce::jlimit(-1.4f, 1.4f, pitch + d.y * 0.01f); } userCameraChanged = true; dragStart = now; }
void Viewport3D::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const auto minimumDistance = juce::jmax(0.001f, sceneRadius * 0.02f);
    const auto maximumDistance = juce::jmax(1000.0f, sceneRadius * 100000.0f);
    distance = juce::jlimit(minimumDistance, maximumDistance, distance * std::exp(-w.deltaY * 0.5f)); userCameraChanged = true;
}
void Viewport3D::resetCamera() { yaw = 0.55f; pitch = 0.35f; panX = panY = 0.0f; userCameraChanged = false; frameScene(); }
void Viewport3D::fitAll() { userCameraChanged = false; frameScene(); }
void Viewport3D::updateSceneBounds(const std::shared_ptr<const djehuti::cad::CadRenderSnapshot>& snapshot)
{
    if (snapshot == nullptr || snapshot->revision == boundsRevision) return;
    boundsRevision = snapshot->revision;
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
        if (!hasFramedInitialScene) { frameScene(); hasFramedInitialScene = true; }
    }
    else if (!hasFramedInitialScene) { targetX = targetY = targetZ = 0.0f; sceneRadius = 1.0f; }
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
void Viewport3D::setSnapshot(std::shared_ptr<const djehuti::cad::CadRenderSnapshot> next) { pickSnapshot = next; updateSceneBounds(next); pendingSnapshot = std::move(next); }
juce::String Viewport3D::diagnostics() const { return glStatus; }
void Viewport3D::prepareFrame(float sharedAngle, std::uint64_t serial)
{
    if (poseSource != nullptr)
        if (auto latest = poseSource(); latest != pose)
        {
            pose = std::move(latest);
            updatePosedBounds();
            const juce::SpinLock::ScopedLockType lock(highlightLock);
            glPose = pose;
        }
    const auto p = projectionFor(getWidth(), getHeight(), distance, juce::jmax(sceneRadius, clipRadius)); const auto v = camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY); const auto m = identity(); juce::ignoreUnused(sharedAngle);
    DjehutiFramePacket packet; std::memcpy(packet.model.v, m.v, sizeof(m.v)); std::memcpy(packet.view.v, v.v, sizeof(v.v)); std::memcpy(packet.projection.v, p.v, sizeof(p.v)); packet.serial = serial; packet.angle = sharedAngle; frames.publish(packet);
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
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION)); const auto* shading = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)); glStatus = juce::String("OpenGL ") + (version != nullptr ? version : "version unavailable") + " / GLSL " + (shading != nullptr ? shading : "unavailable") + " / 3 UBO frames"; glReady = true;
}
void Viewport3D::renderOpenGL()
{
    if (!glReady || shader == nullptr || getWidth() <= 0 || getHeight() <= 0) return;
    for (int i = 0; i < 3; ++i) if (fences[static_cast<size_t>(i)] != nullptr && glClientWaitSync(fences[static_cast<size_t>(i)], 0, 0) != GL_TIMEOUT_EXPIRED) { glDeleteSync(fences[static_cast<size_t>(i)]); fences[static_cast<size_t>(i)] = nullptr; frames.complete(i); }
    if (pendingSnapshot != nullptr && pendingSnapshot->revision != uploadedGeometryRevision)
    {
        std::vector<float> vertices;
        std::vector<std::uint32_t> indices;
        meshRanges.clear();
        for (const auto& mesh : pendingSnapshot->meshes)
        {
            meshRanges.push_back({ mesh.featureId, static_cast<GLsizei>(indices.size()), static_cast<GLsizei>(mesh.indices.size()) });
            const auto base = static_cast<std::uint32_t>(vertices.size() / 6);
            vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            for (const auto index : mesh.indices) indices.push_back(base + index);
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_DYNAMIC_DRAW);
        geometryIndexCount = static_cast<GLsizei>(indices.size());
        uploadedGeometryRevision = pendingSnapshot->revision;
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
        { const juce::SpinLock::ScopedLockType lock(highlightLock); colours = highlights; posed = glPose; }
        const auto colourOf = [&](const juce::String& id) { juce::Colour colour(0xff2e9ec7); for (const auto& item : colours) if (item.first == id) colour = item.second; return colour; };
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
    } if (slot >= 0) fences[activeSlot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}
void Viewport3D::drawCube(const float*) {}
void Viewport3D::releaseGL() { for (auto& f : fences) if (f != nullptr) { glDeleteSync(f); f = nullptr; } glDeleteBuffers(3, frameBuffers.data()); if (vao != 0) glDeleteVertexArrays(1, &vao); if (vbo != 0) glDeleteBuffers(1, &vbo); if (ibo != 0) glDeleteBuffers(1, &ibo); frameBuffers.fill(0); vao = vbo = ibo = 0; frames.reset(); lastSlot = -1; hasFrame = false; uploadedGeometryRevision = 0; geometryIndexCount = 0; meshRanges.clear(); shader.reset(); positionAttribute.reset(); normalAttribute.reset(); glReady = false; }
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
    addAndMakeVisible(addButton); addAndMakeVisible(pauseButton); addAndMakeVisible(resetButton); addAndMakeVisible(fitButton); addButton.onClick = [this] { addView(); }; pauseButton.onClick = [this] { paused = !paused; pauseButton.setButtonText(paused ? "Resume" : "Pause"); }; resetButton.onClick = [this] { for (auto* tile : tiles) tile->viewport().resetCamera(); }; fitButton.onClick = [this] { for (auto* tile : tiles) tile->viewport().fitAll(); }; addView(); addView(); addView(); startTimerHz(60);
}
ThreeDViewsPanel::~ThreeDViewsPanel() { stopTimer(); }
void ThreeDViewsPanel::setPoseSource(std::function<std::shared_ptr<const djehuti::cad::CadPoseSnapshot>()> source) { poseSource = std::move(source); for (auto* tile : tiles) tile->viewport().setPoseSource(poseSource); }
bool ThreeDViewsPanel::addView() { if (tiles.size() >= 6) { addButton.setEnabled(false); return false; } static int nextId = 1; tiles.add(new ViewTile(nextId++, [this](int id) { closeView(id); })); tiles.getLast()->viewport().setPoseSource(poseSource); addAndMakeVisible(tiles.getLast()); if (currentSnapshot != nullptr) tiles.getLast()->viewport().setSnapshot(currentSnapshot); addButton.setEnabled(tiles.size() < 6); resized(); return true; }
void ThreeDViewsPanel::closeView(int id) { if (tiles.size() <= 1) return; for (int i = tiles.size(); --i >= 0;) if (tiles[i]->id() == id) { tiles.remove(i); break; } addButton.setEnabled(tiles.size() < 6); resized(); }
void ThreeDViewsPanel::publishFrames() { if (cadService != nullptr && cadService->document().revision() != shownRevision) { currentSnapshot = cadService->snapshot(); shownRevision = currentSnapshot != nullptr ? currentSnapshot->revision : 0; for (auto* tile : tiles) tile->viewport().setSnapshot(currentSnapshot); } ++frameSerial; for (auto* tile : tiles) tile->viewport().prepareFrame(sharedAngle, frameSerial); }
void ThreeDViewsPanel::timerCallback() { publishFrames(); }
void ThreeDViewsPanel::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff171b20)); }
void ThreeDViewsPanel::resized() { auto area = getLocalBounds().reduced(10); auto bar = area.removeFromTop(32); addButton.setBounds(bar.removeFromLeft(120)); pauseButton.setBounds(bar.removeFromLeft(90).reduced(4, 0)); resetButton.setBounds(bar.removeFromLeft(130).reduced(4, 0)); fitButton.setBounds(bar.removeFromLeft(90).reduced(4, 0)); const int columns = tiles.size() > 2 ? 2 : 1, rows = (tiles.size() + columns - 1) / columns, w = juce::jmax(1, area.getWidth() / columns), h = juce::jmax(1, area.getHeight() / rows); for (int i = 0; i < tiles.size(); ++i) tiles[i]->setBounds(area.getX() + (i % columns) * w + 4, area.getY() + (i / columns) * h + 4, w - 8, h - 8); }
