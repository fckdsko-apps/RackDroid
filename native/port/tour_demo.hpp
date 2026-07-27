#pragma once

namespace rackdroid {

/** Step the interface tour's live demonstration. Call every frame from the
render loop; does nothing unless Java has asked for something. */
void processTourDemo();

/** Queue a demonstration: 1 = select a couple of visible modules, 2 = slide the
selection aside and back, 3 = undo everything the demo did, 4 = slide a single
module aside and back without selecting it. Safe from the UI
thread -- it only sets an atomic, and the work happens on the render thread. */
void tourDemoRequest(int what);

/** Put back anything the demo changed: module positions restored to the exact
coordinates they had, and only the modules the demo selected deselected. Called
when the tour ends or is dismissed, and before the patch is saved. */
void tourDemoRestore();

} // namespace rackdroid
