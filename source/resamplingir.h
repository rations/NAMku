// ResamplingIR — the cabinet impulse response, run at no more than 48 kHz.
//
// dsp::ImpulseResponse convolves sample by sample, every sample against the
// whole IR (up to 8192 taps), at the rate it was built for. Built for the
// session rate, its cost grows with the square of that rate: at 192 kHz it
// does 16 times the work it does at 48 kHz for the same stretch of IR, and
// next to a standard (A1) model that no longer fits in a 1 ms period. So above
// 48 kHz the IR runs at 48 kHz inside a ResamplingContainer (Lanczos, A=12,
// the same resampler the model uses), and at 48 kHz or below it runs at the
// session rate with no resampler at all, exactly as before. A cabinet's
// response above 24 kHz is not audible, and ImpulseResponse scales its gain by
// 48000 / rate, so the level is the same either way.

#pragma once

#include "resamplingnam.h" // the iPlug shims ResamplingContainer needs

#include "ImpulseResponse.h"
#include "ResamplingContainer/ResamplingContainer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace NAMku
{

//------------------------------------------------------------------------
class ResamplingIR
{
public:
    // The highest rate the IR runs at.
    static constexpr double kMaxIrRate = 48000.0;

    // Message thread: loads and resamples the IR, then sizes every buffer for
    // blocks of up to maxBlockSize at sessionRate, so Process never allocates.
    ResamplingIR(const char *path, double sessionRate, int maxBlockSize)
        : mIrRate(std::min(sessionRate, kMaxIrRate)), mIR(path, mIrRate), mResampler(mIrRate)
    {
        if (!ok())
            return;
        mSessionRate = sessionRate;
        mOutput.assign(static_cast<size_t>(maxBlockSize), 0.0);
        mOutputPtr = mOutput.data();
        int irBlock = maxBlockSize;
        if (!bypassed()) {
            mResampler.Reset(sessionRate, maxBlockSize);
            irBlock = static_cast<int>(std::ceil(maxBlockSize * mIrRate / sessionRate));
        }
        // ImpulseResponse grows its history and output buffers on the first
        // block that is larger than any before; do that here.
        std::vector<DSP_SAMPLE> silence(static_cast<size_t>(irBlock), 0.0);
        DSP_SAMPLE *silencePtr = silence.data();
        mIR.Process(&silencePtr, 1, static_cast<size_t>(irBlock));
    }

    bool ok() const
    {
        return mIR.GetWavState() == dsp::wav::LoadReturnCode::SUCCESS;
    }

    // RT: convolves numFrames (<= the maxBlockSize given at construction)
    // session-rate samples and returns the result, valid until the next call.
    DSP_SAMPLE **Process(DSP_SAMPLE **input, int numFrames)
    {
        if (bypassed())
            return mIR.Process(input, 1, static_cast<size_t>(numFrames));
        mResampler.ProcessBlock(input, &mOutputPtr, numFrames,
                                [this](DSP_SAMPLE **in, DSP_SAMPLE **out, int n) {
                                    DSP_SAMPLE **conv = mIR.Process(in, 1, static_cast<size_t>(n));
                                    std::copy(conv[0], conv[0] + n, out[0]);
                                });
        return &mOutputPtr;
    }

    // Latency this adds, in session-rate samples: zero without a resampler.
    int GetLatency() const
    {
        return bypassed() ? 0 : mResampler.GetLatency();
    }

private:
    bool bypassed() const
    {
        return mIrRate == mSessionRate;
    }

    double mIrRate;
    double mSessionRate = 0.0;
    dsp::ImpulseResponse mIR;
    dsp::ResamplingContainer<DSP_SAMPLE, 1, 12> mResampler;
    std::vector<DSP_SAMPLE> mOutput;
    DSP_SAMPLE *mOutputPtr = nullptr;
};

} // namespace NAMku
