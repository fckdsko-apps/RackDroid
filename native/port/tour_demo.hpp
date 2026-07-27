#pragma once

namespace rackdroid {

/** Step the interface tour's live demonstration. Call every frame from the
render loop; does nothing unless Java has asked for something. */
void processTourDemo();

/** Queue a demonstration. Each one starts by bringing the modules it uses into
view:
    1  light up a couple of modules (red halo)
    2  slide the lit modules aside and back
    3  undo everything the demonstrations did
    4  slide a single module aside and back
    5  just centre the view on the modules
    6  zoom in and out, then scroll sideways and back
    7  draw a cable from an output to an input, jacks lighting up
Safe from the UI thread -- it only sets an atomic, and the work happens on the
render thread. */
void tourDemoRequest(int what);

/** Put back anything the demonstrations changed: module positions restored to
the exact coordinates they had, only the modules the demo selected deselected,
the half-made cable dropped, and the view returned to the offset and zoom the
user left it at. Called when the tour ends or is dismissed, and before any
autosave. */
void tourDemoRestore();

/** Modules on the rack, from the count the render thread republishes every
frame. Safe from any thread; the tour uses it to leave out the steps it could
not demonstrate on an empty rack. */
int rackModuleCount();

} // namespace rackdroid
