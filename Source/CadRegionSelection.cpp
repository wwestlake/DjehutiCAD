#include "Viewport3D.h"
#include <cmath>
#include <map>

namespace
{
using P=juce::Point<float>;
float cross(P a,P b,P c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
bool inTriangle(P p,P a,P b,P c)
{
    if(std::abs(cross(a,b,c))<1e-6f) return false;
    const float x=cross(a,b,p), y=cross(b,c,p), z=cross(c,a,p);
    return (x>=0 && y>=0 && z>=0)||(x<=0 && y<=0 && z<=0);
}
float distanceToSegment(P p,P a,P b)
{
    const auto d=b-a; const float len=d.x*d.x+d.y*d.y;
    const float t=len>0?juce::jlimit(0.0f,1.0f,((p.x-a.x)*d.x+(p.y-a.y)*d.y)/len):0;
    return p.getDistanceFrom(a+d*t);
}
bool crossesRect(P a,P b,juce::Rectangle<float> r)
{
    float lo=0,hi=1;
    const float origin[]{a.x,a.y}, delta[]{b.x-a.x,b.y-a.y}, lower[]{r.getX(),r.getY()}, upper[]{r.getRight(),r.getBottom()};
    for(int k=0;k<2;++k)
    {
        if(std::abs(delta[k])<1e-8f) { if(origin[k]<lower[k]||origin[k]>upper[k]) return false; }
        else { float t1=(lower[k]-origin[k])/delta[k],t2=(upper[k]-origin[k])/delta[k]; if(t1>t2) std::swap(t1,t2); lo=juce::jmax(lo,t1); hi=juce::jmin(hi,t2); if(lo>hi) return false; }
    }
    return true;
}
}

// Projected window/crossing selection, including obscured geometry. It never changes the camera or model.
std::vector<djehuti::cad::CadPick> Viewport3D::pickRegion(P start,P end,bool circle) const
{
    using Pick=djehuti::cad::CadPick; using Mode=djehuti::cad::CadSelection::Mode;
    std::vector<Pick> result;
    if(pickSnapshot==nullptr || selection==nullptr) return result;
    const juce::Rectangle<float> bounds(start,end); const float radius=start.getDistanceFrom(end);
    const bool window=!circle && end.x>=start.x;
    const auto contains=[&](P p) { return circle?p.getDistanceFrom(start)<=radius:bounds.expanded(.01f).contains(p); };
    const auto segmentHit=[&](P a,P b) { return circle?distanceToSegment(start,a,b)<=radius:crossesRect(a,b,bounds); };
    const auto triHit=[&](P a,P b,P c)
    {
        if(contains(a)||contains(b)||contains(c)||segmentHit(a,b)||segmentHit(b,c)||segmentHit(c,a)) return true;
        if(circle) return inTriangle(start,a,b,c);
        return inTriangle(bounds.getTopLeft(),a,b,c)||inTriangle(bounds.getBottomRight(),a,b,c);
    };
    struct Candidate { Pick pick; bool inside=true,hit=false,seen=false; };
    std::map<juce::String,Candidate> candidates;
    const auto forDraw=[&](const juce::String& feature,auto callback)
    {
        std::array<float,16> unit{}; unit[0]=unit[5]=unit[10]=unit[15]=1;
        if(pose==nullptr || pose->instancedFeatures.count(feature)==0) callback(feature,unit);
        else for(const auto& instance:pose->instances) if(instance.featureId==feature) callback(instance.instanceId,instance.matrix);
    };
    const auto pickClip=clippingPlanes();std::array<float,3> pickEye{},pickDirection{};rayThrough({getWidth()*.5f,getHeight()*.5f},pickEye,pickDirection);
    const auto project=[&](const std::array<float,16>& m,const float* v,P& out)
    {
        std::array<float,3> point{};
        for(int k=0;k<3;++k) point[(size_t)k]=m[(size_t)k]*v[0]+m[(size_t)(4+k)]*v[1]+m[(size_t)(8+k)]*v[2]+m[(size_t)(12+k)];
        double depth=0;for(int k=0;k<3;++k)depth+=(point[k]-pickEye[k])*pickDirection[k];return depth>=pickClip[0]&&depth<=pickClip[1]&&toScreen(point,out);
    };
    if(selection->mode()==Mode::edge)
    {
        for(const auto& edge:pickSnapshot->edges) if(!edgeTarget||edgeTarget().isEmpty()||edgeTarget()==edge.featureId)forDraw(edge.featureId,[&](const juce::String& id,const auto& matrix)
        {
            auto& entry=candidates[id+"/edge/"+juce::String(edge.edgeIndex)]; entry.pick={id,edge.featureId,-1,edge.edgeIndex};
            P previous; bool prior=false;
            for(size_t i=0;i+2<edge.points.size();i+=3)
            {
                P p; if(!project(matrix,edge.points.data()+i,p)) { entry.inside=false; prior=false; continue; }
                entry.seen=true; entry.inside=entry.inside&&contains(p); entry.hit=entry.hit||contains(p)||(prior&&segmentHit(previous,p)); previous=p; prior=true;
            }
        });
    }
    else for(const auto& mesh:pickSnapshot->meshes) forDraw(mesh.featureId,[&](const juce::String& id,const auto& matrix)
    {
        const int face=selection->mode()==Mode::face?mesh.faceIndex:-1;
        auto& entry=candidates[id+"/face/"+juce::String(face)]; entry.pick={id,mesh.featureId,face,-1};
        for(size_t i=0;i+2<mesh.indices.size();i+=3)
        {
            P a,b,c;
            if(!project(matrix,mesh.vertices.data()+mesh.indices[i]*6u,a)||!project(matrix,mesh.vertices.data()+mesh.indices[i+1]*6u,b)||!project(matrix,mesh.vertices.data()+mesh.indices[i+2]*6u,c)) { entry.inside=false; continue; }
            entry.seen=true; entry.inside=entry.inside&&contains(a)&&contains(b)&&contains(c); entry.hit=entry.hit||triHit(a,b,c);
        }
    });
    for(const auto& [key,entry]:candidates) { juce::ignoreUnused(key); if(entry.seen && (window?entry.inside:entry.hit)) result.push_back(entry.pick); }
    return result;
}

bool runViewportInteractionCheck(juce::String& detail)
{
    using namespace djehuti::cad;
    Viewport3D view(77); view.setSize(640,480);
    auto selection=std::make_shared<CadSelection>(); view.setSelection(selection);
    auto geometry=std::make_shared<CadRenderSnapshot>(); geometry->revision=1; geometry->documentInstance=1;
    CadMesh a; a.featureId="plate"; a.faceIndex=0; a.vertices={-10,-10,0,0,0,1, 10,-10,0,0,0,1, 0,10,0,0,0,1}; a.indices={0,1,2}; geometry->meshes.push_back(a);
    auto b=a; b.featureId="second"; for(size_t i=0;i<b.vertices.size();i+=6) b.vertices[i]+=40; geometry->meshes.push_back(b);
    view.setSnapshot(geometry); view.fitAll();
    const auto check=[&](bool ok,const char* what) { if(!ok) detail=what; return ok; };
    if(!check(view.pickRegion({0,0},{640,480},false).size()==2,"Enclosing box must select both bodies")) return false;
    if(!check(view.pickRegion({640,480},{0,0},false).size()==2,"Crossing box must select both bodies")) return false;
    if(!check(view.pickRegion({0,0},{2,2},false).empty(),"Empty box must select nothing")) return false;
    if(!check(view.pickRegion({320,240},{1200,240},true).size()==2,"Circle must select projected bodies")) return false;
    selection->setMode(CadSelection::Mode::face);
    if(!check(view.pickRegion({0,0},{640,480},false).size()==2,"Face area selection must preserve subshape identity")) return false;
    view.updateReferenceGuides();
    if(!check(view.guideEnabled("floor") && view.gridSpacing>0,"Default shader floor must have valid world spacing")) return false;
    const auto saved=view.viewState(); view.toggleGuide("floor"); view.restoreViewState(saved);
    if(!check(view.guideEnabled("floor"),"Guide visibility must round-trip")) return false;
    ThreeDViewsPanel views(nullptr);
    views.restoreViewState(R"([{"floor":false}])");
    auto states=juce::JSON::parse(views.viewState());
    if(!check(states.size()==1 && !(bool)states[0]["floor"],"Closed views must stay closed when restoring view settings")) return false;
    views.restoreViewState(R"([{"floor":false},{"floor":true}])");
    states=juce::JSON::parse(views.viewState());
    if(!check(states.size()==2 && (bool)states[1]["floor"],"Restored additional view must retain its own settings")) return false;
    detail="Enclosing/crossing boxes, empty region, circle, face identities, world floor, guide visibility and view-count persistence passed.";
    return true;
}

std::vector<djehuti::cad::CadPick> Viewport3D::allPicks() const
{
    using Pick=djehuti::cad::CadPick;std::vector<Pick> result;if(!selection||!pickSnapshot)return result;
    const auto add=[&](const juce::String& feature,int face,int edge){
        const auto insert=[&](const juce::String& draw){for(const auto& p:result)if(p.drawId==draw&&p.face==face&&p.edge==edge)return;result.push_back({draw,feature,face,edge});};
        if(!pose||!pose->instancedFeatures.count(feature))insert(feature);else for(const auto& i:pose->instances)if(i.featureId==feature)insert(i.instanceId);
    };
    if(selection->mode()==djehuti::cad::CadSelection::Mode::edge)for(const auto& e:pickSnapshot->edges){if(!edgeTarget||edgeTarget().isEmpty()||edgeTarget()==e.featureId)add(e.featureId,-1,e.edgeIndex);}
    else for(const auto& m:pickSnapshot->meshes)add(m.featureId,selection->mode()==djehuti::cad::CadSelection::Mode::face?m.faceIndex:-1,-1);
    return result;
}
