#include "Viewport3D.h"

#include <cmath>

using namespace juce::gl;

namespace
{
struct M4 { float v[16] {}; };
M4 identity() { M4 m; m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1.0f; return m; }
M4 multiply(const M4& a, const M4& b)
{
    M4 r;
    for (int c = 0; c < 4; ++c) for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 4; ++k) r.v[c * 4 + row] += a.v[k * 4 + row] * b.v[c * 4 + k];
    return r;
}
M4 perspective(float fov, float aspect, float nearZ, float farZ)
{
    M4 m {}; const float f = 1.0f / std::tan(fov * 0.5f);
    m.v[0] = f / aspect; m.v[5] = f; m.v[10] = (farZ + nearZ) / (nearZ - farZ);
    m.v[11] = -1.0f; m.v[14] = (2.0f * farZ * nearZ) / (nearZ - farZ); return m;
}
M4 view(float yaw, float pitch, float distance)
{
    const float cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    const float x = distance * cp * sy, y = distance * sp, z = distance * cp * cy;
    const float len = std::sqrt(x * x + y * y + z * z); const float fx = -x / len, fy = -y / len, fz = -z / len;
    const float sx = fz, sy2 = 0.0f, sz = -fx; const float sl = std::sqrt(sx * sx + sz * sz);
    M4 m = identity(); m.v[0] = sx / sl; m.v[4] = sy2; m.v[8] = sz / sl;
    m.v[1] = (fy * sz - fz * sy2) / sl; m.v[5] = (fz * sx - fx * sz) / sl; m.v[9] = (fx * sy2 - fy * sx) / sl;
    m.v[2] = -fx; m.v[6] = -fy; m.v[10] = -fz; m.v[12] = -(m.v[0] * x + m.v[4] * y + m.v[8] * z);
    m.v[13] = -(m.v[1] * x + m.v[5] * y + m.v[9] * z); m.v[14] = -(m.v[2] * x + m.v[6] * y + m.v[10] * z); return m;
}
M4 rotation(float a) { M4 m = identity(); m.v[0] = std::cos(a); m.v[2] = std::sin(a); m.v[8] = -std::sin(a); m.v[10] = std::cos(a); return m; }
}

Viewport3D::Viewport3D(int number) : viewNumber(number)
{
    context.setRenderer(this); context.setContinuousRepainting(true); context.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1); context.attachTo(*this);
    startTimerHz(60);
}
Viewport3D::~Viewport3D() { stopTimer(); context.detach(); }
void Viewport3D::paint(juce::Graphics& g) { g.setColour(juce::Colour(0xff0b1015)); g.fillAll(); g.setColour(juce::Colour(0xffa9c8d2)); g.drawText("View " + juce::String(viewNumber), 10, 8, 100, 20, juce::Justification::left); }
void Viewport3D::resized() {}
void Viewport3D::mouseDown(const juce::MouseEvent& e) { dragStart = e.position.roundToInt(); }
void Viewport3D::mouseDrag(const juce::MouseEvent& e) { yaw += (e.position.x - dragStart.x) * 0.01f; pitch = juce::jlimit(-1.4f, 1.4f, pitch + (e.position.y - dragStart.y) * 0.01f); dragStart = e.position.roundToInt(); }
void Viewport3D::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) { distance = juce::jlimit(1.5f, 12.0f, distance * (1.0f - w.deltaY * 0.25f)); }
void Viewport3D::resetCamera() { yaw = 0.55f; pitch = 0.35f; distance = 4.0f; }
void Viewport3D::setPaused(bool p) { paused = p; }
juce::String Viewport3D::diagnostics() const { return glStatus; }
void Viewport3D::timerCallback() { if (!paused) { angle += 0.012f; frames.publish({ angle, static_cast<std::uint64_t>(juce::Time::getMillisecondCounter()) }); } }

void Viewport3D::newOpenGLContextCreated()
{
    const char* vertex = "#version 410 core\nlayout(location=0) in vec3 position; layout(location=1) in vec3 normal; uniform mat4 uMVP; uniform mat4 uModel; out vec3 n; void main(){ n=mat3(uModel)*normal; gl_Position=uMVP*vec4(position,1.0); }";
    const char* fragment = "#version 410 core\nin vec3 n; out vec4 colour; void main(){ vec3 l=normalize(vec3(0.4,0.8,0.6)); float d=max(dot(normalize(n),l),0.0); colour=vec4(vec3(0.18,0.62,0.78)*(0.25+0.75*d),1.0); }";
    shader = std::make_unique<juce::OpenGLShaderProgram>(context);
    if (!shader->addVertexShader(vertex) || !shader->addFragmentShader(fragment) || !shader->link()) { glStatus = "Shader error: " + shader->getLastError(); shader.reset(); return; }
    positionAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "position"); normalAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "normal");
    const float vertices[] = { -1,-1,-1,0,0,-1, 1,-1,-1,0,0,-1, 1,1,-1,0,0,-1, -1,1,-1,0,0,-1, -1,-1,1,0,0,1, 1,-1,1,0,0,1, 1,1,1,0,0,1, -1,1,1,0,0,1, -1,-1,-1,-1,0,0, -1,1,-1,-1,0,0, -1,1,1,-1,0,0, -1,-1,1,-1,0,0, 1,-1,-1,1,0,0, 1,1,-1,1,0,0, 1,1,1,1,0,0, 1,-1,1,1,0,0, -1,-1,-1,0,-1,0, -1,-1,1,0,-1,0, 1,-1,1,0,-1,0, 1,-1,-1,0,-1,0, -1,1,-1,0,1,0, -1,1,1,0,1,0, 1,1,1,0,1,0, 1,1,-1,0,1,0 };
    const unsigned int indices[] = { 0,1,2,2,3,0,4,6,5,6,4,7,8,9,10,10,11,8,12,14,13,14,12,15,16,17,18,18,19,16,20,22,21,22,20,23 };
    glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW); glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(positionAttribute->attributeID); glVertexAttribPointer(positionAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr); glEnableVertexAttribArray(normalAttribute->attributeID); glVertexAttribPointer(normalAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float))); glBindVertexArray(0); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glStatus = "OpenGL 4.1 core shader ready; VAO/VBO/IBO active"; glReady = true;
}
void Viewport3D::renderOpenGL()
{
    if (!glReady || shader == nullptr) return; DjehutiFramePacket packet; frames.consume(packet); angle = packet.serial != 0 ? packet.angle : angle;
    glViewport(0, 0, getWidth(), getHeight()); glClearColor(0.035f, 0.055f, 0.075f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto p = perspective(0.85f, getHeight() > 0 ? (float)getWidth() / (float)getHeight() : 1.0f, 0.1f, 100.0f); const auto v = view(yaw, pitch, distance); const auto m = rotation(angle); const auto mvp = multiply(p, multiply(v, m)); shader->use(); shader->setUniformMat4("uMVP", mvp.v, 1, GL_FALSE); shader->setUniformMat4("uModel", m.v, 1, GL_FALSE); drawCube(mvp.v);
}
void Viewport3D::drawCube(const float*) { glBindVertexArray(vao); glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr); glBindVertexArray(0); }
void Viewport3D::releaseGL() { if (vao != 0) glDeleteVertexArrays(1, &vao); if (vbo != 0) glDeleteBuffers(1, &vbo); if (ibo != 0) glDeleteBuffers(1, &ibo); vao = vbo = ibo = 0; shader.reset(); positionAttribute.reset(); normalAttribute.reset(); glReady = false; }
void Viewport3D::openGLContextClosing() { releaseGL(); }

ThreeDViewsPanel::ThreeDViewsPanel()
{
    addAndMakeVisible(addButton); addAndMakeVisible(pauseButton); addAndMakeVisible(resetButton);
    addButton.onClick = [this] { addView(); }; pauseButton.onClick = [this] { paused = !paused; pauseButton.setButtonText(paused ? "Resume" : "Pause"); for (auto* v : views) v->setPaused(paused); }; resetButton.onClick = [this] { for (auto* v : views) v->resetCamera(); };
    addView(); addView(); addView();
}
void ThreeDViewsPanel::addView() { if (views.size() < 4) { views.add(new Viewport3D(views.size() + 1)); addAndMakeVisible(views.getLast()); resized(); } }
void ThreeDViewsPanel::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff171b20)); }
void ThreeDViewsPanel::resized()
{
    auto area = getLocalBounds().reduced(10); auto bar = area.removeFromTop(32); addButton.setBounds(bar.removeFromLeft(120)); pauseButton.setBounds(bar.removeFromLeft(90).reduced(4, 0)); resetButton.setBounds(bar.removeFromLeft(130).reduced(4, 0));
    const int columns = views.size() > 2 ? 2 : 1, rows = (views.size() + columns - 1) / columns; const int w = juce::jmax(1, area.getWidth() / columns), h = juce::jmax(1, area.getHeight() / rows);
    for (int i = 0; i < views.size(); ++i) views[i]->setBounds(area.getX() + (i % columns) * w + 4, area.getY() + (i / columns) * h + 4, w - 8, h - 8);
}
