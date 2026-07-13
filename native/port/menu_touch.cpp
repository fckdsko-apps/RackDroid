/* Touch restructuring of Rack menus.
 *
 * Rack menus are built for a mouse: rows are narrow (width == text width),
 * and submenus cascade to the RIGHT on hover — off the side of a phone
 * screen. This pass, run every frame after the scene steps, restyles every
 * open menu for touch WITHOUT patching upstream:
 *
 *  - minimum width, so a whole row is an easy tap target
 *  - drill-down: a submenu opens on top of its parent (same position), and
 *    only the deepest (leaf) menu of a chain is shown — like an Android
 *    nested menu, instead of an off-screen cascade
 *  - kept fully on screen
 *
 * Safe because Rack dispatches a menu item's action to the item the touch
 * STARTED on (drag origin), not wherever the finger lifts — so a submenu
 * appearing under the finger can't be mis-triggered.
 *
 * All menus in a chain are direct children of the same MenuOverlay (see
 * Menu::setChildMenu), added parent-before-child, so a single ordered pass
 * over the overlay's children updates parents before their children.
 */
#include <algorithm>

#include <context.hpp>
#include <app/Scene.hpp>
#include <ui/Menu.hpp>
#include <ui/MenuOverlay.hpp>
#include <widget/Widget.hpp>

#include "menu_touch.hpp"

using namespace rack;


namespace rackdroid {


void fixupMenus() {
	app::Scene* scene = APP->scene;
	if (!scene)
		return;
	float minW = std::min(340.f, scene->box.size.x * 0.92f);

	for (widget::Widget* c : scene->children) {
		ui::MenuOverlay* overlay = dynamic_cast<ui::MenuOverlay*>(c);
		if (!overlay)
			continue;
		for (widget::Widget* mc : overlay->children) {
			ui::Menu* menu = dynamic_cast<ui::Menu*>(mc);
			if (!menu)
				continue;

			// Widen the menu and all its rows for fat-finger tapping.
			if (menu->box.size.x < minW) {
				menu->box.size.x = minW;
				for (widget::Widget* item : menu->children)
					item->box.size.x = minW;
			}

			// Drill-down: a submenu takes its parent's position (parents are
			// processed first, so this reads an already-updated position).
			if (menu->parentMenu)
				menu->box.pos = menu->parentMenu->box.pos;

			// Keep on screen.
			menu->box = menu->box.nudge(scene->box.zeroPos());

			// Only the leaf (no open child) of the chain is visible.
			menu->visible = (menu->childMenu == NULL);
		}
	}
}


} // namespace rackdroid
