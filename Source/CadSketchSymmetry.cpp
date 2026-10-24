#include "CadSketchSymmetry.h"
#include <algorithm>
#include <cmath>
namespace djehuti::cad
{
namespace
{
const CadSketchEntity* findEntity(const CadSketch& s,const juce::String& id)
{for(const auto& e:s.entities)if(e.id==id)return &e;return nullptr;}
bool close(double a,double b){return std::abs(a-b)<1e-7;}
bool same(const CadSketchEntity& a,const CadSketchEntity& b)
{
    if(a.type!=b.type||a.construction!=b.construction)return false;
    const auto p=[](double a,double b,double c,double d){return close(a,c)&&close(b,d);};
    if(a.type=="ellipse")return p(a.x1,a.y1,b.x1,b.y1)&&close(a.radius,b.radius)&&close(a.radiusY,b.radiusY)&&close(std::remainder(a.angle-b.angle,juce::MathConstants<double>::pi),0);
    if(a.type=="circle")return p(a.x1,a.y1,b.x1,b.y1)&&close(a.radius,b.radius);
    const bool forward=p(a.x1,a.y1,b.x1,b.y1)&&p(a.x2,a.y2,b.x2,b.y2),reverse=p(a.x1,a.y1,b.x2,b.y2)&&p(a.x2,a.y2,b.x1,b.y1);
    if(a.type=="line")return forward||reverse;
    return (forward&&p(a.cx1,a.cy1,b.cx1,b.cy1)&&p(a.cx2,a.cy2,b.cx2,b.cy2))||(reverse&&p(a.cx1,a.cy1,b.cx2,b.cy2)&&p(a.cx2,a.cy2,b.cx1,b.cy1));
}
bool overlaps(const CadSketchEntity& a,const CadSketchEntity& b)
{
    if(a.type!="line"||b.type!="line"||a.construction!=b.construction||same(a,b))return false;
    const auto dx=a.x2-a.x1,dy=a.y2-a.y1,len=std::hypot(dx,dy);if(len<1e-8)return false;
    const auto cross=[&](double x,double y){return std::abs((x-a.x1)*dy-(y-a.y1)*dx)/len;};if(cross(b.x1,b.y1)>1e-7||cross(b.x2,b.y2)>1e-7)return false;
    const auto t=[&](double x,double y){return ((x-a.x1)*dx+(y-a.y1)*dy)/len;};const auto t1=t(b.x1,b.y1),t2=t(b.x2,b.y2);return std::min(len,std::max(t1,t2))-std::max(0.0,std::min(t1,t2))>1e-7;
}
bool axisValid(const juce::String& axis){return juce::StringArray{"horizontal","vertical","both","line"}.contains(axis);}
}
std::vector<CadSketchEntity> reflectedSketchEntities(const CadSketch& sketch,const CadSketchMirror& mirror,juce::String& error)
{
    std::vector<CadSketchEntity> result;if(!axisValid(mirror.axis)||!std::isfinite(mirror.cx)||!std::isfinite(mirror.cy)){error="Invalid mirror axis or centre.";return result;}
    double ax=mirror.cx,ay=mirror.cy,dx=1,dy=0;
    if(mirror.axis=="line")
    {const auto* line=findEntity(sketch,mirror.axisEntity);if(!line||line->type!="line"||!line->construction||line->mirrorOwner.isNotEmpty()){error="Mirror axis must reference an independent construction line in this sketch.";return result;}ax=line->x1;ay=line->y1;dx=line->x2-line->x1;dy=line->y2-line->y1;const auto len=std::hypot(dx,dy);if(len<1e-8){error="Mirror axis has zero length.";return result;}dx/=len;dy/=len;}
    const int count=mirror.axis=="both"?3:1;
    for(const auto& id:mirror.sources)
    {
        const auto* source=findEntity(sketch,id);if(!source||source->mirrorOwner.isNotEmpty()){error="Mirror source is missing or is itself linked. Select independent source entities.";return {};}
        if(id==mirror.axisEntity){error="The mirror axis cannot also be a mirrored source.";return {};}
        for(int i=1;i<=count;++i)
        {
            auto copy=*source;copy.id=mirror.id+"_"+id+"_"+juce::String(i);copy.groupId.clear();copy.mirrorOwner=mirror.id;
            const auto reflect=[&](double& x,double& y)
            {if(mirror.axis=="line"){const auto px=x-ax,py=y-ay,t=px*dx+py*dy;x=ax+2*t*dx-px;y=ay+2*t*dy-py;}
             else{if(mirror.axis=="vertical"||(mirror.axis=="both"&&(i&1)))x=2*ax-x;if(mirror.axis=="horizontal"||(mirror.axis=="both"&&(i&2)))y=2*ay-y;}};
            reflect(copy.x1,copy.y1);if(copy.type!="circle"&&copy.type!="ellipse")reflect(copy.x2,copy.y2);if(copy.type=="ellipse"){double ux=source->x1+std::cos(source->angle),uy=source->y1+std::sin(source->angle);reflect(ux,uy);copy.angle=std::atan2(uy-copy.y1,ux-copy.x1);}if(copy.type=="bezier"){reflect(copy.cx1,copy.cy1);reflect(copy.cx2,copy.cy2);}
            for(const auto coordinate:{copy.x1,copy.y1,copy.x2,copy.y2,copy.cx1,copy.cy1,copy.cx2,copy.cy2})if(!std::isfinite(coordinate)){error="Reflection exceeds the finite sketch coordinate range.";return {};}
            const auto duplicate=[&](const auto& items){return std::any_of(items.begin(),items.end(),[&](const auto& e){return same(e,copy);});};
            if(!duplicate(sketch.entities)&&!duplicate(result))
            {if(std::any_of(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return overlaps(e,copy);})||std::any_of(result.begin(),result.end(),[&](const auto& e){return overlaps(e,copy);})){error="Reflection would create partially overlapping line segments. Trim the source to a half or quadrant bounded by the symmetry axes.";return {};}result.push_back(copy);}
        }
    }return result;
}
juce::var sketchSymmetryToJson(const CadSketch& s)
{
    auto* v=new juce::DynamicObject();v->setProperty("mode",s.symmetryMode);v->setProperty("x",s.symmetryX);v->setProperty("y",s.symmetryY);juce::Array<juce::var> mirrors;
    for(const auto& m:s.mirrors){auto* o=new juce::DynamicObject();o->setProperty("id",m.id);o->setProperty("axis",m.axis);o->setProperty("axisEntity",m.axisEntity);o->setProperty("x",m.cx);o->setProperty("y",m.cy);o->setProperty("live",m.live);o->setProperty("linked",true);o->setProperty("sources",m.sources);mirrors.add(o);}v->setProperty("mirrors",mirrors);return v;
}
bool readSketchSymmetry(CadSketch& s,const juce::var& v,juce::String& error)
{
    if(v.isVoid())return true;if(!v.isObject()){error="Invalid stored symmetry data.";return false;}
    s.symmetryMode=v.getProperty("mode","off").toString();s.symmetryX=(double)v["x"];s.symmetryY=(double)v["y"];
    if(!juce::StringArray{"off","horizontal","vertical","both"}.contains(s.symmetryMode)||!std::isfinite(s.symmetryX)||!std::isfinite(s.symmetryY)){error="Invalid stored symmetry mode or centre.";return false;}
    juce::StringArray ids;if(auto* list=v["mirrors"].getArray())for(const auto& o:*list)
    {CadSketchMirror m;m.id=o["id"].toString();m.axis=o["axis"].toString();m.axisEntity=o["axisEntity"].toString();m.cx=(double)o["x"];m.cy=(double)o["y"];m.live=(bool)o["live"];if(auto* a=o["sources"].getArray())for(const auto& e:*a)m.sources.addIfNotAlreadyThere(e.toString());if(m.id.isEmpty()||ids.contains(m.id)||!axisValid(m.axis)||!std::isfinite(m.cx)||!std::isfinite(m.cy)){error="Invalid stored mirror relationship.";return false;}ids.add(m.id);s.mirrors.push_back(m);}return true;
}
juce::var CadDocument::sketchSymmetryState(const juce::String& id) const
{if(const auto* s=findSketch(id))return sketchSymmetryToJson(*s);return {};}
void CadDocument::registerSketchSymmetry(CadSketch& sketch,const juce::StringArray& sources)
{
    if(sketch.symmetryMode=="off")return;CadSketchMirror m;m.id=newId("symmetry",nextNumber++);m.axis=sketch.symmetryMode;m.cx=sketch.symmetryX;m.cy=sketch.symmetryY;m.sources=sources;m.live=true;sketch.mirrors.push_back(m);
}
bool CadDocument::updateSketchMirrors(CadSketch& s,juce::String& error) const
{
    std::erase_if(s.entities,[](const auto& e){return e.mirrorOwner.isNotEmpty();});
    for(const auto& m:s.mirrors){juce::String failure;const auto copies=reflectedSketchEntities(s,m,failure);if(failure.isNotEmpty()){error=failure;return false;}s.entities.insert(s.entities.end(),copies.begin(),copies.end());}return true;
}
bool CadDocument::sketchSymmetryCommand(const juce::String& id,const juce::var& args,juce::String& error)
{
    error.clear();if(!args.isObject()){error="Symmetry parameters must be an object.";return false;}
    const auto action=args.getProperty("operation","configure").toString();
    return mutate([&](juce::String& e)
    {
        auto* s=mutableSketch(id);if(!s){e="Open an existing sketch.";return false;}
        const auto number=[&](const char* k,double fallback,double& out){const auto v=args.getProperty(k,fallback);if(!(v.isInt()||v.isInt64()||v.isDouble())||!std::isfinite((double)v)){e="Symmetry centre coordinates must be finite numbers.";return false;}out=(double)v;return true;};
        if(action=="configure")
        {
            const auto mode=args.getProperty("mode",s->symmetryMode).toString();if(!juce::StringArray{"off","horizontal","vertical","both"}.contains(mode)){e="Choose off, horizontal, vertical or both symmetry.";return false;}
            double x,y;if(!number("x",s->symmetryX,x)||!number("y",s->symmetryY,y))return false;s->symmetryMode=mode;s->symmetryX=x;s->symmetryY=y;for(auto& m:s->mirrors)if(m.live){m.cx=x;m.cy=y;}
        }
        else if(action=="mirror")
        {
            CadSketchMirror m;m.id=newId("mirror",nextNumber++);m.axis=args.getProperty("axis","both").toString();m.axisEntity=args["axisEntity"].toString();if(!number("x",s->symmetryX,m.cx)||!number("y",s->symmetryY,m.cy))return false;
            if(auto* ids=args["entities"].getArray())for(const auto& v:*ids)m.sources.addIfNotAlreadyThere(v.toString());if(m.sources.isEmpty()){e="Select source sketch entities to mirror.";return false;}
            const auto copies=reflectedSketchEntities(*s,m,e);if(e.isNotEmpty())return false;
            const auto linked=args.getProperty("linked",true);if(!linked.isBool()){e="Linked must be boolean.";return false;}
            if((bool)linked)s->mirrors.push_back(m);else for(auto copy:copies){copy.id=newId(copy.type.toRawUTF8(),nextNumber++);copy.mirrorOwner.clear();s->entities.push_back(copy);}
        }
        else if(action=="update"||action=="delete")
        {
            const auto found=std::find_if(s->mirrors.begin(),s->mirrors.end(),[&](const auto& m){return m.id==args["id"].toString();});if(found==s->mirrors.end()){e="Unknown mirror relationship.";return false;}
            if(action=="delete")s->mirrors.erase(found);else {auto& m=*found;m.axis=args.getProperty("axis",m.axis).toString();m.axisEntity=args.getProperty("axisEntity",m.axisEntity).toString();if(!number("x",m.cx,m.cx)||!number("y",m.cy,m.cy))return false;m.live=false;}
        }
        else if(action=="construction")
        {
            auto found=std::find_if(s->entities.begin(),s->entities.end(),[&](const auto& entity){return entity.id==args["entity"].toString();});
            if(found==s->entities.end()||found->type!="line"||found->mirrorOwner.isNotEmpty()||!args["value"].isBool()){e="Choose an independent line and a boolean construction value.";return false;}found->construction=(bool)args["value"];
        }
        else{e="Unknown symmetry operation.";return false;}
        if(!updateSketchMirrors(*s,e))return false;return solveSketch(*s,e);
    },error);
}
}
