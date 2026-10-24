#pragma once
#include "CadDocument.h"
#include <cmath>
#include <limits>

namespace djehuti::cad::sketch_edit
{
using Point = juce::Point<double>;
inline juce::var entityJson(const CadSketchEntity& e)
{
    auto* o=new juce::DynamicObject();o->setProperty("id",e.id);o->setProperty("type",e.type);o->setProperty("groupId",e.groupId);o->setProperty("construction",e.construction);
    o->setProperty("x1",e.x1);o->setProperty("y1",e.y1);o->setProperty("x2",e.x2);o->setProperty("y2",e.y2);o->setProperty("radius",e.radius);o->setProperty("radiusY",e.radiusY);o->setProperty("angle",e.angle);
    o->setProperty("cx1",e.cx1);o->setProperty("cy1",e.cy1);o->setProperty("cx2",e.cx2);o->setProperty("cy2",e.cy2);return o;
}
inline CadSketchEntity entityFromJson(const juce::var& v)
{
    CadSketchEntity e;e.id=v["id"].toString();e.type=v["type"].toString();e.groupId=v["groupId"].toString();e.construction=(bool)v["construction"];
    e.x1=(double)v["x1"];e.y1=(double)v["y1"];e.x2=(double)v["x2"];e.y2=(double)v["y2"];e.radius=(double)v["radius"];e.radiusY=(double)v["radiusY"];e.angle=(double)v["angle"];
    e.cx1=(double)v["cx1"];e.cy1=(double)v["cy1"];e.cx2=(double)v["cx2"];e.cy2=(double)v["cy2"];return e;
}
inline Point evaluate(const CadSketchEntity& e, double t)
{
    const auto s=1-t;
    return {s*s*s*e.x1+3*s*s*t*e.cx1+3*s*t*t*e.cx2+t*t*t*e.x2,
            s*s*s*e.y1+3*s*s*t*e.cy1+3*s*t*t*e.cy2+t*t*t*e.y2};
}
inline std::pair<CadSketchEntity,CadSketchEntity> subdivide(const CadSketchEntity& e,double t)
{
    const auto mix=[t](Point a,Point b){return a+(b-a)*t;};
    const Point a{e.x1,e.y1},b{e.cx1,e.cy1},c{e.cx2,e.cy2},d{e.x2,e.y2};
    const auto ab=mix(a,b),bc=mix(b,c),cd=mix(c,d),abc=mix(ab,bc),bcd=mix(bc,cd),p=mix(abc,bcd);
    auto first=e,second=e;
    first.cx1=ab.x;first.cy1=ab.y;first.cx2=abc.x;first.cy2=abc.y;first.x2=p.x;first.y2=p.y;
    second.x1=p.x;second.y1=p.y;second.cx1=bcd.x;second.cy1=bcd.y;second.cx2=cd.x;second.cy2=cd.y;
    return {first,second};
}
inline Point ellipsePoint(const CadSketchEntity& e,double t)
{
    const auto a=e.type=="ellipse"?e.angle:0.0,ry=e.type=="ellipse"?e.radiusY:e.radius;
    const auto x=e.radius*std::cos(t),y=ry*std::sin(t);
    return {e.x1+x*std::cos(a)-y*std::sin(a),e.y1+x*std::sin(a)+y*std::cos(a)};
}
inline std::vector<Point> outline(const CadSketchEntity& e)
{
    std::vector<Point> points;
    if(e.type=="circle"||e.type=="ellipse")for(int i=0;i<=96;++i)points.push_back(ellipsePoint(e,i*juce::MathConstants<double>::twoPi/96));
    else if(e.type=="bezier")for(int i=0;i<=64;++i)points.push_back(evaluate(e,i/64.0));
    else {points.push_back({e.x1,e.y1});points.push_back({e.x2,e.y2});}
    return points;
}
inline double nearestParameter(const CadSketchEntity& e,Point p)
{
    double t=0,best=std::numeric_limits<double>::max();
    for(int i=0;i<=128;++i){const auto u=i/128.0,d=evaluate(e,u).getDistanceSquaredFrom(p);if(d<best){best=d;t=u;}}
    double lo=std::max(0.,t-1/128.),hi=std::min(1.,t+1/128.);
    for(int i=0;i<45;++i){const auto a=(2*lo+hi)/3,b=(lo+2*hi)/3;if(evaluate(e,a).getDistanceSquaredFrom(p)<evaluate(e,b).getDistanceSquaredFrom(p))hi=b;else lo=a;}
    return (lo+hi)*.5;
}
inline Point constrained(Point origin,Point p,double degrees)
{
    const auto a=degrees*juce::MathConstants<double>::pi/180; const Point direction{std::cos(a),std::sin(a)};
    return origin+direction*((p-origin).getDotProduct(direction));
}
inline bool resizeRectangle(std::vector<CadSketchEntity>& entities,const juce::String& group,Point p,int handle)
{
    std::vector<CadSketchEntity*> rectangle;for(auto& e:entities)if(e.groupId==group&&e.type=="line")rectangle.push_back(&e);
    if(rectangle.size()!=4||handle<0||handle>7)return false;
    const Point origin{rectangle[0]->x1,rectangle[0]->y1};
    Point u{rectangle[0]->x2-origin.x,rectangle[0]->y2-origin.y},v{rectangle[1]->x2-rectangle[1]->x1,rectangle[1]->y2-rectangle[1]->y1};
    double x0=0,y0=0,x1=u.getDistanceFromOrigin(),y1=v.getDistanceFromOrigin();if(x1<1e-8||y1<1e-8)return false;
    u/=x1;v/=y1;const auto x=(p-origin).getDotProduct(u),y=(p-origin).getDotProduct(v);
    if(handle<4){if(handle==0)y0=y;if(handle==1)x1=x;if(handle==2)y1=y;if(handle==3)x0=x;}
    else{const auto corner=handle-4;if(corner==0||corner==3)x0=x;else x1=x;if(corner<2)y0=y;else y1=y;}
    if(x1-x0<1e-6||y1-y0<1e-6)return false;
    const Point points[]{origin+u*x0+v*y0,origin+u*x1+v*y0,origin+u*x1+v*y1,origin+u*x0+v*y1};
    for(int i=0;i<4;++i){rectangle[i]->x1=points[i].x;rectangle[i]->y1=points[i].y;rectangle[i]->x2=points[(i+1)%4].x;rectangle[i]->y2=points[(i+1)%4].y;}return true;
}
}
