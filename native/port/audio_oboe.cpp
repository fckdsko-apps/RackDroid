/* rack::audio driver backed by Google Oboe (AAudio/OpenSL under the hood).
 *
 * Replaces src/rtaudio.cpp on Android. One logical device ("Default") maps to
 * the system default output + input. The output stream is the clock: Rack's
 * audio ports are driven from its data callback, which is how the desktop
 * RtAudio driver behaves as well. Input is a second stream read non-blocking
 * inside the output callback (standard Oboe full-duplex pattern); if the input
 * stream can't be opened (e.g. RECORD_AUDIO permission not granted), the
 * device degrades to output-only.
 *
 * v06 Audio Lab adds restart-only A/B switches and low-overhead diagnostics.
 * v07 separated Android callback size from Rack's saved block size.
 * v08.1 adds low-latency-path experiments for output sample-rate selection and
 * AAudio buffer depth while preserving the v07 48 kHz control path as the default.
 *
 * Rack autosaves its active patch periodically and on shutdown. Therefore this lab
 * must not depend on .vcv/autosave sample-rate metadata remaining unchanged between
 * experimental runs. The Android lab owns the output stream rate while enabled:
 * control is pinned to the known v07 48 kHz request, and experimental modes override
 * it. Rack still sees the actual opened stream rate for correct engine/device math.
 */
#include "audio_oboe.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <pthread.h>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

#include <jni.h>
#include <android/log.h>

#include <oboe/Oboe.h>

#include <asset.hpp>
#include <audio.hpp>
#include <common.hpp>
#include <context.hpp>
#include <engine/Engine.hpp>
#include <settings.hpp>
#include <system.hpp>


/* Defined inside Rack's Engine.cpp on Android. The setter is called once from
 * oboeInit(), before an Engine instance or audio callback exists. The meter
 * getters read atomics updated by Engine::stepBlock() once per meter window. */
extern "C" void rackdroid_audio_experiment_set_fast_engine(int enabled);
extern "C" uint64_t rackdroid_audio_experiment_get_engine_average_ppm();
extern "C" uint64_t rackdroid_audio_experiment_get_engine_max_ppm();


// ---- Master WAV recorder ------------------------------------------------
// The audio callback taps the final output buffer into a single-producer/
// single-consumer ring; a writer thread drains it to a 16-bit PCM WAV.
// File I/O never happens on the audio thread; on ring overflow (writer
// stalled) frames are dropped rather than blocking the callback.

namespace {

struct WavRecorder {
	static const size_t RING_FLOATS = 1 << 20; // ~5.5s stereo @48k
	std::vector<float> ring;
	std::atomic<size_t> head{0}; // producer (audio thread)
	std::atomic<size_t> tail{0}; // consumer (writer thread)
	std::atomic<bool> active{false};
	std::thread writer;
	FILE* file = NULL;
	uint32_t dataBytes = 0;
	int sampleRate = 48000;
	int channels = 2;

	bool start(const std::string& path, int sr, int ch) {
		if (active)
			return false;
		file = std::fopen(path.c_str(), "wb");
		if (!file)
			return false;
		sampleRate = sr;
		channels = ch;
		dataBytes = 0;
		writeHeader(); // placeholder sizes, fixed on stop()
		ring.assign(RING_FLOATS, 0.f);
		head = tail = 0;
		active = true;
		writer = std::thread([this] { run(); });
		return true;
	}

	void stop() {
		if (!active)
			return;
		active = false;
		if (writer.joinable())
			writer.join();
		// Patch RIFF/data sizes now that the length is known.
		std::fseek(file, 4, SEEK_SET);
		uint32_t riff = 36 + dataBytes;
		std::fwrite(&riff, 4, 1, file);
		std::fseek(file, 40, SEEK_SET);
		std::fwrite(&dataBytes, 4, 1, file);
		std::fclose(file);
		file = NULL;
	}

	void writeHeader() {
		uint16_t fmt = 1, ch = channels, bits = 16;
		uint32_t sr = sampleRate;
		uint32_t byteRate = sr * ch * bits / 8;
		uint16_t blockAlign = ch * bits / 8;
		uint32_t zero = 0;
		std::fwrite("RIFF", 1, 4, file);
		std::fwrite(&zero, 4, 1, file);
		std::fwrite("WAVEfmt ", 1, 8, file);
		uint32_t fmtSize = 16;
		std::fwrite(&fmtSize, 4, 1, file);
		std::fwrite(&fmt, 2, 1, file);
		std::fwrite(&ch, 2, 1, file);
		std::fwrite(&sr, 4, 1, file);
		std::fwrite(&byteRate, 4, 1, file);
		std::fwrite(&blockAlign, 2, 1, file);
		std::fwrite(&bits, 2, 1, file);
		std::fwrite("data", 1, 4, file);
		std::fwrite(&zero, 4, 1, file);
	}

	/** Audio thread: push interleaved floats; drops on overflow. */
	void push(const float* samples, size_t n) {
		if (!active)
			return;
		size_t h = head.load(std::memory_order_relaxed);
		size_t t = tail.load(std::memory_order_acquire);
		size_t freeSpace = RING_FLOATS - (h - t);
		if (n > freeSpace)
			n = freeSpace;
		for (size_t i = 0; i < n; i++)
			ring[(h + i) % RING_FLOATS] = samples[i];
		head.store(h + n, std::memory_order_release);
	}

	void run() {
		std::vector<int16_t> chunk;
		while (active || tail.load() != head.load()) {
			size_t h = head.load(std::memory_order_acquire);
			size_t t = tail.load(std::memory_order_relaxed);
			if (t == h) {
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				continue;
			}
			size_t n = h - t;
			chunk.resize(n);
			for (size_t i = 0; i < n; i++) {
				float v = ring[(t + i) % RING_FLOATS];
				v = std::fmax(-1.f, std::fmin(1.f, v));
				chunk[i] = (int16_t) (v * 32767.f);
			}
			std::fwrite(chunk.data(), 2, n, file);
			dataBytes += n * 2;
			tail.store(t + n, std::memory_order_release);
		}
	}
};

WavRecorder gRecorder;

} // namespace


namespace rackdroid {


static const int NUM_OUTPUTS = 2;
static const int NUM_INPUTS = 2;
static const int DEFAULT_SAMPLE_RATE = 48000;
static const int DEFAULT_BLOCK_SIZE = 256;
// Defensive ceiling only. A genuine low-latency Oboe callback should be far
// smaller; keeping a fixed preallocated slab means the callback never resizes.
static const int MAX_CALLBACK_FRAMES = 16384;


static uint64_t clockNanos(clockid_t id) {
	timespec ts{};
	if (clock_gettime(id, &ts) != 0)
		return 0;
	return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}


struct AudioLabConfig {
	// callbackFrames is Android-runtime-only and is never written to Rack/.vcv.
	//   -1 = let Oboe/AAudio choose the callback size
	//    0 = legacy behavior: fixed callback follows Rack's saved block size
	//   >0 = explicit fixed Android callback size for the latency experiment
	int callbackFrames = 0;

	// sampleRateMode controls only how the Android output stream is opened.
	//    0 = v07 control: request Rack's sample rate, Oboe SRC quality Medium
	//   -1 = native/unspecified output rate, Oboe SRC disabled
	// 44100 = explicit 44.1 kHz output rate, Oboe SRC disabled
	int sampleRateMode = 0;

	// 0 = leave AAudio/Oboe buffer size alone (v07 control).
	// 1/2 = request that many reported hardware bursts after opening.
	int bufferBursts = 0;

	bool fastEngine = false;
	bool inputEnabled = true;
};

static AudioLabConfig gAudioLabConfig;

static bool parseBool(const std::string& value, bool& out) {
	if (value == "1" || value == "true" || value == "on" || value == "yes") {
		out = true;
		return true;
	}
	if (value == "0" || value == "false" || value == "off" || value == "no") {
		out = false;
		return true;
	}
	return false;
}

static bool parseCallbackFrames(const std::string& value, int& out) {
	// Deliberately whitelist only the experiment modes exposed by Audio Lab.
	// A malformed/stale config therefore falls back to the legacy Rack block.
	if (value == "-1") { out = -1; return true; }
	if (value == "0") { out = 0; return true; }
	if (value == "96") { out = 96; return true; }
	if (value == "128") { out = 128; return true; }
	if (value == "192") { out = 192; return true; }
	if (value == "256") { out = 256; return true; }
	return false;
}

static bool parseSampleRateMode(const std::string& value, int& out) {
	if (value == "-1") { out = -1; return true; }
	if (value == "0") { out = 0; return true; }
	if (value == "44100") { out = 44100; return true; }
	return false;
}

static bool parseBufferBursts(const std::string& value, int& out) {
	if (value == "0") { out = 0; return true; }
	if (value == "1") { out = 1; return true; }
	if (value == "2") { out = 2; return true; }
	return false;
}

static std::string trim(std::string s) {
	const char* ws = " \t\r\n";
	size_t first = s.find_first_not_of(ws);
	if (first == std::string::npos)
		return "";
	size_t last = s.find_last_not_of(ws);
	return s.substr(first, last - first + 1);
}

static void loadAudioLabConfig() {
	gAudioLabConfig = AudioLabConfig(); // baseline defaults if absent/malformed
	std::string path = rack::asset::user("audio-lab.cfg");
	std::ifstream in(path.c_str());
	std::string line;
	bool callbackFramesSeen = false;
	while (in && std::getline(in, line)) {
		line = trim(line);
		if (line.empty() || line[0] == '#')
			continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = trim(line.substr(0, eq));
		std::string value = trim(line.substr(eq + 1));
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return (char) std::tolower(c);
		});
		bool parsed = false;
		if (key == "native_callback") {
			// Backward compatibility with v06 configs. callback_frames, when
			// present, is authoritative and is written after this legacy key.
			if (!callbackFramesSeen && parseBool(value, parsed))
				gAudioLabConfig.callbackFrames = parsed ? -1 : 0;
		}
		else if (key == "callback_frames") {
			int frames = 0;
			if (parseCallbackFrames(value, frames)) {
				gAudioLabConfig.callbackFrames = frames;
				callbackFramesSeen = true;
			}
		}
		else if (key == "sample_rate_mode") {
			int mode = 0;
			if (parseSampleRateMode(value, mode))
				gAudioLabConfig.sampleRateMode = mode;
		}
		else if (key == "buffer_bursts") {
			int bursts = 0;
			if (parseBufferBursts(value, bursts))
				gAudioLabConfig.bufferBursts = bursts;
		}
		else if (key == "fast_engine") {
			if (parseBool(value, parsed)) gAudioLabConfig.fastEngine = parsed;
		}
		else if (key == "input_enabled") {
			if (parseBool(value, parsed)) gAudioLabConfig.inputEnabled = parsed;
		}
	}

	// The Engine flag is startup-only. Audio Lab never mutates it live.
	rackdroid_audio_experiment_set_fast_engine(gAudioLabConfig.fastEngine ? 1 : 0);
	std::string callbackLabel;
	if (gAudioLabConfig.callbackFrames < 0) callbackLabel = "oboe-managed";
	else if (gAudioLabConfig.callbackFrames == 0) callbackLabel = "rack-block";
	else callbackLabel = "fixed-" + std::to_string(gAudioLabConfig.callbackFrames);

	std::string rateLabel;
	if (gAudioLabConfig.sampleRateMode < 0) rateLabel = "native-unspecified";
	else if (gAudioLabConfig.sampleRateMode == 0) rateLabel = "rack-requested+src";
	else rateLabel = std::to_string(gAudioLabConfig.sampleRateMode) + "-no-src";

	INFO("Audio Lab config: callback=%s rate=%s bufferBursts=%d engine=%s input=%s",
		callbackLabel.c_str(),
		rateLabel.c_str(),
		gAudioLabConfig.bufferBursts,
		gAudioLabConfig.fastEngine ? "fast-single" : "upstream",
		gAudioLabConfig.inputEnabled ? "on" : "off");
}


struct AudioStatsSnapshot {
	uint64_t callbacks = 0;
	uint64_t frames = 0;
	int minFrames = 0;
	int maxFrames = 0;
	uint64_t otherFramesCallbacks = 0;
	uint64_t inputShortfallFrames = 0;
	uint64_t oversizedCallbacks = 0;
	std::array<uint64_t, 65> frameBins{};
};

struct AudioStats {
	std::atomic<uint64_t> callbacks;
	std::atomic<uint64_t> frames;
	std::atomic<int> minFrames;
	std::atomic<int> maxFrames;
	std::atomic<uint64_t> otherFramesCallbacks;
	std::atomic<uint64_t> inputShortfallFrames;
	std::atomic<uint64_t> oversizedCallbacks;
	std::array<std::atomic<uint64_t>, 65> frameBins;

	AudioStats() { reset(); }

	void reset() {
		callbacks.store(0, std::memory_order_relaxed);
		frames.store(0, std::memory_order_relaxed);
		minFrames.store(INT_MAX, std::memory_order_relaxed);
		maxFrames.store(0, std::memory_order_relaxed);
		otherFramesCallbacks.store(0, std::memory_order_relaxed);
		inputShortfallFrames.store(0, std::memory_order_relaxed);
		oversizedCallbacks.store(0, std::memory_order_relaxed);
		for (auto& b : frameBins)
			b.store(0, std::memory_order_relaxed);
	}

	/** Single audio-writer, relaxed diagnostic counters only. No clocks, locks,
	 * allocation, logging, or file/UI work are added to the steady-state callback. */
	void record(int numFrames) {
		callbacks.fetch_add(1, std::memory_order_relaxed);
		frames.fetch_add((uint64_t) std::max(numFrames, 0), std::memory_order_relaxed);

		int prevMin = minFrames.load(std::memory_order_relaxed);
		while (numFrames < prevMin &&
				!minFrames.compare_exchange_weak(prevMin, numFrames, std::memory_order_relaxed)) {}
		int prevMax = maxFrames.load(std::memory_order_relaxed);
		while (numFrames > prevMax &&
				!maxFrames.compare_exchange_weak(prevMax, numFrames, std::memory_order_relaxed)) {}

		if (numFrames > 0 && numFrames <= 1024 && (numFrames % 16) == 0)
			frameBins[(size_t) numFrames / 16].fetch_add(1, std::memory_order_relaxed);
		else
			otherFramesCallbacks.fetch_add(1, std::memory_order_relaxed);
	}

	AudioStatsSnapshot snapshot() const {
		AudioStatsSnapshot s;
		s.callbacks = callbacks.load(std::memory_order_relaxed);
		s.frames = frames.load(std::memory_order_relaxed);
		int min = minFrames.load(std::memory_order_relaxed);
		s.minFrames = (min == INT_MAX) ? 0 : min;
		s.maxFrames = maxFrames.load(std::memory_order_relaxed);
		s.otherFramesCallbacks = otherFramesCallbacks.load(std::memory_order_relaxed);
		s.inputShortfallFrames = inputShortfallFrames.load(std::memory_order_relaxed);
		s.oversizedCallbacks = oversizedCallbacks.load(std::memory_order_relaxed);
		for (size_t i = 0; i < s.frameBins.size(); i++)
			s.frameBins[i] = frameBins[i].load(std::memory_order_relaxed);
		return s;
	}
};

static AudioStats gAudioStats;

static const char* apiName(oboe::AudioApi api) {
	switch (api) {
		case oboe::AudioApi::AAudio: return "AAudio";
		case oboe::AudioApi::OpenSLES: return "OpenSL ES";
		default: return "Unspecified";
	}
}

static const char* performanceName(oboe::PerformanceMode mode) {
	switch (mode) {
		case oboe::PerformanceMode::LowLatency: return "LowLatency";
		case oboe::PerformanceMode::PowerSaving: return "PowerSaving";
		case oboe::PerformanceMode::None: return "None";
		default: return "Unknown";
	}
}

static const char* sharingName(oboe::SharingMode mode) {
	switch (mode) {
		case oboe::SharingMode::Exclusive: return "Exclusive";
		case oboe::SharingMode::Shared: return "Shared";
		default: return "Unknown";
	}
}

static const char* srcQualityName(oboe::SampleRateConversionQuality quality) {
	switch (quality) {
		case oboe::SampleRateConversionQuality::None: return "None";
		case oboe::SampleRateConversionQuality::Fastest: return "Fastest";
		case oboe::SampleRateConversionQuality::Low: return "Low";
		case oboe::SampleRateConversionQuality::Medium: return "Medium";
		case oboe::SampleRateConversionQuality::High: return "High";
		case oboe::SampleRateConversionQuality::Best: return "Best";
		default: return "Unknown";
	}
}

struct OboeDevice;
static std::mutex gActiveDeviceMutex;
static OboeDevice* gActiveDevice = NULL;


struct OboeDevice : rack::audio::Device, oboe::AudioStreamDataCallback, oboe::AudioStreamErrorCallback {
	std::shared_ptr<oboe::AudioStream> outputStream;
	std::shared_ptr<oboe::AudioStream> inputStream;
	// The value Rack/.vcv/autosave last requested. It is diagnostic metadata only
	// while Audio Lab is active; v08.1 deliberately decouples the Android stream
	// experiment from Rack autosave so a 44.1 kHz experimental run cannot poison
	// a later 48 kHz control run.
	float rackRequestedSampleRate = DEFAULT_SAMPLE_RATE;
	int blockSize = DEFAULT_BLOCK_SIZE;
	std::vector<float> inputBuffer;
	int effectiveSampleRateMode = 0;
	bool sampleRateFallbackUsed = false;
	std::string sampleRateFallbackReason;
	int requestedBufferFrames = 0;
	std::string bufferSetError;
	uint64_t measurementStartWallNs = 0;
	uint64_t measurementStartProcessNs = 0;
	std::atomic<uint64_t> measurementStartCallbackCpuNs{0};
	// pthread CPU clock for the Oboe callback thread. Captured once by that
	// thread, then queried only from Audio Lab so steady-state callbacks do not
	// pay two timing calls each.
	std::atomic<int64_t> callbackClockIdRaw{0};
	int xrunBaseline = 0;
	bool xrunBaselineValid = false;
	/** Guards stream open/close and non-RT diagnostics queries. */
	std::mutex streamMutex;

	OboeDevice() {
		openStreams();
		std::lock_guard<std::mutex> lock(gActiveDeviceMutex);
		gActiveDevice = this;
	}

	~OboeDevice() override {
		{
			std::lock_guard<std::mutex> lock(gActiveDeviceMutex);
			if (gActiveDevice == this)
				gActiveDevice = NULL;
		}
		closeStreams();
	}

	void resetMeasurementsNoLock() {
		gAudioStats.reset();
		measurementStartWallNs = clockNanos(CLOCK_MONOTONIC);
		measurementStartProcessNs = clockNanos(CLOCK_PROCESS_CPUTIME_ID);

		int64_t rawClock = callbackClockIdRaw.load(std::memory_order_acquire);
		if (rawClock != 0)
			measurementStartCallbackCpuNs.store(
				clockNanos((clockid_t) rawClock), std::memory_order_relaxed);
		else
			measurementStartCallbackCpuNs.store(0, std::memory_order_relaxed);

		xrunBaselineValid = false;
		xrunBaseline = 0;
		if (outputStream) {
			auto xruns = outputStream->getXRunCount();
			if (xruns) {
				xrunBaseline = xruns.value();
				xrunBaselineValid = true;
			}
		}
	}

	void resetMeasurements() {
		std::lock_guard<std::mutex> lock(streamMutex);
		resetMeasurementsNoLock();
	}

	void openStreams() {
		std::lock_guard<std::mutex> lock(streamMutex);
		// A reopened Oboe stream may receive callbacks on a different thread.
		// Force the first callback of this stream to publish its own CPU clock.
		callbackClockIdRaw.store(0, std::memory_order_relaxed);
		measurementStartCallbackCpuNs.store(0, std::memory_order_relaxed);
		effectiveSampleRateMode = gAudioLabConfig.sampleRateMode;
		sampleRateFallbackUsed = false;
		sampleRateFallbackReason.clear();
		requestedBufferFrames = 0;
		bufferSetError.clear();

		auto openOutputForMode = [&](int rateMode) -> oboe::Result {
			oboe::AudioStreamBuilder outBuilder;
			outBuilder.setDirection(oboe::Direction::Output)
				->setPerformanceMode(oboe::PerformanceMode::LowLatency)
				->setSharingMode(oboe::SharingMode::Exclusive)
				->setFormat(oboe::AudioFormat::Float)
				->setChannelCount(NUM_OUTPUTS);

			if (rateMode == 0) {
				// v08.1 control is intentionally pinned to the known v07 baseline.
				// Do NOT source this from Rack/.vcv: Rack periodically autosaves the
				// actual device rate, so a 44.1 kHz experiment could otherwise turn a
				// later "control" run into 44.1 kHz on restart.
				outBuilder.setSampleRate(DEFAULT_SAMPLE_RATE)
					->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
			}
			else {
				// Low-latency experiments avoid Oboe SRC. A negative mode leaves
				// sample rate unspecified so AAudio can choose the optimal rate.
				outBuilder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
				if (rateMode > 0)
					outBuilder.setSampleRate(rateMode);
			}

			if (gAudioLabConfig.callbackFrames >= 0) {
				const int callbackFrames = gAudioLabConfig.callbackFrames > 0
					? gAudioLabConfig.callbackFrames : blockSize;
				outBuilder.setFramesPerDataCallback(callbackFrames);
			}
			outBuilder.setDataCallback(this)
				->setErrorCallback(this);
			return outBuilder.openStream(outputStream);
		};

		oboe::Result result = openOutputForMode(effectiveSampleRateMode);
		if (result != oboe::Result::OK && effectiveSampleRateMode != 0) {
			// A failed experiment must not strand the app without output. Record
			// the failure and retry the known-working v07 stream configuration.
			sampleRateFallbackUsed = true;
			sampleRateFallbackReason = oboe::convertToText(result);
			WARN("Oboe: sample-rate experiment failed (%s), retrying v07 control",
				sampleRateFallbackReason.c_str());
			outputStream.reset();
			effectiveSampleRateMode = 0;
			result = openOutputForMode(0);
		}
		if (result != oboe::Result::OK) {
			WARN("Oboe: could not open output stream: %s", oboe::convertToText(result));
			return;
		}

		// Tune the *active* output buffer after open. AAudio may clamp the request.
		if (gAudioLabConfig.bufferBursts > 0) {
			const int burst = outputStream->getFramesPerBurst();
			const int capacity = outputStream->getBufferCapacityInFrames();
			if (burst > 0 && capacity > 0) {
				requestedBufferFrames = std::min(capacity,
					burst * gAudioLabConfig.bufferBursts);
				auto bufferResult = outputStream->setBufferSizeInFrames(requestedBufferFrames);
				if (!bufferResult)
					bufferSetError = oboe::convertToText(bufferResult.error());
			}
			else {
				bufferSetError = "stream did not report usable burst/capacity";
			}
		}

		if (gAudioLabConfig.inputEnabled) {
			oboe::AudioStreamBuilder inBuilder;
			inBuilder.setDirection(oboe::Direction::Input)
				->setPerformanceMode(oboe::PerformanceMode::LowLatency)
				->setFormat(oboe::AudioFormat::Float)
				->setChannelCount(NUM_INPUTS)
				->setSampleRate(outputStream->getSampleRate());

			result = inBuilder.openStream(inputStream);
			if (result != oboe::Result::OK) {
				WARN("Oboe: no input stream (%s), running output-only", oboe::convertToText(result));
				inputStream.reset();
			}
		}
		else {
			inputStream.reset();
		}

		// Never allocate/resize this vector from onAudioReady(). 16k frames is
		// vastly above a sane low-latency callback and costs only 128 KiB stereo.
		inputBuffer.assign((size_t) MAX_CALLBACK_FRAMES * NUM_INPUTS, 0.f);

		if (inputStream)
			inputStream->requestStart();
		outputStream->requestStart();

		// Default measurement window starts with this stream. Audio Lab can reset
		// it after the representative patch is loaded, which removes startup/load
		// time from A/B CPU comparisons.
		resetMeasurementsNoLock();
		INFO("Oboe: stream started, sampleRate=%d hwRate=%d burst=%d callback=%d buffer=%d/%d api=%s mode=%s sharing=%s",
			outputStream->getSampleRate(),
			outputStream->getHardwareSampleRate(),
			outputStream->getFramesPerBurst(),
			outputStream->getFramesPerDataCallback(),
			outputStream->getBufferSizeInFrames(),
			outputStream->getBufferCapacityInFrames(),
			apiName(outputStream->getAudioApi()),
			performanceName(outputStream->getPerformanceMode()),
			sharingName(outputStream->getSharingMode()));
		onStartStream();
	}

	void closeStreams() {
		std::lock_guard<std::mutex> lock(streamMutex);
		if (outputStream) {
			outputStream->stop();
			outputStream->close();
			outputStream.reset();
			onStopStream();
		}
		if (inputStream) {
			inputStream->stop();
			inputStream->close();
			inputStream.reset();
		}
	}

	// rack::audio::Device

	std::string getName() override {
		return "Default";
	}
	int getNumInputs() override {
		return inputStream ? NUM_INPUTS : 0;
	}
	int getNumOutputs() override {
		return NUM_OUTPUTS;
	}

	std::set<float> getSampleRates() override {
		return {44100.f, 48000.f};
	}
	float getSampleRate() override {
		// Rack's Core audio module must see the *actual stream rate* so its own
		// engine/device resampler math stays correct in native-rate mode.
		if (outputStream)
			return (float) outputStream->getSampleRate();
		return rackRequestedSampleRate;
	}
	void setSampleRate(float sr) override {
		if (sr <= 0.f)
			return;
		// Rack calls this while loading .vcv/autosave metadata. Keep the value
		// for diagnostics, but do not let autosave reopen or retarget the Android
		// stream during the v08.1 lab. The lab's sample-rate selector owns that
		// variable, and getSampleRate() still reports the actual opened rate back
		// to Rack so engine/device resampling remains correct.
		rackRequestedSampleRate = sr;
	}

	std::set<int> getBlockSizes() override {
		return {64, 128, 256, 512, 1024};
	}
	int getBlockSize() override {
		return blockSize;
	}
	void setBlockSize(int bs) override {
		if (bs == blockSize)
			return;
		blockSize = bs;
		// In Oboe-managed or explicit Android callback modes, Rack's block size
		// remains portable .vcv metadata only. It must not overwrite the phone-
		// specific experiment or force an unnecessary stream reopen.
		if (gAudioLabConfig.callbackFrames != 0)
			return;
		closeStreams();
		openStreams();
	}

	// oboe::AudioStreamDataCallback

	oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override {
		// Capture the callback thread's CPU clock exactly once. After this first
		// callback, diagnostics can query that thread's accumulated CPU time from
		// a normal UI/JNI thread; steady-state audio callbacks do not call a timer.
		if (callbackClockIdRaw.load(std::memory_order_relaxed) == 0) {
			clockid_t cid;
			if (pthread_getcpuclockid(pthread_self(), &cid) == 0) {
				callbackClockIdRaw.store((int64_t) cid, std::memory_order_release);
				uint64_t nowCpu = clockNanos(cid);
				uint64_t expected = 0;
				measurementStartCallbackCpuNs.compare_exchange_strong(
					expected, nowCpu, std::memory_order_relaxed);
			}
		}

		float* output = (float*) audioData;
		int channels = stream->getChannelCount();

		// A pathological callback must never turn into an allocation or buffer
		// overrun on the real-time thread. Record it, output silence, continue.
		if (numFrames <= 0 || numFrames > MAX_CALLBACK_FRAMES) {
			if (numFrames > 0 && output && channels > 0)
				std::fill(output, output + (size_t) numFrames * channels, 0.f);
			gAudioStats.oversizedCallbacks.fetch_add(1, std::memory_order_relaxed);
			gAudioStats.record(numFrames);
			return oboe::DataCallbackResult::Continue;
		}

		const float* input = NULL;
		uint64_t inputShortfall = 0;
		if (inputStream) {
			// Non-blocking read; on underrun the missing frames are explicitly zeroed.
			auto readResult = inputStream->read(inputBuffer.data(), numFrames, 0);
			int framesRead = readResult ? readResult.value() : 0;
			framesRead = std::max(0, std::min(framesRead, (int) numFrames));
			if (framesRead < numFrames) {
				inputShortfall = (uint64_t) (numFrames - framesRead);
				std::fill(inputBuffer.begin() + (size_t) framesRead * NUM_INPUTS,
					inputBuffer.begin() + (size_t) numFrames * NUM_INPUTS, 0.f);
			}
			input = inputBuffer.data();
		}

		// Drives Engine::stepBlock() through the subscribed audio Ports.
		processBuffer(input, NUM_INPUTS, output, channels, numFrames);
		gRecorder.push(output, (size_t) numFrames * channels);

		if (inputShortfall)
			gAudioStats.inputShortfallFrames.fetch_add(inputShortfall, std::memory_order_relaxed);
		gAudioStats.record(numFrames);
		return oboe::DataCallbackResult::Continue;
	}

	// oboe::AudioStreamErrorCallback

	void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override {
		// Device disconnected (headphones unplugged, route change): reopen.
		WARN("Oboe: stream error %s, reopening", oboe::convertToText(error));
		closeStreams();
		openStreams();
	}

	std::string diagnostics() {
		std::lock_guard<std::mutex> lock(streamMutex);
		std::ostringstream s;
		s << std::fixed << std::setprecision(2);
		s << "Running config\n";
		s << "  callback: ";
		if (gAudioLabConfig.callbackFrames < 0)
			s << "Oboe-managed / unspecified";
		else if (gAudioLabConfig.callbackFrames == 0)
			s << "fixed Rack block";
		else
			s << "fixed " << gAudioLabConfig.callbackFrames << " frames (Android-only)";
		s << "\n";
		s << "  output sample-rate mode: ";
		if (gAudioLabConfig.sampleRateMode < 0)
			s << "native / unspecified, Oboe SRC off";
		else if (gAudioLabConfig.sampleRateMode == 0)
			s << "Rack-requested rate, Oboe SRC Medium (v07 control)";
		else
			s << gAudioLabConfig.sampleRateMode << " Hz explicit, Oboe SRC off";
		s << "\n";
		s << "  output buffer target: ";
		if (gAudioLabConfig.bufferBursts == 0)
			s << "system/default (v07 control)";
		else
			s << gAudioLabConfig.bufferBursts << " hardware burst"
			  << (gAudioLabConfig.bufferBursts == 1 ? "" : "s");
		s << "\n";
		s << "  engine: " << (gAudioLabConfig.fastEngine ? "fast single-thread" : "upstream Rack") << "\n";
		s << "  audio input requested: " << (gAudioLabConfig.inputEnabled ? "on" : "off") << "\n";
		s << "  Rack engine threads: " << rack::settings::threadCount;
		if (gAudioLabConfig.fastEngine && rack::settings::threadCount != 1)
			s << " (fast path inactive: requires 1)";
		s << "\n";
		s << "  Rack/.vcv requested sample rate (diagnostic only in v08.1): "
		  << rackRequestedSampleRate << " Hz\n";
		if (gAudioLabConfig.sampleRateMode == 0)
			s << "  v08.1 control output request: " << DEFAULT_SAMPLE_RATE << " Hz (pinned)\n";
		if (APP && APP->engine)
			s << "  Rack engine sample rate: " << APP->engine->getSampleRate() << " Hz\n";
		s << "  saved Rack block size: " << blockSize << " frames\n\n";

		if (!outputStream) {
			s << "Output stream is not open.\n";
			return s.str();
		}

		int sr = outputStream->getSampleRate();
		int hwSr = outputStream->getHardwareSampleRate();
		int burst = outputStream->getFramesPerBurst();
		int cb = outputStream->getFramesPerDataCallback();
		int buf = outputStream->getBufferSizeInFrames();
		int cap = outputStream->getBufferCapacityInFrames();

		s << "Oboe stream\n";
		s << "  API: " << apiName(outputStream->getAudioApi()) << "\n";
		s << "  performance requested/granted: LowLatency / "
		  << performanceName(outputStream->getPerformanceMode()) << "\n";
		s << "  sharing requested/granted: Exclusive / "
		  << sharingName(outputStream->getSharingMode()) << "\n";
		s << "  sample-rate mode effective: ";
		if (effectiveSampleRateMode < 0)
			s << "native / unspecified";
		else if (effectiveSampleRateMode == 0)
			s << "v07 control";
		else
			s << effectiveSampleRateMode << " Hz explicit";
		if (sampleRateFallbackUsed)
			s << " (FELL BACK after " << sampleRateFallbackReason << ")";
		s << "\n";
		s << "  Oboe sample-rate conversion: "
		  << srcQualityName(outputStream->getSampleRateConversionQuality()) << "\n";
		s << "  sample rate: " << sr << " Hz\n";
		s << "  hardware sample rate: " << (hwSr > 0 ? std::to_string(hwSr) + " Hz" : "unavailable (< API 34 or route did not report it)") << "\n";
		s << "  frames/burst: " << burst << "\n";
		s << "  reported frames/data callback: " << (cb > 0 ? std::to_string(cb) : "variable/unspecified") << "\n";
		s << "  buffer: " << buf << " / " << cap << " frames";
		if (sr > 0 && buf > 0)
			s << " (" << (1000.0 * buf / sr) << " ms configured)";
		s << "\n";
		if (gAudioLabConfig.bufferBursts > 0) {
			s << "  buffer request: " << requestedBufferFrames << " frames";
			if (!bufferSetError.empty())
				s << " (FAILED: " << bufferSetError << ")";
			else
				s << " -> granted " << buf;
			s << "\n";
		}
		s << "  input actually open: " << (inputStream ? "yes" : "no") << "\n";
		s << "  master recorder active: " << (gRecorder.active.load(std::memory_order_relaxed) ? "yes" : "no") << "\n";

		uint64_t nowWall = clockNanos(CLOCK_MONOTONIC);
		uint64_t nowProcess = clockNanos(CLOCK_PROCESS_CPUTIME_ID);
		if (measurementStartWallNs && measurementStartProcessNs &&
				nowWall > measurementStartWallNs && nowProcess >= measurementStartProcessNs) {
			double wallSeconds = (nowWall - measurementStartWallNs) / 1e9;
			double cpuSeconds = (nowProcess - measurementStartProcessNs) / 1e9;
			s << "  whole-process CPU in measurement window: "
			  << (100.0 * cpuSeconds / wallSeconds)
			  << "% of one core-equivalent over " << wallSeconds << " s\n";

			int64_t rawClock = callbackClockIdRaw.load(std::memory_order_acquire);
			uint64_t callbackStart = measurementStartCallbackCpuNs.load(std::memory_order_relaxed);
			if (rawClock != 0 && callbackStart != 0) {
				uint64_t callbackNow = clockNanos((clockid_t) rawClock);
				if (callbackNow >= callbackStart) {
					double callbackCpuSeconds = (callbackNow - callbackStart) / 1e9;
					s << "  Oboe callback-thread CPU in window: "
					  << (100.0 * callbackCpuSeconds / wallSeconds)
					  << "% of one core-equivalent\n";
				}
			}
		}

		auto xruns = outputStream->getXRunCount();
		if (xruns) {
			s << "  xruns total: " << xruns.value() << "\n";
			if (xrunBaselineValid)
				s << "  xruns in measurement window: "
				  << std::max(0, xruns.value() - xrunBaseline) << "\n";
		}
		else
			s << "  xruns: unsupported (" << oboe::convertToText(xruns.error()) << ")\n";

		auto latency = outputStream->calculateLatencyMillis();
		if (latency)
			s << "  estimated output latency: " << latency.value() << " ms\n";
		else
			s << "  estimated output latency: unavailable (" << oboe::convertToText(latency.error()) << ")\n";

		AudioStatsSnapshot st = gAudioStats.snapshot();
		s << "\nCallback measurements in current measurement window\n";
		s << "  callbacks: " << st.callbacks << "\n";
		if (st.callbacks > 0) {
			double audioSeconds = (sr > 0) ? ((double) st.frames / sr) : 0.0;
			double callbacksPerSec = (audioSeconds > 0.0) ? (st.callbacks / audioSeconds) : 0.0;
			s << "  callback frame min/max: " << st.minFrames << " / " << st.maxFrames << "\n";
			s << "  callbacks/sec: " << callbacksPerSec << "\n";
			s << "  frame-size counts: ";
			bool first = true;
			for (size_t i = 1; i < st.frameBins.size(); i++) {
				if (!st.frameBins[i]) continue;
				if (!first) s << ", ";
				first = false;
				s << (i * 16) << "=" << st.frameBins[i];
			}
			if (st.otherFramesCallbacks) {
				if (!first) s << ", ";
				s << "other=" << st.otherFramesCallbacks;
				first = false;
			}
			if (first) s << "(none yet)";
			s << "\n";
		}
		s << "  input shortfall frames: " << st.inputShortfallFrames << "\n";
		s << "  oversized callbacks: " << st.oversizedCallbacks << "\n";

		uint64_t engineAvg = rackdroid_audio_experiment_get_engine_average_ppm();
		uint64_t engineMax = rackdroid_audio_experiment_get_engine_max_ppm();
		s << "\nRack engine meter (last completed ~1 s window)\n";
		s << "  average: " << (engineAvg / 10000.0) << "% of real time\n";
		s << "  max block: " << (engineMax / 10000.0) << "% of real time\n";
		return s.str();
	}
};


struct OboeDriver : rack::audio::Driver {
	OboeDevice* device = NULL;

	std::string getName() override {
		return "Android (Oboe)";
	}
	std::vector<int> getDeviceIds() override {
		return {0};
	}
	int getDefaultDeviceId() override {
		return 0;
	}
	std::string getDeviceName(int deviceId) override {
		return (deviceId == 0) ? "Default" : "";
	}
	int getDeviceNumInputs(int deviceId) override {
		// Preserve the pre-v06 driver's advertised input count when the input
		// experiment is enabled. Only the explicit Off condition changes it.
		return (deviceId == 0 && gAudioLabConfig.inputEnabled) ? NUM_INPUTS : 0;
	}
	int getDeviceNumOutputs(int deviceId) override {
		return (deviceId == 0) ? NUM_OUTPUTS : 0;
	}

	rack::audio::Device* subscribe(int deviceId, rack::audio::Port* port) override {
		if (deviceId != 0)
			return NULL;
		if (!device)
			device = new OboeDevice;
		device->subscribe(port);
		return device;
	}

	void unsubscribe(int deviceId, rack::audio::Port* port) override {
		if (deviceId != 0 || !device)
			return;
		device->unsubscribe(port);
		if (device->subscribed.empty()) {
			delete device;
			device = NULL;
		}
	}
};


void oboeInit() {
	loadAudioLabConfig();
	rack::audio::addDriver(OBOE_DRIVER_ID, new OboeDriver);
}


// ---- JNI: Audio Lab diagnostics -----------------------------------------

extern "C" JNIEXPORT jstring JNICALL
Java_org_rackdroid_AudioLabActivity_nativeGetAudioStatus(JNIEnv* env, jobject thiz) {
	std::string text;
	{
		std::lock_guard<std::mutex> lock(gActiveDeviceMutex);
		if (gActiveDevice)
			text = gActiveDevice->diagnostics();
		else {
			std::ostringstream s;
			s << "RackDroid audio stream is not active in this process.\n"
			  << "Open RackDroid, let the test patch run, then open Audio Lab and Refresh.\n"
			  << "The checkboxes below are the config file for the next full RackDroid launch.";
			text = s.str();
		}
	}
	return env->NewStringUTF(text.c_str());
}



extern "C" JNIEXPORT jboolean JNICALL
Java_org_rackdroid_AudioLabActivity_nativeResetAudioMeasurements(JNIEnv* env, jobject thiz) {
	std::lock_guard<std::mutex> lock(gActiveDeviceMutex);
	if (!gActiveDevice)
		return (jboolean) 0;
	gActiveDevice->resetMeasurements();
	return (jboolean) 1;
}


// ---- JNI: master recording toggle (MainActivity's ⏺ button) ----

extern "C" JNIEXPORT jboolean JNICALL
Java_org_rackdroid_MainActivity_nativeRecordStart(JNIEnv* env, jobject thiz, jstring jPath) {
	const char* chars = env->GetStringUTFChars(jPath, NULL);
	std::string path = chars ? chars : "";
	env->ReleaseStringUTFChars(jPath, chars);
	// The stream's actual rate lives in the device; 48k is the default and
	// the only rate the port opens in practice unless the user changed it.
	int sr = DEFAULT_SAMPLE_RATE;
	if (APP && APP->engine)
		sr = (int) APP->engine->getSampleRate();
	bool ok = gRecorder.start(path, sr, NUM_OUTPUTS);
	__android_log_print(ANDROID_LOG_INFO, "rackdroid", "record start %s: %d", path.c_str(), ok);
	return ok;
}

extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeRecordStop(JNIEnv* env, jobject thiz) {
	gRecorder.stop();
	__android_log_print(ANDROID_LOG_INFO, "rackdroid", "record stop");
}


} // namespace rackdroid
