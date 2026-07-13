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
#include "window_android.hpp"
#include "jni_bridge.hpp"


namespace rackdroid {


static const double LONG_PRESS_SECONDS = 0.6;
static const float LONG_PRESS_SLOP_PX = 16.f; // in scene units
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

	// Long-press without movement → context menu (right click).
	if (st.down && st.leftSent && !st.gesture && !st.longPressFired) {
		double held = rack::system::getTime() - st.downTime;
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
