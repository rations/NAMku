// NAMku editor geometry. The canvas size and palette are mirrored from
// gui/geometry.sh (the art pipeline's source of truth); the control rects are
// derived from the original Neural Amp Modeler layout (its 600x400 panel:
// window pad -20, content pad -10, a six-cell knob grid at y[105,225], two
// toggles under knobs 2 and 4, 200x30 model/IR rows near the bottom, and
// 30px-wide vertical meters on the outer edges). Keep the canvas/palette in
// sync with geometry.sh.

#pragma once

#include "namids.h"

#include "pluginterfaces/vst/vsttypes.h"

namespace NAMku
{
namespace geo
{

// Editor canvas (geometry.sh: WIN_W/WIN_H).
constexpr int kWinW = 600;
constexpr int kWinH = 400;

// Palette (geometry.sh; from the original Colors.h). 0xRRGGBB.
constexpr uint32_t kBgColor = 0x1D1A1F;   // raisin-black backdrop
constexpr uint32_t kAzure = 0x5085E8;     // theme accent
constexpr uint32_t kTextColor = 0xF2F2F2; // near-white labels
constexpr uint32_t kDimColor = 0xA2B2BF;  // empty / disabled text
constexpr uint32_t kPeakColor = 0xFF3B30; // meter peak marker

// Title band.
constexpr int kTitleX = 30, kTitleY = 30, kTitleW = 540, kTitleH = 50;

// --- Knobs (6), from the original knob grid ---
struct KnobSpec {
    Steinberg::Vst::ParamID id;
    int cx, cy, r; // face center and radius
    const char *label;
};
constexpr int kKnobCount = 6;
constexpr KnobSpec kKnobs[kKnobCount] = {
    {kInputGainId, 92, 148, 33, "Input"},          //
    {kNoiseGateThresholdId, 175, 148, 33, "Gate"}, //
    {kBassId, 258, 148, 33, "Bass"},               //
    {kMiddleId, 342, 148, 33, "Middle"},           //
    {kTrebleId, 425, 148, 33, "Treble"},           //
    {kOutputGainId, 508, 148, 33, "Output"},       //
};
// Knob pointer/arc: ~270-degree sweep with a dead zone at the bottom. Angles
// in degrees, 0 = straight up, positive = clockwise (see namview.cpp).
constexpr double kKnobSweepDeg = 270.0;
constexpr double kKnobPointerFrac = 0.72; // pointer length / radius
constexpr int kKnobValueDY = 46;          // value baseline below face center
constexpr int kKnobLabelDY = 64;          // label baseline below face center

// --- Toggle switches (2) ---
struct ToggleSpec {
    Steinberg::Vst::ParamID id;
    int cx, cy; // pill center
    const char *label;
};
constexpr int kToggleW = 44, kToggleH = 22;
constexpr int kToggleCount = 2;
constexpr ToggleSpec kToggles[kToggleCount] = {
    {kNoiseGateOnId, 175, 250, "Noise Gate"},
    {kToneStackOnId, 342, 250, "EQ"},
};

// --- File rows (model / IR) ---
struct FileRow {
    int x, y, w, h;
    const char *placeholder;
    const char *ext; // file-panel filter (no dot)
};
constexpr FileRow kModelRow = {200, 309, 200, 30, "Select model...", "nam"};
constexpr FileRow kIrRow = {200, 347, 200, 30, "Select IR...", "wav"};
constexpr int kFileGlyph = 16;  // load glyph box at the row's left
constexpr int kClearGlyph = 14; // clear (x) box at the row's right

// --- Level meters (input / output) ---
struct MeterRect {
    Steinberg::Vst::ParamID id;
    int x, y, w, h;
};
constexpr MeterRect kInputMeter = {kInputMeterId, 10, 75, 30, 200};
constexpr MeterRect kOutputMeter = {kOutputMeterId, 560, 75, 30, 200};

// --- Gear (settings) button, top-right ---
constexpr int kGearCX = 556, kGearCY = 46, kGearR = 13;

// --- Settings overlay (gear) ---
constexpr int kSettingsX = 120, kSettingsY = 66, kSettingsW = 360, kSettingsH = 268;

// --- Slim: icon by the model row (slimmable models only) + overlay knob ---
constexpr int kSlimIconX = 410, kSlimIconY = 313, kSlimIconS = 22;
constexpr int kSlimKnobCX = 300, kSlimKnobCY = 186, kSlimKnobR = 42;

} // namespace geo
} // namespace NAMku
