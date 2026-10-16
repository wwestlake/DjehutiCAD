#include "Viewport3D.h"

#include <cmath>
#include <cstring>
using namespace juce::gl;

namespace
{
struct M4 { float v[16] {}; };
M4 identity() { M4 m; m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1.0f; return m; }
M4 mul(const M4& a, const M4& b) { M4 r; for (int c = 0; c < 4; ++c) for (int row = 0; row < 4; ++row) for (int k = 0; k < 4; ++k) r.v[c * 4 + row] += a.v[k * 4 + row] * b.v[c * 4 + k]; return r; }
M4 perspective(float fov, float aspect, float nearZ, float farZ) { M4 m; const auto f = 1.0f / std::tan(fov * 0.5f); m.v[0] = f / aspect; m.v[5] = f; m.v[10] = (farZ + nearZ) / (nearZ - farZ); m.v[11] = -1.0f; m.v[14] = 2.0f * farZ * nearZ / (nearZ - farZ); return m; }
M4 camera(float yaw, float pitch, float distance, float panX, float panY)
{
    const auto cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    const auto x = distance * cp * sy, y = distance * sp, z = distance * cp * cy;
    const auto fx = -x / distance, fy = -y / distance, fz = -z / distance;
    const auto sx = fz, sz = -fx, sl = std::sqrt(sx * sx + sz * sz);
    M4 m = identity(); m.v[0] = sx / sl; m.v[8] = sz / sl; m.v[1] = -fy * sz / sl; m.v[5] = fz * sx / sl; m.v[9] = -fy * sx / sl; m.v[2] = -fx; m.v[6] = -fy; m.v[10] = -fz;
    m.v[12] = -(m.v[0] * x + m.v[8] * z) + panX; m.v[13] = -(m.v[1] * x + m.v[5] * y + m.v[9] * z) + panY; m.v[14] = -(m.v[2] * x + m.v[6] * y + m.v[10] * z); return m;
}
M4 rotation(float a) { M4 m = identity(); m.v[0] = std::cos(a); m.v[2] = std::sin(a); m.v[8] = -std::sin(a); m.v[10] = std::cos(a); return m; }
}

Viewport3D::Viewport3D(int number) : viewNumber(number)
{
    setOpaque(false); context.setRenderer(this); context.setContinuousRepainting(true); context.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1); context.attachTo(*this);
}
Viewport3D::~Viewport3D() { context.detach(); }
void Viewport3D::paint(juce::Graphics& g) { g.setColour(juce::Colour(0xffa9c8d2)); g.drawText("View " + juce::String(viewNumber), 10, 8, 100, 20, juce::Justification::left); g.setColour(juce::Colour(0xff8fa4ad)); g.setFont(juce::Font(10.0f)); g.drawFittedText(glStatus, 10, juce::jmax(24, getHeight() - 28), getWidth() - 20, 18, juce::Justification::left, 1); }
void Viewport3D::resized() {}
void Viewport3D::timerCallback() {}
void Viewport3D::mouseDown(const juce::MouseEvent& e) { dragStart = e.position.roundToInt(); }
void Viewport3D::mouseDrag(const juce::MouseEvent& e) { const auto now = e.position.roundToInt(), d = now - dragStart; if (e.mods.isRightButtonDown() || e.mods.isShiftDown()) { panX += d.x * 0.01f; panY -= d.y * 0.01f; } else { yaw += d.x * 0.01f; pitch = juce::jlimit(-1.4f, 1.4f, pitch + d.y * 0.01f); } dragStart = now; }
void Viewport3D::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) { distance = juce::jlimit(1.5f, 12.0f, distance * (1.0f - w.deltaY * 0.25f)); }
void Viewport3D::resetCamera() { yaw = 0.55f; pitch = 0.35f; distance = 4.0f; panX = panY = 0.0f; }
void Viewport3D::fitAll() { distance = 8.0f; panX = panY = 0.0f; }
void Viewport3D::setSnapshot(std::shared_ptr<const djehuti::cad::CadRenderSnapshot> next) { pendingSnapshot = std::move(next); }
juce::String Viewport3D::diagnostics() const { return glStatus; }
void Viewport3D::prepareFrame(float sharedAngle, std::uint64_t serial)
{
    const auto p = perspective(0.85f, getHeight() > 0 ? (float) getWidth() / (float) getHeight() : 1.0f, 0.1f, 100.0f); const auto v = camera(yaw, pitch, distance, panX, panY); const auto m = identity(); juce::ignoreUnused(sharedAngle);
    DjehutiFramePacket packet; std::memcpy(packet.model.v, m.v, sizeof(m.v)); std::memcpy(packet.view.v, v.v, sizeof(v.v)); std::memcpy(packet.projection.v, p.v, sizeof(p.v)); packet.serial = serial; packet.angle = sharedAngle; frames.publish(packet);
}

void Viewport3D::newOpenGLContextCreated()
{
    const char* vertex = "#version 410 core\nlayout(location=0) in vec3 position; layout(location=1) in vec3 normal; layout(std140) uniform FrameData { mat4 uMVP; mat4 uModel; }; out vec3 n; void main(){ n=mat3(uModel)*normal; gl_Position=uMVP*vec4(position,1.0); }";
    const char* fragment = "#version 410 core\nin vec3 n; out vec4 colour; void main(){ vec3 l=normalize(vec3(0.4,0.8,0.6)); float d=max(dot(normalize(n),l),0.0); colour=vec4(vec3(0.18,0.62,0.78)*(0.25+0.75*d),1.0); }";
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
        for (const auto& mesh : pendingSnapshot->meshes)
        {
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
    const auto scale = static_cast<float>(context.getRenderingScale()); glViewport(0, 0, juce::roundToInt(scale * (float) getWidth()), juce::roundToInt(scale * (float) getHeight())); glClearColor(0.035f, 0.055f, 0.075f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto activeSlot = static_cast<size_t>(lastSlot); M4 projection, viewMatrix, model; std::memcpy(projection.v, lastFrame.projection.v, sizeof(projection.v)); std::memcpy(viewMatrix.v, lastFrame.view.v, sizeof(viewMatrix.v)); std::memcpy(model.v, lastFrame.model.v, sizeof(model.v)); const auto finalMvp = mul(projection, mul(viewMatrix, model));
    if (slot >= 0)
    {
        DjehutiMat4 matrices[2]; std::memcpy(matrices[0].v, finalMvp.v, sizeof(finalMvp.v)); matrices[1] = lastFrame.model;
        glBindBuffer(GL_UNIFORM_BUFFER, frameBuffers[activeSlot]); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(matrices), matrices);
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameBuffers[activeSlot]); shader->use(); if (geometryIndexCount > 0) { glBindVertexArray(vao); glDrawElements(GL_TRIANGLES, geometryIndexCount, GL_UNSIGNED_INT, nullptr); glBindVertexArray(0); } if (slot >= 0) fences[activeSlot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}
void Viewport3D::drawCube(const float*) {}
void Viewport3D::releaseGL() { for (auto& f : fences) if (f != nullptr) { glDeleteSync(f); f = nullptr; } glDeleteBuffers(3, frameBuffers.data()); if (vao != 0) glDeleteVertexArrays(1, &vao); if (vbo != 0) glDeleteBuffers(1, &vbo); if (ibo != 0) glDeleteBuffers(1, &ibo); frameBuffers.fill(0); vao = vbo = ibo = 0; frames.reset(); lastSlot = -1; hasFrame = false; uploadedGeometryRevision = 0; geometryIndexCount = 0; shader.reset(); positionAttribute.reset(); normalAttribute.reset(); glReady = false; }
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
bool ThreeDViewsPanel::addView() { if (tiles.size() >= 6) { addButton.setEnabled(false); return false; } static int nextId = 1; tiles.add(new ViewTile(nextId++, [this](int id) { closeView(id); })); addAndMakeVisible(tiles.getLast()); if (currentSnapshot != nullptr) tiles.getLast()->viewport().setSnapshot(currentSnapshot); addButton.setEnabled(tiles.size() < 6); resized(); return true; }
void ThreeDViewsPanel::closeView(int id) { if (tiles.size() <= 1) return; for (int i = tiles.size(); --i >= 0;) if (tiles[i]->id() == id) { tiles.remove(i); break; } addButton.setEnabled(tiles.size() < 6); resized(); }
void ThreeDViewsPanel::publishFrames() { if (cadService != nullptr && cadService->document().revision() != shownRevision) { currentSnapshot = cadService->snapshot(); shownRevision = currentSnapshot != nullptr ? currentSnapshot->revision : 0; for (auto* tile : tiles) tile->viewport().setSnapshot(currentSnapshot); } ++frameSerial; for (auto* tile : tiles) tile->viewport().prepareFrame(sharedAngle, frameSerial); }
void ThreeDViewsPanel::timerCallback() { publishFrames(); }
void ThreeDViewsPanel::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff171b20)); }
void ThreeDViewsPanel::resized() { auto area = getLocalBounds().reduced(10); auto bar = area.removeFromTop(32); addButton.setBounds(bar.removeFromLeft(120)); pauseButton.setBounds(bar.removeFromLeft(90).reduced(4, 0)); resetButton.setBounds(bar.removeFromLeft(130).reduced(4, 0)); fitButton.setBounds(bar.removeFromLeft(90).reduced(4, 0)); const int columns = tiles.size() > 2 ? 2 : 1, rows = (tiles.size() + columns - 1) / columns, w = juce::jmax(1, area.getWidth() / columns), h = juce::jmax(1, area.getHeight() / rows); for (int i = 0; i < tiles.size(); ++i) tiles[i]->setBounds(area.getX() + (i % columns) * w + 4, area.getY() + (i / columns) * h + 4, w - 8, h - 8); }
