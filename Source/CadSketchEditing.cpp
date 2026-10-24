#include "CadSketchEditing.h"
#include <map>
#include <set>

namespace djehuti::cad
{
bool CadDocument::editSketchGeometry(const juce::String& sketchId,const juce::StringArray& selection,
    const juce::String& operation,const juce::var& p,juce::StringArray& resultIds,juce::String& error)
{
    resultIds.clear();
    return mutate([&](juce::String& e){
        auto* s=mutableSketch(sketchId);if(!s){e="Unknown sketch.";return false;}
        const auto number=[&](const char* key,double fallback=0.){return (double)p.getProperty(key,fallback);};
        for(const auto* key:{"x","y","rx","ry","angle","ax","ay","bx","by","t"})
            if(p.hasProperty(key)&&(!p[key].isDouble()&&!p[key].isInt()&&!p[key].isInt64()||!std::isfinite(number(key)))){e="Geometry values must be finite numbers.";return false;}
        if(operation=="paste"&&p["clipboard"].isObject())
        {
            const auto clipboard=p["clipboard"];const auto* entities=clipboard["entities"].getArray();
            if(!entities||entities->isEmpty()){e="Clipboard contains no sketch geometry.";return false;}
            std::map<juce::String,juce::String> ids,groups;
            for(const auto& v:*entities)
            {
                auto entity=sketch_edit::entityFromJson(v);
                if(!juce::StringArray{"line","bezier","circle","ellipse"}.contains(entity.type)){e="Unsupported clipboard geometry.";return false;}
                for(const auto n:{entity.x1,entity.y1,entity.x2,entity.y2,entity.cx1,entity.cy1,entity.cx2,entity.cy2,entity.radius,entity.radiusY,entity.angle})if(!std::isfinite(n)){e="Invalid clipboard geometry.";return false;}
                const auto original=entity.id;entity.id=entity.type+"-"+juce::String(nextNumber++);ids[original]=entity.id;
                if(entity.groupId.isNotEmpty()){auto& group=groups[entity.groupId];if(group.isEmpty())group="group-"+juce::String(nextNumber++);entity.groupId=group;}
                entity.x1+=number("x");entity.y1+=number("y");
                if(entity.type=="line"||entity.type=="bezier"){entity.x2+=number("x");entity.y2+=number("y");}
                if(entity.type=="bezier"){entity.cx1+=number("x");entity.cy1+=number("y");entity.cx2+=number("x");entity.cy2+=number("y");}
                resultIds.add(entity.id);s->entities.push_back(entity);
            }
            const auto mapRef=[&](const juce::String& ref){const auto base=ref.upToFirstOccurrenceOf(":",false,false);const auto it=ids.find(base);return it==ids.end()?juce::String():it->second+ref.substring(base.length());};
            if(const auto* constraints=clipboard["constraints"].getArray())for(const auto& value:*constraints)
            {
                CadSketchConstraint c;c.id="constraint-"+juce::String(nextNumber++);c.type=value["type"].toString();c.firstEntity=mapRef(value["first"].toString());c.secondEntity=mapRef(value["second"].toString());c.value=(double)value["value"];
                if(c.firstEntity.isNotEmpty()&&(value["second"].toString().isEmpty()||c.secondEntity.isNotEmpty()))s->constraints.push_back(c);
            }
            return solveSketch(*s,e);
        }
        if(operation=="ellipse"||operation=="resize_ellipse")
        {
            CadSketchEntity* target=nullptr;
            if(operation=="resize_ellipse")for(auto& entity:s->entities)if(selection.contains(entity.id))target=&entity;
            if(operation=="resize_ellipse"&&(!target||(target->type!="circle"&&target->type!="ellipse")||target->mirrorOwner.isNotEmpty())){e="Select an independent circle or ellipse.";return false;}
            const auto rx=number("rx",target?target->radius:0),ry=number("ry",target?(target->type=="ellipse"?target->radiusY:target->radius):0);
            if(rx<minimumSketchCircleRadius||ry<minimumSketchCircleRadius){e="Ellipse radii must be positive.";return false;}
            if(!target){CadSketchEntity entity;entity.id="ellipse-"+juce::String(nextNumber++);s->entities.push_back(entity);target=&s->entities.back();}
            target->type="ellipse";target->radius=rx;target->radiusY=ry;target->angle=number("angle",target->angle);
            target->x1=number("x",target->x1);target->y1=number("y",target->y1);resultIds.add(target->id);
            const auto targetId=target->id;
            if(operation=="ellipse")registerSketchSymmetry(*s,{targetId});
            std::erase_if(s->constraints,[&](const auto& c){return c.firstEntity==targetId&&(c.type=="radius"||c.type=="diameter");});
            return solveSketch(*s,e);
        }
        juce::StringArray ids=selection;
        for(const auto& id:selection)
        {
            const CadSketchEntity* found=nullptr;for(const auto& entity:s->entities)if(entity.id==id)found=&entity;
            if(!found||found->mirrorOwner.isNotEmpty()){e="Selection contains missing or linked geometry; select independent sources.";return false;}
            if(found->groupId.isNotEmpty())for(const auto& entity:s->entities)if(entity.groupId==found->groupId)ids.addIfNotAlreadyThere(entity.id);
        }
        if(ids.isEmpty()){e="Select sketch geometry first.";return false;}
        if(operation=="resize_rectangle")
        {
            juce::String group;for(const auto& entity:s->entities)if(ids.contains(entity.id)&&entity.groupId.isNotEmpty()){group=entity.groupId;break;}
            if(group.isEmpty()||!sketch_edit::resizeRectangle(s->entities,group,{number("x"),number("y")},(int)p.getProperty("handle",-1))){e="Rectangle resize requires a valid corner/edge and positive dimensions.";return false;}
            for(const auto& entity:s->entities)if(entity.groupId==group){resultIds.add(entity.id);for(auto& c:s->constraints)if(c.firstEntity==entity.id&&(c.type=="distance"||c.type=="length"))c.value=std::hypot(entity.x2-entity.x1,entity.y2-entity.y1);}
            return solveSketch(*s,e);
        }
        if(operation=="split")
        {
            if(selection.size()!=1){e="Select one line or curve to split.";return false;}
            auto it=std::find_if(s->entities.begin(),s->entities.end(),[&](const auto& entity){return entity.id==selection[0];});
            if(it->type!="line"&&it->type!="bezier"){e="Only lines and Bezier curves can be split.";return false;}
            const auto old=*it;const auto t=p.hasProperty("t")?number("t"):old.type=="bezier"?sketch_edit::nearestParameter(old,{number("x"),number("y")}):
                ((number("x")-old.x1)*(old.x2-old.x1)+(number("y")-old.y1)*(old.y2-old.y1))/(std::pow(old.x2-old.x1,2)+std::pow(old.y2-old.y1,2));
            if(!std::isfinite(t)||t<=1e-6||t>=1-1e-6){e="Split point must be inside the segment.";return false;}
            CadSketchEntity second=old;
            if(old.type=="bezier"){const auto halves=sketch_edit::subdivide(old,t);*it=halves.first;second=halves.second;}
            else{it->x2=second.x1=old.x1+t*(old.x2-old.x1);it->y2=second.y1=old.y1+t*(old.y2-old.y1);}
            second.id=old.type+"-"+juce::String(nextNumber++);
            if(old.groupId.isNotEmpty())for(auto& entity:s->entities)if(entity.groupId==old.groupId)entity.groupId.clear();
            second.groupId.clear();
            std::erase_if(s->constraints,[&](const auto& c){return c.firstEntity==old.id&&c.type!="coincident";});
            for(auto& c:s->constraints){if(c.firstEntity==old.id+":p2")c.firstEntity=second.id+":p2";if(c.secondEntity==old.id+":p2")c.secondEntity=second.id+":p2";}
            CadSketchConstraint join;join.id="constraint-"+juce::String(nextNumber++);join.type="coincident";join.firstEntity=old.id+":p2";join.secondEntity=second.id+":p1";s->constraints.push_back(join);
            resultIds.add(old.id);resultIds.add(second.id);s->entities.push_back(second);return solveSketch(*s,e);
        }
        const bool copy=operation=="paste"||operation=="mirror";
        if(!copy&&operation!="rotate"&&operation!="translate"){e="Unsupported sketch editing operation.";return false;}
        const auto angle=number("angle")*juce::MathConstants<double>::pi/180,c=std::cos(angle),sn=std::sin(angle);
        const double ax=number("ax"),ay=number("ay"),dx=number("bx",1)-ax,dy=number("by")-ay,q=dx*dx+dy*dy;
        if(operation=="mirror"&&q<1e-16){e="Mirror axis needs two distinct points.";return false;}
        const auto transform=[&](double& x,double& y){
            if(operation=="mirror"){const auto u=x-ax,v=y-ay,t=(u*dx+v*dy)/q;x=ax+2*t*dx-u;y=ay+2*t*dy-v;}
            else if(operation=="rotate"){const auto u=x-number("x"),v=y-number("y");x=number("x")+u*c-v*sn;y=number("y")+u*sn+v*c;}
            else{x+=number("x");y+=number("y");}
        };
        std::map<juce::String,juce::String> remap,groups;
        std::vector<CadSketchEntity> copies,expected;
        for(auto& entity:s->entities)if(ids.contains(entity.id))
        {
            auto edited=entity;
            if(copy){edited.id=entity.type+"-"+juce::String(nextNumber++);remap[entity.id]=edited.id;edited.mirrorOwner.clear();if(entity.groupId.isNotEmpty()){auto& group=groups[entity.groupId];if(group.isEmpty())group="group-"+juce::String(nextNumber++);edited.groupId=group;}}
            transform(edited.x1,edited.y1);
            if(edited.type!="circle"&&edited.type!="ellipse")transform(edited.x2,edited.y2);
            if(edited.type=="bezier"){transform(edited.cx1,edited.cy1);transform(edited.cx2,edited.cy2);}
            if(edited.type=="ellipse"){if(operation=="rotate")edited.angle+=angle;else if(operation=="mirror")edited.angle=2*std::atan2(dy,dx)-edited.angle;}
            expected.push_back(edited);resultIds.add(edited.id);if(copy)copies.push_back(edited);else entity=edited;
        }
        const auto references=[&](const juce::String& ref){return ids.contains(ref.upToFirstOccurrenceOf(":",false,false));};
        if(copy)
        {
            std::vector<CadSketchConstraint> constraints;
            const auto mapRef=[&](juce::String ref){const auto base=ref.upToFirstOccurrenceOf(":",false,false);return remap[base]+ref.substring(base.length());};
            for(auto constraint:s->constraints)
                if(references(constraint.firstEntity)&&(constraint.secondEntity.isEmpty()||references(constraint.secondEntity)))
                {if(operation=="mirror"&&(constraint.type=="horizontal"||constraint.type=="vertical"))continue;constraint.id="constraint-"+juce::String(nextNumber++);constraint.firstEntity=mapRef(constraint.firstEntity);if(constraint.secondEntity.isNotEmpty())constraint.secondEntity=mapRef(constraint.secondEntity);constraints.push_back(constraint);}
            s->entities.insert(s->entities.end(),copies.begin(),copies.end());s->constraints.insert(s->constraints.end(),constraints.begin(),constraints.end());
        }
        else if(operation=="rotate")std::erase_if(s->constraints,[&](const auto& constraint){return references(constraint.firstEntity)&&(constraint.type=="horizontal"||constraint.type=="vertical");});
        if(!solveSketch(*s,e))return false;
        for(const auto& wanted:expected)for(const auto& actual:s->entities)if(actual.id==wanted.id)
        {
            const auto changed=std::hypot(actual.x1-wanted.x1,actual.y1-wanted.y1)>1e-7
                ||((actual.type=="line"||actual.type=="bezier")&&std::hypot(actual.x2-wanted.x2,actual.y2-wanted.y2)>1e-7);
            if(changed){e="The transformation conflicts with constraints to unselected geometry. Select the connected group or change its constraints first.";return false;}
        }
        return true;
    },error);
}
}
