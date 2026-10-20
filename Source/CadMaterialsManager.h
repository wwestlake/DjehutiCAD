#pragma once
#include "CadDocument.h"

// Workspace manager dialog. Every mutation uses CadCommandService.
juce::DialogWindow* showCadMaterialsManager(std::shared_ptr<djehuti::cad::CadCommandService>, const juce::String& selectedFeature);
// Isolated control/renderer smoke check. Runs on the JUCE message thread with an active event loop.
bool runCadStockUiCheck(juce::var& report, const juce::File& outputFolder);
