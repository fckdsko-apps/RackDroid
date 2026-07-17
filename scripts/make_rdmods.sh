#!/usr/bin/env bash
# Package every non-base plugin pack as a .rdmod (a zip: plugin.json + res/ +
# libplugin_*.so) so they can be side-loaded on-demand (see MODULES.md).
# Res source per pack mirrors app/build.gradle.kts packSystemAssets: as-is
# packs take res/ from third_party, regen-art packs take it from graphics/.
# Run from rack-android/ AFTER a release build (uses the stripped .so).
set -euo pipefail
cd "$(dirname "$0")/.."

# The lean APK excludes non-base plugin .so at packaging time (see
# app/build.gradle.kts packaging.jniLibs.excludes), so they no longer reach
# stripped_native_libs. Take them straight from the CMake obj dir instead —
# but that only holds a plugin if CMake actually built it, and the default
# build restricts `targets` to the base three. Build every plugin first:
#   ANDROID_HOME=~/android-sdk gradle externalNativeBuildRelease \
#     -Pandroid.injected.build.abi=arm64-v8a   # or drop the `targets` line
# Newest release config only: a bare */* glob can hit a stale Debug dir
# whose .so predate the current toolchain flags (e.g. 16 KB page align).
SO=$(ls -dt app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a 2>/dev/null | head -1)
[ -z "$SO" ] && SO=app/build/intermediates/stripped_native_libs/release/stripReleaseDebugSymbols/out/lib/arm64-v8a
OUT="${1:-/tmp/rdmods}"
mkdir -p "$OUT"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# pack <slug> <soname> <third_party_dir> <regen_res_dir|-> [extra copy specs…]
pack() {
	local slug="$1" so="$2" tp="$3" regen="$4"; shift 4
	local d="$work/$slug"; rm -rf "$d"; mkdir -p "$d"
	if [ ! -f "$SO/$so" ]; then echo "skip $slug (no $so)"; return; fi
	cp "$SO/$so" "$d/"
	cp "third_party/$tp/plugin.json" "$d/"
	[ -d "third_party/$tp/presets" ] && cp -r "third_party/$tp/presets" "$d/" || true
	if [ "$regen" = "-" ]; then
		cp -r "third_party/$tp/res" "$d/res"
	else
		cp -r "graphics/$regen" "$d/res"
	fi
	# extra file/dir copies (RJ runtime data, Befaco IR, Aria overlay/excludes)
	for spec in "$@"; do eval "$spec"; done
	( cd "$d" && zip -qr "$OUT/$slug.rdmod" . )
	echo "packed $slug.rdmod"
}

# --- as-is art (res from third_party) ---
pack Bogaudio          libplugin_bogaudio.so      BogaudioModules  -
pack Valley            libplugin_valley.so        ValleyRackFree   -
pack HetrickCV         libplugin_hetrickcv.so     HetrickCV        -
pack JW-Modules        libplugin_jw.so            JW-Modules       -
pack ML_modules        libplugin_ml.so            ML_modules       -
pack RJModules         libplugin_rj.so            RJModules        - \
	'[ -d third_party/RJModules/rawwaves ] && cp -r third_party/RJModules/rawwaves "$d/" || true' \
	'[ -d third_party/RJModules/soundfonts ] && cp -r third_party/RJModules/soundfonts "$d/" || true'
pack computerscare     libplugin_computerscare.so computerscare    -
pack LittleUtils       libplugin_littleutils.so   Little-Utils     -
pack Autinn            libplugin_autinn.so        Autinn           -
pack Venom             libplugin_venom.so         VenomModules     -
pack SonusModular      libplugin_sonus.so         sonusmodular     -
pack NonlinearCircuits libplugin_nlc.so           nonlinearcircuits -
pack AriaSalvatrice    libplugin_aria.so          AriaModules      - \
	'rm -rf "$d/res/signature" "$d/res/Arcane"' \
	'cp -r graphics/aria-res/. "$d/res/"'

# --- regenerated art (res from graphics/<pack>-res) ---
pack FrozenWasteland   libplugin_frozenwasteland.so FrozenWasteland frozenwasteland-res
pack AudibleInstruments libplugin_audible.so       AudibleInstruments audible-res
pack ImpromptuModular  libplugin_impromptu.so      ImpromptuModular impromptu-res
pack Bidoo             libplugin_bidoo.so          Bidoo            bidoo-res
pack GrandeModular     libplugin_grande.so         GrandeModular    grande-res
pack Befaco            libplugin_befaco.so         Befaco           befaco-res \
	'[ -f third_party/Befaco/res/SpringReverbIR.f32 ] && cp third_party/Befaco/res/SpringReverbIR.f32 "$d/res/" || true'

echo "--- done, packs in $OUT ---"
ls -la "$OUT"
