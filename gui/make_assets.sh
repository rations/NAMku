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

echo "Importing NAM art from: $NAM_UPSTREAM"
import Background          background.png
import Lines               lines.png
import KnobBackground      knob_face.png
import FileBackground      file_bg.png
import MeterBackground     meter_bg.png
import SlideSwitchHandle   switch_handle.png
import InputLevelBackground input_bg.png
echo "Done -> $OUT"
