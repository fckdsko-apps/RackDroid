#pragma once

namespace rackdroid {

/** Loads the plugins bundled as shared libraries next to the app binary
(Fundamental, Bogaudio, ...) via dlopen, like desktop Rack. Call after
rack::plugin::init(). Each plugin's manifest and res/ must be present at
<systemDir>/plugins/<slug>/ (shipped in system.zip). */
void loadStaticPlugins();

} // namespace rackdroid
