#pragma once
#include <string>

struct AAssetManager;

namespace rackdroid {

/** Extracts assets/system.zip from the APK into `systemDir` (Rack's res/,
Core.json, template.vcv...). Skips work if the installed version marker
already matches. Returns true on success. */
bool extractSystemAssets(AAssetManager* am, const std::string& systemDir);

/** Extracts assets/thumbnails.zip (module browser tile art, see
app/build.gradle.kts' packThumbnailAssets and native/host/main_ui_host.cpp's
--export-thumbnails) into `thumbsDir`. Same skip-if-unchanged behavior as
extractSystemAssets, tracked independently so a system.zip-only update
doesn't force a ~8MB re-extraction and vice versa. */
bool extractThumbnailAssets(AAssetManager* am, const std::string& thumbsDir);

/** Copies the bundled demo patches (systemDir/patches/*.vcv) into the user
patches dir, skipping files that already exist there. */
void seedDemoPatches(const std::string& systemDir, const std::string& userDir);

} // namespace rackdroid
