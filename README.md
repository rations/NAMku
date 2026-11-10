# NAMku

> This repository is the source of the `namku` package, part of the
> [JackDAW stack](https://github.com/rations/jackDAW-renku) for RenkuOS. The 0.5
> release's `install.sh` is for Haiku hrev60256; there is no RenkuOS release yet.

A [Neural Amp Modeler](https://www.neuralampmodeler.com/) VST3 plug-in for
**RenkuOS** (a Haiku fork) — a raw Steinberg VST3 plug-in (no JUCE, no framework),
built on the port of the VST 3 SDK from
[VST3-renku](https://github.com/rations/VST3-renku).

NAMku loads `.nam` neural amp models and `.wav` impulse responses and runs
them live in a VST3 host, or on its own as the **NAMku app**. Since plug-in GUIs are not available on RenkuOS yet,
file loading works without a GUI through a small COM-style host extension
(`INamFileLoader`, see `source/inamfileloader.h`): hosts that know the
interface (VST3-renku's `vst3jackhost`, JackDAW) offer *load model* /
*load IR* controls; all other hosts see a normal VST3 effect.

## Standalone app

`NAMku` (Applications in the Deskbar) plays NAMku without a DAW: one window
with NAMku's own editor, **NAMku** and **Audio** menus and a status line.

- **Audio ▸ Media Kit / JACK** picks the audio system. The Media Kit is built
  in: NAMku becomes one media node (Cortex shows it) whose output goes to the
  sound card's first two playback channels. JACK uses Kasen's JACK backend, loaded from the
  `add-ons/NAMku/jack.so` add-on only when chosen, so the app runs without the
  jack package; its output goes to the first two playback ports. If JACK is not
  running, the app says so and stays on the Media Kit.
- **Audio ▸ Input** connects the guitar input (one capture channel).
- The status line shows the audio system, rate, buffer size, late cycles and
  input.
- The model, IR, knobs, audio system, input and window position are restored
  next time.
- **NAMku ▸ Save preset… / Open preset…** keep amp settings (model, IR, knobs)
  in `.kasenpreset` files (first offered in `~/config/settings/NAMku/presets`).
  They are the presets Kasen's effects window saves and opens, so they move
  between the app and Kasen either way; the app reads and writes them itself
  and does not need Kasen. A preset stores where the model and IR files are,
  not the files.

The app hosts the same `NAMku.vst3` bundle a DAW loads (beside the app, or in
`add-ons/media/VST3`), through VST3-renku's audio host code.

## Signal chain

input gain → noise gate → NAM model → tone stack (bass/middle/treble) →
IR convolution → output gain, with normalization/calibrated output modes.
The NAM model runs at its own rate (usually 48 kHz) and the IR at no more than
48 kHz; other session rates are handled by Lanczos resamplers. Model and IR are hot-swapped with atomic pointer swaps —
never allocated or freed on the audio thread.

## Building

Requires CMake ≥ 3.25, ninja, a C++20 compiler, and a checkout of the
patched VST 3 SDK (vendored in
[VST3-renku](https://github.com/rations/VST3-renku)):

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DVST3_SDK_DIR=/path/to/VST3-renku/vst3sdk
ninja -C build
```

If NAMku and VST3-haiku are sibling directories, `VST3_SDK_DIR` defaults to
`../VST3-renku/vst3sdk`. Install by copying the built bundle to the user
add-ons directory:

```sh
cp -r build/VST3/Release/NAMku.vst3 ~/config/non-packaged/add-ons/media/VST3/
```

NAMku also builds as a CMake subdirectory of VST3-renku itself.

## Install / packaging (RenkuOS)

- From source: `./build-from-source.sh` (installs `NAMku.vst3` to
  `~/config/non-packaged/add-ons/media/VST3`).
- From source: `./build-from-source.sh` also installs the app to
  `~/config/non-packaged/apps` with a Deskbar entry.
- Prebuilt package: `packaging/make-hpkg.sh` → `namku-0.5.0-1-x86_64.hpkg`
  (`pkgman install ./namku-*.hpkg`): the plug-in, the app and its JACK add-on
  (when jack is installed at build time). The HaikuPorts recipe in
  `packaging/namku-0.1.0.recipe` is older and not yet validated.
- See the [stack overview](https://github.com/rations/jackDAW-renku/blob/master/STACK.md).

## Credits and licenses

- NAMku: MIT (see `LICENSE`); third-party attribution in `NOTICE`
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
  and [AudioDSPTools](https://github.com/sdatkinson/AudioDSPTools) by Steven
  Atkinson — MIT, vendored under `deps/`
- [Eigen](https://eigen.tuxfamily.org/) — MPL2, vendored under `deps/eigen`
- Tone stack adapted from the author's Linux NAM port ("namix")
- VST is a trademark of Steinberg Media Technologies GmbH
