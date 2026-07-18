// nambench — standalone throughput benchmark for the NAM core DSP.
//
// Loads a .nam capture and pushes blocks of audio through it, reporting the
// per-block cost against the real-time budget. No plug-in host, no JACK, no
// audio device: this measures the model inference and nothing else, so the
// same binary can be built and run on any machine to compare raw CPU
// throughput for identical work.
//
// Usage:  nambench <model.nam> [--rate 48000] [--block 256] [--seconds 10]
//                              [--wrap]
//
// --wrap runs the model through NAMku's ResamplingNAM wrapper instead of
// calling nam::DSP::process() directly. At matched rates the wrapper is
// supposed to bypass the resampler entirely, so --wrap and plain mode should
// cost the same; a difference means the bypass is not firing.
//
// --duty reproduces a host's duty cycle instead of looping back to back: after
// each block it walks a buffer larger than last-level cache and then idles out
// the rest of the block period. A tight loop keeps the model's weights resident
// across blocks, which a DAW does not — there, a GUI, a mixer and other tracks
// run in between and evict them, so every callback re-streams the weights from
// RAM. If a model is memory-bound this mode costs several times more than the
// default, and that difference is the measurement that separates "the model is
// slow" from "the model is being evicted".
//
// Build with bench/Makefile; see its header for the A/B flag variants.

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include "../source/resamplingnam.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

// Wall-clock microseconds from a steady source. std::chrono::steady_clock is
// the portable equivalent of the CLOCK_MONOTONIC reads the engine uses.
long long now_us()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// Evict the model's weights the way a host's other work would: stride through
// a buffer bigger than last-level cache, one write per cache line. Marked
// volatile so the compiler cannot decide the stores are dead and drop them.
void thrash_cache(std::vector<unsigned char> &buf)
{
    volatile unsigned char *p = buf.data();
    const size_t n = buf.size();
    for (size_t i = 0; i < n; i += 64)
        p[i] = static_cast<unsigned char>(i);
}

void usage(const char *argv0)
{
    std::fprintf(stderr,
                 "usage: %s <model.nam> [--rate HZ] [--block N] [--seconds S]\n"
                 "\n"
                 "  --rate     host sample rate to simulate   (default 48000)\n"
                 "  --block    frames per process() call      (default 256)\n"
                 "  --seconds  audio seconds to push through  (default 10)\n"
                 "  --wrap     run through NAMku's ResamplingNAM wrapper\n"
                 "  --duty     pace blocks in realtime and evict cache between\n"
                 "             them, as a host does (default: tight loop)\n"
                 "  --thrash   MB to walk per block under --duty (default 16;\n"
                 "             pick comfortably above last-level cache)\n",
                 argv0);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    const char *modelPath = argv[1];
    double rate = 48000.0;
    int block = 256;
    double seconds = 10.0;
    bool wrap = false;
    bool duty = false;
    int thrashMb = 16;

    for (int i = 2; i < argc; i++) {
        const bool haveNext = (i + 1 < argc);
        if (!std::strcmp(argv[i], "--rate") && haveNext)
            rate = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--block") && haveNext)
            block = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && haveNext)
            seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--wrap"))
            wrap = true;
        else if (!std::strcmp(argv[i], "--duty"))
            duty = true;
        else if (!std::strcmp(argv[i], "--thrash") && haveNext)
            thrashMb = std::atoi(argv[++i]);
        else {
            std::fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (rate <= 0.0 || block <= 0 || seconds <= 0.0) {
        std::fprintf(stderr, "rate, block and seconds must all be positive\n");
        return 2;
    }
    if (thrashMb < 1) {
        std::fprintf(stderr, "--thrash must be at least 1 MB\n");
        return 2;
    }

    // ---- Load the model -------------------------------------------------
    std::unique_ptr<nam::DSP> model;
    try {
        model = nam::get_dsp(std::filesystem::path(modelPath));
    } catch (const std::exception &e) {
        std::fprintf(stderr, "failed to load %s: %s\n", modelPath, e.what());
        return 1;
    }
    if (!model) {
        std::fprintf(stderr, "failed to load %s (no model returned)\n", modelPath);
        return 1;
    }

    const double modelRate = model->GetExpectedSampleRate();

    // Optionally reproduce the plug-in's own wrapping so the wrapper's cost is
    // included in the measurement.
    if (wrap)
        model = std::make_unique<NAMku::ResamplingNAM>(std::move(model), rate);

    // Match the plug-in's setup: allocate internal buffers for this block size
    // and run the model's prewarm so the benchmark measures steady state.
    model->ResetAndPrewarm(rate, block);

    // ---- Buffers --------------------------------------------------------
    // A quiet sine rather than silence: some architectures short-circuit on
    // exact zeros, and denormals in a decaying tail would flatter the result.
    std::vector<NAM_SAMPLE> in(static_cast<size_t>(block));
    std::vector<NAM_SAMPLE> out(static_cast<size_t>(block));
    NAM_SAMPLE *inPtr = in.data();
    NAM_SAMPLE *outPtr = out.data();

    const long long totalBlocks = static_cast<long long>((seconds * rate) / block);
    if (totalBlocks < 1) {
        std::fprintf(stderr, "--seconds too small for this block size\n");
        return 2;
    }

    // ---- Report the configuration before the run ------------------------
    const double budgetUs = (block / rate) * 1e6;
    std::printf("model:      %s\n", modelPath);
    std::printf("path:       %s\n",
                wrap ? "ResamplingNAM wrapper (as the plug-in runs it)" : "nam::DSP direct");
    std::printf("model rate: %.0f Hz%s\n", modelRate,
                (modelRate != rate) ? "  [RESAMPLING ACTIVE]" : "  (matches host, no resampling)");
    std::printf("host rate:  %.0f Hz\n", rate);
    std::printf("block:      %d frames  (budget %.1f us/block)\n", block, budgetUs);
    std::printf("sample:     %zu bytes (%s precision)\n", sizeof(NAM_SAMPLE),
                sizeof(NAM_SAMPLE) == 4 ? "float" : "double");
    if (duty)
        std::printf("mode:       duty cycle (%d MB evicted + idle between blocks)\n", thrashMb);
    else
        std::printf("mode:       tight loop (model stays cache-resident)\n");
    std::printf("running:    %lld blocks (%.1f s of audio)\n\n", totalBlocks, seconds);
    std::fflush(stdout);

    // ---- Warm-up (not measured): let caches and branch predictors settle -
    double phase = 0.0;
    const double phaseStep = 2.0 * M_PI * 220.0 / rate;
    for (int w = 0; w < 32; w++) {
        for (int i = 0; i < block; i++, phase += phaseStep)
            in[static_cast<size_t>(i)] = static_cast<NAM_SAMPLE>(0.1 * std::sin(phase));
        model->process(&inPtr, &outPtr, block);
    }

    // ---- Measured run ---------------------------------------------------
    std::vector<long long> samples;
    samples.reserve(static_cast<size_t>(totalBlocks));

    // Allocated even when unused so the measured loop below stays branch-simple.
    std::vector<unsigned char> thrashBuf(duty ? static_cast<size_t>(thrashMb) * 1024u * 1024u : 0u);

    for (long long b = 0; b < totalBlocks; b++) {
        for (int i = 0; i < block; i++, phase += phaseStep)
            in[static_cast<size_t>(i)] = static_cast<NAM_SAMPLE>(0.1 * std::sin(phase));

        const long long t0 = now_us();
        model->process(&inPtr, &outPtr, block);
        const long long elapsed = now_us() - t0;
        samples.push_back(elapsed);

        // Only process() is timed; the eviction and the idle are the host's
        // time, not the model's, and must stay outside the measurement.
        if (duty) {
            thrash_cache(thrashBuf);
            const long long slack = static_cast<long long>(budgetUs) - (now_us() - t0);
            if (slack > 0)
                std::this_thread::sleep_for(std::chrono::microseconds(slack));
        }
    }

    // Consume the output so the optimiser cannot elide the work.
    double sink = 0.0;
    for (int i = 0; i < block; i++)
        sink += static_cast<double>(out[static_cast<size_t>(i)]);

    // ---- Statistics -----------------------------------------------------
    std::sort(samples.begin(), samples.end());
    const size_t n = samples.size();
    long long total = 0;
    for (long long v : samples)
        total += v;

    const double mean = static_cast<double>(total) / static_cast<double>(n);
    const long long p50 = samples[n / 2];
    const long long p99 = samples[(n * 99) / 100];
    const long long worst = samples[n - 1];

    auto pct = [budgetUs](double us) { return 100.0 * us / budgetUs; };

    std::printf("per-block cost over %zu blocks:\n", n);
    std::printf("  mean   %8.1f us   (%6.1f%% of budget)\n", mean, pct(mean));
    std::printf("  median %8lld us   (%6.1f%% of budget)\n", p50, pct((double)p50));
    std::printf("  p99    %8lld us   (%6.1f%% of budget)\n", p99, pct((double)p99));
    std::printf("  worst  %8lld us   (%6.1f%% of budget)\n", worst, pct((double)worst));
    std::printf("\n");
    std::printf("  per frame: %.3f us\n", mean / block);
    std::printf("  realtime:  %.2fx  (>1.0 means faster than realtime)\n", budgetUs / mean);
    std::printf("\n");

    if (mean >= budgetUs)
        std::printf("VERDICT: over budget — cannot sustain this block size.\n");
    else if (p99 >= budgetUs)
        std::printf("VERDICT: mean fits but p99 overruns — expect intermittent xruns.\n");
    else
        std::printf("VERDICT: fits with %.0f%% headroom.\n", 100.0 - pct(mean));

    // Keep `sink` observable without polluting the report.
    if (std::isnan(sink))
        std::fprintf(stderr, "(model produced NaN)\n");

    return 0;
}
