# NAMku

A [Neural Amp Modeler](https://www.neuralampmodeler.com/) VST3 plug-in for
**Haiku OS** — a raw Steinberg VST3 plug-in (no JUCE, no framework), built on
the Haiku port of the VST 3 SDK from
[VST3-haiku](https://github.com/rations/VST3-haiku).

NAMku loads `.nam` neural amp models and `.wav` impulse responses and runs
them live in a VST3 host. Since plug-in GUIs are not available on Haiku yet,
file loading works without a GUI through a small COM-style host extension
(`INamFileLoader`, see `source/inamfileloader.h`): hosts that know the
interface (VST3-haiku's `vst3jackhost`, jackDAW-haiku) offer *load model* /
*load IR* controls; all other hosts see a normal VST3 effect.

## Signal chain

input gain → noise gate → NAM model → tone stack (bass/middle/treble) →
IR convolution → output gain, with normalization/calibrated output modes.
The NAM model always runs at 48 kHz; other session rates are handled by a
Lanczos resampler. Model and IR are hot-swapped with atomic pointer swaps —
never allocated or freed on the audio thread.

## Building

Requires CMake ≥ 3.25, ninja, a C++20 compiler, and a checkout of the
Haiku-patched VST 3 SDK (vendored in
[VST3-haiku](https://github.com/rations/VST3-haiku)):

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DVST3_SDK_DIR=/path/to/VST3-haiku/vst3sdk
ninja -C build
```

If NAMku and VST3-haiku are sibling directories, `VST3_SDK_DIR` defaults to
`../VST3-haiku/vst3sdk`. Install by copying the built bundle to the user
add-ons directory:

```sh
cp -r build/VST3/Release/NAMku.vst3 ~/config/non-packaged/add-ons/media/VST3/
```

NAMku also builds as a CMake subdirectory of VST3-haiku itself.

## Install / packaging (Haiku)

- From source: `./build-from-source.sh` (installs `NAMku.vst3` to
  `~/config/non-packaged/add-ons/media/VST3`).
- Prebuilt package: `packaging/make-hpkg.sh` → `namku-0.1.0-1-x86_64.hpkg`
  (`pkgman install ./namku-*.hpkg`). HaikuPorts recipe: `packaging/namku-0.1.0.recipe`.
- See the stack overview in `jackDAW-haiku/STACK.md`.

## Credits and licenses

- NAMku: MIT (see `LICENSE`); third-party attribution in `NOTICE`
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
  and [AudioDSPTools](https://github.com/sdatkinson/AudioDSPTools) by Steven
  Atkinson — MIT, vendored under `deps/`
- [Eigen](https://eigen.tuxfamily.org/) — MPL2, vendored under `deps/eigen`
- Tone stack adapted from the author's Linux NAM port ("namix")
- VST is a trademark of Steinberg Media Technologies GmbH
