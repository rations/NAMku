# NAMku editor art

The raster layers under `../resource/gui/` are imported from the **Neural Amp
Modeler Plugin** by Steven Atkinson
(<https://github.com/sdatkinson/NeuralAmpModelerPlugin>), which is distributed
under the **MIT License** (Copyright (c) 2022 Steven Atkinson). The image assets
fall under that licence; they are reused here unmodified except for format
normalisation to 32-bit PNG.

Imported layers (see `make_assets.sh`): the textured backdrop, the line
overlay, the knob face, the file-row and meter backgrounds, the switch handle,
and the input-level field background.

Everything else in the NAMku editor — the knob pointer and value arc, the meter
fill and peak marker, the switch on/off state, and the small functional glyphs
(gear, clear, load) — is drawn in code with the Be API, matching how the
original iPlug2 plugin renders those parts.

`make_assets.sh` runs on the development host only (it needs ImageMagick); the
committed PNGs are what ship in the plugin bundle. Point `NAM_UPSTREAM` at a
NeuralAmpModelerPlugin checkout's `resources/img` directory to re-import.
