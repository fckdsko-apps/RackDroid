/* Live demonstrations for the interface tour.
 *
 * The tour explains the screen; these let it SHOW the screen working — modules
 * lighting up as they are chosen, then sliding across the rack and back. Java
 * drives it a step at a time; everything here runs on the render thread, where
 * the rack widget belongs.
 *
 * The rule that shapes all of it: the tour is a demonstration, not an edit. It
 * can be started at any moment from Help ▸ Interface tour, on top of whatever
 * the user is building. So nothing here touches history, nothing is left
 * selected, and a moved module is put back at exactly the coordinates it had --
 * restore() runs when the demo ends, when the tour is dismissed halfway, and
 * before the patch can be autosaved.
 */
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

#include <system.hpp>
#include <app/ModuleWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <math.hpp>

#include <jni.h>

#include "tour_demo.hpp"

using namespace rack;

namespace rackdroid {


namespace {

struct Moved {
	int64_t moduleId;
	math::Vec original;
};

struct TourDemo {
	// What Java has asked for, applied on the next render pass.
	std::atomic<int> request{0};
	// Modules we selected ourselves, so we clear exactly those and leave any
	// selection the user already had alone.
	std::vector<int64_t> selected;
	std::vector<Moved> moved;
	// Nudge animation, in seconds since it started; negative means idle. Timed
	// rather than counted in frames: the render loop is not capped at a known
	// rate, and the frame-stepped version of this finished in a fraction of a
	// second on a fast device -- the demonstration ran, but too fast to see.
	double nudgeStart = -1.0;
};

TourDemo g;

/** Up to `count` modules, nearest the middle of the screen first, so the demo
 * picks what the user can actually see rather than something off-screen. */
std::vector<app::ModuleWidget*> visibleModules(int count) {
	std::vector<app::ModuleWidget*> out;
	if (!APP->scene || !APP->scene->rack)
		return out;
	math::Vec centre = APP->scene->box.size.div(2.f);
	std::vector<std::pair<float, app::ModuleWidget*>> ranked;
	for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
		if (!mw)
			continue;
		math::Vec c = mw->getAbsoluteOffset(mw->box.size.div(2.f));
		if (c.x < 0.f || c.y < 0.f || c.x > APP->scene->box.size.x || c.y > APP->scene->box.size.y)
			continue;
		ranked.push_back({c.minus(centre).norm(), mw});
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

} // namespace


void tourDemoRequest(int what) {
	g.request.store(what);
}


void tourDemoRestore() {
	if (!APP || !APP->scene || !APP->scene->rack)
		return;
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
	g.nudgeStart = -1.0;
}


void processTourDemo() {
	if (!APP->scene || !APP->scene->rack)
		return;

	int request = g.request.exchange(0);
	switch (request) {
		case 1: { // light up a couple of modules
			tourDemoRestore();
			for (app::ModuleWidget* mw : visibleModules(2)) {
				APP->scene->rack->select(mw, true);
				if (mw->module)
					g.selected.push_back(mw->module->id);
			}
			break;
		}
		case 2: { // slide what is selected aside and back
			if (g.selected.empty())
				break;
			g.moved.clear();
			for (int64_t id : g.selected) {
				if (app::ModuleWidget* mw = findById(id))
					g.moved.push_back({id, mw->box.pos});
			}
			g.nudgeStart = system::getTime();
			break;
		}
		case 4: { // slide one module across, without selecting anything
				tourDemoRestore();
				std::vector<app::ModuleWidget*> mws = visibleModules(1);
				if (mws.empty() || !mws[0]->module)
					break;
				g.moved.push_back({mws[0]->module->id, mws[0]->box.pos});
				g.nudgeStart = system::getTime();
				break;
			}
		case 3: // put everything back
			tourDemoRestore();
			break;
		default:
			break;
	}

	if (g.nudgeStart < 0.0 || g.moved.empty())
		return;

	// Out, a pause at the far end, and back: slow enough to read as a
	// deliberate move rather than a glitch, and about a third of a module's
	// height, so the module never leaves the screen.
	const double OUT = 0.75, HOLD = 0.40, BACK = 0.75;
	const float DISTANCE = 45.f;
	double t = system::getTime() - g.nudgeStart;

	if (t >= OUT + HOLD + BACK) {
		// Home again: the exact original coordinates, not an interpolated
		// value, so repeating the demonstration cannot drift the patch.
		for (const Moved& m : g.moved) {
			if (app::ModuleWidget* mw = findById(m.moduleId))
				mw->box.pos = m.original;
		}
		g.moved.clear();
		g.nudgeStart = -1.0;
		return;
	}

	double phase;
	if (t < OUT)
		phase = t / OUT;                       // sliding away
	else if (t < OUT + HOLD)
		phase = 1.0;                           // held there
	else
		phase = 1.0 - (t - OUT - HOLD) / BACK; // sliding back
	float eased = (float) (0.5 - 0.5 * std::cos(phase * 3.14159265));
	for (const Moved& m : g.moved) {
		if (app::ModuleWidget* mw = findById(m.moduleId))
			mw->box.pos = m.original.plus(math::Vec(0.f, DISTANCE * eased));
	}
}


} // namespace rackdroid


extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeTourDemo(JNIEnv*, jobject, jint what) {
	rackdroid::tourDemoRequest(what);
}
