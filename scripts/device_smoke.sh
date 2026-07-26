#!/usr/bin/env bash
# Install and launch the release APK on one authorized Android device, fail on
# startup crashes, and save a screenshot/log for manual release review.
set -euo pipefail

cd "$(dirname "$0")/.."
ADB="${ADB:-${ANDROID_HOME:+$ANDROID_HOME/platform-tools/adb}}"
ADB="${ADB:-adb}"
# Flavored builds land in outputs/apk/<flavor>/release/. With no argument take
# the most recently built one, so the usual "build, then smoke" loop keeps
# working whichever distribution was just assembled.
APK="${1:-}"
if [[ -z "$APK" ]]; then
  APK=$(ls -t app/build/outputs/apk/*/release/*.apk 2>/dev/null | head -1)
fi
REPORT_DIR="${DEVICE_SMOKE_REPORT_DIR:-app/build/reports/device-smoke}"
PACKAGE=org.rackdroid
ACTIVITY="$PACKAGE/.MainActivity"

command -v "$ADB" >/dev/null 2>&1 || {
  echo "adb not found; set ANDROID_HOME or ADB" >&2
  exit 2
}
[[ -n "$APK" && -f "$APK" ]] || {
  echo "APK not found${APK:+: $APK}" >&2
  echo "Build it with ./gradlew assembleSideloadRelease -PdevKeystore first," >&2
  echo "or pass the path to an APK as the first argument." >&2
  exit 2
}
echo "APK: $APK"

mapfile -t devices < <("$ADB" devices | awk 'NR > 1 && $2 == "device" {print $1}')
# ANDROID_SERIAL names the target when more than one device is authorized --
# typically a phone on USB alongside an x86_64 tablet over TCP. Without it the
# test still refuses to guess which device to install on.
serial="${ANDROID_SERIAL:-}"
if [[ -n "$serial" ]]; then
  printf '%s\n' "${devices[@]}" | grep -qxF "$serial" || {
    echo "ANDROID_SERIAL=$serial is not an authorized device." >&2
    "$ADB" devices -l >&2
    exit 2
  }
elif (( ${#devices[@]} != 1 )); then
  echo "Expected exactly one authorized device, found ${#devices[@]}." >&2
  "$ADB" devices -l >&2
  echo "Unlock the phone and accept the USB debugging authorization prompt," >&2
  echo "or set ANDROID_SERIAL to one of the serials listed above." >&2
  exit 2
else
  serial="${devices[0]}"
fi
adb=("$ADB" -s "$serial")
mkdir -p "$REPORT_DIR"

printf 'Device: '
"${adb[@]}" shell getprop ro.product.manufacturer | tr -d '\r'
printf ' '
"${adb[@]}" shell getprop ro.product.model | tr -d '\r'
printf 'Android/API: '
"${adb[@]}" shell getprop ro.build.version.release | tr -d '\r'
printf ' / '
"${adb[@]}" shell getprop ro.build.version.sdk | tr -d '\r'

"${adb[@]}" install -r "$APK"
"${adb[@]}" logcat -c
"${adb[@]}" shell am force-stop "$PACKAGE"
"${adb[@]}" shell am start -W -n "$ACTIVITY" | tee "$REPORT_DIR/activity-start.txt"

# ActivityManager returning only means the Android activity window exists.
# Rack still has to extract assets, register plugins and restore the patch;
# wait for its factual completion marker so screenshots never capture the
# expected black EGL surface between Activity launch and first Rack frame.
set +e
set +o pipefail # grep deliberately closes adb's pipe as soon as it finds it
timeout 45s "${adb[@]}" logcat -v brief -s rackdroid:I \
  | grep -m1 -q 'Patch launched:'
patch_ready=$?
set -o pipefail
set -e
if (( patch_ready != 0 )); then
  echo "Rack engine did not report a completed patch launch within 45 seconds." >&2
  "${adb[@]}" logcat -d -v threadtime > "$REPORT_DIR/logcat.txt"
  exit 1
fi

pid=$("${adb[@]}" shell pidof -s "$PACKAGE" | tr -d '\r')
[[ -n "$pid" ]] || {
  echo "RackDroid process is not running after launch." >&2
  "${adb[@]}" logcat -d -v threadtime > "$REPORT_DIR/logcat.txt"
  exit 1
}

"${adb[@]}" logcat -d -v threadtime --pid="$pid" > "$REPORT_DIR/logcat.txt"
"${adb[@]}" exec-out screencap -p > "$REPORT_DIR/startup.png"
"${adb[@]}" shell dumpsys activity activities > "$REPORT_DIR/activities.txt"

if grep -Eq 'FATAL EXCEPTION|Fatal signal|UnsatisfiedLinkError|Abort message' "$REPORT_DIR/logcat.txt"; then
  echo "Fatal startup error found; see $REPORT_DIR/logcat.txt" >&2
  grep -E 'FATAL EXCEPTION|Fatal signal|UnsatisfiedLinkError|Abort message' "$REPORT_DIR/logcat.txt" >&2
  exit 1
fi
if ! grep -q "$PACKAGE/.MainActivity" "$REPORT_DIR/activities.txt"; then
  echo "RackDroid activity is not present after launch." >&2
  exit 1
fi

echo "Device smoke test passed. Reports: $REPORT_DIR"
echo "Inspect startup.png and perform the manual checks in PUBLISHING.md."
