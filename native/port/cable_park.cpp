/* Cable parking bar -- see cable_park.hpp for what it is for.
 *
 * Implementation notes:
 *
 * A slot stores the port's IDENTITY (module id + port id + direction), never a
 * PortWidget*. The widget behind a port can be destroyed while the end waits
 * in the bar -- the user is free to delete the module, or reload the patch --
 * and a dangling pointer would be a use-after-free at connect time. Resolving
 * the id back to a widget on demand simply fails when the module is gone.
 *
 * The bar is a Scene child, not a RackWidget child: it must stay pinned to the
 * screen edge while the rack pans and zooms underneath it.
 */
#include <app/CableWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <engine/Engine.hpp>
#include <engine/Module.hpp>
#include <widget/Widget.hpp>
#include <window/Window.hpp>
#include <asset.hpp>
#include <engine/PortInfo.hpp>
#include <plugin/Model.hpp>
#include <memory>
#include <string>

#include <android/log.h>
#include <logger.hpp>
#include <system.hpp>
#include <jni.h>

#include "cable_park.hpp"

// Both sinks, like the other port files: logcat needs adb, while INFO() lands
// in user/log.txt, which the in-app viewer and the Documents export can reach.
// Diagnostics only a developer with a cable can read are diagnostics nobody
// reads.
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, "rackdroid.cablepark", __VA_ARGS__); INFO(__VA_ARGS__); } while (0)

using namespace rack;

namespace rackdroid {


// The bar starts with MIN_HOLES and grows a hole at a time, up to MAX_SLOTS, as
// they fill -- a spare hole is revealed while a cable is in flight and every
// visible hole is taken, so there is always somewhere to drop the next end.
static const int MAX_SLOTS = 10;
static const int MIN_HOLES = 3;
// Screen units (Rack's own, i.e. pre-pixelRatio) -- the Scene is laid out in
// them, so the bar keeps its size on every density.
static const float BAR_W = CABLE_PARK_BAR_W;
static const float HOLE_R = 13.f;
static const float HOLE_GAP = 46.f;

struct Slot {
	int64_t moduleId = -1;
	int portId = -1;
	int type = -1;              // engine::Port::INPUT / OUTPUT
	// Captured when parking: the module can be renamed or deleted while the
	// end waits, and the label should still say what you put in there.
	std::string moduleName;
	std::string portName;
	NVGcolor color = nvgRGB(0xC8, 0x98, 0x5C);
	bool filled() const { return moduleId >= 0; }
};

static Slot g_slots[MAX_SLOTS];
static bool g_visible = false;
static int g_dragSlot = -1;
// Slot to flash red, and when it started: a refusal the user cannot see is
// indistinguishable from a feature that does not work.
static int g_refuseSlot = -1;
static double g_refuseTime = 0.0;
static float g_dragX = 0.f, g_dragY = 0.f;


// Where the in-flight cable end currently is, in screen units, reported by the
// touch layer while a left drag is active. Used to reveal the spare hole only
// once the cable is actually over the bar.
static float g_inflightX = 1e6f, g_inflightY = 0.f;

/** True while a cable end is being dragged that could be parked -- Rack keeps it
as an incomplete cable. Pulling a parked end back out is our own drag and leaves
no incomplete cable, so it does not count (you need no spare hole to pull out). */
static bool parkableCableInFlight() {
	return APP->scene && APP->scene->rack &&
		!APP->scene->rack->getIncompleteCables().empty();
}

/** True when a parkable cable is being dragged AND its end is over the bar. The
spare hole is an invitation to drop right here, so it should only appear once the
user brings the cable onto the bar -- not every time any cable moves anywhere. */
static bool parkableOverBar() {
	return parkableCableInFlight() && g_inflightX <= BAR_W;
}

/** How many holes to show right now: MIN_HOLES, grown to keep every filled hole
visible, and grown one further (up to MAX_SLOTS) while a cable is dragged onto
the bar and every visible hole is full -- that extra hole is the drop target. */
static int visibleHoles() {
	int highest = -1;
	for (int i = 0; i < MAX_SLOTS; i++)
		if (g_slots[i].filled())
			highest = i;
	int n = highest + 1;
	if (n < MIN_HOLES)
		n = MIN_HOLES;
	if (n < MAX_SLOTS && parkableOverBar()) {
		bool allFull = true;
		for (int i = 0; i < n; i++)
			if (!g_slots[i].filled()) { allFull = false; break; }
		if (allFull)
			n++;
	}
	return n;
}

/** Vertical centre of hole `i`, in screen units. Hole 0 is anchored so the
first MIN_HOLES sit centred on screen; extra holes grow downward and never shift
the ones above them (a moving target mid-drop is worse than an off-centre bar). */
static float holeY(int i) {
	float h = APP->scene ? APP->scene->box.size.y : 800.f;
	float first = h * 0.5f - (MIN_HOLES - 1) * HOLE_GAP * 0.5f;
	return first + i * HOLE_GAP;
}

static float holeX() { return BAR_W * 0.5f; }


static app::PortWidget* resolvePort(const Slot& s) {
	if (!s.filled() || !APP->scene || !APP->scene->rack)
		return NULL;
	app::ModuleWidget* mw = APP->scene->rack->getModule(s.moduleId);
	if (!mw)
		return NULL;
	return s.type == engine::Port::INPUT ? mw->getInput(s.portId) : mw->getOutput(s.portId);
}


/** Geomini if the assets are there, DejaVu otherwise -- same fallback the
panel label overlay uses. */
static int labelFont(const widget::Widget::DrawArgs& args) {
	static std::shared_ptr<window::Font> font;
	if (!font)
		font = APP->window->loadFont(rack::asset::system("res/fonts/Geomini.ttf"));
	if (!font || !font->handle)
		font = APP->window->loadFont(rack::asset::system("res/fonts/DejaVuSans.ttf"));
	return font && font->handle ? font->handle : 0;
}


struct CableParkBar : widget::Widget {
	void step() override {
		// Follows rotation/resize here, where the context is guaranteed.
		if (APP->scene)
			box.size = APP->scene->box.size;
		// Drop any parked end whose module has been deleted. It has nowhere to
		// return to, so leaving it in the hole just dangles a reference to a
		// module that no longer exists. Keyed on the module id, which survives a
		// patch reload (ids are restored from JSON), so a reload that destroys
		// and recreates the same modules does not clear these -- only a real
		// deletion (or New/empty patch) does. Widget stepping is single-threaded
		// and patch load is synchronous, so step() never runs mid-reload.
		if (APP->scene && APP->scene->rack) {
			for (int i = 0; i < MAX_SLOTS; i++) {
				if (g_slots[i].filled() &&
					!APP->scene->rack->getModule(g_slots[i].moduleId)) {
					LOGI("slot %d module %lld deleted; clearing parked end",
						i, (long long) g_slots[i].moduleId);
					cableParkClear(i);
				}
			}
		}
		widget::Widget::step();
	}

	void draw(const DrawArgs& args) override {
		if (!g_visible)
			return;
		int holes = visibleHoles();
		// Bar body: same smoked-glass slab as the Kotlin surfaces. Sized to the
		// currently visible holes so it grows and shrinks with them.
		float top = holeY(0) - HOLE_GAP * 0.5f - 10.f;
		float height = holes * HOLE_GAP + 20.f;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 4.f, top, BAR_W - 8.f, height, 16.f);
		nvgFillColor(args.vg, nvgRGBA(0x1B, 0x18, 0x13, 0xE0));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0x18));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		for (int i = 0; i < holes; i++) {
			float cx = holeX(), cy = holeY(i);
			// Jack: dark bore with a metal ring, mirroring the panel art so the
			// holes read as sockets rather than as buttons.
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, cx, cy, HOLE_R);
			nvgFillColor(args.vg, nvgRGB(0x14, 0x12, 0x0E));
			nvgFill(args.vg);
			float refuse = 0.f;
			if (i == g_refuseSlot) {
				double age = rack::system::getTime() - g_refuseTime;
				refuse = age < 1.0 ? (float) (1.0 - age) : 0.f;
			}
			// A slot being dragged out has left its hole -- the end is on the
			// finger now, so the hole must read as empty. Drawing the resting
			// plug here as well as the in-flight cable below is what made a
			// pulled-out end look doubled.
			bool resting = g_slots[i].filled() && i != g_dragSlot;
			NVGcolor ring = resting
				? g_slots[i].color : nvgRGBA(0x8A, 0x81, 0x73, 0x90);
			if (refuse > 0.f)
				ring = nvgLerpRGBA(ring, nvgRGB(0xE0, 0x50, 0x40), refuse);
			nvgStrokeColor(args.vg, ring);
			nvgStrokeWidth(args.vg, resting ? 2.5f + refuse * 2.f : 1.5f);
			nvgStroke(args.vg);

			if (resting) {
				// Plug sitting in the hole.
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, cx, cy, HOLE_R * 0.45f);
				nvgFillColor(args.vg, g_slots[i].color);
				nvgFill(args.vg);
			}
		}

		// Cable still attached to the port it came from. Drawn every frame from
		// the port's CURRENT position, so it follows the rack as you pan and
		// zoom -- that motion is the whole point: it shows you where the end
		// belongs while you go looking for its destination.
		nvgFontSize(args.vg, 8.f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < MAX_SLOTS; i++) {
			// The dragged slot's cable is drawn to the finger below, not to its
			// hole -- skip its resting stub and label so it does not read as a
			// second cable branching from the hole.
			if (!g_slots[i].filled() || i == g_dragSlot)
				continue;
			float cx = holeX(), cy = holeY(i);
			app::PortWidget* pw = resolvePort(g_slots[i]);
			if (pw) {
				math::Vec e = pw->getAbsoluteOffset(pw->box.size.div(2.f));
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, cx, cy);
				nvgQuadTo(args.vg, (cx + e.x) * 0.5f, (cy + e.y) * 0.5f + 45.f, e.x, e.y);
				nvgStrokeColor(args.vg, nvgTransRGBA(g_slots[i].color, 0xB0));
				nvgStrokeWidth(args.vg, 4.f);
				nvgLineCap(args.vg, NVG_ROUND);
				nvgStroke(args.vg);
			}
			// Which module and which jack is waiting here.
			float tx = BAR_W + 8.f;
			nvgFontFaceId(args.vg, labelFont(args));
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 190));
			nvgText(args.vg, tx + 0.4f, cy - 5.f + 0.4f, g_slots[i].moduleName.c_str(), NULL);
			nvgFillColor(args.vg, nvgRGB(0xED, 0xE6, 0xD8));
			nvgText(args.vg, tx, cy - 5.f, g_slots[i].moduleName.c_str(), NULL);
			if (!g_slots[i].portName.empty()) {
				std::string sub = g_slots[i].portName +
					(g_slots[i].type == engine::Port::INPUT ? " in" : " out");
				nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 190));
				nvgText(args.vg, tx + 0.4f, cy + 6.f + 0.4f, sub.c_str(), NULL);
				nvgFillColor(args.vg, g_slots[i].color);
				nvgText(args.vg, tx, cy + 6.f, sub.c_str(), NULL);
			}
		}

		// While a parked end is in flight, light up every jack that could take
		// it. Without this the user aims blind and only learns the answer from
		// the red flash AFTER letting go -- every bug report on this feature so
		// far was a correct refusal that simply could not be seen coming.
		if (g_dragSlot >= 0 && g_slots[g_dragSlot].filled() && APP->scene->rack) {
			int want = g_slots[g_dragSlot].type == engine::Port::INPUT
				? engine::Port::OUTPUT : engine::Port::INPUT;
			// The one that would actually be chosen right now, so the strong
			// ring is a promise rather than a hint.
			app::PortWidget* pick =
				cableParkNearestPort(g_dragX, g_dragY, want);
			NVGcolor tint = g_slots[g_dragSlot].color;
			for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
				if (!mw)
					continue;
				for (app::PortWidget* pw : mw->getPorts()) {
					if (!pw || pw->type != want)
						continue;
					math::Vec c = pw->getAbsoluteOffset(pw->box.size.div(2.f));
					// Cull off-screen jacks: a big patch has hundreds and this
					// runs every frame alongside a real-time audio engine.
					if (c.x < -20.f || c.y < -20.f ||
						c.x > box.size.x + 20.f || c.y > box.size.y + 20.f)
						continue;
					bool chosen = (pw == pick);
					// Ring the jack at its actual on-screen size. With a fixed
					// radius the rings overlap into a mesh as soon as the rack
					// is zoomed out, and the panel underneath disappears.
					float r = cableParkPortRadius(pw);
					nvgBeginPath(args.vg);
					nvgCircle(args.vg, c.x, c.y, chosen ? r * 1.7f : r * 1.15f);
					nvgStrokeColor(args.vg, nvgTransRGBA(tint, chosen ? 0xFF : 0x55));
					nvgStrokeWidth(args.vg, chosen ? r * 0.3f : r * 0.14f);
					nvgStroke(args.vg);
				}
			}
		}

		// The end being pulled out, drawn as ONE cable from its fixed source
		// port to the finger. It starts at the source, not the hole: the loose
		// end has lifted OUT of the hole and is following the finger, so the
		// hole is empty and there is a single strand, not two meeting at the
		// hole (which is what read as the cable "doubling").
		if (g_dragSlot >= 0 && g_slots[g_dragSlot].filled()) {
			app::PortWidget* pw = resolvePort(g_slots[g_dragSlot]);
			math::Vec start = pw
				? pw->getAbsoluteOffset(pw->box.size.div(2.f))
				: math::Vec(holeX(), holeY(g_dragSlot));
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, start.x, start.y);
			// Slack in the middle, like Rack's own cables.
			float mx = (start.x + g_dragX) * 0.5f;
			float my = (start.y + g_dragY) * 0.5f + 40.f;
			nvgQuadTo(args.vg, mx, my, g_dragX, g_dragY);
			nvgStrokeColor(args.vg, g_slots[g_dragSlot].color);
			nvgStrokeWidth(args.vg, 5.f);
			nvgLineCap(args.vg, NVG_ROUND);
			nvgStroke(args.vg);
		}
	}
};

static CableParkBar* g_bar = NULL;


void installCableParkBar() {
	if (g_bar || !APP->scene)
		return;
	g_bar = new CableParkBar;
	g_bar->box.pos = math::Vec(0, 0);
	g_bar->box.size = APP->scene->box.size;
	APP->scene->addChild(g_bar);
}


/** Callable from ANY thread, including Java's UI thread via JNI: it touches
plain state only. Rack's context is per-thread (see contextSet's own docs), so
APP is NULL on any thread that never had one set -- reading APP->scene here
dereferenced null and took the whole app down. Anything needing the context is
done by the widget below, which runs on the render thread. */
void cableParkSetVisible(bool visible) {
	g_visible = visible;
	if (!visible)
		g_dragSlot = -1;
}

bool cableParkVisible() { return g_visible; }


int cableParkSlotAt(float x, float y) {
	if (!g_visible || x > BAR_W)
		return -1;
	// Only the holes currently on screen are droppable; the spare that appears
	// while parking is included by visibleHoles() so it can be dropped onto.
	int holes = visibleHoles();
	for (int i = 0; i < holes; i++) {
		float dy = y - holeY(i);
		float dx = x - holeX();
		// Generous radius: fingers are not mouse pointers.
		if (dx * dx + dy * dy <= (HOLE_R + 9.f) * (HOLE_R + 9.f))
			return i;
	}
	return -1;
}

bool cableParkSlotFilled(int slot) {
	return slot >= 0 && slot < MAX_SLOTS && g_slots[slot].filled();
}


bool cableParkStore(int slot, app::PortWidget* port) {
	if (slot < 0 || slot >= MAX_SLOTS || !port || !port->module)
		return false;
	if (g_slots[slot].filled())
		return false;
	g_slots[slot].moduleId = port->module->id;
	g_slots[slot].portId = port->portId;
	g_slots[slot].type = port->type;
	g_slots[slot].moduleName = port->module->model ? port->module->model->name : "?";
	{
		engine::PortInfo* info = port->getPortInfo();
		g_slots[slot].portName = info ? info->getName() : "";
	}
	// Claim the colour now, not at connect time. Two ends waiting in identical
	// tan drew as the same cable twice, and the colour the bar showed was not
	// the colour the finished cable got.
	if (APP->scene && APP->scene->rack)
		g_slots[slot].color = APP->scene->rack->getNextCableColor();
	LOGI("parked %s port %d of module %lld in slot %d [complete cables now=%zu]",
		port->type == engine::Port::INPUT ? "input" : "output",
		port->portId, (long long) port->module->id, slot,
		APP->scene && APP->scene->rack ? APP->scene->rack->getCompleteCables().size() : 0);
	return true;
}


int cableParkSlotType(int slot) {
	if (slot < 0 || slot >= MAX_SLOTS || !g_slots[slot].filled())
		return -1;
	return g_slots[slot].type;
}


float cableParkPortRadius(app::PortWidget* pw) {
	math::Vec o = pw->getAbsoluteOffset(math::Vec(0.f, 0.f));
	math::Vec e = pw->getAbsoluteOffset(pw->box.size);
	float r = (e.x - o.x) * 0.5f;
	return r > 3.f ? r : 3.f;
}


app::PortWidget* cableParkNearestPort(float x, float y, int wantType) {
	if (!APP->scene || !APP->scene->rack)
		return NULL;
	app::PortWidget* best = NULL;
	float bestDist = -1.f;
	for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
		if (!mw)
			continue;
		for (app::PortWidget* pw : mw->getPorts()) {
			if (!pw || pw->type != wantType)
				continue;
			// Centre of the jack in screen units.
			math::Vec c = pw->getAbsoluteOffset(pw->box.size.div(2.f));
			float dx = c.x - x, dy = c.y - y;
			float d = dx * dx + dy * dy;
			float reach = cableParkSnapRadius(pw);
			if (d > reach * reach)
				continue;
			if (bestDist < 0.f || d < bestDist) {
				bestDist = d;
				best = pw;
			}
		}
	}
	return best;
}


bool cableParkConnect(int slot, app::PortWidget* target) {
	if (slot < 0 || slot >= MAX_SLOTS || !target || !target->module)
		return false;
	const Slot& s = g_slots[slot];
	if (!s.filled())
		return false;
	// A cable needs one input end and one output end.
	if (s.type == target->type) {
		// A cable runs from an output to an input; two of the same cannot pair.
		LOGI("refused: both ends are %s", s.type == engine::Port::INPUT ? "inputs" : "outputs");
		g_refuseSlot = slot;
		g_refuseTime = rack::system::getTime();
		return false;
	}
	app::PortWidget* parked = resolvePort(s);
	if (!parked) {
		LOGI("parked module %lld is gone; dropping slot %d", (long long) s.moduleId, slot);
		cableParkClear(slot);
		return false;
	}
	app::PortWidget* in = s.type == engine::Port::INPUT ? parked : target;
	app::PortWidget* out = s.type == engine::Port::OUTPUT ? parked : target;
	if (APP->scene->rack->getCable(out, in)) {
		// The link the user is dropping this end onto already exists, so the
		// end has effectively reached its destination. Empty the hole rather
		// than flashing and keeping it: a parked end that will not leave the
		// bar even though its cable is right there reads as a duplicate.
		LOGI("slot %d target already connected; clearing", slot);
		cableParkClear(slot);
		return true;
	}
	app::CableWidget* cw = new app::CableWidget;
	// Rack picks the colour when the USER drags a cable; building one by hand
	// skips that, and the default-constructed NVGcolor drew as a dead grey wire
	// that looked disabled. Reuse the colour claimed at park time so the cable
	// the user was looking at in the bar is the cable they end up with.
	cw->color = s.color;
	cw->inputPort = in;
	cw->outputPort = out;
	// Registers the cable with the engine from the two ports.
	cw->updateCable();
	if (!cw->isComplete()) {
		delete cw;
		return false;
	}
	APP->scene->rack->addCable(cw);
	cableParkClear(slot);
	LOGI("connected slot %d [complete cables now=%zu]",
		slot, APP->scene->rack->getCompleteCables().size());
	return true;
}


void cableParkFlashRefused(int slot) {
	if (slot < 0 || slot >= MAX_SLOTS)
		return;
	g_refuseSlot = slot;
	g_refuseTime = rack::system::getTime();
}


void cableParkClear(int slot) {
	if (slot < 0 || slot >= MAX_SLOTS)
		return;
	g_slots[slot] = Slot();
	if (g_dragSlot == slot)
		g_dragSlot = -1;
}


void cableParkSetDragging(int slot, float x, float y) {
	g_dragSlot = slot;
	g_dragX = x;
	g_dragY = y;
}

int cableParkDraggingSlot() { return g_dragSlot; }

void cableParkSetInflightPos(float x, float y) {
	g_inflightX = x;
	g_inflightY = y;
}


} // namespace rackdroid


extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeSetCableParkVisible(JNIEnv*, jobject, jboolean visible) {
	rackdroid::cableParkSetVisible(visible);
}

