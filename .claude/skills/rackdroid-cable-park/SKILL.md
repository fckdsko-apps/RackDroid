---
name: rackdroid-cable-park
description: Use when working on RackDroid's cable parking bar — the left-hand rail of holes where a cable end waits while you scroll the rack ("cable park", "parcheggio cavi", "native/port/cable_park.cpp", parking a cable, connecting from a hole).
version: 0.1.0
---

# Cable parking bar (`native/port/cable_park.cpp`)

You cannot pan the rack while dragging a cable, so wiring two modules that are
never on screen together was close to impossible on a phone. Drop a cable end
into a hole on the left, scroll wherever you like, then drag it back out onto
the target port. Toggled from a toolbar button (`nativeSetCableParkVisible`).

**The bar grows on demand: `MIN_HOLES` (3) up to `MAX_SLOTS` (10).** `g_slots`
is sized to the max; `visibleHoles()` decides how many are shown each frame —
enough to keep every filled hole visible (min 3), plus one spare while a cable
is in flight and every visible hole is full. That spare is the hole the next end
drops into; without an in-flight incomplete cable it does not appear (so pulling
a parked end out, which is our own drag with no incomplete cable, never sprouts
one). Holes are anchored from the top so the first three stay centred and extra
ones grow downward — an existing hole must never shift under a finger mid-drop.
`cableParkSlotAt` only matches within `visibleHoles()`, so nothing off the bar
is droppable.

**The spare appears only once the cable is over the bar**, not the instant any
cable is grabbed. The touch layer reports the dragged end's position
(`cableParkSetInflightPos`), set on every MOVE **and on DOWN** — the DOWN anchor
matters: the cable Rack makes on the press is checked before the first MOVE, and
without a fresh position it kept the last drag's value (often over the bar right
after a park) and the spare flashed for a frame. `parkableOverBar()` gates the
spare on `g_inflightX <= CABLE_PARK_BAR_W`, the same edge that separates a
"cancel" from a "discard" (below).

**Pull-out drops discard in the void, cancel over the bar.** Carrying a parked
end off and letting go in the rack (`pos.x > CABLE_PARK_BAR_W`) clears its slot;
letting go back over the bar keeps it parked. A parked end whose module is
deleted is also dropped, in `step()`.

**Collapse handle.** A glass pill with a chevron at the top of the bar (matching
the toolbar's own collapse handle, `glassPill()`), `cableParkArrowAt` +
`cableParkToggleCollapsed`, checked before the hole grab. It **collapses to just
the pill** (`g_collapsed`) rather than hiding — the chevron flips "‹" ↔ "›" and
the pill stays at a fixed spot above hole 0 so it never jumps. Collapse is a
distinct state from the toolbar's visible/invisible toggle, so it does NOT need
to call back to Kotlin — the feature stays on while collapsed. (An earlier
version fully hid and used a `nativeCableParkHidden` callback to keep the toolbar
button in sync; collapsing removed the need.) While collapsed, `cableParkSlotAt`
returns -1 (nothing droppable), and `cableParkSetVisible(true)` always opens
expanded.

## How it works, and why that way

**A slot stores the port's IDENTITY, never a `PortWidget*`** — module id, port
id, direction, plus the module and port NAMES captured at park time. The module
can be deleted or the patch reloaded while the end waits; a stored pointer
would be a use-after-free at connect time, and names resolved on demand would
vanish. `resolvePort()` turns the identity back into a widget on demand and
simply fails when the module is gone.

**Rack's own drag is observed, never intercepted.** Releasing over a hole reads
which port the half-made cable came from (`getIncompleteCables()`, falling back
to `getDraggedWidget()` for cables that were already plugged in) and then lets
Rack discard its incomplete cable exactly as usual. No upstream file is
touched, which is the project's core rule.

**Connecting builds a `CableWidget` by hand**: set `inputPort`/`outputPort`,
call `updateCable()` (which registers it with the engine), then
`rack->addCable()`. Two things are easy to forget here:
- `cw->color` must be a real colour. Rack assigns one when the *user* drags a
  cable; a hand-built one gets a default-constructed `NVGcolor` and draws as a
  dead grey wire that reads as disabled. The slot claims its colour from
  `rack->getNextCableColor()` at **park** time, not at connect time, and the
  cable reuses it — so several parked ends are told apart at a glance, and what
  waits in the bar is the cable you actually get.
- A cable needs one input end and one output end; two of the same is refused.

**Drops snap to the nearest compatible port** (`cableParkNearestPort`) rather
than requiring a hit on the jack itself, and an incompatible jack under the
finger does not end the search. A fingertip covers several jacks; without this
a drop that looks on target lands on the module body and nothing happens.

**While a parked end is in flight, every compatible jack is ringed** in the
cable's colour, and the one a release would actually pick gets a thicker, opaque
ring. This is what turns the bar from guess-then-read-the-red-flash into
something you can aim.

**Both the rings and the snap distance are relative to the jack's on-screen
size** (`cableParkPortRadius`, `cableParkSnapRadius`), never a constant in scene
units. Scene units do not shrink with the rack's zoom, so a fixed radius is
correct at exactly one zoom level: zoomed out it drew a mesh of overlapping
rings that hid the panels entirely, and the snap distance grew past the spacing
between jacks, so any drop hit whatever was nearest. Derive the size by
measuring the port through the same transform that positions it
(`getAbsoluteOffset(Vec(0,0))` vs `getAbsoluteOffset(box.size)`) — that follows
zoom for free. `CABLE_PARK_SNAP_MIN` is the only absolute number, and it exists
for a physical reason: a fingertip cannot aim finer than that.

## The rule this feature keeps re-learning

**Every refusal must be visible on screen.** The bar flashes the slot red for a
second on any failed drop — wrong direction, already connected, no port within
reach, hole already taken, or a release over a hole while nothing was actually
in flight. Every bug report on this feature so far has actually been a correct
refusal that said nothing: from the outside, "refused for a good reason" and
"broken" look identical. If you add a new failure path, flash it too, and log
it with `LOGI`.

**`LOGI`, never bare `INFO`.** Both `cable_park.cpp` and `touch_input.cpp`
define a `LOGI` that writes to logcat *and* `INFO()`. A release build is not
debuggable, so `run-as` cannot read `user/log.txt`, while a user without a cable
cannot read logcat — a message in only one sink is invisible to somebody. The
park's own diagnostics sat in `INFO()` alone through a whole debugging session
and read, from logcat, exactly like code that never ran.

## Threading

`cableParkSetVisible` is called from Java's UI thread via JNI, so it touches
plain state ONLY. Reading `APP->scene` there crashed the app — Rack's context
is per-thread. Anything needing the context happens in `CableParkBar::step()`
/ `draw()`, on the render thread. See [[rackdroid-app-layer]].

The bar is a **Scene child**, not a RackWidget child, so it stays pinned to the
screen edge while the rack pans and zooms underneath.

## Verified on device

Park → pan → connect, three ends parked at once in three colours, the ring
highlighting, the target ring matching the port actually connected, the colour
carrying from the bar into the finished cable, and the refusal flash have all
been seen working on a Galaxy S22 (2026-07-22). What has **not** been checked:
parking across a patch reload or a module deletion — the identity-not-pointer
design exists precisely for that, and `cableParkConnect` has the "parked module
is gone" branch, but neither has ever been exercised.

Scripting this over adb is slow for one reason worth knowing: a jack is about
five pixels across on a zoomed-out rack, so a DOWN two pixels off grabs the
**module** and drags it across the patch instead. Re-derive jack centres from a
fresh screenshot every time (a magnified crop with a pixel grid is the quickest
way), and keep undo in reach.

## Related skills

- [[rackdroid-device-testing]] — cable drags need `input motionevent`, not
  `input swipe`.
- [[rackdroid-app-layer]] — the JNI threading rule this feature violated once.
