#include "Viewport3D.h"
#include <cmath>
#include <limits>
using namespace juce::gl;

std::array<float,2> Viewport3D::navigationRange() const
{
    return {1e-6f,(float)juce::jmin((double)std::numeric_limits<float>::max()/1024,
                                  juce::jmax(1000.0,(double)juce::jmax(emptySceneRadius,sceneRadius)*100000))};
}

std::array<float,2> Viewport3D::clippingPlanes() const
{
    const double forward[]{-std::cos(pitch)*std::sin(yaw),std::cos(pitch)*std::cos(yaw),-std::sin(pitch)};
    const double target[]{targetX,targetY,targetZ};
    const double floor=juce::jmax(1e-9,juce::jmin((double)distance,(double)sceneRadius)*1e-7);
    double nearest=std::numeric_limits<double>::infinity(),farthest=0;
    for(const auto& box:posedBoxes)
    {
        double low=std::numeric_limits<double>::infinity(),high=-low;
        for(const auto& p:box)
        {double depth=distance;for(int i=0;i<3;++i)depth+=forward[i]*((double)p[i]-target[i]);low=juce::jmin(low,depth);high=juce::jmax(high,depth);}
        if(high<=floor)continue;
        nearest=juce::jmin(nearest,low>floor?low:floor);
        farthest=juce::jmax(farthest,high+juce::jmax(floor,(high-low)*.05));
    }
    if(!std::isfinite(nearest))return {(float)juce::jmax(floor,distance*.05),(float)juce::jmax(floor*2,distance*4.0)};
    const auto nearPlane=juce::jmax(floor,nearest*.5);
    return {(float)nearPlane,(float)juce::jmax(nearPlane*2,farthest)};
}

bool Viewport3D::fitSelection()
{
    if(selection==nullptr||selection->items().empty())return false;
    std::array<double,3> low{},high{};bool found=false;
    const auto include=[&](const Bounds& b,const float* matrix)
    {
        for(int c=0;c<8;++c)
        {
            double p[]{b.min[0],b.min[1],b.min[2]};for(int i=0;i<3;++i)if(c&(1<<i))p[i]=b.max[i];
            if(matrix!=nullptr){double moved[3]{};for(int i=0;i<3;++i)moved[i]=matrix[i]*p[0]+matrix[4+i]*p[1]+matrix[8+i]*p[2]+matrix[12+i];std::copy(std::begin(moved),std::end(moved),p);}
            for(int i=0;i<3;++i){low[i]=found?juce::jmin(low[i],p[i]):p[i];high[i]=found?juce::jmax(high[i],p[i]):p[i];}found=true;
        }
    };
    for(const auto& pick:selection->items())if(const auto it=featureBounds.find(pick.featureId);it!=featureBounds.end())
    {
        if(pick.drawId==pick.featureId&&(pose==nullptr||!pose->instancedFeatures.count(pick.featureId)))include(it->second,nullptr);
        if(pose!=nullptr)for(const auto& instance:pose->instances)if(instance.instanceId==pick.drawId)include(it->second,instance.matrix.data());
    }
    if(!found)return false;
    setFlyMode(false);orbitPointPicking=false;
    targetX=(float)((low[0]+high[0])*.5);targetY=(float)((low[1]+high[1])*.5);targetZ=(float)((low[2]+high[2])*.5);
    double r2=0;for(int i=0;i<3;++i)r2+=(high[i]-low[i])*(high[i]-low[i]);
    const double aspect=(double)juce::jmax(1,getWidth())/juce::jmax(1,getHeight());
    const auto fov=juce::jmin(.425,std::atan(std::tan(.425)*aspect));
    distance=(float)juce::jmax(1e-6,std::sqrt(r2)*.625/std::tan(fov));panX=panY=0;
    cameraChanged();markDirty();return true;
}

void Viewport3D::createFloorRenderer()
{
    floorShader=std::make_unique<juce::OpenGLShaderProgram>(context);
    const char* vertex=R"(#version 410 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0.0,1.0);})";
    const char* fragment=R"(#version 410 core
uniform vec2 uViewport,uInvProjection;
uniform vec3 uEye,uRight,uUp,uBack;
out vec4 colour;
float lineMask(vec2 p,float spacing){vec2 q=p/spacing;vec2 w=max(fwidth(q),vec2(1e-8));vec2 d=abs(fract(q+0.5)-0.5)/w;return 1.0-smoothstep(0.35,1.05,min(d.x,d.y));}
vec3 level(vec2 p,float spacing){return vec3(0.16,0.23,0.28)*lineMask(p,spacing)*0.30+vec3(0.24,0.32,0.37)*lineMask(p,5.0*spacing)*0.50;}
void main(){
 vec2 ndc=gl_FragCoord.xy/uViewport*2.0-1.0;
 vec3 ray=uRight*(ndc.x*uInvProjection.x)+uUp*(ndc.y*uInvProjection.y)-uBack;
 if(abs(ray.z)<1e-7)discard;
 float t=-uEye.z/ray.z;if(t<=0.0)discard;
 vec2 p=uEye.xy+t*ray.xy;
 float desired=max(max(length(dFdx(p)),length(dFdy(p)))*16.0,1e-12);
 float decade=pow(10.0,floor(log(desired)/log(10.0))),fraction=desired/decade;
 float a=fraction<2.0?1.0:fraction<5.0?2.0:5.0,b=fraction<2.0?2.0:fraction<5.0?5.0:10.0;
 float blend=smoothstep(0.0,1.0,log(fraction/a)/log(b/a));
 vec3 lines=mix(level(p,decade*a),level(p,decade*b),blend);
 vec2 width=max(fwidth(p),vec2(1e-12));vec2 axes=1.0-smoothstep(vec2(.35),vec2(1.1),abs(p)/width);
 lines=mix(lines,vec3(.31,.53,.40)*.6,axes.x);lines=mix(lines,vec3(.64,.37,.35)*.6,axes.y);
 float horizon=smoothstep(.003,.06,abs(ray.z)/length(ray));
 colour=vec4(vec3(.035,.055,.075)+lines*horizon,1.0);
})";
    if(!floorShader->addVertexShader(vertex)||!floorShader->addFragmentShader(fragment)||!floorShader->link())
    {glStatus="Floor shader error: "+floorShader->getLastError();juce::Logger::writeToLog(glStatus);floorShader.reset();return;}
    glGenVertexArrays(1,&floorVao);glGenQueries(3,floorQueries);floorReady=true;
}

void Viewport3D::renderFloor(const DjehutiFramePacket& packet)
{
    for(int i=0;i<3;++i)if(floorQueryPending[i])
    {GLuint ready=0;glGetQueryObjectuiv(floorQueries[i],GL_QUERY_RESULT_AVAILABLE,&ready);if(ready){GLuint64 ns=0;glGetQueryObjectui64v(floorQueries[i],GL_QUERY_RESULT,&ns);floorGpuNs=ns;floorQueryPending[i]=false;}}
    if(!packet.floorVisible||floorShader==nullptr||floorVao==0)return;
    const auto& v=packet.view.v;float eye[3]{};for(int i=0;i<3;++i)eye[i]=-(v[i*4]*v[12]+v[i*4+1]*v[13]+v[i*4+2]*v[14]);
    floorShader->use();const float scale=(float)context.getRenderingScale();floorShader->setUniform("uViewport",getWidth()*scale,getHeight()*scale);
    floorShader->setUniform("uInvProjection",1/packet.projection.v[0],1/packet.projection.v[5]);floorShader->setUniform("uEye",eye[0],eye[1],eye[2]);
    floorShader->setUniform("uRight",v[0],v[4],v[8]);floorShader->setUniform("uUp",v[1],v[5],v[9]);floorShader->setUniform("uBack",v[2],v[6],v[10]);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glBindVertexArray(floorVao);
    const int query=floorQueryCursor;const bool measure=!floorQueryPending[query];if(measure)glBeginQuery(GL_TIME_ELAPSED,floorQueries[query]);
    glDrawArrays(GL_TRIANGLES,0,3);
    if(measure){glEndQuery(GL_TIME_ELAPSED);floorQueryPending[query]=true;floorQueryCursor=(query+1)%3;}
    glBindVertexArray(0);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);
}

bool Viewport3D::captureViewport(const juce::File& file,juce::String& error)
{
    if(!file.isAbsolutePath(file.getFullPathName())||!file.hasFileExtension("png")||!file.getParentDirectory().isDirectory())
    {error="Capture requires an absolute PNG path in an existing directory.";return false;}
    if(!glReady||!isShowing()){error="Show the viewport and wait for OpenGL before capturing.";return false;}
    // Read the application-owned framebuffer on its GL thread. This is a render
    // diagnostic, not a screenshot of native UI or evidence of pointer testing.
    context.executeOnGLThread([this,file](juce::OpenGLContext&)
    {
        const auto scale=context.getRenderingScale();const int w=juce::roundToInt(getWidth()*scale),h=juce::roundToInt(getHeight()*scale);if(w<1||h<1)return;
        // A dedicated target avoids reading a stale front buffer while JUCE is
        // compositing its overlay. Capture exactly the current model/grid pass.
        GLint previousDraw=0,previousRead=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
        GLuint framebuffer=0,texture=0,depth=0;glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        const bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        std::vector<juce::uint8> rgba((size_t)w*h*4);
        if(complete){renderOpenGL();glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());}
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)previousDraw);glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previousRead);
        glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&texture);glDeleteFramebuffers(1,&framebuffer);
        if(!complete){juce::Logger::writeToLog("Viewport capture framebuffer is incomplete.");return;}
        juce::Image result(juce::Image::ARGB,w,h,true);juce::Image::BitmapData pixels(result,juce::Image::BitmapData::writeOnly);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto* p=rgba.data()+((size_t)(h-1-y)*w+x)*4;pixels.setPixelColour(x,y,juce::Colour(p[0],p[1],p[2],p[3]));}
        if(auto stream=file.createOutputStream()){stream->setPosition(0);stream->truncate();juce::PNGImageFormat().writeImageToStream(result,*stream);}
    },false);
    context.triggerRepaint();return true;
}
