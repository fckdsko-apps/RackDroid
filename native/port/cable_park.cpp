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


static const int SLOT_COUNT = 5;
// Screen units (Rack's own, i.e. pre-pixelRatio) -- the Scene is laid out in
// them, so the bar keeps its size on every density.
static const float BAR_W = 46.f;
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

static Slot g_slots[SLOT_COUNT];
static bool g_visible = false;
static int g_dragSlot = -1;
// Slot to flash red, and when it started: a refusal the user cannot see is
// indistinguishable from a feature that does not work.
static int g_refuseSlot = -1;
static double g_refuseTime = 0.0;
static float g_dragX = 0.f, g_dragY = 0.f;


/** Vertical centre of hole `i`, in screen units. */
static float holeY(int i) {
	float h = APP->scene ? APP->scene->box.size.y : 800.f;
	float total = (SLOT_COUNT - 1) * HOLE_GAP;
	return h * 0.5f - total * 0.5f + i * HOLE_GAP;
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
		widget::Widget::step();
	}

	void draw(const DrawArgs& args) override {
		if (!g_visible)
			return;
		float h = box.size.y;
		// Bar body: same smoked-glass slab as the Kotlin surfaces.
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 4.f, h * 0.5f - (SLOT_COUNT * HOLE_GAP) * 0.5f - 10.f,
			BAR_W - 8.f, SLOT_COUNT * HOLE_GAP + 20.f, 16.f);
		nvgFillColor(args.vg, nvgRGBA(0x1B, 0x18, 0x13, 0xE0));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0x18));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		for (int i = 0; i < SLOT_COUNT; i++) {
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
			NVGcolor ring = g_slots[i].filled()
				? g_slots[i].color : nvgRGBA(0x8A, 0x81, 0x73, 0x90);
			if (refuse > 0.f)
				ring = nvgLerpRGBA(ring, nvgRGB(0xE0, 0x50, 0x40), refuse);
			nvgStrokeColor(args.vg, ring);
			nvgStrokeWidth(args.vg, g_slots[i].filled() ? 2.5f + refuse * 2.f : 1.5f);
			nvgStroke(args.vg);

			if (g_slots[i].filled()) {
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
		for (int i = 0; i < SLOT_COUNT; i++) {
			if (!g_slots[i].filled())
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

		// Cable being pulled out of a hole towards the finger.
		if (g_dragSlot >= 0 && g_slots[g_dragSlot].filled()) {
			float cx = holeX(), cy = holeY(g_dragSlot);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, cx, cy);
			// Slack in the middle, like Rack's own cables.
			float mx = (cx + g_dragX) * 0.5f;
			float my = (cy + g_dragY) * 0.5f + 40.f;
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
	for (int i = 0; i < SLOT_COUNT; i++) {
		float dy = y - holeY(i);
		float dx = x - holeX();
		// Generous radius: fingers are not mouse pointers.
		if (dx * dx + dy * dy <= (HOLE_R + 9.f) * (HOLE_R + 9.f))
			return i;
	}
	return -1;
}

bool cableParkSlotFilled(int slot) {
	return slot >= 0 && slot < SLOT_COUNT && g_slots[slot].filled();
}


bool cableParkStore(int slot, app::PortWidget* port) {
	if (slot < 0 || slot >= SLOT_COUNT || !port || !port->module)
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
	LOGI("parked %s port %d of module %lld in slot %d",
		port->type == engine::Port::INPUT ? "input" : "output",
		port->portId, (long long) port->module->id, slot);
	return true;
}


int cableParkSlotType(int slot) {
	if (slot < 0 || slot >= SLOT_COUNT || !g_slots[slot].filled())
		return -1;
	return g_slots[slot].type;
}


app::PortWidget* cableParkNearestPort(float x, float y, int wantType, float radius) {
	if (!APP->scene || !APP->scene->rack)
		return NULL;
	app::PortWidget* best = NULL;
	float bestDist = radius * radius;
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
			if (d < bestDist) {
				bestDist = d;
				best = pw;
			}
		}
	}
	return best;
}


bool cableParkConnect(int slot, app::PortWidget* target) {
	if (slot < 0 || slot >= SLOT_COUNT || !target || !target->module)
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
		LOGI("refused: those ports are already connected");
		g_refuseSlot = slot;
		g_refuseTime = rack::system::getTime();
		return false;
	}
	app::CableWidget* cw = new app::CableWidget;
	// Rack picks the colour when the USER drags a cable; building one by hand
	// skips that, and the default-constructed NVGcolor drew as a dead grey
	// wire that looked disabled. Take the same next colour from the rotation.
	cw->color = APP->scene->rack->getNextCableColor();
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
	LOGI("connected slot %d", slot);
	return true;
}


void cableParkFlashRefused(int slot) {
	if (slot < 0 || slot >= SLOT_COUNT)
		return;
	g_refuseSlot = slot;
	g_refuseTime = rack::system::getTime();
}


void cableParkClear(int slot) {
	if (slot < 0 || slot >= SLOT_COUNT)
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


} // namespace rackdroid


extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeSetCableParkVisible(JNIEnv*, jobject, jboolean visible) {
	rackdroid::cableParkSetVisible(visible);
}

