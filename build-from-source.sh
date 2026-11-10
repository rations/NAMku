#!/bin/sh
# build-from-source.sh — build NAMku (plug-in and standalone app) and install it for
# the current user.
# Needs the VST3-renku SDK checkout (default ../VST3-renku; override VST3_SDK_DIR).
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SDK="${VST3_SDK_DIR:-$HERE/../VST3-renku/vst3sdk}"

pkgman install -y cmake ninja gcc make
[ -d "$SDK" ] || { echo "!! VST3 SDK not found at $SDK (set VST3_SDK_DIR)" >&2; exit 1; }

cd "$HERE"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_DIR="$SDK"
ninja -C build

NP="$HOME/config/non-packaged"
mkdir -p "$NP/add-ons/media/VST3" "$NP/apps" "$NP/data/deskbar/menu/Applications"
cp -r build/VST3/Release/NAMku.vst3 "$NP/add-ons/media/VST3/"
cp build/bin/Release/NAMku "$NP/apps/NAMku"
mimeset -f "$NP/apps/NAMku"
ln -sf "$NP/apps/NAMku" "$NP/data/deskbar/menu/Applications/NAMku"
if [ -f build/bin/Release/backends/jack.so ]; then
	mkdir -p "$NP/add-ons/NAMku"
	cp build/bin/Release/backends/jack.so "$NP/add-ons/NAMku/jack.so"
fi
echo ">> Installed NAMku.vst3 to ~/config/non-packaged/add-ons/media/VST3"
echo ">> and the NAMku app to ~/config/non-packaged/apps (Deskbar: Applications)"
