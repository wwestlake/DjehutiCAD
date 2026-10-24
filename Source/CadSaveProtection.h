#pragma once
#include <JuceHeader.h>

// The same Save / Don't Save / Cancel policy for document replacement, tabs and exit.
namespace djehuti::cad::saveprotection
{
using Completion=std::function<void(bool)>;
using Save=std::function<void(Completion)>;
using Prompt=std::function<void(const juce::String&,std::function<void(int)>)>;
inline void showPrompt(juce::Component* parent,const juce::String& name,std::function<void(int)> answer)
{
    juce::AlertWindow::showYesNoCancelBox(juce::AlertWindow::QuestionIcon,"Unsaved changes",
        "Save changes to "+name+" before closing?","Save","Don't Save","Cancel",parent,
        juce::ModalCallbackFunction::create(std::move(answer)));
}
inline void confirm(bool dirty,const juce::String& name,Prompt prompt,Save save,Completion complete)
{
    if(!dirty){complete(true);return;}
    prompt(name,[save=std::move(save),complete=std::move(complete)](int result){
        if(result==1)save(complete);else complete(result==2);
    });
}
}
