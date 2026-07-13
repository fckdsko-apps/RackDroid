import java.util.Properties

plugins {
	id("com.android.application")
	id("org.jetbrains.kotlin.android")
}

// Rack's system assets (res/, Core.json, template.vcv) are packed into a
// single zip because AAssetDir cannot enumerate subdirectories at runtime.
// native/port/asset_extract.cpp unpacks it on first launch.
val packSystemAssets = tasks.register<Zip>("packSystemAssets") {
	// Later entries win: original RackDroid graphics (graphics/, GPLv3)
	// override VCV's non-commercial ComponentLibrary + Core + Fundamental
	// panel SVGs so the app is commercially distributable. Upstream res is
	// taken for everything EXCEPT those (fonts, etc.).
	duplicatesStrategy = DuplicatesStrategy.INCLUDE
	from(rootProject.file("third_party/Rack")) {
		include("res/**")
		exclude("res/ComponentLibrary/**")
		exclude("res/Core/**")
		exclude("res/icon.png") // VCV logo (trademark) — not shipped
		include("translations/**")
		include("Core.json")
		include("template.vcv")
		include("LICENSE-GPLv3.txt")
	}
	from(rootProject.file("graphics/system-res")) {
		into("res")
	}
	// Touch-first tutorial patch (overrides Rack's desktop template, whose
	// notes talk about right-click/Ctrl/QWERTY) + self-playing demo patches
	// copied to the user patches dir on first run (asset_extract.cpp).
	from(rootProject.file("graphics/template-android")) {
	}
	from(rootProject.file("graphics/NOTICE-graphics.md")) {
	}
	// Manifests/resources of the bundled plugins. Fundamental panels come
	// from graphics/ (originals); its knobs/etc. use the system
	// ComponentLibrary (also original now).
	from(rootProject.file("third_party/Fundamental")) {
		include("plugin.json")
		include("presets/**")
		include("LICENSE*")
		into("plugins/Fundamental")
	}
	from(rootProject.file("graphics/fundamental-res")) {
		into("plugins/Fundamental/res")
	}
	// RackDroid Drums: first-party pack, code and panels both original to
	// this repo -- lives directly under drums/, not third_party/.
	from(rootProject.file("drums")) {
		include("plugin.json")
		include("res/**")
		into("plugins/RackDroidDrums")
	}
	// Lean base: only Fundamental (above) + RackDroid Drums are bundled.
	// Every other pack ships as an on-demand .rdmod (scripts/make_rdmods.sh,
	// MODULES.md); its res/ travels inside the pack, not in system.zip.
	archiveFileName.set("system.zip")
	destinationDirectory.set(layout.buildDirectory.dir("generated/assets"))
}

// Module browser tile art (ModuleBrowserSheet.kt): one PNG per model,
// generated once by `rack_ui_smoke --export-thumbnails` (see
// native/host/main_ui_host.cpp) and committed under graphics/browser-thumbs/,
// same convention as graphics/regen_graphics.py's SVG output. Packed
// separately from system.zip so it can be revisioned/extracted independently
// (native/port/asset_extract.cpp) — it's pure UI art, unrelated to the engine
// assets and an order of magnitude larger.
val packThumbnailAssets = tasks.register<Zip>("packThumbnailAssets") {
	from(rootProject.file("graphics/browser-thumbs"))
	archiveFileName.set("thumbnails.zip")
	destinationDirectory.set(layout.buildDirectory.dir("generated/assets"))
}

android {
	namespace = "org.rackdroid"
	compileSdk = 35

	defaultConfig {
		applicationId = "org.rackdroid"
		// bionic gained <execinfo.h> (used by Rack's system::getStackTrace)
		// in API 33. A compat shim can lower this later.
		minSdk = 33
		targetSdk = 35
		versionCode = 73
		versionName = "0.41.1"

		ndk {
			abiFilters += listOf("arm64-v8a")
		}
		externalNativeBuild {
			cmake {
				arguments += listOf(
					"-DANDROID_STL=c++_shared",
					"-DANDROID_PLATFORM=android-33"
				)
				// rackdroid pulls in librack_engine.so; the plugin .so are
				// dlopen'd at runtime and must be packaged explicitly.
				targets += listOf("rackdroid", "plugin_fundamental", "plugin_drums")
			}
		}
	}

	externalNativeBuild {
		cmake {
			path = file("../native/CMakeLists.txt")
			version = "3.22.1"
		}
	}

	sourceSets {
		getByName("main") {
			assets.srcDir(layout.buildDirectory.dir("generated/assets"))
		}
	}

	signingConfigs {
		create("release") {
			// Production signing: if ~/rackdroid-keystore.properties exists
			// (storeFile/storePassword/keyAlias/keyPassword), it is used —
			// the keystore lives OUTSIDE the repo and must never be
			// committed. Otherwise fall back to the public development
			// keystore (sideload update continuity only, NOT authenticity).
			// -PdevKeystore forces the dev key even when the production
			// properties exist: GitHub sideload APKs keep update continuity
			// with installs made before the production key existed, while
			// Play AABs (built without the flag) get the private key.
			val useDev = project.hasProperty("devKeystore")
			val propsFile = File(System.getProperty("user.home"), "rackdroid-keystore.properties")
			if (!useDev && propsFile.exists()) {
				val props = Properties()
				propsFile.inputStream().use { stream -> props.load(stream) }
				storeFile = file(props.getProperty("storeFile"))
				storePassword = props.getProperty("storePassword")
				keyAlias = props.getProperty("keyAlias")
				keyPassword = props.getProperty("keyPassword")
			} else {
				storeFile = file("../keystore/rackdroid.keystore")
				storePassword = "rackdroid"
				keyAlias = "rackdroid"
				keyPassword = "rackdroid"
			}
		}
	}

	buildTypes {
		release {
			isMinifyEnabled = false
			signingConfig = signingConfigs.getByName("release")
		}
	}

	// Lean base APK: CMake still compiles every plugin (so
	// scripts/make_rdmods.sh can package them from stripped_native_libs), but
	// only Fundamental + Drums ride along in the APK. Every other plugin .so is
	// dropped at packaging time and shipped instead as an on-demand .rdmod
	// (MODULES.md). Keep this list in sync with native/CMakeLists.txt plugins.
	packaging {
		jniLibs {
			excludes += listOf(
				"**/libplugin_aria.so",
				"**/libplugin_audible.so",
				"**/libplugin_autinn.so",
				"**/libplugin_befaco.so",
				"**/libplugin_bidoo.so",
				"**/libplugin_bogaudio.so",
				"**/libplugin_computerscare.so",
				"**/libplugin_countmodula.so",
				"**/libplugin_frozenwasteland.so",
				"**/libplugin_grande.so",
				"**/libplugin_hetrickcv.so",
				"**/libplugin_impromptu.so",
				"**/libplugin_jw.so",
				"**/libplugin_littleutils.so",
				"**/libplugin_ml.so",
				"**/libplugin_nlc.so",
				"**/libplugin_packone.so",
				"**/libplugin_rj.so",
				"**/libplugin_sonus.so",
				"**/libplugin_valley.so",
				"**/libplugin_venom.so"
			)
		}
	}

	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_17
		targetCompatibility = JavaVersion.VERSION_17
	}
	kotlinOptions {
		jvmTarget = "17"
	}
}

tasks.named("preBuild") {
	dependsOn(packSystemAssets)
	dependsOn(packThumbnailAssets)
}

dependencies {
	// FileProvider for sharing patches
	implementation("androidx.core:core-ktx:1.13.1")
	// Module browser grid
	implementation("androidx.recyclerview:recyclerview:1.3.2")
}
