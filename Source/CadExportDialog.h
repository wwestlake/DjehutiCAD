#pragma once

#include <JuceHeader.h>
#include <memory>

namespace djehuti::cad { class CadCommandService; }

// Opens the shared Export... dialog (3MF, binary STL, STEP). preselected lists body or instance ids to check
// initially; empty checks every visible part of the modeled assembly.
void showCadExportDialog(std::shared_ptr<djehuti::cad::CadCommandService> service, const juce::StringArray& preselected);
