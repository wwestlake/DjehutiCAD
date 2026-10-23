#include "Viewport3D.h"
#include <cmath>

bool runViewportRangeCheck(juce::var& report)
{
    using namespace djehuti::cad;juce::Array<juce::var> checks;int failed=0;juce::String error;
    const auto check=[&](bool ok,const juce::String& name,const juce::String& detail=juce::String())
    {auto* item=new juce::DynamicObject();item->setProperty("name",name);item->setProperty("ok",ok);if(!ok){++failed;item->setProperty("detail",detail);}checks.add(item);};
    const auto closeEnough=[](double a,double b,double relative=1e-5){return std::isfinite(a)&&std::abs(a-b)<=juce::jmax(1e-9,std::abs(b)*relative);};
    const auto cube=[](const juce::String& id,float size,float x=0,float y=0,float z=0)
    {
        CadMesh m;m.featureId=id;m.faceIndex=0;for(int i=0;i<8;++i)m.vertices.insert(m.vertices.end(),{x+((i&1)?size:0),y+((i&2)?size:0),z+((i&4)?size:0),0,0,1});
        m.indices={0,1,3,0,3,2,4,6,7,4,7,5,0,4,5,0,5,1,2,3,7,2,7,6,0,2,6,0,6,4,1,5,7,1,7,3};return m;
    };
    Viewport3D view(66);view.setSize(1280,720);std::uint64_t revision=0;
    for(const auto size:{.0001f,.01f,1.0f,1000.0f,10000000.0f})
    {
        auto snapshot=std::make_shared<CadRenderSnapshot>();snapshot->revision=++revision;snapshot->documentInstance=1;snapshot->meshes.push_back(cube("component",size));view.setSnapshot(snapshot);view.fitAll();const auto clip=view.clippingPlanes();
        check(clip[0]>0&&clip[1]>clip[0]&&std::isfinite(clip[1]),"Finite adaptive clipping at component size "+juce::String(size));
        check(clip[1]/clip[0]<20,"Fitted objects use a tight depth ratio at size "+juce::String(size));
        bool fits=true;for(int i=0;i<8;++i){juce::Point<float> pixel;fits=fits&&view.toScreen({(i&1)?size:0,(i&2)?size:0,(i&4)?size:0},pixel)&&pixel.x>=0&&pixel.x<=1280&&pixel.y>=0&&pixel.y<=720;}
        check(fits,"Zoom to Fit contains all model corners at size "+juce::String(size));
        const auto centre=view.pick({640,360},CadSelection::Mode::body);check(centre.featureId=="component","Body picking survives scale "+juce::String(size));
        auto p=juce::var(new juce::DynamicObject());p.getDynamicObject()->setProperty("distance",(double)view.distance*100);check(view.controlCamera(p,error),"Camera accepts long view distance at size "+juce::String(size),error);
        const auto distantClip=view.clippingPlanes();check(distantClip[1]/distantClip[0]<3&&distantClip[0]<view.distance&&distantClip[1]>view.distance,"Long views keep fitted geometry inside a tight clipping slab");
    }
    view.setSize(160,1000);view.fitAll();juce::Point<float> narrow;check(view.toScreen({0,0,0},narrow)&&narrow.x>=0&&narrow.x<=160&&narrow.y>=0&&narrow.y<=1000,"Portrait viewport fit respects horizontal field of view");view.setSize(1280,720);
    auto selection=std::make_shared<CadSelection>();view.setSelection(selection);check(!view.fitSelection(),"Selection framing rejects an empty selection");
    auto snapshot=std::make_shared<CadRenderSnapshot>();snapshot->revision=++revision;snapshot->documentInstance=1;snapshot->meshes={cube("small",.01f),cube("large",10000,0,100000,0)};view.setSnapshot(snapshot);view.fitAll();const auto allDistance=view.distance;
    selection->setSelected({"small","small",-1,-1});check(view.fitSelection()&&view.distance<.1&&view.distance<allDistance*.00001,"Selection framing isolates a small part in a large assembly");
    check(closeEnough(view.targetX,.005)&&view.panX==0&&view.panY==0,"Selection framing preserves world coordinates and clears view pan");
    check(view.cameraState()["depthMode"].toString()=="logarithmic","Wide scene depth ranges select logarithmic depth");
    view.yaw=0;view.pitch=0;view.targetX=view.targetY=view.targetZ=0;view.distance=.01f;view.panX=view.panY=0;
    auto depthScene=std::make_shared<CadRenderSnapshot>();depthScene->revision=++revision;depthScene->documentInstance=1;
    CadMesh nearMesh;nearMesh.featureId="nearMesh";nearMesh.faceIndex=2;nearMesh.vertices={-.001f,0,-.001f,0,-1,0,.001f,0,-.001f,0,-1,0,0,0,.001f,0,-1,0};nearMesh.indices={0,1,2};auto farMesh=nearMesh;farMesh.featureId="farMesh";for(size_t i=0;i<farMesh.vertices.size();i+=6){farMesh.vertices[i]*=100000;farMesh.vertices[i+1]=1000000;farMesh.vertices[i+2]*=100000;}depthScene->meshes={farMesh,nearMesh};view.setSnapshot(depthScene);
    const auto wide=view.clippingPlanes();check(wide[1]/wide[0]>1e8,"Representative tiny/remote objects exercise a wide depth range");check(view.pick({640,360},CadSelection::Mode::face).featureId=="nearMesh","Picking orders fragments by view depth across extreme nearMesh/farMesh ratios");
    std::array<float,3> origin{},direction{};check(view.rayThrough({640,360},origin,direction)&&closeEnough(direction[1],1)&&closeEnough(origin[1],-.01),"Analytic picking rays remain finite without unprojecting a remote farMesh plane");
    view.distance=.00001f;view.updatePosedBounds();check(view.clippingPlanes()[0]<view.distance,"Near clipping adapts to precision zoom rather than the whole assembly radius");
    view.distance=1;view.targetY=1000000;view.updatePosedBounds();check(view.clippingPlanes()[1]<10,"Clipping updates when the camera target changes without geometry changes");
    // Posed instances must frame their rendered transform, not the source part.
    auto instanceScene=std::make_shared<CadRenderSnapshot>();instanceScene->revision=++revision;instanceScene->documentInstance=1;instanceScene->meshes.push_back(cube("part",10));view.setSnapshot(instanceScene);
    auto posed=std::make_shared<CadPoseSnapshot>();posed->instancedFeatures.insert("part");CadPoseInstance instance;instance.instanceId="instance";instance.featureId="part";instance.matrix[0]=instance.matrix[5]=instance.matrix[10]=instance.matrix[15]=1;instance.matrix[12]=1000000;instance.matrix[13]=-2000000;posed->instances.push_back(instance);view.pose=posed;view.updatePosedBounds();selection->setSelected({"instance","part",-1,-1});check(view.fitSelection()&&closeEnough(view.targetX,1000005)&&closeEnough(view.targetY,-1999995),"Selection framing uses posed assembly instance bounds");
    view.pose.reset();view.updatePosedBounds();view.fitAll();view.yaw=.5f;view.pitch=.6f;view.distance=200;view.panX=view.panY=0;
    const auto event=[&](juce::Point<float> point,int mods){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(mods),1,0,0,0,0,&view,&view,juce::Time::getCurrentTime(),{},juce::Time::getCurrentTime(),1,true);};
    view.dragStart={0,0};view.mouseDrag(event({10,5},juce::ModifierKeys::leftButtonModifier));check(closeEnough(view.yaw,.6)&&closeEnough(view.pitch,.65),"Orbit callback preserves existing yaw/elevation conventions");
    const auto unitsPerPixel=400*std::tan(.425)/720;view.dragStart={0,0};view.mouseDrag(event({10,5},juce::ModifierKeys::rightButtonModifier));check(closeEnough(view.panX,10*unitsPerPixel)&&closeEnough(view.panY,-5*unitsPerPixel),"Pan callback scales pixel movement at the current viewing distance");
    juce::MouseWheelDetails wheel;wheel.deltaY=.4f;view.mouseWheelMove(event({},0),wheel);check(closeEnough(view.distance,200*std::exp(-.2)),"Wheel zoom retains exponential navigation behavior");
    for(const auto distance:{.00001f,1.0f,200.0f,100000.0f,5000000.0f})
    {view.distance=distance;view.updateReferenceGuides();check(view.glGuides.empty()&&view.gridSpacing>0,"Floor range uses constant shader geometry at distance "+juce::String(distance));std::array<float,3> rayOrigin{},rayDirection{};view.rayThrough({640,600},rayOrigin,rayDirection);const double t=-rayOrigin[2]/rayDirection[2];check(t>0&&std::isfinite(t),"World XY plane remains reachable after pan/orbit at distance "+juce::String(distance));}
    const auto saved=view.viewState();view.toggleGuide("floor");view.restoreViewState(saved);check(view.guideEnabled("floor"),"Floor visibility retains its existing persistence controls");
    check(!view.prepareFrame(0,1),"Hidden viewport still avoids redraw work");
    DjehutiFrameResourceRing frames;DjehutiFramePacket packet;packet.serial=7;packet.nearPlane=.00001f;packet.farPlane=1000000;packet.floorVisible=false;DjehutiFramePacket returned;int slot=-1;check(frames.publish(packet)&&frames.consumeNewest(returned,slot)&&returned.nearPlane==packet.nearPlane&&returned.farPlane==packet.farPlane&&!returned.floorVisible,"Clip/grid state travels with the same bounded frame resource as its camera");
    juce::String detail;check(runViewportCameraCheck(detail),"Existing camera orientation regression",detail);check(runViewportInteractionCheck(detail),"Existing body/face/region selection and guide-state regression",detail);
    auto* result=new juce::DynamicObject();result->setProperty("ok",failed==0);result->setProperty("passed",checks.size()-failed);result->setProperty("failed",failed);result->setProperty("checks",checks);report=result;return failed==0;
}
