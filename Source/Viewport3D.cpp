#include "Viewport3D.h"

#include <cmath>
#include <cstring>
#include <limits>
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
    const V3 forward {-cp*sy,cp*cy,-sp};
    const V3 right {cy,sy,0};
    const auto up = cross(right,forward);
    M4 m = identity();
    m.v[0] = right.x; m.v[4] = right.y; m.v[8] = right.z;
    m.v[1] = up.x; m.v[5] = up.y; m.v[9] = up.z;
    m.v[2] = -forward.x; m.v[6] = -forward.y; m.v[10] = -forward.z;
    m.v[12] = (float)(-(double)right.x*target.x-(double)right.y*target.y-(double)right.z*target.z+panX);
    m.v[13] = (float)(-(double)up.x*target.x-(double)up.y*target.y-(double)up.z*target.z+panY);
    m.v[14] = (float)((double)forward.x*target.x+(double)forward.y*target.y+(double)forward.z*target.z-distance);
    return m;
}
M4 projectionFor(int width,int height,const std::array<float,2>& clip)
{
    const auto aspect=height>0?juce::jmax(0.01f,(float)width/(float)height):1.0f;
    return perspective(0.85f,aspect,clip[0],clip[1]);
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
void Viewport3D::paint(juce::Graphics& g) { if(faceSnapMarker&&faceSnapEnabled&&faceSnapEnabled()){g.setColour(juce::Colour(0xffffce73));g.drawRect(juce::Rectangle<float>(faceSnapScreen.x-4,faceSnapScreen.y-4,8,8),1.5f);g.drawText("face",(int)faceSnapScreen.x+9,(int)faceSnapScreen.y-18,55,18,juce::Justification::left);} drawReferenceGuides(g); g.setColour(juce::Colour(0xffa9c8d2)); g.drawText(flyMode?"FLY | WASD move, Q/E down/up, drag/arrow keys look, Shift faster, Esc exits":orbitPointPicking?"Set orbit point: click a surface or the XY floor; Esc cancels":title.isNotEmpty() ? title : "View " + juce::String(viewNumber), 10, 8, juce::jmax(100, getWidth() - 20), 20, juce::Justification::left); g.setColour(juce::Colour(0xff8fa4ad)); g.setFont(juce::Font(10.0f)); g.drawFittedText(glStatus, 10, juce::jmax(24, getHeight() - 28), getWidth() - 20, 18, juce::Justification::left, 1); }
void Viewport3D::resized() { if (hasSceneBounds && !userCameraChanged && !fitPending) frameScene(); tryPendingFit(); markDirty(); }
void Viewport3D::timerCallback()
{
    const auto now=juce::Time::getMillisecondCounterHiRes();const auto elapsed=(float)juce::jlimit(0.0,0.05,(now-flyTick)/1000.0);flyTick=now;
    if(!flyMode||!isShowing()||!hasKeyboardFocus(false))return;
    const auto mods=juce::ModifierKeys::getCurrentModifiersRealtime();if(mods.isCommandDown()||mods.isAltDown())return;
    const auto down=[](int key){return juce::KeyPress::isKeyCurrentlyDown(key);};
    const float speed=juce::jmax(.01f,sceneRadius)*elapsed*(mods.isShiftDown()?4.f:1.f);
    const float f=(float)(down('W')-down('S')),r=(float)(down('D')-down('A')),u=(float)(down('E')-down('Q'));
    if(f!=0||r!=0||u!=0)moveFly(f*speed,r*speed,u*speed);
    const float x=(float)(down(juce::KeyPress::rightKey)-down(juce::KeyPress::leftKey)),y=(float)(down(juce::KeyPress::upKey)-down(juce::KeyPress::downKey));if(x!=0||y!=0)lookFly(x*elapsed,y*elapsed);
}
void Viewport3D::setFlyMode(bool value)
{
    if(flyMode==value)return;flyMode=value;selectingRegion=false;orbitPointPicking=false;
    if(value){const float cy=std::cos(yaw),sy=std::sin(yaw),sp=std::sin(pitch),cp=std::cos(pitch);targetX-=panX*cy-panY*sp*sy;targetY-=panX*sy+panY*sp*cy;targetZ-=panY*cp;panX=panY=0;flyTick=juce::Time::getMillisecondCounterHiRes();startTimerHz(60);grabKeyboardFocus();}
    else stopTimer();cameraChanged();repaint();
}
bool Viewport3D::keyStateChanged(bool){return flyMode;}
void Viewport3D::focusLost(FocusChangeType){flyTick=juce::Time::getMillisecondCounterHiRes();}
void Viewport3D::moveFly(float forward,float right,float up)
{
    if(!flyMode||!std::isfinite(forward)||!std::isfinite(right)||!std::isfinite(up))return;
    targetX+=-forward*std::cos(pitch)*std::sin(yaw)+right*std::cos(yaw);
    targetY+=forward*std::cos(pitch)*std::cos(yaw)+right*std::sin(yaw);
    targetZ+=-forward*std::sin(pitch)+up;cameraChanged();repaint();
}
void Viewport3D::lookFly(float dx,float dy)
{
    if(!flyMode)return;const std::array<float,3> eye{targetX+distance*std::cos(pitch)*std::sin(yaw),targetY-distance*std::cos(pitch)*std::cos(yaw),targetZ+distance*std::sin(pitch)};
    yaw+=dx;pitch=juce::jlimit(-1.55f,1.55f,pitch+dy);
    targetX=eye[0]-distance*std::cos(pitch)*std::sin(yaw);targetY=eye[1]+distance*std::cos(pitch)*std::cos(yaw);targetZ=eye[2]-distance*std::sin(pitch);cameraChanged();repaint();
}
void Viewport3D::chooseOrbitPoint(){setFlyMode(false);selectingRegion=false;draggingTarget=false;orbitPointPicking=true;grabKeyboardFocus();repaint();}
bool Viewport3D::setOrbitPoint(const std::array<float,3>& point)
{
    for(auto v:point)if(!std::isfinite(v))return false;setFlyMode(false);orbitPointPicking=false;
    targetX=point[0];targetY=point[1];targetZ=point[2];panX=panY=0;cameraChanged();markDirty();repaint();return true;
}

void Viewport3D::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragStart = pressPosition = e.position.roundToInt();
    draggingTarget = false;
    if(flyMode||orbitPointPicking)return;
    selectingRegion=selection!=nullptr && (selection->tool()==djehuti::cad::CadSelection::Tool::box||selection->tool()==djehuti::cad::CadSelection::Tool::circle) && !e.mods.isRightButtonDown() && !e.mods.isShiftDown() && !(wantsBodyPick && wantsBodyPick());
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
    if(flyMode)return;
    if(orbitPointPicking){
        if(e.mods.isPopupMenu()){orbitPointPicking=false;repaint();return;}
        if(e.position.roundToInt().getDistanceFrom(pressPosition)>3)return;
        auto hit=pick(e.position,djehuti::cad::CadSelection::Mode::face);
        if(hit.valid()&&hit.hasPoint){std::array<float,3> world{(float)hit.point[0],(float)hit.point[1],(float)hit.point[2]};
            if(pose)for(const auto& i:pose->instances)if(i.instanceId==hit.drawId){const auto p=world;for(int k=0;k<3;++k)world[k]=i.matrix[k]*p[0]+i.matrix[4+k]*p[1]+i.matrix[8+k]*p[2]+i.matrix[12+k];}
            setOrbitPoint(world);
        }else{std::array<float,3> o{},d{};if(rayThrough(e.position,o,d)&&std::abs(d[2])>1e-8f){const float t=-o[2]/d[2];if(t>0)setOrbitPoint({o[0]+t*d[0],o[1]+t*d[1],0});}}
        return;
    }
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
        if(e.mods.isCtrlDown()||(wantsEdgePick&&wantsEdgePick())) { auto prior=selection->items(); for(const auto& p:picked) if(std::none_of(prior.begin(),prior.end(),[&](const auto& old){return old.drawId==p.drawId&&old.featureId==p.featureId&&old.face==p.face&&old.edge==p.edge;})) prior.push_back(p); picked=std::move(prior); }
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
    auto picked=pick(e.position,faceSnapEnabled&&faceSnapEnabled()?djehuti::cad::CadSelection::Mode::face:selection!=nullptr && !(wantsBodyPick&&wantsBodyPick())?selection->mode():djehuti::cad::CadSelection::Mode::body);
    if(faceSnapEnabled&&faceSnapEnabled()&&refineFaceSnap)refineFaceSnap(picked);
    if(selection!=nullptr){
        if(!(wantsEdgePick&&wantsEdgePick())&&(e.mods.isCtrlDown()||selection->tool()==djehuti::cad::CadSelection::Tool::multiple)){
            auto items=selection->items();const auto found=std::find_if(items.begin(),items.end(),[&](const auto& p){return p.drawId==picked.drawId&&p.featureId==picked.featureId&&p.face==picked.face&&p.edge==picked.edge;});if(found!=items.end())items.erase(found);else if(picked.valid())items.push_back(picked);selection->setSelectedMany(std::move(items));
        }else selection->setSelected(picked);
    }
    if(picked.face<0 && picked.edge<0&&!(selection&&(selection->tool()==djehuti::cad::CadSelection::Tool::multiple||e.mods.isCtrlDown())))
    {
        if(onBodyPicked) onBodyPicked(picked.drawId);
        else if(onContextCommand) onContextCommand("inspect",picked);
    }
    markDirty();

}
// Analytic world ray avoids precision loss when a remote far plane becomes infinite in float projection.
bool Viewport3D::rayThrough(juce::Point<float> position, std::array<float, 3>& origin, std::array<float, 3>& direction) const
{
    if (getWidth() <= 0 || getHeight() <= 0) return false;
    const double cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw);
    const double right[]{cy,sy,0},up[]{-sp*sy,sp*cy,cp},back[]{cp*sy,-cp*cy,sp},target[]{targetX,targetY,targetZ};
    const double x=(2.0*position.x/getWidth()-1.0)*std::tan(.425)*getWidth()/getHeight(),y=(1.0-2.0*position.y/getHeight())*std::tan(.425);
    double norm=0;for(int i=0;i<3;++i){origin[i]=(float)(target[i]+distance*back[i]-panX*right[i]-panY*up[i]);direction[i]=(float)(right[i]*x+up[i]*y-back[i]);norm+=(double)direction[i]*direction[i];}
    norm=std::sqrt(norm);for(auto& c:direction)c=(float)(c/norm);return true;
}
bool Viewport3D::toScreen(const std::array<float, 3>& p, juce::Point<float>& screen) const
{
    if (getWidth() <= 0 || getHeight() <= 0) return false;
    // Screen X/Y need no depth range. Region selection projects many vertices;
    // avoid rescanning all assembly bounds for each of them.
    const auto v = camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY);
    double eye[3];
    for (int row = 0; row < 3; ++row) eye[row] = (double)v.v[row]*p[0]+(double)v.v[4+row]*p[1]+(double)v.v[8+row]*p[2]+v.v[12+row];
    if (-eye[2] <= 1.0e-12) return false;
    const double factor=1.0/std::tan(.425),aspect=(double)getWidth()/getHeight();
    screen = { (float)((eye[0]*factor/aspect/-eye[2]+1)*.5*getWidth()), (float)((1-eye[1]*factor/-eye[2])*.5*getHeight()) };
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
    const auto pickClip=clippingPlanes();
    const auto viewProjection = mul(projectionFor(getWidth(), getHeight(), pickClip), camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY));
    const auto w = (float) getWidth(), h = (float) getHeight();
    float bestDepth = std::numeric_limits<float>::infinity();
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
            if (clip[3] <= 1.0e-12f) return false;
            screen = { (clip[0] / clip[3] + 1.0f) * 0.5f * w, (1.0f - clip[1] / clip[3]) * 0.5f * h }; depth = clip[3];
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
            const auto depth = 1.0f/(u/da+v/db+t/dc);
            if (depth>=pickClip[0] && depth<=pickClip[1] && depth < bestDepth)
            {
                bestDepth = depth; result.drawId = draw.id; result.featureId = mesh.featureId; result.face = mesh.faceIndex;
                double weights[3]{u,v,t},sum=0;
                for(int k=0;k<3;++k){const auto* vertex=mesh.vertices.data()+mesh.indices[i+k]*6u;const auto clipW=mvp.v[3]*vertex[0]+mvp.v[7]*vertex[1]+mvp.v[11]*vertex[2]+mvp.v[15];weights[k]/=clipW;sum+=weights[k];}
                result.point={0,0,0};for(int k=0;k<3;++k)for(int j=0;j<3;++j)result.point[j]+=weights[k]/sum*mesh.vertices[mesh.indices[i+k]*6u+j];result.hasPoint=true;
            }
        }
    }
    if(wantsSketchPick&&wantsSketchPick())
    {
        float nearest=7.0f;
        for(const auto& outline:pickSnapshot->sketchOutlines)if(!outline.featureId.startsWith("__lathe_"))
        for(size_t i=0;i+5<outline.points.size();i+=6)
        {
            juce::Point<float> screen[2];float depth[2];bool visible=true;
            for(int end=0;end<2;++end)
            {
                const auto* p=outline.points.data()+i+(size_t)end*3;float clip[4];
                for(int row=0;row<4;++row)clip[row]=viewProjection.v[row]*p[0]+viewProjection.v[4+row]*p[1]+viewProjection.v[8+row]*p[2]+viewProjection.v[12+row];
                depth[end]=clip[3];if(clip[3]<=pickClip[0]||clip[3]>=pickClip[1]){visible=false;break;}
                screen[end]={(clip[0]/clip[3]+1)*.5f*w,(1-clip[1]/clip[3])*.5f*h};
            }
            if(!visible)continue;const auto delta=screen[1]-screen[0];const auto length=delta.x*delta.x+delta.y*delta.y;
            const auto fraction=length>0?juce::jlimit(0.0f,1.0f,((position.x-screen[0].x)*delta.x+(position.y-screen[0].y)*delta.y)/length):0;
            const auto distance=(screen[0]+delta*fraction).getDistanceFrom(position);if(distance>=nearest)continue;nearest=distance;
            const auto denominator=(1-fraction)/depth[0]+fraction/depth[1];const auto t=(fraction/depth[1])/denominator;
            result={outline.featureId,outline.featureId,-1,-1};result.hasPoint=true;
            for(int k=0;k<3;++k)result.point[(size_t)k]=outline.points[i+(size_t)k]*(1-t)+outline.points[i+3+(size_t)k]*t;
        }
        if(result.hasPoint&&result.face<0&&result.edge<0)return result;
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
                if (clip[3] <= 1.0e-12f) return false;
                screen = { (clip[0] / clip[3] + 1.0f) * 0.5f * w, (1.0f - clip[1] / clip[3]) * 0.5f * h }; depth = clip[3];
                return true;
            };
            for (size_t i = 0; i + 1 < line.points.size() / 3; ++i)
            {
                juce::Point<float> a, b; float da = 0, db = 0;
                if (!project(i, a, da) || !project(i + 1, b, db)) continue;
                const auto ab = b - a; const auto len2 = ab.x * ab.x + ab.y * ab.y;
                const auto t = len2 > 0.0f ? juce::jlimit(0.0f, 1.0f, ((position.x - a.x) * ab.x + (position.y - a.y) * ab.y) / len2) : 0.0f;
                const auto closest = a + ab * t; const auto distanceToEdge = closest.getDistanceFrom(position);
                const auto depth = 1.0f/((1.0f-t)/da+t/db);
                if (depth>=pickClip[0] && depth<=pickClip[1] && distanceToEdge < bestDistance && (!std::isfinite(bestDepth) || depth <= bestDepth + juce::jmax(depth*1e-5f,4*distance*std::tan(.425f)/juce::jmax(1,getHeight())))) { bestDistance = distanceToEdge; edgePick = { id, line.featureId, -1, line.edgeIndex }; }
            }
        }
    }
    return edgePick;
}
void Viewport3D::mouseMove(const juce::MouseEvent& e)
{
    faceSnapMarker=false;
    if(faceSnapEnabled&&faceSnapEnabled()&&refineFaceSnap){auto hit=pick(e.position,djehuti::cad::CadSelection::Mode::face);faceSnapMarker=refineFaceSnap(hit);faceSnapScreen=e.position;if(selection)selection->setHover(faceSnapMarker?hit:djehuti::cad::CadPick{});repaint();}
    else {if (selection == nullptr || selection->mode() == djehuti::cad::CadSelection::Mode::body) return;selection->setHover(pick(e.position, selection->mode()));}
    if (onFrameRequested) onFrameRequested();
}
void Viewport3D::mouseExit(const juce::MouseEvent&) {faceSnapMarker=false;repaint(); if (selection != nullptr) selection->setHover({}); }
void Viewport3D::setInspectionLines(std::vector<djehuti::cad::CadOverlayLine> next) { { const juce::SpinLock::ScopedLockType lock(highlightLock); glInspection = std::move(next); } markDirty(); }
void Viewport3D::setHighlights(std::vector<std::pair<juce::String, juce::Colour>> next) { { const juce::SpinLock::ScopedLockType lock(highlightLock); if (highlights == next) return; highlights = std::move(next); } markDirty(); }
void Viewport3D::mouseDrag(const juce::MouseEvent& e)
{
    if(flyMode){const auto now=e.position.roundToInt(),d=now-dragStart;lookFly(d.x*.006f,d.y*.006f);dragStart=now;return;}
    if(orbitPointPicking)return;
    if(selectingRegion) { regionEnd=e.position; repaint(); return; }
    if (draggingTarget)
    {
        std::array<float, 3> point {};
        if (dragPoint(e.position, point) && onTargetDragged) onTargetDragged(dragTargetId, point, false);
        if (onFrameRequested) onFrameRequested();
        return;
    }
    const auto now = e.position.roundToInt(), d = now - dragStart; if (e.mods.isRightButtonDown() || e.mods.isShiftDown()) { const auto unitsPerPixel=2.0f*distance*std::tan(0.425f)/(float)juce::jmax(1,getHeight()); panX += d.x * unitsPerPixel; panY -= d.y * unitsPerPixel; } else { yaw += d.x * 0.01f; pitch = juce::jlimit(-1.4f, 1.4f, pitch + d.y * 0.01f); } dragStart = now; cameraChanged(); }
void Viewport3D::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const auto range=navigationRange();const auto minimumDistance=range[0], maximumDistance=range[1];
    distance = juce::jlimit(minimumDistance, maximumDistance, distance * std::exp(-w.deltaY * 0.5f)); cameraChanged();
}

juce::var Viewport3D::cameraState() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("yawDeg", (double)juce::radiansToDegrees(yaw)); o->setProperty("pitchDeg", (double)juce::radiansToDegrees(pitch));
    o->setProperty("flyMode",flyMode);o->setProperty("orbitPointPicking",orbitPointPicking);
    o->setProperty("distance", distance); o->setProperty("panX", panX); o->setProperty("panY", panY);
    o->setProperty("targetX", targetX); o->setProperty("targetY", targetY); o->setProperty("targetZ", targetZ);
    const auto clip=clippingPlanes();o->setProperty("nearPlane",clip[0]);o->setProperty("farPlane",clip[1]);o->setProperty("depthMode",clip[1]/clip[0]>50000?"logarithmic":"linear");
    o->setProperty("gridRenderer","adaptive_shader");o->setProperty("floorReady",floorReady.load());o->setProperty("gridVertices",showFloor?3:0);o->setProperty("gridSpacing",gridSpacing);o->setProperty("floorGpuMs",(double)floorGpuNs.load()/1e6);o->setProperty("renderedFrames",(juce::int64)renderedFrames.load());o->setProperty("geometryUploads",(juce::int64)geometryUploads.load());
    o->setProperty("showing", isShowing()); return o;
}
bool Viewport3D::controlCamera(const juce::var& args, juce::String& error)
{
    const auto op = args.getProperty("operation", "set").toString();
    if(op=="fly"){setFlyMode(args.getProperty("enabled",!flyMode));return true;}
    if(op=="orbitPoint"){
        std::array<float,3> point{};const char* keys[]{"x","y","z"};for(int i=0;i<3;++i){const auto v=args[keys[i]];if(!(v.isDouble()||v.isInt()||v.isInt64())||!std::isfinite((double)v)||std::abs((double)v)>1e20){error="Orbit point requires finite world coordinates.";return false;}point[i]=(float)(double)v;}return setOrbitPoint(point);
    }
    if (op == "fit") { fitAll(); return true; }
    if(op=="fitSelection"){if(fitSelection())return true;error="Select visible model geometry to frame.";return false;}
    if(op=="capture"){if(!juce::File::isAbsolutePath(args["path"].toString())){error="Capture needs an absolute PNG path.";return false;}return captureViewport(juce::File(args["path"].toString()),error);}
    if (op == "reset") { resetCamera(); return true; }
    if (op == "state") return true;
    if (op != "set") { error = "Camera operation must be set, fit, fitSelection, reset, state or capture."; return false; }
    for (const auto* key : { "yawDeg", "pitchDeg", "distance", "panX", "panY" }) if (args.hasProperty(key))
    {
        const auto value = args[key];
        if (!(value.isDouble() || value.isInt() || value.isInt64()) || !std::isfinite((double)value) || std::abs((double)value) > std::numeric_limits<float>::max()/1024.0)
        { error = juce::String(key) + " must be a finite, representable camera value."; return false; }
    }
    const auto range=navigationRange();const auto minimum=range[0],maximum=range[1];
    const auto nextDistance = (double)args.getProperty("distance", distance);
    const auto nextPitch = (double)args.getProperty("pitchDeg", juce::radiansToDegrees(pitch));
    if (nextDistance < minimum || nextDistance > maximum || std::abs(nextPitch) > juce::radiansToDegrees(1.4))
    { error = "Camera distance or elevation exceeds the same bounds used by mouse navigation."; return false; }
    yaw = (float)juce::degreesToRadians(std::remainder((double)args.getProperty("yawDeg", juce::radiansToDegrees(yaw)),360.0));
    pitch = (float)juce::degreesToRadians(nextPitch); distance = (float)nextDistance;
    panX = (float)(double)args.getProperty("panX", panX); panY = (float)(double)args.getProperty("panY", panY);
    cameraChanged(); markDirty(); return true;
}
void Viewport3D::resetCamera() { setFlyMode(false);orbitPointPicking=false; yaw = homeYaw; pitch = homePitch; panX = panY = 0.0f; userCameraChanged = false; if (hasSceneBounds) frameScene(); else { distance = homeDistance; targetX = targetY = targetZ = 0.0f; fitPending = true; } markDirty(); }
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
    posedBoxes.clear();bool found = false;
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f, maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
    std::vector<V3> corners;
    auto addBox = [&](const Bounds& b, const float* matrix)
    {
        std::array<std::array<float,3>,8> box;
        for (int c = 0; c < 8; ++c)
        {
            const V3 local { (c & 1) ? b.max[0] : b.min[0], (c & 2) ? b.max[1] : b.min[1], (c & 4) ? b.max[2] : b.min[2] };
            const V3 p = matrix == nullptr ? local : V3 { matrix[0] * local.x + matrix[4] * local.y + matrix[8] * local.z + matrix[12], matrix[1] * local.x + matrix[5] * local.y + matrix[9] * local.z + matrix[13], matrix[2] * local.x + matrix[6] * local.y + matrix[10] * local.z + matrix[14] };
            corners.push_back(p);box[(size_t)c]={p.x,p.y,p.z};
            if (!found) { minX = maxX = p.x; minY = maxY = p.y; minZ = maxZ = p.z; found = true; }
            else { minX = juce::jmin(minX, p.x); minY = juce::jmin(minY, p.y); minZ = juce::jmin(minZ, p.z); maxX = juce::jmax(maxX, p.x); maxY = juce::jmax(maxY, p.y); maxZ = juce::jmax(maxZ, p.z); }
        }
        posedBoxes.push_back(box);
    };
    for (const auto& [id, b] : featureBounds) if (pose == nullptr || pose->instancedFeatures.count(id) == 0) addBox(b, nullptr);
    if (pose != nullptr) for (const auto& instance : pose->instances) if (const auto b = featureBounds.find(instance.featureId); b != featureBounds.end()) addBox(b->second, instance.matrix.data());
    hasSceneBounds = found;
    if (found)
    {
        sceneCenterX = (minX + maxX) * 0.5f; sceneCenterY = (minY + maxY) * 0.5f; sceneCenterZ = (minZ + maxZ) * 0.5f;
        const auto dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
        sceneRadius = juce::jmax(1.0e-6f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
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
    const auto margin = juce::jmax(1.0e-6f, sceneRadius) * 1.25f;
    targetX = sceneCenterX; targetY = sceneCenterY; targetZ = sceneCenterZ;
    distance = juce::jmax(1.0e-6f, margin / std::tan(limitingHalfFov));
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
    const auto p = projectionFor(getWidth(), getHeight(), clippingPlanes()); const auto v = camera(yaw, pitch, distance, targetX, targetY, targetZ, panX, panY); const auto m = identity(); juce::ignoreUnused(sharedAngle);
    std::array<float, 32> cameraState {};
    std::memcpy(cameraState.data(), p.v, sizeof(p.v)); std::memcpy(cameraState.data() + 16, v.v, sizeof(v.v));
    if (!renderDirty.exchange(false) && hasPublished && cameraState == lastCamera) return false;   // nothing changed: no frame, no render
    updateReferenceGuides();
    DjehutiFramePacket packet; std::memcpy(packet.model.v, m.v, sizeof(m.v)); std::memcpy(packet.view.v, v.v, sizeof(v.v)); std::memcpy(packet.projection.v, p.v, sizeof(p.v)); packet.serial = serial; packet.angle = sharedAngle;const auto clip=clippingPlanes();packet.nearPlane=clip[0];packet.farPlane=clip[1];packet.floorVisible=showFloor;
    if (frames.publish(packet)) { lastCamera = cameraState; hasPublished = true; }
    else renderDirty = true;   // every slot is still in flight: the render below retires fences, then retry next tick
    context.triggerRepaint();
    return true;
}

void Viewport3D::newOpenGLContextCreated()
{
    const char* vertex = "#version 410 core\nlayout(location=0) in vec3 position; layout(location=1) in vec3 normal; layout(std140) uniform FrameData { mat4 uMVP; mat4 uModel; }; uniform mat4 uInstance; out vec3 n; out float viewDepth; void main(){ n=mat3(uModel)*mat3(uInstance)*normal; gl_Position=uMVP*uInstance*vec4(position,1.0); viewDepth=gl_Position.w; }";
    // Shared illumination for ordinary and logarithmic-depth passes; world-space
    // lights keep model shading stable as the camera orbits.
    const juce::String lighting=R"(
float surfaceLight(vec3 normal){
    vec3 N=normalize(normal);
    float key=max(dot(N,normalize(vec3(.4,.8,.6))),0.0);
    float fill=max(dot(N,normalize(vec3(-.8,-.4,.4))),0.0);
    float lower=max(dot(N,normalize(vec3(.2,-.6,-.8))),0.0);
    return min(1.0,.42+.40*key+.16*fill+.10*lower);
}
)";
    const juce::String fragment = "#version 410 core\nin vec3 n; in float viewDepth; uniform vec3 uColour; uniform vec2 uClip; uniform float uDepthBias; out vec4 colour; "+lighting+" void main(){ if(viewDepth<uClip.x || viewDepth>uClip.y)discard; gl_FragDepth=(uClip.y/uClip.x>50000.0 ? log2(1.0+viewDepth/uClip.x)/log2(1.0+uClip.y/uClip.x) : gl_FragCoord.z)+uDepthBias; colour=vec4(uColour*surfaceLight(gl_FrontFacing?n:-n),1.0); }";
    shader = std::make_unique<juce::OpenGLShaderProgram>(context);
    const juce::String regular=juce::String(R"(#version 410 core
in vec3 n; uniform vec3 uColour; out vec4 colour;
)")+lighting+"void main(){colour=vec4(uColour*surfaceLight(gl_FrontFacing?n:-n),1.0);}";
    if (!shader->addVertexShader(vertex) || !shader->addFragmentShader(regular) || !shader->link()) { glStatus = "Shader error: " + shader->getLastError(); juce::Logger::writeToLog(glStatus); shader.reset(); repaint(); return; }
    rangeShader=std::make_unique<juce::OpenGLShaderProgram>(context);
    if(!rangeShader->addVertexShader(vertex)||!rangeShader->addFragmentShader(fragment)||!rangeShader->link()){glStatus="Range shader error: "+rangeShader->getLastError();juce::Logger::writeToLog(glStatus);rangeShader.reset();return;}
    const auto rangeBlock=glGetUniformBlockIndex(rangeShader->getProgramID(),"FrameData");if(rangeBlock!=GL_INVALID_INDEX)glUniformBlockBinding(rangeShader->getProgramID(),rangeBlock,0);
    positionAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "position"); normalAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*shader, "normal");
    glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW); glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW); glGenBuffers(3, frameBuffers.data());
    for (auto b : frameBuffers) { glBindBuffer(GL_UNIFORM_BUFFER, b); glBufferData(GL_UNIFORM_BUFFER, sizeof(DjehutiMat4) * 2, nullptr, GL_STREAM_DRAW); }
    const auto block = glGetUniformBlockIndex(shader->getProgramID(), "FrameData"); if (block != GL_INVALID_INDEX) glUniformBlockBinding(shader->getProgramID(), block, 0);
    glEnableVertexAttribArray(positionAttribute->attributeID); glVertexAttribPointer(positionAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr); glEnableVertexAttribArray(normalAttribute->attributeID); glVertexAttribPointer(normalAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float))); glBindVertexArray(0); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
    // Joint axis indications share the shader: same vertex layout, drawn as lines.
    glGenVertexArrays(1, &axisVao); glBindVertexArray(axisVao); glGenBuffers(1, &axisVbo); glBindBuffer(GL_ARRAY_BUFFER, axisVbo); glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(positionAttribute->attributeID); glVertexAttribPointer(positionAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr); glEnableVertexAttribArray(normalAttribute->attributeID); glVertexAttribPointer(normalAttribute->attributeID, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float))); glBindVertexArray(0);
    createFloorRenderer();
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
            meshRanges.push_back({ mesh.featureId, static_cast<GLsizei>(indices.size()), static_cast<GLsizei>(mesh.indices.size()), mesh.faceIndex, mesh.doubleSided });
            const auto base = static_cast<std::uint32_t>(vertices.size() / 6);
            vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            for (const auto index : mesh.indices) indices.push_back(base + index);
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_DYNAMIC_DRAW);
        geometryIndexCount = static_cast<GLsizei>(indices.size());
        ++geometryUploads;uploadedGeometryRevision = pendingSnapshot->revision; uploadedGeometryDocument=pendingSnapshot->documentInstance;
    }
    DjehutiFramePacket packet; int slot = -1; if (frames.consumeNewest(packet, slot)) { lastFrame = packet; hasFrame = true; lastSlot = slot; }
    if (!hasFrame) return;
    const bool wideDepth=lastFrame.farPlane/lastFrame.nearPlane>50000;auto* drawShader=wideDepth?rangeShader.get():shader.get();if(drawShader==nullptr)return;
    // JUCE paints the component overlay after renderOpenGL() and leaves GL_DEPTH_TEST disabled, so restore the
    // 3D pipeline state every frame; otherwise parts are layered by draw order instead of depth while orbiting.
    glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); glDisable(GL_BLEND);
    const auto scale = static_cast<float>(context.getRenderingScale()); glViewport(0, 0, juce::roundToInt(scale * (float) getWidth()), juce::roundToInt(scale * (float) getHeight())); glClearColor(0.035f, 0.055f, 0.075f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto activeSlot = static_cast<size_t>(lastSlot); M4 projection, viewMatrix, model; std::memcpy(projection.v, lastFrame.projection.v, sizeof(projection.v)); std::memcpy(viewMatrix.v, lastFrame.view.v, sizeof(viewMatrix.v)); std::memcpy(model.v, lastFrame.model.v, sizeof(model.v)); const auto finalMvp = mul(projection, mul(viewMatrix, model));
    if (slot >= 0)
    {
        DjehutiMat4 matrices[2]; std::memcpy(matrices[0].v, finalMvp.v, sizeof(finalMvp.v)); matrices[1] = lastFrame.model;
        glBindBuffer(GL_UNIFORM_BUFFER, frameBuffers[activeSlot]); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(matrices), matrices);
    }
    renderFloor(lastFrame);++renderedFrames;
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameBuffers[activeSlot]); drawShader->use();if(wideDepth){drawShader->setUniform("uClip",lastFrame.nearPlane,lastFrame.farPlane);drawShader->setUniform("uDepthBias",0.0f);}
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
            if(range.doubleSided)glDisable(GL_CULL_FACE);else glEnable(GL_CULL_FACE);
            const auto colour = colourOf(id);
            drawShader->setUniformMat4("uInstance", matrix, 1, GL_FALSE);
            drawShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
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
            drawShader->setUniformMat4("uInstance", matrix, 1, GL_FALSE);
            drawShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
            glDrawArrays(strip ? GL_LINE_STRIP : GL_LINES, 0, static_cast<GLsizei>(points.size() / 3));
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        };
        std::vector<std::pair<djehuti::cad::CadPick,juce::Colour>> pickColours {{hoverPick,juce::Colour(0xff9be7ff)}};
        for(const auto& p:pickedItems) pickColours.push_back({p,juce::Colour(p.edge>=0?0xffff4b44:0xffff4fd8)});
        for (const auto& [p, colour] : pickColours)
        {
            M4 m;
            if (!p.valid() || !matrixFor(p, m)) continue;
            if (p.face >= 0)
            {
                if(wideDepth)drawShader->setUniform("uDepthBias",-2.0e-6f);glDepthFunc(GL_LEQUAL); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f);
                glBindVertexArray(vao);
                drawShader->setUniformMat4("uInstance", m.v, 1, GL_FALSE);
                drawShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
                for (const auto& range : meshRanges)
                    if (range.featureId == p.featureId && range.faceIndex == p.face)
                    {if(range.doubleSided)glDisable(GL_CULL_FACE);else glEnable(GL_CULL_FACE);glDrawElements(GL_TRIANGLES, range.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<size_t>(range.first) * sizeof(std::uint32_t)));}
                glBindVertexArray(0);
                if(wideDepth)drawShader->setUniform("uDepthBias",0.0f);glDisable(GL_POLYGON_OFFSET_FILL); glDepthFunc(GL_LESS);
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
            drawShader->setUniformMat4("uInstance", unit.v, 1, GL_FALSE);
            for (size_t i = 0; i < posed->overlay.size(); ++i)
            {
                const juce::Colour colour(posed->overlay[i].argb);
                drawShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
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
            drawShader->setUniformMat4("uInstance", unit.v, 1, GL_FALSE);
            const juce::Colour colour(inspection.front().argb);
            drawShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
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
    const auto unit=identity(); drawShader->setUniformMat4("uInstance",unit.v,1,GL_FALSE);
    for(const auto& [argb,vertices]:batches)
    {
        const juce::Colour colour(argb); drawShader->setUniform("uColour",colour.getFloatRed(),colour.getFloatGreen(),colour.getFloatBlue());
        glBindBuffer(GL_ARRAY_BUFFER,axisVbo); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES,0,(GLsizei)(vertices.size()/6));
    }
    glBindVertexArray(0); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
    if (slot >= 0) fences[activeSlot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}
void Viewport3D::drawCube(const float*) {}
void Viewport3D::releaseGL() { if(floorVao)glDeleteVertexArrays(1,&floorVao);floorVao=0;glDeleteQueries(3,floorQueries);std::fill(std::begin(floorQueries),std::end(floorQueries),0);std::fill(std::begin(floorQueryPending),std::end(floorQueryPending),false);floorShader.reset();rangeShader.reset();floorReady=false; uploadedGeometryRevision=0; uploadedGeometryDocument=0; for (auto& f : fences) if (f != nullptr) { glDeleteSync(f); f = nullptr; } glDeleteBuffers(3, frameBuffers.data()); if (vao != 0) glDeleteVertexArrays(1, &vao); if (vbo != 0) glDeleteBuffers(1, &vbo); if (ibo != 0) glDeleteBuffers(1, &ibo); if (axisVao != 0) glDeleteVertexArrays(1, &axisVao); if (axisVbo != 0) glDeleteBuffers(1, &axisVbo); frameBuffers.fill(0); vao = vbo = ibo = axisVao = axisVbo = 0; frames.reset(); lastSlot = -1; hasFrame = false; renderDirty = true; uploadedGeometryRevision = 0; geometryIndexCount = 0; meshRanges.clear(); shader.reset(); positionAttribute.reset(); normalAttribute.reset(); glReady = false; }
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
    if(key==juce::KeyPress::escapeKey&&flyMode){setFlyMode(false);return true;}
    if(key==juce::KeyPress::escapeKey&&orbitPointPicking){orbitPointPicking=false;repaint();return true;}
    if(flyMode&&!key.getModifiers().isCommandDown()&&!key.getModifiers().isAltDown()){const auto c=juce::CharacterFunctions::toUpperCase(key.getTextCharacter());if(c=='W'||c=='A'||c=='S'||c=='D'||c=='Q'||c=='E'||key.getKeyCode()==juce::KeyPress::leftKey||key.getKeyCode()==juce::KeyPress::rightKey||key.getKeyCode()==juce::KeyPress::upKey||key.getKeyCode()==juce::KeyPress::downKey)return true;}
    if(onWorkspaceKey&&onWorkspaceKey(key))return true;
    auto navigationKey=key.getKeyCode();if(navigationKey>=1&&navigationKey<=26)navigationKey+='A'-1;navigationKey=(int)juce::CharacterFunctions::toUpperCase((juce::juce_wchar)navigationKey);
    if(key.getModifiers().isCtrlDown()&&key.getModifiers().isShiftDown()){
        if(navigationKey=='F'){setFlyMode(!flyMode);return true;}if(navigationKey=='O'){chooseOrbitPoint();return true;}
    }
    if(key==juce::KeyPress::homeKey){resetCamera();return true;}
    if(key==juce::KeyPress::escapeKey) { selectingRegion=false; if(selection!=nullptr) selection->setTool(djehuti::cad::CadSelection::Tool::pointer); markDirty(); repaint(); return true; }
    if((key.getTextCharacter()=='F'||key.getTextCharacter()=='f')&&key.getModifiers().isShiftDown()){fitSelection();return true;}
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
