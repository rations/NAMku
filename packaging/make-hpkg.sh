#!/bin/sh
# make-hpkg.sh — build the `namku` .hpkg on a running RenkuOS (x86_64): the
# NAMku.vst3 plug-in and the NAMku standalone app (with its JACK add-on when
# jack is installed at build time). Builds against the VST3-renku SDK checkout
# (default ../VST3-renku/vst3sdk; override VST3_SDK_DIR).
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
VERSION=0.5.0
REVISION=2
STAGE="$HERE/stage"
SDK="${VST3_SDK_DIR:-$ROOT/../VST3-renku/vst3sdk}"

pkgman install -y cmake ninja gcc make || true
[ -d "$SDK" ] || { echo "!! VST3 SDK not found at $SDK (set VST3_SDK_DIR)" >&2; exit 1; }

cd "$ROOT"
rm -rf build "$STAGE"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_DIR="$SDK"
ninja -C build

# The plug-in
BUNDLE=$(find build -type d -name 'NAMku.vst3' | head -1)
[ -n "$BUNDLE" ] || { echo "!! NAMku.vst3 not built" >&2; exit 1; }
mkdir -p "$STAGE/add-ons/media/VST3"
cp -r "$BUNDLE" "$STAGE/add-ons/media/VST3/"

# The standalone app, its Deskbar entry, and its JACK add-on (looked for in
# add-ons/NAMku/; without it the app plays on the Media Kit only)
APP=build/bin/Release/NAMku
[ -f "$APP" ] || { echo "!! the NAMku app was not built" >&2; exit 1; }
mkdir -p "$STAGE/apps" "$STAGE/data/deskbar/menu/Applications"
cp "$APP" "$STAGE/apps/NAMku"
mimeset -f "$STAGE/apps/NAMku"
ln -s ../../../../apps/NAMku "$STAGE/data/deskbar/menu/Applications/NAMku"
if [ -f build/bin/Release/backends/jack.so ]; then
	mkdir -p "$STAGE/add-ons/NAMku"
	cp build/bin/Release/backends/jack.so "$STAGE/add-ons/NAMku/jack.so"
else
	echo ">> jack not installed: the package has no JACK add-on (Media Kit only)"
fi

cp "$HERE/namku.PackageInfo" "$STAGE/.PackageInfo"
OUT="$HERE/namku-$VERSION-$REVISION-x86_64.hpkg"
package create -C "$STAGE" "$OUT"
echo ">> built $OUT"
