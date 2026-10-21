#include "CadComponents.h"
#include <cmath>
#include <cstdlib>
#include <map>

namespace djehuti::cad::components
{
namespace
{
struct Cell{juce::var value;juce::String formula,hyperlink;};
using Row=std::map<int,Cell>;
void localTags(juce::XmlElement& element)
{
    if(element.isTextElement())return;const auto tag=element.getTagName();if(tag.containsChar(':'))element.setTagName(tag.fromLastOccurrenceOf(":",false,false));
    for(auto* child:element.getChildIterator())localTags(*child);
}
juce::String text(const juce::XmlElement& e)
{
    juce::String out;if(e.hasTagName("t"))return e.getAllSubText();
    for(auto* child:e.getChildIterator())out+=text(*child);return out;
}
std::unique_ptr<juce::XmlElement> xml(juce::ZipFile& zip,const juce::String& path)
{
    const int index=zip.getIndexOfFileName(path);if(index<0)return {};
    const auto* entry=zip.getEntry(index);if(!entry||entry->uncompressedSize>8*1024*1024)return {};
    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));auto root=stream?juce::XmlDocument::parse(stream->readEntireStreamAsString()):nullptr;if(root)localTags(*root);return root;
}
int column(const juce::String& reference)
{int value=0;for(const auto c:reference){if(c<'A'||c>'Z')break;value=value*26+c-'A'+1;}return value-1;}
bool numeric(const juce::var& v){return (v.isDouble()||v.isInt()||v.isInt64())&&std::isfinite((double)v);}
Row readRow(const juce::XmlElement& element,const juce::StringArray& strings,const std::map<juce::String,juce::String>& links)
{
    Row row;
    for(auto* c:element.getChildIterator())if(c->hasTagName("c"))
    {
        const auto ref=c->getStringAttribute("r"),type=c->getStringAttribute("t");const int index=column(ref);if(index<0||index>1024)continue;
        Cell value;const auto* v=c->getChildByName("v");const auto raw=v?v->getAllSubText():juce::String();
        if(const auto* f=c->getChildByName("f"))value.formula="="+f->getAllSubText();
        if(type=="s"){const int n=raw.getIntValue();if(n>=0&&n<strings.size())value.value=strings[n];}
        else if(type=="inlineStr"){if(const auto* s=c->getChildByName("is"))value.value=text(*s);}
        else if(type=="str")value.value=raw;
        else if(type=="e")value.value="Workbook error: "+raw;
        else if(raw.isNotEmpty())
        {const auto utf=raw.toStdString();char* end=nullptr;const double n=std::strtod(utf.c_str(),&end);if(end==utf.c_str()+utf.size()&&std::isfinite(n))value.value=n;else value.value=raw;}
        if(const auto found=links.find(ref);found!=links.end())value.hyperlink=found->second;row[index]=value;
    }
    return row;
}
juce::String category(const juce::String& kind,bool battery)
{
    if(battery)return "Electrical/Battery packs/"+kind;
    if(kind.containsIgnoreCase("rail"))return "Structure/Extrusions";
    if(kind.containsIgnoreCase("actuator"))return "Motion/Smart actuators";
    if(kind.containsIgnoreCase("gearmotor"))return "Motion/Gearmotors/"+kind;
    return "Components/"+kind;
}
juce::var record(const Row& row,const Row& headers,bool battery,const juce::File& file,const juce::String& sheet,int number)
{
    const auto at=[&](int index)->juce::var{const auto hit=row.find(index);return hit==row.end()?juce::var():hit->second.value;};
    auto result=object();const auto manufacturer=at(battery?1:2).toString().trim(),model=at(battery?2:3).toString().trim(),part=battery?model:at(4).toString().trim();
    put(result,"id","component-"+digest(manufacturer.toLowerCase()+"|"+(part.isNotEmpty()?part:model).toLowerCase()).substring(0,32));put(result,"revision",1);put(result,"origin","commercial");
    put(result,"name",manufacturer+" "+model);put(result,"manufacturer",manufacturer);put(result,"model",model);put(result,"partNumber",part);put(result,"recordId",at(0));
    put(result,"category",category(at(battery?3:1).toString(),battery));put(result,"description",at(battery?25:21));put(result,"sourceUrl",at(battery?24:20));
    put(result,"reviewStatus",at(battery?23:18));
    auto provenance=object();put(provenance,"workbook",file.getFileName());put(provenance,"sheet",sheet);put(provenance,"row",number);put(provenance,"recordId",at(0));put(result,"provenance",provenance);
    juce::Array<juce::var> properties;
    for(const auto& [index,header]:headers)
    {
        auto property=object();put(property,"label",header.value);put(property,"value",at(index));
        const auto hit=row.find(index);if(hit!=row.end())
        {if(hit->second.formula.isNotEmpty())put(property,"formula",hit->second.formula);if(hit->second.hyperlink.isNotEmpty())put(property,"hyperlink",hit->second.hyperlink);}
        // Only declared column units are normalized. Original headers, typed values and formulas survive.
        double factor=0;juce::String unit;
        if(battery)
        {
            if(index==5||index==12){factor=1;unit="V";}else if(index==6){factor=3600;unit="C";}
            else if(index==7||index==8){factor=3600;unit="J";}else if(index==9||index==10){factor=1;unit="A";}
            else if(index==11){factor=1;unit="s";}else if(index>=13&&index<=15){factor=.001;unit="m";}
            else if(index==16){factor=1;unit="kg";}else if(index==18){factor=.001;unit="m^3";}
        }
        else
        {
            if((index>=6&&index<=8)||index==15){factor=.001;unit="m";}else if(index==9){factor=.001;unit="kg";}
            else if(index==10){factor=1;unit="V";}else if(index==11){factor=1;unit="1";}
            else if(index==12){factor=juce::MathConstants<double>::twoPi/60;unit="rad/s";}
            else if(index==13){factor=1;unit="N*m";}else if(index==14){factor=1;unit="A";}
        }
        if(factor!=0&&numeric(at(index))){put(property,"siValue",(double)at(index)*factor);put(property,"siUnit",unit);}properties.add(property);
    }
    put(result,"properties",properties);
    const auto mass=at(battery?16:9);if(numeric(mass)&&(double)mass>0)put(result,"massKg",(double)mass*(battery?1:.001));
    auto geometry=object();const auto width=at(battery?14:6),height=at(battery?15:7),length=at(battery?13:8);
    if(numeric(width)&&numeric(height)&&numeric(length)&&(double)width>0&&(double)height>0&&(double)length>0)
    {
        const bool cylinder=!battery&&at(5).toString().contains(juce::String::charToString(0xd8))&&std::abs((double)height-(double)width)<1e-9;
        put(geometry,"kind",cylinder?"cylinder":"envelope");put(geometry,"level",cylinder?"simplified":"envelope");
        put(geometry,"label",cylinder?"Simplified outer cylinder; no shaft or mounting details":"Documented overall envelope; no mounting or internal detail");
        put(geometry,"widthMm",width);put(geometry,"heightMm",height);put(geometry,"lengthMm",length);if(cylinder)put(geometry,"diameterMm",width);
    }
    else {put(geometry,"kind","none");put(geometry,"level","catalogOnly");put(geometry,"label","Catalog only: overall dimensions incomplete");}
    put(result,"geometry",geometry);return result;
}
}
bool readWorkbook(const juce::File& file,juce::Array<juce::var>& output,juce::var& report,juce::String& error)
{
    if(!file.existsAsFile()||file.getSize()>32*1024*1024){error="Workbook is missing or exceeds the 32 MB import limit.";return false;}
    juce::ZipFile zip(file);auto workbook=xml(zip,"xl/workbook.xml"),relationships=xml(zip,"xl/_rels/workbook.xml.rels");
    if(!workbook||!relationships){error="Not a supported unencrypted .xlsx workbook.";return false;}
    std::map<juce::String,juce::String> targets;for(auto* r:relationships->getChildIterator())targets[r->getStringAttribute("Id")]=r->getStringAttribute("Target");
    juce::StringArray strings;if(auto shared=xml(zip,"xl/sharedStrings.xml"))for(auto* s:shared->getChildIterator())strings.add(text(*s));
    const auto* sheets=workbook->getChildByName("sheets");if(!sheets){error="Workbook contains no sheets.";return false;}
    juce::Array<juce::var> skipped,invalid,references;int matched=0,catalogOnly=0;
    for(auto* sheet:sheets->getChildIterator())
    {
        const auto name=sheet->getStringAttribute("name");const bool battery=name=="Commercial Battery Packs",robot=name=="Robot Components";
        auto target=targets[sheet->getStringAttribute("r:id")];if(target.startsWith("/"))target=target.substring(1);else if(!target.startsWith("xl/"))target="xl/"+target;
        auto document=xml(zip,target);const auto* data=document?document->getChildByName("sheetData"):nullptr;
        if(!data){error="Cannot read worksheet "+name;return false;}
        if(!battery&&!robot)
        {
            if(name=="Sources & Licenses"||name=="Source Register")
            {Row labels;for(auto* line:data->getChildIterator()){const auto row=readRow(*line,strings,{});if(labels.empty()){labels=row;continue;}auto reference=object();juce::Array<juce::var> fields;for(const auto& [i,label]:labels){auto field=object();put(field,"label",label.value);if(const auto hit=row.find(i);hit!=row.end())put(field,"value",hit->second.value);fields.add(field);}put(reference,"sheet",name);put(reference,"properties",fields);references.add(reference);}}
            auto v=object();put(v,"sheet",name);put(v,"rows",juce::jmax(0,data->getNumChildElements()-1));put(v,"reason","Reference, material or research-lead sheet; not an insertable component table. Matching source-register entries are retained as attribution.");skipped.add(v);continue;
        }
        ++matched;std::map<juce::String,juce::String> hyperlinkTargets,links;
        const auto relPath=target.upToLastOccurrenceOf("/",true,false)+"_rels/"+target.fromLastOccurrenceOf("/",false,false)+".rels";
        if(auto rels=xml(zip,relPath))for(auto* r:rels->getChildIterator())hyperlinkTargets[r->getStringAttribute("Id")]=r->getStringAttribute("Target");
        if(const auto* h=document->getChildByName("hyperlinks"))for(auto* link:h->getChildIterator())links[link->getStringAttribute("ref")]=hyperlinkTargets[link->getStringAttribute("r:id")];
        Row headers;bool foundHeader=false;int scanned=0;
        for(auto* line:data->getChildIterator())
        {
            if(++scanned>20000){error="Worksheet exceeds the 20000-row import limit.";return false;}
            auto row=readRow(*line,strings,links);if(row.empty())continue;
            if(!foundHeader){headers=row;foundHeader=true;
                const juce::StringArray expected=battery?juce::StringArray{"recordid","manufacturer","commercialmodelsku","productclass","chemistry","nominalv","capacityah","publishedwh","vahwh","continuousa","peaka","peakdurations","fullchargev","lengthmm","widthmm","heightmm","weightkgpublished","weightlbconverted","envelopevolumel","vahperkg","terminalconnector","mounting","housingcadproxy","datareviewstatus","manufacturersourceurl","engineeringnotes","checkeddate"}:juce::StringArray{"id","category","manufacturer","partvariant","partno","forminterface","widthmm","heightmm","lengthmm","massg","nominalv","gearratio","noloadrpm","stalltorquenm","stallcurrenta","shaftmm","materialnotes","cadavailability","recordbasis","cadredistribution","sourceurl","engineeringnotes"};
                for(int i=0;i<expected.size();++i)if(headers[i].value.toString().toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789")!=expected[i]){error="Unsupported column layout/units in "+name+", column "+juce::String(i+1)+". Expected the researched catalog schema.";return false;}continue;}
            const auto id=row[0].value.toString();if(id.isEmpty())continue;
            auto value=record(row,headers,battery,file,name,line->getIntAttribute("r"));juce::String reason;
            if(row[battery?1:2].value.toString().trim().isEmpty()||row[battery?2:3].value.toString().trim().isEmpty()||!validate(value,reason))
            {auto v=object();put(v,"record",id);put(v,"reason",reason.isNotEmpty()?reason:"Missing manufacturer or product identity.");invalid.add(v);continue;}
            catalogOnly+=value["geometry"]["kind"].toString()=="none";output.add(value);
        }
    }
    if(matched==0){error="No Robot Components or Commercial Battery Packs sheet was found. Reference-only sheets are not component definitions.";return false;}
    for(auto& item:output){juce::Array<juce::var> matches;for(const auto& reference:references){bool match=false;for(const auto& field:*reference["properties"].getArray())match|=field["value"].toString()==item["sourceUrl"].toString();if(match)matches.add(reference);}auto provenance=item["provenance"];put(provenance,"references",matches);}
    report=object();put(report,"workbook",file.getFileName());put(report,"parsed",output.size());put(report,"catalogOnly",catalogOnly);put(report,"invalid",invalid);put(report,"skippedSheets",skipped);return true;
}
}
