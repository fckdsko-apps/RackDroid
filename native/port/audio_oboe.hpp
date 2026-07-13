#pragma once

namespace rackdroid {

/** Driver ID registered in rack::audio. Must not collide with RtAudio's
driver IDs (RtAudio::Api values, < 16) since it is stored in patch files. */
static const int OBOE_DRIVER_ID = 777;

/** Registers the Oboe driver with rack::audio. Call once after audio::init(). */
void oboeInit();

} // namespace rackdroid
