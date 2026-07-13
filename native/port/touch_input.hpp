#pragma once

struct AInputEvent;

namespace rackdroid {

/** Translates an Android input event into Rack UI events.
Returns 1 if the event was consumed. */
int touchHandleEvent(AInputEvent* event);

/** Per-frame housekeeping (long-press detection). Call once per rendered
frame, before Window::step(). */
void touchStep();

} // namespace rackdroid
