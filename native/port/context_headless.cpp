/* Headless replacement for Rack's src/context.cpp.
 *
 * The upstream Context destructor deletes the Scene, EventState, Window and
 * PatchManager, which would pull the whole UI stack into the link. In the
 * phase-1 headless build those subsystems are never created, so this
 * destructor only tears down what the headless adapter actually allocates.
 * When the UI phases land, this file is dropped in favor of the upstream one.
 */
#include <context.hpp>
#include <engine/Engine.hpp>
#include <midiloopback.hpp>


namespace rack {


Context::~Context() {
	INFO("Deleting engine");
	delete engine;
	engine = NULL;

	INFO("Deleting MIDI loopback");
	delete midiLoopbackContext;
	midiLoopbackContext = NULL;
}


static thread_local Context* threadContext = NULL;

Context* contextGet() {
	assert(threadContext);
	return threadContext;
}

void contextSet(Context* context) {
	threadContext = context;
}


} // namespace rack
