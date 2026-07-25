/* Touch → Rack event translation.
 *
 * Rack's event system is mouse-centric (EventState::handleButton/Hover/
 * Scroll with GLFW button/mod codes), so this layer emulates a mouse:
 *
 *  - one finger: hover + left button (tap = click, drag = knob/cable drag)
 *  - long-press without movement: right click (context menus)
 *  - two fingers: scroll (pan the rack); pinch: Ctrl+scroll (zoom)
 *
 * Coordinates: the EGL surface is 1:1 with touch coordinates (windowRatio=1),
 * and Rack scene coordinates are framebuffer / pixelRatio — same conversion
 * as upstream's cursorPosCallback.
 */
#include <atomic>
#include <cmath>

#include <android/input.h>
#include <jni.h>

#include <GLFW/glfw3.h>

#include <context.hpp>
#include <widget/event.hpp>
#include <window/Window.hpp>
#include <ui/TextField.hpp>
#include <app/ModuleWidget.hpp>
#include <app/PortWidget.hpp>
#include <system.hpp>

#include "touch_input.hpp"
#include <typeinfo>
#include <android/log.h>
#include "cable_park.hpp"
#include <app/CableWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include "window_android.hpp"
#include "jni_bridge.hpp"
#include <logger.hpp>


namespace rackdroid {


/* Cable-park diagnostics have to reach BOTH sinks: logcat for whoever has the
phone on a cable, and user/log.txt for everyone else, since a release build is
not debuggable and `run-as` cannot read the private log. Plain INFO() alone
already cost one debugging round trip here — the messages existed and were
invisible. */
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, "rackdroid.cablepark", __VA_ARGS__); INFO(__VA_ARGS__); } while (0)


static const double LONG_PRESS_SECONDS = 0.6;
static const float LONG_PRESS_SLOP_PX = 16.f; // in scene units
// How far the finger may drift and still count as "at rest" for the long-press
// timer. Small, so any real drag resets it; a resting finger's jitter does not.
static const float LONG_PRESS_STILL_PX = 5.f;
static const float PINCH_DETECT_RATIO = 0.02f;
static const float PINCH_ZOOM_SPEED = 8.f;
// Inertia (momentum) for two-finger panning
static const float INERTIA_MIN_SPEED = 80.f;   // scene units/s to start coasting
static const float INERTIA_STOP_SPEED = 20.f;  // stop below this
static const float INERTIA_DECAY = 4.f;         // exponential decay per second

struct TouchState {
	bool down = false;
	bool leftSent = false;
	bool longPressFired = false;
	double downTime = 0.0;
	rack::math::Vec downPos;
	rack::math::Vec lastPos;
	// When the finger last came to rest, and where. The long-press (context
	// menu) is timed from here, not from the down: a slow drag keeps nudging
	// this forward so it never fires -- moving a module must not pop its menu.
	double stillTime = 0.0;
	rack::math::Vec stillPos;

	// Multitouch gesture state
	bool gesture = false; // two-finger mode active
	rack::math::Vec lastCentroid;
	float lastDist = 0.f;
	double lastMoveTime = 0.0;
	rack::math::Vec panVelocity; // scene units/s, smoothed

	// Pan inertia after the fingers lift
	bool inertiaActive = false;
	rack::math::Vec inertiaVel;
	rack::math::Vec inertiaCentroid;
	double inertiaTime = 0.0;
};

static TouchState st;

/** Patch lock (toolbar padlocks). 0 = off. 1 = layout lock: module drags
 * and port/cable touches are swallowed, params stay live. 2 = full lock:
 * every single-finger press on the canvas is swallowed -- the patch is
 * immutable and only two-finger pan/zoom (and the toolbar, a separate
 * window) still works. Session-only by design. */
static std::atomic<int> lockMode{0};

/** Decide from the freshly-hovered widget whether this press is frozen.
 * Called after handleHover so APP->event->hoveredWidget is the touch
 * target. Layout lock: a press whose deepest target IS a ModuleWidget is a
 * module drag (controls hover as their own widgets, children of it) and a
 * press on/inside a PortWidget is a cable interaction -- both swallowed. */
static bool pressBlockedByLock() {
	int mode = lockMode.load(std::memory_order_relaxed);
	if (mode <= 0)
		return false;
	if (mode >= 2)
		return true;
	rack::widget::Widget* w = APP->event->hoveredWidget;
	if (!w)
		return false;
	for (rack::widget::Widget* p = w; p; p = p->parent) {
		if (dynamic_cast<rack::app::PortWidget*>(p))
			return true;
		if (dynamic_cast<rack::app::ModuleWidget*>(p))
			return p == w;
	}
	return false;
}


static void startInertia() {
	if (st.panVelocity.norm() >= INERTIA_MIN_SPEED) {
		st.inertiaActive = true;
		st.inertiaVel = st.panVelocity;
		st.inertiaCentroid = st.lastCentroid;
		st.inertiaTime = rack::system::getTime();
	}
	st.panVelocity = rack::math::Vec();
}


/** Slot whose parked cable end the finger is currently pulling out, or -1.
Lives here rather than in cable_park because it is touch state: the bar only
needs to know where to draw the cable. */
static int g_parkDrag = -1;


/** The port an in-flight Rack cable drag started from, or NULL. Rack keeps the
half-made cable in the rack widget; we only ever read it. */
static rack::app::PortWidget* incompleteCablePort() {
	if (!APP->scene || !APP->scene->rack)
		return NULL;
	std::vector<rack::app::CableWidget*> cables = APP->scene->rack->getIncompleteCables();
	if (!cables.empty()) {
		rack::app::CableWidget* cw = cables.back();
		rack::app::PortWidget* p = cw->inputPort ? cw->inputPort : cw->outputPort;
		if (p)
			return p;
	}
	// Dragging a cable that was ALREADY plugged in goes through the plug, not
	// through a freshly made incomplete cable, so the list above can be empty
	// while a cable end is very much in flight. Fall back to whatever widget
	// the drag actually started on.
	rack::widget::Widget* dragged = APP->event->getDraggedWidget();
	if (rack::app::PortWidget* pw = dynamic_cast<rack::app::PortWidget*>(dragged))
		return pw;
	LOGI("cablepark: nothing to park (%zu incomplete cables, dragged widget is %s)",
		cables.size(), dragged ? typeid(*dragged).name() : "nothing");
	return NULL;
}


static rack::math::Vec scenePos(float x, float y) {
	float ratio = APP->window ? APP->window->pixelRatio : 1.f;
	return rack::math::Vec(x, y).div(ratio);
}


static void sendLeftButton(rack::math::Vec pos, int action) {
	APP->event->handleButton(pos, GLFW_MOUSE_BUTTON_LEFT, action, 0);
}


static void endLeftDrag(rack::math::Vec pos) {
	if (st.leftSent) {
		sendLeftButton(pos, GLFW_RELEASE);
		st.leftSent = false;
	}
}


int touchHandleEvent(AInputEvent* event) {
	if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
		return 0;
	if (!APP->event || !APP->window)
		return 0;

	windowNoteInteraction();

	int32_t action = AMotionEvent_getAction(event);
	int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
	size_t pointerCount = AMotionEvent_getPointerCount(event);

	rack::math::Vec pos = scenePos(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));

	switch (actionMasked) {
		case AMOTION_EVENT_ACTION_DOWN: {
			st.down = true;
			st.gesture = false;
			st.longPressFired = false;
			st.inertiaActive = false; // any touch cancels coasting
			st.panVelocity = rack::math::Vec();
			st.downTime = rack::system::getTime();
			st.downPos = pos;
			st.lastPos = pos;
			st.stillTime = st.downTime;
			st.stillPos = pos;
			// Anchor the in-flight position at the press point right away. The
			// cable Rack creates on this press is checked against it before the
			// first MOVE arrives; without this it keeps last drag's stale value
			// and the spare hole flashes for a frame if that value was over the
			// bar (e.g. just after a park).
			rackdroid::cableParkSetInflightPos(pos.x, pos.y);
			// Tapping the bar's handle collapses it to just that handle, or
			// expands it again. Checked before the hole grab so the handle is
			// never read as a park interaction.
			if (rackdroid::cableParkArrowAt(pos.x, pos.y)) {
				rackdroid::cableParkToggleCollapsed();
				return 1;
			}
			// Pulling a parked cable end out of the bar: our own drag, Rack
			// must not see it at all or it would start panning the rack.
			{
				int slot = rackdroid::cableParkSlotAt(pos.x, pos.y);
				if (slot >= 0 && rackdroid::cableParkSlotFilled(slot)) {
					g_parkDrag = slot;
					rackdroid::cableParkSetDragging(slot, pos.x, pos.y);
					return 1;
				}
			}
			// Move the virtual cursor there first, then press.
			APP->event->handleHover(pos, rack::math::Vec());
			// Padlock active and the target is frozen: swallow the press.
			// leftSent stays false, so MOVE/UP/long-press all no-op, but
			// a second finger still enters pan/zoom as usual.
			if (pressBlockedByLock())
				return 1;
			sendLeftButton(pos, GLFW_PRESS);
			st.leftSent = true;
			return 1;
		}

		case AMOTION_EVENT_ACTION_POINTER_DOWN: {
			// Second finger: abort the left drag, enter pan/zoom mode.
			if (pointerCount == 2) {
				endLeftDrag(st.lastPos);
				st.gesture = true;
				st.inertiaActive = false;
				st.panVelocity = rack::math::Vec();
				rack::math::Vec p1 = scenePos(AMotionEvent_getX(event, 1), AMotionEvent_getY(event, 1));
				st.lastCentroid = pos.plus(p1).mult(0.5f);
				st.lastDist = pos.minus(p1).norm();
				st.lastMoveTime = rack::system::getTime();
			}
			return 1;
		}

		case AMOTION_EVENT_ACTION_MOVE: {
			if (g_parkDrag >= 0) {
				// Hover so the port under the finger lights up as it would in a
				// normal Rack cable drag; the bar draws the cable itself.
				APP->event->handleHover(pos, pos.minus(st.lastPos));
				rackdroid::cableParkSetDragging(g_parkDrag, pos.x, pos.y);
				st.lastPos = pos;
				return 1;
			}
			if (st.gesture && pointerCount >= 2) {
				rack::math::Vec p1 = scenePos(AMotionEvent_getX(event, 1), AMotionEvent_getY(event, 1));
				rack::math::Vec centroid = pos.plus(p1).mult(0.5f);
				float dist = pos.minus(p1).norm();

				// Pinch → Ctrl+scroll (Rack's zoom gesture)
				if (st.lastDist > 0.f) {
					float ratio = dist / st.lastDist - 1.f;
					if (std::fabs(ratio) > PINCH_DETECT_RATIO) {
						windowSetMods(GLFW_MOD_CONTROL);
						APP->event->handleScroll(centroid, rack::math::Vec(0.f, ratio * PINCH_ZOOM_SPEED * 50.f));
						windowSetMods(0);
					}
				}
				// Two-finger pan → scroll
				rack::math::Vec delta = centroid.minus(st.lastCentroid);
				if (delta.norm() > 0.f)
					APP->event->handleScroll(centroid, delta);

				// Track panning velocity for release inertia (EMA).
				double now = rack::system::getTime();
				double dt = now - st.lastMoveTime;
				if (dt > 1e-4) {
					rack::math::Vec instV = delta.div(dt);
					st.panVelocity = st.panVelocity.mult(0.5f).plus(instV.mult(0.5f));
				}
				st.lastMoveTime = now;
				st.lastCentroid = centroid;
				st.lastDist = dist;
				st.lastPos = pos;
				return 1;
			}

			if (st.down && st.leftSent) {
				rack::math::Vec delta = pos.minus(st.lastPos);
				APP->event->handleHover(pos, delta);
				// Tell the bar where an in-flight cable end is, so it only
				// reveals its spare hole once the cable is dragged over it.
				rackdroid::cableParkSetInflightPos(pos.x, pos.y);
				// Moved past a hair? Then the finger is dragging, not resting:
				// push the still-clock forward so the long-press never fires
				// mid-drag (a slow module move used to pop the context menu).
				if (pos.minus(st.stillPos).norm() > LONG_PRESS_STILL_PX) {
					st.stillTime = rack::system::getTime();
					st.stillPos = pos;
				}
				st.lastPos = pos;
			}
			return 1;
		}

		case AMOTION_EVENT_ACTION_POINTER_UP: {
			if (st.gesture && pointerCount == 2) {
				// Back to single-finger mode; don't resume the left drag.
				st.gesture = false;
				st.lastDist = 0.f;
				startInertia();
			}
			return 1;
		}

		case AMOTION_EVENT_ACTION_UP: {
			// Report the release point so a drop onto the spare hole still sees
			// it (visibleHoles keys the spare on the in-flight end being on the
			// bar, and this is the last position before the cable is discarded).
			rackdroid::cableParkSetInflightPos(pos.x, pos.y);
			// Dropping a parked end onto a port completes the cable.
			if (g_parkDrag >= 0) {
				rack::app::PortWidget* target =
					dynamic_cast<rack::app::PortWidget*>(APP->event->getHoveredWidget());
				// An incompatible jack under the finger is not an answer: keep
				// looking for one that could actually take this end, so landing
				// on an input while holding an input still finds the output
				// next to it.
				int want = rackdroid::cableParkSlotType(g_parkDrag) == 0 ? 1 : 0;
				if (target && target->type != want)
					target = NULL;
				if (!target) {
					// Landed beside the jack rather than on it: snap to the
					// nearest port that could actually take this end.
					target = rackdroid::cableParkNearestPort(pos.x, pos.y, want);
				}
				if (target)
					rackdroid::cableParkConnect(g_parkDrag, target);
				else if (pos.x <= rackdroid::CABLE_PARK_BAR_W) {
					// Let go again over the bar: a cancel. Keep it parked.
					LOGI("cablepark: pull-out cancelled, slot %d stays", g_parkDrag);
				}
				else {
					// Carried off and dropped in the rack void: discard it. An
					// end the user pulled out and abandoned is one they no longer
					// want -- leaving it in its hole is the "why is this still
					// here" that flash-and-keep used to cause.
					LOGI("cablepark: dropped in the void, discarding slot %d", g_parkDrag);
					rackdroid::cableParkClear(g_parkDrag);
				}
				rackdroid::cableParkSetDragging(-1, 0.f, 0.f);
				g_parkDrag = -1;
				st.down = false;
				st.gesture = false;
				return 1;
			}
			// Releasing an in-flight Rack cable drag over an empty hole parks
			// it: we only record which port it came from, then let Rack discard
			// its half-made cable as usual.
			if (!st.gesture && st.leftSent) {
				int slot = rackdroid::cableParkSlotAt(pos.x, pos.y);
				if (slot >= 0) {
					// Dropped on the bar: the end always goes to the first free
					// hole, so the holes stay packed from the top (drop anywhere,
					// no gaps). Every way of failing still has to say so on screen.
					int free = rackdroid::cableParkFirstFree();
					if (free < 0) {
						LOGI("cablepark: bar is full");
						rackdroid::cableParkFlashRefused(slot);
					}
					else if (rack::app::PortWidget* from = incompleteCablePort()) {
						size_t before = APP->scene->rack->getCompleteCables().size();
						size_t inc = APP->scene->rack->getIncompleteCables().size();
						LOGI("cablepark: at park, complete=%zu incomplete=%zu, from is %s port %d",
							before, inc, from->type == rack::engine::Port::INPUT ? "input" : "output", from->portId);
						rackdroid::cableParkStore(free, from);
					}
					else {
						// incompleteCablePort() already logged why.
						rackdroid::cableParkFlashRefused(free);
					}
				}
			}
			if (!st.gesture) {
				endLeftDrag(pos);
				// Tap landed on a text field: no hardware keyboard on
				// Android, so edit it through a system prompt.
				rack::ui::TextField* field = dynamic_cast<rack::ui::TextField*>(APP->event->selectedWidget);
				if (field && !st.longPressFired) {
					std::string edited;
					std::string title = field->placeholder.empty() ? "Edit text" : field->placeholder;
					if (dialogPrompt(title, field->getText(), edited))
						field->setText(edited);
				}
			}
			st.down = false;
			st.gesture = false;
			return 1;
		}

		case AMOTION_EVENT_ACTION_CANCEL: {
			if (g_parkDrag >= 0) {
				rackdroid::cableParkSetDragging(-1, 0.f, 0.f);
				g_parkDrag = -1;
			}
			endLeftDrag(st.lastPos);
			st.down = false;
			st.gesture = false;
			return 1;
		}

		default:
			return 0;
	}
}


void touchStep() {
	if (!APP->event || !APP->window)
		return;

	// Pan inertia: coast the rack after a two-finger flick.
	if (st.inertiaActive) {
		double now = rack::system::getTime();
		double dt = now - st.inertiaTime;
		st.inertiaTime = now;
		if (dt <= 0.0 || dt > 0.1)
			dt = 1.0 / 60.0; // clamp after stalls
		rack::math::Vec delta = st.inertiaVel.mult(dt);
		APP->event->handleScroll(st.inertiaCentroid, delta);
		st.inertiaVel = st.inertiaVel.mult(std::exp(-INERTIA_DECAY * dt));
		windowNoteInteraction(); // keep rendering at full rate while coasting
		if (st.inertiaVel.norm() < INERTIA_STOP_SPEED)
			st.inertiaActive = false;
	}

	// Long-press without movement → context menu (right click). Timed from when
	// the finger last came to rest, and only while it is still near the press
	// point -- so a drag (even a slow one) never opens the menu.
	if (st.down && st.leftSent && !st.gesture && !st.longPressFired) {
		double held = rack::system::getTime() - st.stillTime;
		float moved = st.lastPos.minus(st.downPos).norm();
		if (held >= LONG_PRESS_SECONDS && moved < LONG_PRESS_SLOP_PX) {
			st.longPressFired = true;
			// Release the left drag, then emit a right click at the position.
			endLeftDrag(st.lastPos);
			APP->event->handleButton(st.lastPos, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
			APP->event->handleButton(st.lastPos, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
		}
	}
}


// Toolbar padlocks (MainActivity): 0 unlocked, 1 layout lock, 2 full lock.
extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeSetLockMode(JNIEnv*, jobject, jint mode) {
	lockMode.store(mode, std::memory_order_relaxed);
}


} // namespace rackdroid
