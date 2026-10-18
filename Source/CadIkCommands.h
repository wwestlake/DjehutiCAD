#pragma once

#include "CadDocument.h"
#include "CadMotionRuntime.h"

// Shared IK command service for automation (BYOK agent, FRust, node editor). Every adapter sends the same
// JSON arguments here and receives a structured JSON result; the workspace panel uses the same document
// operations and CadMotionRuntime::solveIk underneath, so validation and solving never diverge.
//
// Commands: create, update, remove, inspect, solve, status, resetPreview. Must run on the message thread.
// Task JSON: name, adjustableJoints [joint ids], positionToleranceMm, angleToleranceDeg, targets [{ id, name,
// instance, referenceInstance ("" or "world" = world), mode (pose|position|orientation), enabled,
// toolOrigin [mm, instance frame], toolRotationDeg, desiredOrigin [mm, reference frame], desiredRotationDeg,
// captureCurrent (desired := current pose) }]. Rotations are extrinsic X, Y, Z in degrees.
namespace djehuti::cad::ikcmd
{
juce::String execute(const juce::String& command, const juce::var& args, CadCommandService& service, CadMotionRuntime& runtime);
}
