// NAMku — Neural Amp Modeler for Haiku, a native VST3 plug-in.
//
// Based on NeuralAmpModelerPlugin by Steven Atkinson (MIT licence); the DSP
// core (NeuralAmpModelerCore, AudioDSPTools) is reused directly. This plug-in
// is written against the VST3 SDK only — no plug-in framework.

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace NAMku
{

// Parameter IDs. Never change these after a release — projects embed them.
enum ParamIDs : Steinberg::Vst::ParamID {
    kBypassId = 100,
    kInputGainId = 101,             // -40 .. +40 dB, default 0
    kOutputGainId = 102,            // -40 .. +40 dB, default 0
    kNoiseGateThresholdId = 103,    // -100 .. 0 dB, default -80
    kBassId = 104,                  // 0 .. 10, default 5
    kMiddleId = 105,                // 0 .. 10, default 5
    kTrebleId = 106,                // 0 .. 10, default 5
    kToneStackOnId = 107,           // toggle, default on
    kNoiseGateOnId = 108,           // toggle, default on
    kOutputModeId = 109,            // Raw / Normalized / Calibrated, default Normalized
    kSlimId = 110,                  // 0 .. 1, default 0 — slimmable (A2) models only
    kCalibrateInputId = 111,        // toggle, default off — models with input level only
    kInputCalibrationLevelId = 112, // -60 .. +60 dBu, default 12
};

// Plain-value ranges shared by the processor (denormalization) and the
// controller (RangeParameter setup). Keep the two sides in sync via these.
namespace ranges
{
inline constexpr double kGainMin = -40.0, kGainMax = 40.0, kGainDefault = 0.0;
inline constexpr double kNgMin = -100.0, kNgMax = 0.0, kNgDefault = -80.0;
inline constexpr double kToneMin = 0.0, kToneMax = 10.0, kToneDefault = 5.0;
// Input calibration level (dBu) — range and default match the original plug-in.
inline constexpr double kCalMin = -60.0, kCalMax = 60.0, kCalDefault = 12.0;
} // namespace ranges

// Message IDs for controller -> processor file loading (IConnectionPoint).
// Attribute "path" carries a UTF-8 byte string (setBinary); empty = clear.
inline constexpr const char *kMsgLoadModel = "NAMkuLoadModel";
inline constexpr const char *kMsgLoadIr = "NAMkuLoadIR";
inline constexpr const char *kMsgPathAttr = "path";

// Slim travels controller -> processor as a message too (attribute "slim",
// setFloat): SetSlimmableSize rebuilds part of the network, which is
// thread-safe but NOT RT-safe, so it must run on the message thread — the
// RT parameter queue only carries the value for state bookkeeping.
inline constexpr const char *kMsgSetSlim = "NAMkuSetSlim";
inline constexpr const char *kSlimAttr = "slim";

// Model capabilities travel processor -> controller after every model load
// or clear (int attributes, 0/1), so the controller can retitle the
// parameters that the current capture does not support — the sliders-only
// equivalent of the original GUI greying/hiding those controls.
inline constexpr const char *kMsgModelCaps = "NAMkuModelCaps";
inline constexpr const char *kCapsSlimmableAttr = "slimmable";
inline constexpr const char *kCapsInLevelAttr = "hasInputLevel";
inline constexpr const char *kCapsOutLevelAttr = "hasOutputLevel";

static DECLARE_UID(NamkuProcessorUID, 0x80781530, 0x12284EB4, 0x89676AE5, 0x52A4FB2B);
static DECLARE_UID(NamkuControllerUID, 0xFD4220E5, 0xACFD437A, 0x9318389D, 0x1DED6791);

} // namespace NAMku
