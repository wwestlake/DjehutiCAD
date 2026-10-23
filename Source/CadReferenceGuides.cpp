#include "Viewport3D.h"
#include <cmath>

namespace
{
using GuidePoint = std::array<float,3>;
GuidePoint transform(const std::array<float,16>& m, float x,float y,float z)
{
    GuidePoint p {}; for (int k=0;k<3;++k) p[(size_t)k]=m[(size_t)k]*x+m[(size_t)(4+k)]*y+m[(size_t)(8+k)]*z+m[(size_t)(12+k)]; return p;
}
std::array<float,16> unitFrame() { std::array<float,16> m {}; m[0]=m[5]=m[10]=m[15]=1; return m; }
}

bool Viewport3D::guideEnabled(const juce::String& name) const
{
    return name=="floor" ? showFloor : name=="world" ? showWorld : name=="local" ? showLocal : name=="joints" && showJoints;
}
void Viewport3D::toggleGuide(const juce::String& name)
{
    if (name=="floor") showFloor=!showFloor; else if(name=="world") showWorld=!showWorld;
    else if(name=="local") showLocal=!showLocal; else if(name=="joints") showJoints=!showJoints;
    markDirty(); repaint();
}
juce::var Viewport3D::viewState() const
{
    auto o=std::make_unique<juce::DynamicObject>();
    o->setProperty("floor",showFloor); o->setProperty("world",showWorld); o->setProperty("local",showLocal); o->setProperty("joints",showJoints);
    return juce::var(o.release());
}
void Viewport3D::restoreViewState(const juce::var& value)
{
    if (!value.isObject()) return;
    showFloor=value.getProperty("floor",true); showWorld=value.getProperty("world",false);
    showLocal=value.getProperty("local",false); showJoints=value.getProperty("joints",false);
    markDirty(); repaint();
}

void Viewport3D::updateReferenceGuides()
{
    using Line=djehuti::cad::CadOverlayLine;
    std::vector<Line> lines;
    const auto add=[&](GuidePoint a,GuidePoint b,juce::uint32 colour) { lines.push_back({a,b,colour}); };
    const auto triad=[&](const std::array<float,16>& frame,float size,bool muted)
    {
        const juce::uint32 colour[] { muted?0xff805550:0xffee7a70,muted?0xff507859:0xff7cd88d,muted?0xff526a86:0xff79b5ff };
        const auto origin=transform(frame,0,0,0);
        add(origin,transform(frame,size,0,0),colour[0]); add(origin,transform(frame,0,size,0),colour[1]); add(origin,transform(frame,0,0,size),colour[2]);
    };
    const auto planes=[&](const std::array<float,16>& frame,float size)
    {
        for(int i=-5;i<=5;++i)
        {
            const float a=size*(float)i/5;
            add(transform(frame,a,-size,0),transform(frame,a,size,0),0xff465458); add(transform(frame,-size,a,0),transform(frame,size,a,0),0xff465458);
            add(transform(frame,a,0,-size),transform(frame,a,0,size),0xff494c60); add(transform(frame,-size,0,a),transform(frame,size,0,a),0xff494c60);
            add(transform(frame,0,a,-size),transform(frame,0,a,size),0xff43594e); add(transform(frame,0,-size,a),transform(frame,0,size,a),0xff43594e);
        }
        triad(frame,size*1.2f,false);
    };
    // Readout at the camera target depth. The shader selects/blends world-aligned
    // 1/2/5 levels per pixel, including the distant floor near the horizon.
    const double desired=juce::jmax(1e-9,32.0*distance*std::tan(.425)/juce::jmax(1,getHeight()));
    const double decade=std::pow(10.0,std::floor(std::log10(desired))),fraction=desired/decade;
    gridSpacing=(float)(decade*(fraction<2?1:fraction<5?2:5));
    if(showWorld) planes(unitFrame(),juce::jmax(1.0f,sceneRadius*0.65f));
    if(showLocal && pickSnapshot!=nullptr && selection!=nullptr)
    {
        const auto selected=selection->selected();
        const float size=juce::jmax(1.0f,sceneRadius*0.3f);
        bool found=false;
        if(pose!=nullptr) for(const auto& frame:pose->localFrames) if(frame.id==selected.drawId) { planes(frame.matrix,size); found=true; }
        if(!found) for(const auto& frame:pickSnapshot->localFrames) if(frame.id==selected.featureId) planes(frame.matrix,size);
    }
    if(showJoints && pose!=nullptr)
    {
        juce::String selectedJoint;
        for(const auto& axis:pose->axes) for(const auto& highlight:highlights) if(highlight.first==axis.jointId) selectedJoint=axis.jointId;
        const float size=pose->axisLength*0.7f;
        constexpr double tau=6.283185307179586;
        for(const auto& joint:pose->axes)
        {
            if(selectedJoint.isNotEmpty() && joint.jointId!=selectedJoint) continue;
            triad(joint.parentFrame,size,true); triad(joint.movingFrame,size,false);
            for(int i=-4;i<=4;++i)
            {
                const float d=size*(float)i/4;
                add(transform(joint.parentFrame,d,-size,0),transform(joint.parentFrame,d,size,0),0xff4c5157);
                add(transform(joint.parentFrame,-size,d,0),transform(joint.parentFrame,size,d,0),0xff4c5157);
            }
            if(joint.linear)
            {
                const float start=joint.hasLimits?(float)((joint.lower-joint.reference)*1000.0):-size;
                const float end=joint.hasLimits?(float)((joint.upper-joint.reference)*1000.0):size;
                add(transform(joint.parentFrame,0,0,start),transform(joint.parentFrame,0,0,end),0xffe2bd70);
                for(float z:{start,end}) if(joint.hasLimits) add(transform(joint.parentFrame,-size*.15f,0,z),transform(joint.parentFrame,size*.15f,0,z),0xffffca78);
                add(transform(joint.parentFrame,0,0,0),transform(joint.movingFrame,0,0,0),0xff9ee0ed);
            }
            else
            {
                const double from=joint.hasLimits?joint.lower-joint.reference:0;
                const double requested=joint.hasLimits?joint.upper-joint.reference:tau;
                const double to=juce::jlimit(from-tau*2,from+tau*2,requested);
                for(int i=0;i<64;++i)
                {
                    const double a=from+(to-from)*(double)i/64,b=from+(to-from)*(double)(i+1)/64;
                    add(transform(joint.parentFrame,size*(float)std::cos(a),size*(float)std::sin(a),0),transform(joint.parentFrame,size*(float)std::cos(b),size*(float)std::sin(b),0),0xffcfa663);
                }
                if(joint.hasLimits) for(double a:{from,to}) add(transform(joint.parentFrame,size*.85f*(float)std::cos(a),size*.85f*(float)std::sin(a),0),transform(joint.parentFrame,size*1.15f*(float)std::cos(a),size*1.15f*(float)std::sin(a),0),0xffffd492);
                add(transform(joint.parentFrame,0,0,0),transform(joint.movingFrame,size,0,0),0xfff3dc8a);
            }
        }
    }
    const juce::SpinLock::ScopedLockType lock(highlightLock); glGuides=std::move(lines);
}

void Viewport3D::drawReferenceGuides(juce::Graphics& g)
{
    g.setFont(juce::Font(11.0f)); g.setColour(juce::Colour(0xff9ab5c4));
    if(showFloor) g.drawText("XY floor | adaptive grid ~ "+juce::String(gridSpacing,gridSpacing<1?3:1)+" mm",10,30,280,18,juce::Justification::left);
    const auto labelFrame=[&](const djehuti::cad::CadLocalFrame& frame)
    {
        juce::Point<float> p;
        if(toScreen(transform(frame.matrix,0,0,0),p)) g.drawText(frame.name+" origin",(int)p.x+6,(int)p.y+6,180,18,juce::Justification::left);
        const float size=juce::jmax(1.0f,sceneRadius*.36f);
        const char* axes[]{"X","Y","Z"};
        for(int k=0;k<3;++k) if(toScreen(transform(frame.matrix,k==0?size:0,k==1?size:0,k==2?size:0),p)) g.drawText(axes[k],(int)p.x+3,(int)p.y-8,20,16,juce::Justification::left);
    };
    if(showWorld) labelFrame({"world","World",unitFrame()});
    if(showLocal && selection!=nullptr && pickSnapshot!=nullptr)
    {
        const auto picked=selection->selected(); bool found=false;
        if(pose!=nullptr) for(const auto& f:pose->localFrames) if(f.id==picked.drawId) { labelFrame(f); found=true; }
        if(!found) for(const auto& f:pickSnapshot->localFrames) if(f.id==picked.featureId) labelFrame(f);
    }
    if(showJoints && pose!=nullptr)
    {
        juce::String chosen; for(const auto& j:pose->axes) for(const auto& h:highlights) if(h.first==j.jointId) chosen=j.jointId;
        for(const auto& j:pose->axes)
        {
            if(chosen.isNotEmpty() && chosen!=j.jointId) continue;
            juce::Point<float> p; if(!toScreen(j.origin,p)) continue;
            const auto value=j.linear?j.coordinate*1000:j.coordinate*180/juce::MathConstants<double>::pi;
            auto text=j.name+": "+juce::String(value,2)+(j.linear?" mm":" deg")+" ["+j.role+"]";
            if(j.hasLimits) text+=" | "+juce::String((j.linear?1000.0:180/juce::MathConstants<double>::pi)*j.lower,1)+" .. "+juce::String((j.linear?1000.0:180/juce::MathConstants<double>::pi)*j.upper,1)+(j.hardLimits?" hard":" advisory");
            g.setColour(juce::Colour(0xffe8cf9c)); g.drawText(text,(int)p.x+8,(int)p.y-26,420,20,juce::Justification::left);
        }
    }
    if(selectingRegion)
    {
        const auto start=pressPosition.toFloat(); g.setColour(juce::Colour(0xff8fdac7).withAlpha(.15f));
        if(selection!=nullptr && selection->tool()==djehuti::cad::CadSelection::Tool::circle)
        {
            const auto radius=start.getDistanceFrom(regionEnd); const juce::Rectangle<float> b(start.x-radius,start.y-radius,2*radius,2*radius);
            g.fillEllipse(b); g.setColour(juce::Colour(0xff8fdac7)); g.drawEllipse(b,1);
        }
        else { const juce::Rectangle<float> b(start,regionEnd); g.fillRect(b); g.setColour(juce::Colour(0xff8fdac7)); g.drawRect(b,1); }
    }
}

void Viewport3D::showContextMenu(const juce::MouseEvent& e)
{
    const auto hit=pick(e.position,selection!=nullptr?selection->mode():djehuti::cad::CadSelection::Mode::body);
    if(selection!=nullptr) selection->setSelected(hit);
    juce::PopupMenu menu;
    if(hit.valid())
    {
        menu.addItem(1,"Inspect selection");
        if(hit.face>=0) menu.addItem(2,"Create sketch on face...");
        else if(hit.edge<0) { menu.addItem(3,"Export selected..."); if(hit.drawId==hit.featureId) { menu.addItem(4,"Hide selected"); menu.addItem(5,"Delete selected"); } }
        menu.addSeparator();
    }
    menu.addItem(12,"Fit selection",hit.valid());menu.addItem(10,"Fit all"); menu.addItem(11,"Reset camera"); menu.addSeparator();
    const juce::StringArray labels{"Floor grid","World reference planes","Selected local planes","Joint frames and limits"};
    const juce::StringArray keys{"floor","world","local","joints"};
    for(int i=0;i<4;++i) menu.addItem(20+i,labels[i],true,guideEnabled(keys[i]));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe=juce::Component::SafePointer<Viewport3D>(this),hit](int id)
    {
        if(safe==nullptr || id==0) return;
        if(id==12) safe->fitSelection();else if(id==10) safe->fitAll(); else if(id==11) safe->resetCamera();
        else if(id>=20 && id<24) safe->toggleGuide(juce::StringArray{"floor","world","local","joints"}[id-20]);
        else if(safe->onContextCommand) safe->onContextCommand(juce::StringArray{"inspect","sketch","export","hide","delete"}[id-1],hit);
    });
}
