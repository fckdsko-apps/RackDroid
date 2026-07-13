#pragma once

namespace rackdroid {

/** Per-frame poll: detects Rack's own module browser opening (long-press on
 * empty rack space, Enter key) and redirects it to the native Android
 * browser sheet instead of the canvas UI. Call once per rendered frame,
 * alongside processNativeMenus(). */
void processNativeBrowser();

} // namespace rackdroid
