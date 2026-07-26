/* Halo around the selected modules.
 *
 * Upstream marks a selected module by washing its whole panel in 25% red, which
 * buries the very artwork the selection is pointing at -- and on a touch screen,
 * where selecting is a normal part of editing, that wash is on screen a lot. The
 * wash is patched out at build time (see native/CMakeLists.txt, which keeps the
 * thin outline) and replaced by this overlay: the same soft box gradient Rack
 * already uses for a dragged module's shadow, tinted red and drawn OUTSIDE the
 * panel, so the module stays readable while still reading as picked.
 *
 * Drawn as a scene child on top of the rack, with the module's own rectangle
 * punched out as a hole -- so nothing is painted over the panel itself.
 */
#include <set>

#include <app/ModuleWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <widget/Widget.hpp>

#include <nanovg.h>

#include "selection_glow.hpp"

using namespace rack;

namespace rackdroid {


// Screen units at 100% zoom; scaled with the rack so the halo keeps its
// proportions when zoomed out (a fixed spread turns into a red fog).
static const float GLOW_SPREAD = 15.f;
static const float GLOW_CORNER = 5.f;


struct SelectionGlow : widget::Widget {
	void step() override {
		// Follows rotation/resize, like the cable bar.
		if (APP->scene)
			box.size = APP->scene->box.size;
		widget::Widget::step();
	}

	void draw(const DrawArgs& args) override {
		if (!APP->scene || !APP->scene->rack)
			return;
		for (app::ModuleWidget* mw : APP->scene->rack->getSelected()) {
			if (!mw || mw->box.size.x <= 0.f)
				continue;
			// Absolute, zoom-scaled rectangle: getAbsoluteOffset walks the
			// parent transforms, so this is where the panel really is on screen.
			math::Vec o = mw->getAbsoluteOffset(math::Vec(0.f, 0.f));
			math::Vec e = mw->getAbsoluteOffset(mw->box.size);
			float w = e.x - o.x, h = e.y - o.y;
			if (w <= 0.f || h <= 0.f)
				continue;
			float scale = w / mw->box.size.x;
			float spread = GLOW_SPREAD * scale;
			if (spread < 2.f)
				spread = 2.f;
			// Off-screen modules cost a gradient each; a big patch has plenty.
			if (o.x - spread > box.size.x || o.y - spread > box.size.y ||
				o.x + w + spread < 0.f || o.y + h + spread < 0.f)
				continue;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, o.x - spread, o.y - spread,
				w + spread * 2.f, h + spread * 2.f);
			// The panel itself is a hole: the halo is only ever outside it.
			nvgRect(args.vg, o.x, o.y, w, h);
			nvgPathWinding(args.vg, NVG_HOLE);
			nvgFillPaint(args.vg, nvgBoxGradient(args.vg, o.x, o.y, w, h,
				GLOW_CORNER * scale, spread,
				nvgRGBA(0xFF, 0x3B, 0x30, 0xE0), nvgRGBA(0xFF, 0x3B, 0x30, 0x00)));
			nvgFill(args.vg);
		}
		widget::Widget::draw(args);
	}
};


static SelectionGlow* g_glow = NULL;


void installSelectionGlow() {
	if (g_glow || !APP->scene)
		return;
	g_glow = new SelectionGlow;
	g_glow->box.pos = math::Vec(0, 0);
	g_glow->box.size = APP->scene->box.size;
	APP->scene->addChild(g_glow);
}


} // namespace rackdroid
