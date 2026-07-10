// NAMku edit controller implementation.

#include "namcontroller.h"
#include "namids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <cstring>

using namespace Steinberg;

namespace NAMku {

// The one and only definition of the INamFileLoader interface ID.
DEF_CLASS_IID(INamFileLoader)

//------------------------------------------------------------------------
tresult PLUGIN_API NamController::initialize(FUnknown *context)
{
    tresult result = EditController::initialize(context);
    if (result != kResultOk)
        return result;

    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
                            Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass,
                            kBypassId);

    auto *inGain = new Vst::RangeParameter(STR16("Input Gain"), kInputGainId, STR16("dB"),
                                           ranges::kGainMin, ranges::kGainMax,
                                           ranges::kGainDefault);
    inGain->setPrecision(1);
    parameters.addParameter(inGain);

    auto *outGain = new Vst::RangeParameter(STR16("Output Gain"), kOutputGainId, STR16("dB"),
                                            ranges::kGainMin, ranges::kGainMax,
                                            ranges::kGainDefault);
    outGain->setPrecision(1);
    parameters.addParameter(outGain);

    auto *ngThresh = new Vst::RangeParameter(STR16("Noise Gate"), kNoiseGateThresholdId,
                                             STR16("dB"), ranges::kNgMin, ranges::kNgMax,
                                             ranges::kNgDefault);
    ngThresh->setPrecision(1);
    parameters.addParameter(ngThresh);

    auto *bass = new Vst::RangeParameter(STR16("Bass"), kBassId, nullptr, ranges::kToneMin,
                                         ranges::kToneMax, ranges::kToneDefault);
    bass->setPrecision(1);
    parameters.addParameter(bass);

    auto *middle = new Vst::RangeParameter(STR16("Middle"), kMiddleId, nullptr, ranges::kToneMin,
                                           ranges::kToneMax, ranges::kToneDefault);
    middle->setPrecision(1);
    parameters.addParameter(middle);

    auto *treble = new Vst::RangeParameter(STR16("Treble"), kTrebleId, nullptr, ranges::kToneMin,
                                           ranges::kToneMax, ranges::kToneDefault);
    treble->setPrecision(1);
    parameters.addParameter(treble);

    parameters.addParameter(STR16("Tone Stack"), nullptr, 1, 1.0,
                            Vst::ParameterInfo::kCanAutomate, kToneStackOnId);
    parameters.addParameter(STR16("Noise Gate On"), nullptr, 1, 1.0,
                            Vst::ParameterInfo::kCanAutomate, kNoiseGateOnId);

    auto *outputMode = new Vst::StringListParameter(STR16("Output Mode"), kOutputModeId);
    outputMode->appendString(STR16("Raw"));
    outputMode->appendString(STR16("Normalized"));
    outputMode->appendString(STR16("Calibrated"));
    outputMode->setNormalized(0.5); // default: Normalized
    parameters.addParameter(outputMode);

    return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NamController::setComponentState(IBStream *state)
{
    // Mirror of NamProcessor::getState — keep the two in sync.
    if (!state)
        return kResultFalse;
    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if (!streamer.readInt32(version) || version != 1)
        return kResultFalse;

    const Vst::ParamID ids[10] = {kBypassId,    kInputGainId,   kOutputGainId,
                                  kNoiseGateThresholdId, kBassId, kMiddleId,
                                  kTrebleId,    kToneStackOnId, kNoiseGateOnId,
                                  kOutputModeId};
    for (Vst::ParamID id : ids) {
        double v = 0;
        if (!streamer.readDouble(v))
            return kResultFalse;
        setParamNormalized(id, v);
    }

    if (char8 *modelPath = streamer.readStr8()) {
        mModelPath = modelPath;
        delete[] modelPath;
    }
    if (char8 *irPath = streamer.readStr8()) {
        mIrPath = irPath;
        delete[] irPath;
    }
    return kResultOk;
}

//------------------------------------------------------------------------
tresult NamController::sendPath(const char *messageID, const char8 *path)
{
    // Forward the path to the processor over the connection. When no peer is
    // connected (e.g. a host inspecting the controller alone), the local copy
    // is still updated and kResultFalse is returned.
    IPtr<Vst::IMessage> message = owned(allocateMessage());
    if (!message)
        return kResultFalse;
    message->setMessageID(messageID);
    const char *p = path ? path : "";
    message->getAttributes()->setBinary(kMsgPathAttr, p, static_cast<uint32>(strlen(p)));
    return sendMessage(message);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NamController::setModelFile(const char8 *path)
{
    mModelPath = path ? path : "";
    return sendPath(kMsgLoadModel, path);
}

tresult PLUGIN_API NamController::setIrFile(const char8 *path)
{
    mIrPath = path ? path : "";
    return sendPath(kMsgLoadIr, path);
}

//------------------------------------------------------------------------
tresult NamController::copyPath(const std::string &src, char8 *buffer, int32 bufferSize)
{
    if (!buffer || bufferSize <= static_cast<int32>(src.size()))
        return kResultFalse;
    memcpy(buffer, src.c_str(), src.size() + 1);
    return kResultOk;
}

tresult PLUGIN_API NamController::getModelFile(char8 *buffer, int32 bufferSize)
{
    return copyPath(mModelPath, buffer, bufferSize);
}

tresult PLUGIN_API NamController::getIrFile(char8 *buffer, int32 bufferSize)
{
    return copyPath(mIrPath, buffer, bufferSize);
}

} // namespace NAMku
