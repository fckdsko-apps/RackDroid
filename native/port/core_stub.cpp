/* Stub for the built-in Core plugin (src/core/*).
 *
 * The real Core plugin modules (Audio, MIDI>CV, Notes, ...) carry their
 * ModuleWidgets in the same translation units, so they can only be compiled
 * once the UI stack is in (phase 2). With no models registered, plugin.cpp
 * rejects the Core manifest at startup ("module ... not defined in plugin") —
 * that warning is expected in phase 1. The port layer drives audio directly
 * via audio_oboe in the meantime.
 */
#include <plugin/Plugin.hpp>


namespace rack {
namespace core {


void init(rack::plugin::Plugin* p) {
	(void) p;
	// Models are added here in phase 2 (real src/core/plugin.cpp).
}


} // namespace core
} // namespace rack
