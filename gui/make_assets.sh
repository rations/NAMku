#!/bin/sh
# Import the Neural Amp Modeler's MIT-licensed raster art into the NAMku bundle
# resource set. Dev-host only (needs ImageMagick's `convert`); the committed
# PNGs under ../resource/gui are what ship — the target never runs this.
#
# The six layers imported here are the ones that carry the visual identity of
# the original panel: the textured backdrop, the line overlay, the knob face,
# the file-row and meter backgrounds, and the switch handle. Everything else
# (knob pointer/arc, meter fill, switch state, gear/clear/load glyphs) is drawn
# in code, matching how the original iPlug2 plugin renders those parts. See
# gui/README for provenance and licence.
#
# @2x sources are used where available and drawn downscaled for crispness.
set -eu
cd "$(dirname "$0")"
. ./geometry.sh

OUT=../resource/gui
mkdir -p "$OUT"

src() { # src <basename> -> preferred source path (@2x if present, else 1x)
    if [ -f "$NAM_UPSTREAM/$1@2x.png" ]; then
        echo "$NAM_UPSTREAM/$1@2x.png"
    elif [ -f "$NAM_UPSTREAM/$1@2x.jpg" ]; then
        echo "$NAM_UPSTREAM/$1@2x.jpg"
    elif [ -f "$NAM_UPSTREAM/$1.png" ]; then
        echo "$NAM_UPSTREAM/$1.png"
    else
        echo "$NAM_UPSTREAM/$1.jpg"
    fi
}

import() { # import <upstream-basename> <output-name>
    s=$(src "$1")
    [ -f "$s" ] || { echo "MISSING: $1 (looked near $NAM_UPSTREAM)" >&2; exit 1; }
    convert "$s" -strip PNG32:"$OUT/$2"
    echo "  $2  <-  $(basename "$s")"
}

# Rasterize an SVG icon (needs rsvg-convert / librsvg2-bin). Rendered at 2x the
# display size and drawn downscaled by the view for crisp anti-aliasing.
svg() { # svg <upstream-basename> <output-name> <2x-args...>
    s="$NAM_UPSTREAM/$1.svg"
    [ -f "$s" ] || { echo "MISSING SVG: $1" >&2; exit 1; }
    rsvg-convert "$@" "$s" -o "$OUT/$2" 2>/dev/null && echo "  $2  <-  $1.svg"
}
svg2() { # svg2 <basename> <output> <2x-w-or-h-flag> : convenience wrapper
    b="$1"; o="$2"; shift 2
    s="$NAM_UPSTREAM/$b.svg"
    [ -f "$s" ] || { echo "MISSING SVG: $b" >&2; exit 1; }
    rsvg-convert "$@" "$s" -o "$OUT/$o" 2>/dev/null && echo "  $o  <-  $b.svg"
}

command -v rsvg-convert >/dev/null || {
    echo "rsvg-convert not found (install librsvg2-bin) - SVG icons will be skipped" >&2
    RSVG_MISSING=1
}

echo "Importing NAM art from: $NAM_UPSTREAM"
import Background          background.png
import Lines               lines.png
import KnobBackground      knob_face.png
import FileBackground      file_bg.png
import MeterBackground     meter_bg.png
import SlideSwitchHandle   switch_handle.png
import InputLevelBackground input_bg.png

if [ -z "${RSVG_MISSING:-}" ]; then
    # Icons, rendered at 2x their on-screen size (view draws them downscaled).
    svg2 Gear          gear.png        -h 52
    svg2 File          load.png        -h 36
    svg2 Cross         clear.png       -h 32
    svg2 SlimmableIcon slimmable.png   -w 80 -h 40
    svg2 IRIconOn      ir_on.png       -h 44
    svg2 IRIconOff     ir_off.png      -h 44
    svg2 ModelIcon     model.png       -w 84
    svg2 Globe         globe.png       -h 36
    svg2 ArrowLeft     arrow_left.png  -h 28
    svg2 ArrowRight    arrow_right.png -h 28
fi
echo "Done -> $OUT"
