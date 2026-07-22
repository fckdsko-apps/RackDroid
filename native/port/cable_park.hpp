#pragma once

namespace rack { namespace app { struct PortWidget; } namespace math { struct Vec; } }

namespace rackdroid {

/** Cable parking bar.
 *
 * On a phone you cannot pan the rack while dragging a cable, so wiring two
 * modules that are not on screen together at the same time is close to
 * impossible. This bar gives a cable end somewhere to wait: drop it in one of
 * the holes, pan wherever you like, then drag it out onto the target port.
 *
 * Five holes, because that is enough for the "route a voice through several
 * modules" case without the bar eating the screen.
 *
 * Everything here lives in the port layer -- Rack's own drag is left alone
 * and only observed, so no upstream file is touched. */

/** Adds the bar widget to the scene. Call once, after the Scene exists. */
void installCableParkBar();

void cableParkSetVisible(bool visible);
bool cableParkVisible();

/** Index of the hole under a screen position, or -1. */
int cableParkSlotAt(float x, float y);
/** True if that hole currently holds a cable end. */
bool cableParkSlotFilled(int slot);

/** Records the port an in-flight cable drag came from into `slot`.
Returns false if the slot is taken or there is no cable being dragged. */
bool cableParkStore(int slot, rack::app::PortWidget* port);

/** Direction of the end parked in `slot`: engine::Port::INPUT (0) or
OUTPUT (1), or -1 when the slot is empty. */
int cableParkSlotType(int slot);

/** Nearest port of `wantType` (engine::Port::INPUT/OUTPUT) within `radius` of
a screen position, or NULL. A jack is a few units across and a fingertip is
not: without this, a drop that looks on target lands on the module body and
nothing happens. */
rack::app::PortWidget* cableParkNearestPort(float x, float y, int wantType, float radius);

/** Connects the end parked in `slot` to `target`, clearing the slot.
Returns false if the two ports cannot form a cable (same direction, missing
module, already connected). */
bool cableParkConnect(int slot, rack::app::PortWidget* target);

/** Flash a hole red: the drop was refused or found nothing. Feedback the user
can see, since the reason otherwise only exists in the log. */
void cableParkFlashRefused(int slot);

void cableParkClear(int slot);

/** While the user drags a parked end out of the bar, so the bar can draw the
line to the finger. Pass -1 to stop. */
void cableParkSetDragging(int slot, float x, float y);
int cableParkDraggingSlot();

} // namespace rackdroid
