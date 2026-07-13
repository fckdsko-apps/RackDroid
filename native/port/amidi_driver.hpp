#pragma once

namespace rackdroid {

/** Driver ID registered in rack::midi (stored in patches; must not collide
with RtMidi's, which are < 16). */
static const int AMIDI_DRIVER_ID = 778;

/** Registers the Android MIDI driver. Call once after midi::init(). Devices
are announced asynchronously by MainActivity via the nativeMidiDevice* JNI
callbacks (amidi_driver.cpp), which may arrive before or after this call. */
void amidiInit();

} // namespace rackdroid
