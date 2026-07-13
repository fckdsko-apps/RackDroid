#pragma once

namespace rackdroid {

/** Bridges Rack menus to native Android bottom sheets. Call every frame from
the render loop, before drawing. Representable menus are shown natively and
their canvas overlay hidden; menus with non-list widgets are left to the
canvas path. */
void processNativeMenus();

} // namespace rackdroid
