/* Virtual on-screen MIDI keyboard bridge.
 *
 * Thin passthrough to rack::keyboard::press()/release() -- the built-in
 * "Computer keyboard/mouse" MIDI driver (third_party/Rack/src/keyboard.cpp)
 * -- using the exact same QWERTY-row key codes it already maps to notes.
 * That means the on-screen keyboard plays through whatever MIDI-CV module
 * is already configured for "Computer keyboard/mouse" (the tutorial
 * patch's default), with no new driver, octave tracking, or MIDI routing
 * to build: PianoKeyboardView.kt just needs to know which GLFW_KEY_* each
 * key corresponds to (they're plain ASCII codes for every key on the
 * QWERTY row, so the Kotlin side never needs the actual GLFW header).
 *
 * Safe to call directly from the Java UI thread with no render-thread
 * marshalling: InputDevice::onMessage is thread-safe by design in Rack
 * (see amidi_driver.cpp's header comment, which already relies on this for
 * real MIDI hardware).
 */
#include <keyboard.hpp>

#include <jni.h>

namespace rackdroid {

extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeKeyboardPress(JNIEnv*, jobject, jint key) {
	rack::keyboard::press(key);
}

extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeKeyboardRelease(JNIEnv*, jobject, jint key) {
	rack::keyboard::release(key);
}

} // namespace rackdroid
