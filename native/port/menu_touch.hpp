#pragma once

namespace rackdroid {

/** Restyles all open Rack menus for touch (min width, drill-down submenus,
on-screen, leaf-only visibility). Call every frame after the scene steps and
before drawing. */
void fixupMenus();

} // namespace rackdroid
