---
name: rackdroid-cable-park
description: Use when working on RackDroid's cable parking bar — the left-hand rail of holes where a cable end waits while you scroll the rack ("cable park", "parcheggio cavi", "native/port/cable_park.cpp", parking a cable, connecting from a hole).
version: 0.1.0
---

# Cable parking bar (`native/port/cable_park.cpp`)

You cannot pan the rack while dragging a cable, so wiring two modules that are
never on screen together was close to impossible on a phone. Drop a cable end
into one of five holes on the left, scroll wherever you like, then drag it back
out onto the target port. Toggled from a toolbar button
(`nativeSetCableParkVisible`).

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
- `cw->color` must be set from `rack->getNextCableColor()`. Rack assigns the
  colour when the *user* drags a cable; a hand-built one gets a
  default-constructed `NVGcolor` and draws as a dead grey wire that reads as
  disabled.
- A cable needs one input end and one output end; two of the same is refused.

**Drops snap to the nearest compatible port** (`cableParkNearestPort`) rather
than requiring a hit on the jack itself, and an incompatible jack under the
finger does not end the search. A fingertip covers several jacks; without this
a drop that looks on target lands on the module body and nothing happens.

## The rule this feature keeps re-learning

**Every refusal must be visible on screen.** The bar flashes the slot red for a
second on any failed drop — wrong direction, already connected, no port within
reach. Every bug report on this feature so far has actually been a correct
refusal that said nothing: from the outside, "refused for a good reason" and
"broken" look identical. If you add a new failure path, flash it too, and log
it with `LOGI` (which writes both to logcat and to `user/log.txt`).

## Threading

`cableParkSetVisible` is called from Java's UI thread via JNI, so it touches
plain state ONLY. Reading `APP->scene` there crashed the app — Rack's context
is per-thread. Anything needing the context happens in `CableParkBar::step()`
/ `draw()`, on the render thread. See [[rackdroid-app-layer]].

The bar is a **Scene child**, not a RackWidget child, so it stays pinned to the
screen edge while the rack pans and zooms underneath.

## Known gap

While dragging, **compatible ports are not highlighted** — the user cannot see
where a drop would succeed and has to guess, then read the red flash. Lighting
up valid targets during the drag is the change that would make this feature
usable rather than guessable. `cableParkNearestPort` already knows what
compatible means; the work is per-frame drawing over visible ports, which needs
care with a real-time audio engine running.

## Related skills

- [[rackdroid-device-testing]] — cable drags need `input motionevent`, not
  `input swipe`.
- [[rackdroid-app-layer]] — the JNI threading rule this feature violated once.
