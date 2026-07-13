/* Host (desktop Linux) smoke test for the Rack engine subset.
 *
 * Verifies, outside of the official Makefile build, that the CMake-built
 * engine can: initialize the Rack runtime, load settings, bring up the plugin
 * framework (Core stub), run the Engine for one simulated second of audio,
 * and shut down cleanly. This is the same code path the Android build takes
 * minus Oboe/EGL, so a green run here validates the port's build system.
 */
#include <cstdio>
#include <chrono>

#include <common.hpp>
#include <system.hpp>
#include <asset.hpp>
#include <logger.hpp>
#include <random.hpp>
#include <settings.hpp>
#include <audio.hpp>
#include <midi.hpp>
#include <midiloopback.hpp>
#include <plugin.hpp>
#include <context.hpp>
#include <engine/Engine.hpp>

using namespace rack;


int main(int argc, char* argv[]) {
	std::string tmpDir = system::getTempDirectory() + "/rackdroid-smoke";
	// The Rack checkout doubles as system dir (Core.json, res/, template.vcv).
	asset::systemDir = RACKDROID_RACK_DIR;
	asset::userDir = tmpDir + "/user";
	system::createDirectories(asset::userDir);

	settings::devMode = true; // log to stderr
	settings::headless = true;

	system::init();
	system::resetFpuFlags();
	asset::init();
	logger::init();
	random::init();

	std::printf("== %s %s (engine subset smoke test)\n", APP_NAME.c_str(), APP_VERSION.c_str());

	settings::init();
	settings::sampleRate = 48000.f;

	audio::init();
	midi::init();
	midiloopback::init();
	plugin::init();

	contextSet(new Context);
	APP->midiLoopbackContext = new midiloopback::Context;
	APP->engine = new engine::Engine;

	// Simulate 1 second of audio in blocks of 256 frames.
	const int blockSize = 256;
	const int blocks = 48000 / blockSize;
	auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < blocks; i++) {
		APP->engine->stepBlock(blockSize);
	}
	auto end = std::chrono::steady_clock::now();
	double ms = std::chrono::duration<double, std::milli>(end - start).count();
	std::printf("== stepped %d blocks (%d frames) in %.2f ms\n", blocks, blocks * blockSize, ms);

	Context* ctx = APP;
	contextSet(NULL);
	delete ctx;
	settings::destroy();
	plugin::destroy();
	midi::destroy();
	audio::destroy();
	logger::destroy();

	std::printf("== OK\n");
	return 0;
}
