/* Live demonstrations for the interface tour.
 *
 * The tour explains the screen; these let it SHOW the screen working -- the view
 * settling on the modules, the rack zooming and scrolling under them, modules
 * lighting up as they are chosen and sliding across the rack, and a cable being
 * drawn from an output to an input with the jacks that can take it lit up. Java
 * drives it a step at a time; everything here runs on the render thread, where
 * the rack widget belongs.
 *
 * The rule that shapes all of it: the tour is a demonstration, not an edit. It
 * can be started at any moment from Help ▸ Interface tour, on top of whatever
 * the user is building. So nothing here touches history, nothing is left
 * selected, no cable is ever handed to the engine (the one it draws is an
 * incomplete cable, the same half-made thing Rack holds while you drag one),
 * and a moved module is put back at exactly the coordinates it had -- restore()
 * runs when the tour ends, when it is dismissed halfway, and before any
 * autosave.
 *
 * Every demonstration opens by bringing the modules it is about to use into
 * view: a step that moves something off screen teaches nothing.
 */
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

#include <system.hpp>
#include <app/ModuleWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/CableWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/RackScrollWidget.hpp>
#include <app/Scene.hpp>
#include <widget/event.hpp>
#include <context.hpp>
#include <math.hpp>

#if __ANDROID__
#include <jni.h>
#endif

#include "tour_demo.hpp"
#include "cable_park.hpp"

using namespace rack;

namespace rackdroid {


namespace {

// Kept in step with the constants in HelpUi.kt.
enum {
	D_NONE = 0,
	D_SELECT = 1,   // red halo on a couple of modules
	D_NUDGE = 2,    // slide the selection aside and back
	D_RESTORE = 3,  // undo everything the demo did
	D_MOVE = 4,     // slide a single module aside and back
	D_CENTRE = 5,   // just bring the modules into view
	D_ZOOM = 6,     // zoom in and out, then scroll sideways and back
	D_CABLE = 7,    // draw a cable from an output to an input
};

// How long the view takes to settle on the modules before a demonstration
// starts. Long enough to read as a camera move, short enough not to be a wait.
const double CENTRE_T = 0.60;

struct Moved {
	int64_t moduleId;
	math::Vec original;
};

struct TourDemo {
	// What Java has asked for, applied on the next render pass.
	std::atomic<int> request{0};
	int demo = D_NONE;
	double t0 = 0.0;

	// Modules we selected ourselves, so we clear exactly those and leave any
	// selection the user already had alone.
	std::vector<int64_t> selected;
	std::vector<Moved> moved;

	// The view as the user left it, put back when the tour ends. Saved once, by
	// the first demonstration that moves the camera.
	bool viewSaved = false;
	math::Vec viewOffset;
	float viewZoom = 1.f;

	// Camera move at the start of a demonstration: the rect to frame, plus
	// where the view starts from. Zoom is animated with the offset so a pair of
	// modules too wide for the screen still ends up fully in frame.
	math::Rect centreBound;
	math::Vec centreFrom;
	float centreZoomFrom = 1.f, centreZoomTo = 1.f;

	// Zoom/scroll demonstration.
	float zoomFrom = 1.f;
	bool panBased = false;
	math::Vec panBase;

	// Cable demonstration. Incomplete: it is drawn, it lights up the compatible
	// jacks, and it is never given to the engine. The pair is chosen before the
	// camera moves, so the framing can take both modules in.
	app::CableWidget* cable = NULL;
	int64_t cableOutModule = -1, cableInModule = -1;
	math::Vec cableFrom, cableTo;
};

TourDemo g;

/** How many modules are on the rack, republished every frame so Java can read
it from the UI thread: the rack widget belongs to the render thread, and the
tour has to know before it builds its step list.
 *
 * Starts at -1, meaning "no frame has run yet". The first-run tour is built
 * before the first frame, and a 0 there would be read as an empty rack -- which
 * is how the welcome tour ended up dropping three of its steps on a patch that
 * had nine modules in it. */
std::atomic<int> g_moduleCount{-1};

/** Flat at both ends, fastest in the middle: every movement here is eased, so
it reads as deliberate rather than as a glitch. */
float ease(double x) {
	if (x < 0.0) x = 0.0;
	if (x > 1.0) x = 1.0;
	return (float) (0.5 - 0.5 * std::cos(x * 3.14159265358979));
}

app::RackScrollWidget* scroll() {
	return APP->scene ? APP->scene->rackScroll : NULL;
}

/** Up to `count` modules, nearest the middle of the patch first.
 *
 * Ranked by distance from the centre of ALL modules rather than from the centre
 * of the screen: a demonstration has to work even when the user has scrolled
 * off into empty rack, and it is followed by a camera move that brings whatever
 * it picked into view anyway. */
std::vector<app::ModuleWidget*> demoModules(int count) {
	std::vector<app::ModuleWidget*> out;
	if (!APP->scene || !APP->scene->rack)
		return out;
	std::vector<app::ModuleWidget*> all = APP->scene->rack->getModules();

	math::Rect bound;
	bool first = true;
	for (app::ModuleWidget* mw : all) {
		if (!mw)
			continue;
		bound = first ? mw->box : bound.expand(mw->box);
		first = false;
	}
	if (first)
		return out; // nothing on the rack
	math::Vec centre = bound.getCenter();

	std::vector<std::pair<float, app::ModuleWidget*>> ranked;
	for (app::ModuleWidget* mw : all) {
		if (mw)
			ranked.push_back({mw->box.getCenter().minus(centre).norm(), mw});
	}
	std::sort(ranked.begin(), ranked.end(),
		[](const std::pair<float, app::ModuleWidget*>& a,
		   const std::pair<float, app::ModuleWidget*>& b) { return a.first < b.first; });
	for (auto& r : ranked) {
		if ((int) out.size() >= count)
			break;
		out.push_back(r.second);
	}
	return out;
}

app::ModuleWidget* findById(int64_t id) {
	if (!APP->scene || !APP->scene->rack)
		return NULL;
	for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
		if (mw && mw->module && mw->module->id == id)
			return mw;
	}
	return NULL;
}

/** The scroll offset that puts `bound` (in rack coordinates) in the middle of
the viewport at `zoom` -- the arithmetic RackScrollWidget's own zoomToBound
uses, with the zoom passed in rather than taken from the widget. */
math::Vec offsetCentring(math::Rect bound, float zoom) {
	app::RackScrollWidget* rs = scroll();
	if (!rs)
		return math::Vec();
	return bound.getCenter().mult(zoom).minus(rs->box.size.div(2.f));
}

void saveView() {
	app::RackScrollWidget* rs = scroll();
	if (!rs || g.viewSaved)
		return;
	g.viewOffset = rs->offset;
	g.viewZoom = rs->getZoom();
	g.viewSaved = true;
}

/** Aims the camera at the modules this demonstration is about to use, pulling
back if they do not all fit at the zoom the user was on. */
void beginCentring(const std::vector<app::ModuleWidget*>& mws) {
	app::RackScrollWidget* rs = scroll();
	if (!rs)
		return;
	saveView();
	g.centreFrom = rs->offset;
	g.centreZoomFrom = rs->getZoom();
	g.centreZoomTo = g.centreZoomFrom;
	if (mws.empty()) {
		// Nothing to aim at: stay put.
		g.centreBound = math::Rect(
			rs->offset.plus(rs->box.size.div(2.f)).div(g.centreZoomFrom), math::Vec());
		return;
	}
	math::Rect bound;
	bool first = true;
	for (app::ModuleWidget* mw : mws) {
		bound = first ? mw->box : bound.expand(mw->box);
		first = false;
	}
	g.centreBound = bound;
	// Zoom out only, and only as far as needed: a demonstration that shrinks
	// the rack it is explaining has made things worse, not better.
	if (bound.size.x > 0.f && bound.size.y > 0.f) {
		float fit = std::min(rs->box.size.x / bound.size.x, rs->box.size.y / bound.size.y) * 0.92f;
		if (fit < g.centreZoomTo)
			g.centreZoomTo = std::max(fit, 0.25f);
	}
}

void dropCable() {
	if (!g.cable)
		return;
	if (APP->scene && APP->scene->rack)
		APP->scene->rack->removeCable(g.cable);
	delete g.cable;
	g.cable = NULL;
	// Park the highlight machinery's idea of the in-flight end far off screen,
	// which is how cable_park reads "no cable in flight".
	cableParkSetInflightPos(1e6f, 0.f);
	// The demonstration hovered its way across the panels, and Rack keeps a
	// jack's tooltip up while the cursor rests on it. Nothing will move that
	// cursor again -- a finger only hovers while it is down -- so the tooltip
	// would sit there for the rest of the session. handleLeave does NOT undo
	// it (upstream deliberately keeps the hover when the pointer leaves the
	// window, in case it is mid-drag); hovering a point outside every widget
	// does, because that dispatches Leave to the jack and hovers nothing.
	if (APP->event)
		APP->event->handleHover(math::Vec(-1e4f, -1e4f), math::Vec());
}

/** Picks the two modules the demonstration will run a cable between: the pair
whose jacks are furthest apart, so the cable is drawn across the screen rather
than between two neighbouring holes -- a hand's width of movement is what makes
the gesture legible. Returns them for the camera to frame. */
std::vector<app::ModuleWidget*> chooseCablePair() {
	std::vector<app::ModuleWidget*> out;
	g.cableOutModule = -1;
	g.cableInModule = -1;
	float best = -1.f;
	app::ModuleWidget* bestA = NULL;
	app::ModuleWidget* bestB = NULL;
	std::vector<app::ModuleWidget*> mws = demoModules(4);
	for (app::ModuleWidget* a : mws) {
		std::vector<app::PortWidget*> outs = a->getOutputs();
		if (outs.empty() || !a->module)
			continue;
		for (app::ModuleWidget* b : mws) {
			if (b == a || !b->module)
				continue;
			std::vector<app::PortWidget*> ins = b->getInputs();
			if (ins.empty())
				continue;
			float d = outs.front()->getRelativeOffset(math::Vec(), APP->scene->rack)
				.minus(ins.front()->getRelativeOffset(math::Vec(), APP->scene->rack)).norm();
			if (d > best) {
				best = d;
				bestA = a;
				bestB = b;
			}
		}
	}
	if (!bestA || !bestB)
		return out;
	g.cableOutModule = bestA->module->id;
	g.cableInModule = bestB->module->id;
	out.push_back(bestA);
	out.push_back(bestB);
	return out;
}

/** Hangs a half-made cable off the chosen output, ready to be drawn to the
chosen input. Called once the camera has settled, because the jacks move while
it is moving. */
bool buildDemoCable() {
	if (!APP->scene || !APP->scene->rack)
		return false;
	app::ModuleWidget* a = findById(g.cableOutModule);
	app::ModuleWidget* b = findById(g.cableInModule);
	if (!a || !b)
		return false;
	std::vector<app::PortWidget*> outs = a->getOutputs();
	std::vector<app::PortWidget*> ins = b->getInputs();
	if (outs.empty() || ins.empty())
		return false;
	app::PortWidget* from = outs.front();
	app::PortWidget* to = ins.front();

	app::CableWidget* cw = new app::CableWidget;
	// Building one by hand skips the colour Rack picks when the USER starts a
	// drag, and a default-constructed NVGcolor draws as a dead grey wire.
	cw->color = APP->scene->rack->getNextCableColor();
	cw->outputPort = from;
	APP->scene->rack->addCable(cw);
	g.cable = cw;
	g.cableFrom = from->getAbsoluteOffset(from->box.size.div(2.f));
	g.cableTo = to->getAbsoluteOffset(to->box.size.div(2.f));
	return true;
}

/** Moves the virtual cursor: an in-flight cable end follows it, and the jacks
that can take the cable light up around it. */
void hoverAt(math::Vec pos) {
	if (!APP->event)
		return;
	APP->event->handleHover(pos, math::Vec());
	cableParkSetInflightPos(pos.x, pos.y);
}

void endDemo() {
	g.demo = D_NONE;
}

} // namespace


int rackModuleCount() {
	return g_moduleCount.load();
}


void tourDemoRequest(int what) {
	g.request.store(what);
}


void tourDemoRestore() {
	if (!APP || !APP->scene || !APP->scene->rack)
		return;
	dropCable();
	for (const Moved& m : g.moved) {
		if (app::ModuleWidget* mw = findById(m.moduleId))
			mw->box.pos = m.original;
	}
	g.moved.clear();
	for (int64_t id : g.selected) {
		if (app::ModuleWidget* mw = findById(id))
			APP->scene->rack->select(mw, false);
	}
	g.selected.clear();
	if (g.viewSaved) {
		if (app::RackScrollWidget* rs = scroll()) {
			rs->setZoom(g.viewZoom);
			rs->offset = g.viewOffset;
		}
		g.viewSaved = false;
	}
	g.panBased = false;
	endDemo();
}


void processTourDemo() {
	if (!APP->scene || !APP->scene->rack)
		return;
	g_moduleCount.store((int) APP->scene->rack->getModules().size());

	int request = g.request.exchange(0);
	if (request == D_RESTORE) {
		tourDemoRestore();
		return;
	}
	if (request != D_NONE) {
		// One demonstration at a time: whatever the last one still held (a
		// half-made cable, a module part way through a slide) goes back before
		// the next starts. The selection is left alone on purpose -- the nudge
		// is meant to move what the select demonstration lit up.
		dropCable();
		for (const Moved& m : g.moved) {
			if (app::ModuleWidget* mw = findById(m.moduleId))
				mw->box.pos = m.original;
		}
		g.moved.clear();
		g.panBased = false;

		g.demo = request;
		g.t0 = system::getTime();

		if (request == D_NUDGE) {
			std::vector<app::ModuleWidget*> mws;
			for (int64_t id : g.selected) {
				if (app::ModuleWidget* mw = findById(id)) {
					g.moved.push_back({id, mw->box.pos});
					mws.push_back(mw);
				}
			}
			beginCentring(mws);
		}
		else if (request == D_MOVE) {
			std::vector<app::ModuleWidget*> mws = demoModules(1);
			if (!mws.empty() && mws[0]->module) {
				g.moved.push_back({mws[0]->module->id, mws[0]->box.pos});
				beginCentring(mws);
			}
			else
				endDemo();
		}
		else if (request == D_CABLE) {
			// Frame the pair the cable will run between; it is built once the
			// camera has settled, because the jacks move while it does.
			std::vector<app::ModuleWidget*> pair = chooseCablePair();
			if (pair.empty())
				endDemo();
			else
				beginCentring(pair);
		}
		else {
			beginCentring(demoModules(request == D_SELECT ? 2 : 3));
			if (request == D_ZOOM)
				g.zoomFrom = g.centreZoomTo; // the zoom the framing settles on
		}
	}

	if (g.demo == D_NONE)
		return;

	app::RackScrollWidget* rs = scroll();
	if (!rs) {
		endDemo();
		return;
	}
	double t = system::getTime() - g.t0;

	// Every demonstration opens with the same camera move.
	if (t < CENTRE_T) {
		float k = ease(t / CENTRE_T);
		float z = g.centreZoomFrom + (g.centreZoomTo - g.centreZoomFrom) * k;
		rs->zoomWidget->setZoom(z);
		rs->offset = g.centreFrom.plus(
			offsetCentring(g.centreBound, z).minus(g.centreFrom).mult(k));
		return;
	}
	if (g.demo != D_ZOOM || !g.panBased) {
		rs->zoomWidget->setZoom(g.centreZoomTo);
		rs->offset = offsetCentring(g.centreBound, g.centreZoomTo);
	}
	double b = t - CENTRE_T; // time within the demonstration proper

	switch (g.demo) {
		case D_CENTRE:
			endDemo();
			break;

		case D_SELECT: {
			if (g.selected.empty()) {
				for (app::ModuleWidget* mw : demoModules(2)) {
					APP->scene->rack->select(mw, true);
					if (mw->module)
						g.selected.push_back(mw->module->id);
				}
			}
			endDemo(); // the halo stays until the tour puts it back
			break;
		}

		case D_MOVE:
		case D_NUDGE: {
			// Out, a pause at the far end, and back: about a third of a
			// module's height, so it never leaves the screen.
			const double OUT = 0.75, HOLD = 0.40, BACK = 0.75;
			const float DISTANCE = 45.f;
			if (g.moved.empty()) {
				endDemo();
				break;
			}
			if (b >= OUT + HOLD + BACK) {
				// Home again: the exact original coordinates, not an
				// interpolated value, so repeating the tour cannot drift the
				// patch.
				for (const Moved& m : g.moved) {
					if (app::ModuleWidget* mw = findById(m.moduleId))
						mw->box.pos = m.original;
				}
				g.moved.clear();
				endDemo();
				break;
			}
			double phase = (b < OUT) ? b / OUT
				: (b < OUT + HOLD) ? 1.0
				: 1.0 - (b - OUT - HOLD) / BACK;
			float k = ease(phase);
			for (const Moved& m : g.moved) {
				if (app::ModuleWidget* mw = findById(m.moduleId))
					mw->box.pos = m.original.plus(math::Vec(0.f, DISTANCE * k));
			}
			break;
		}

		case D_ZOOM: {
			// In, hold, out -- then the same trip sideways, so one step covers
			// both of the things two fingers do to the rack.
			const double IN = 0.80, HOLD = 0.30, OUT = 0.80;
			const double PAN = 0.60, PAN_BACK = 0.60;
			const float FACTOR = 1.6f;
			if (b < IN + HOLD + OUT) {
				double phase = (b < IN) ? b / IN
					: (b < IN + HOLD) ? 1.0
					: 1.0 - (b - IN - HOLD) / OUT;
				rs->setZoom(g.zoomFrom * (1.f + (FACTOR - 1.f) * ease(phase)));
				break;
			}
			double p = b - (IN + HOLD + OUT);
			if (!g.panBased) {
				// The zoom is back where it started; whatever offset that left
				// is what the scrolling leg swings away from.
				rs->setZoom(g.zoomFrom);
				g.panBase = rs->offset;
				g.panBased = true;
			}
			if (p >= PAN + PAN_BACK) {
				rs->offset = g.panBase;
				endDemo();
				break;
			}
			double phase = (p < PAN) ? p / PAN : 1.0 - (p - PAN) / PAN_BACK;
			float dx = rs->box.size.x * 0.35f * ease(phase);
			rs->offset = g.panBase.plus(math::Vec(dx, 0.f));
			break;
		}

		case D_CABLE: {
			const double DRAW = 1.20, SNAP = 0.60;
			if (!g.cable && !buildDemoCable()) {
				endDemo();
				break;
			}
			if (b >= DRAW + SNAP) {
				dropCable();
				endDemo();
				break;
			}
			if (b < DRAW) {
				float k = ease(b / DRAW);
				hoverAt(g.cableFrom.plus(g.cableTo.minus(g.cableFrom).mult(k)));
			}
			else {
				// Arrived: held on the jack, where a real drop would snap it,
				// so the connection reads as made.
				hoverAt(g.cableTo);
			}
			break;
		}

		default:
			endDemo();
			break;
	}
}


} // namespace rackdroid


#if __ANDROID__
extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeTourDemo(JNIEnv*, jobject, jint what) {
	rackdroid::tourDemoRequest(what);
}


extern "C" JNIEXPORT jint JNICALL
Java_org_rackdroid_MainActivity_nativeRackModuleCount(JNIEnv*, jobject) {
	return rackdroid::rackModuleCount();
}
#endif
