#!/usr/bin/env bash
# Package every non-base plugin pack as a .rdmod (a zip: plugin.json + res/ +
# libplugin_*.so) so they can be side-loaded on-demand (see MODULES.md).
# Res source per pack mirrors app/build.gradle.kts packSystemAssets: as-is
# packs take res/ from third_party, regen-art packs take it from graphics/.
# Run from the repository root AFTER a release build (uses the stripped .so).
# Usage: scripts/make_rdmods.sh [output-dir] [arm64-v8a|x86_64]
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-/tmp/rdmods}"
ABI="${2:-arm64-v8a}"
case "$ABI" in
	arm64-v8a|x86_64) ;;
	*) echo "unsupported ABI '$ABI' (expected arm64-v8a or x86_64)" >&2; exit 2 ;;
esac

# The lean APK excludes non-base plugin .so at packaging time (see
# app/build.gradle.kts packaging.jniLibs.excludes), so they no longer reach
# stripped_native_libs. Take them straight from the CMake obj dir instead —
# but that only holds a plugin if CMake actually built it, and the default
# build restricts `targets` to the base three. Build every plugin first:
#   ANDROID_HOME=~/android-sdk ./gradlew assembleRelease \
#     -PdevKeystore -PallPlugins -PtargetAbis=x86_64 --no-daemon
# Newest release config only: a bare */* glob can hit a stale Debug dir
# whose .so predate the current toolchain flags (e.g. 16 KB page align).
SO=$(ls -dt "app/build/intermediates/cxx/RelWithDebInfo"/*/obj/"$ABI" 2>/dev/null | head -1 || true)
[ -z "$SO" ] && SO="app/build/intermediates/stripped_native_libs/release/stripReleaseDebugSymbols/out/lib/$ABI"
# The obj dir holds UNSTRIPPED RelWithDebInfo libraries — debug info made
# e.g. Bogaudio's .so 56 MB instead of 3.8 MB. Strip with the same NDK
# llvm-strip the AGP pipeline uses, so packs match APK-shipped libs.
STRIP=$(ls "${ANDROID_HOME:-$HOME/android-sdk}"/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip 2>/dev/null | sort -V | tail -1)
[ -z "$STRIP" ] && { echo "llvm-strip not found under ANDROID_HOME ndk/"; exit 1; }
mkdir -p "$OUT"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
packed=0

# pack <slug> <soname> <third_party_dir> <regen_res_dir|-> [extra copy specs…]
pack() {
	local slug="$1" so="$2" tp="$3" regen="$4"; shift 4
	local d="$work/$slug"; rm -rf "$d"; mkdir -p "$d"
	[ -f "$SO/$so" ] || {
		echo "missing $SO/$so; build with -PallPlugins first" >&2
		exit 1
	}
	"$STRIP" --strip-unneeded -o "$d/$so" "$SO/$so"
	cp "third_party/$tp/plugin.json" "$d/"
	[ -d "third_party/$tp/presets" ] && cp -r "third_party/$tp/presets" "$d/" || true
	if [ "$regen" = "-" ]; then
		cp -r "third_party/$tp/res" "$d/res"
	else
		cp -r "graphics/$regen" "$d/res"
	fi
	# Module browser tile art for this plugin travels inside its own .rdmod
	# (graphics/browser-thumbs/<slug>/, slug == plugin.json's "slug" == this
	# dir name) instead of the base APK's thumbnails.zip (app/build.gradle.kts
	# packThumbnailAssets bundles only Core/Fundamental/RackDroidDrums now).
	# ThumbnailCache.get() (ModuleThumbnails.kt) falls back here for keys it
	# can't find under the bundled thumbnails dir.
	[ -d "graphics/browser-thumbs/$slug" ] && cp -r "graphics/browser-thumbs/$slug" "$d/thumbs"
	# extra file/dir copies (RJ runtime data, Befaco IR, Aria overlay/excludes)
	for spec in "$@"; do eval "$spec"; done
	rm -f "$OUT/$slug.rdmod"
	( cd "$d" && zip -qr "$OUT/$slug.rdmod" . )
	zip -T "$OUT/$slug.rdmod" >/dev/null
	packed=$((packed + 1))
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
# Stoermelder's own LICENSE.md is plain GPLv3 over the whole repo, res/
# included (its LICENSE-dist.md only collects notices for code borrowed from
# other plugins), so the upstream panels ship as-is.
pack Stoermelder-P1    libplugin_packone.so       PackOne          -

# --- regenerated art (res from graphics/<pack>-res) ---
pack FrozenWasteland   libplugin_frozenwasteland.so FrozenWasteland frozenwasteland-res
pack AudibleInstruments libplugin_audible.so       AudibleInstruments audible-res
pack ImpromptuModular  libplugin_impromptu.so      ImpromptuModular impromptu-res
pack Bidoo             libplugin_bidoo.so          Bidoo            bidoo-res
pack GrandeModular     libplugin_grande.so         GrandeModular    grande-res
# Count Modula's licence forbids derivative works from using its logo and
# panel graphics, so this pack MUST ship the regenerated art, never res/.
pack CountModula       libplugin_countmodula.so    CountModula      countmodula-res
pack Befaco            libplugin_befaco.so         Befaco           befaco-res \
	'[ -f third_party/Befaco/res/SpringReverbIR.f32 ] && cp third_party/Befaco/res/SpringReverbIR.f32 "$d/res/" || true'

[ "$packed" -eq 21 ] || {
	echo "expected 21 .rdmod packs, generated $packed" >&2
	exit 1
}
echo "--- done, $ABI packs in $OUT ---"
ls -la "$OUT"
