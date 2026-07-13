/* RackDroid Drums — original 808-style analog drum voices.
 *
 * All DSP written from scratch for RackDroid (GPL-3.0-or-later, no
 * third-party art or code): each voice models the classic architecture —
 * bridged-T resonators approximated by exponentially decaying sines,
 * square banks through band filters for metallic voices, filtered noise
 * for snap — rather than sampling.
 *
 * Widgets use only Rack's stock component classes, whose SVGs RackDroid
 * already ships as original regenerated art, so the pack adds no new
 * licensing surface.
 */
#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelBD808;
extern Model* modelSD808;
extern Model* modelHH808;
extern Model* modelCP808;
extern Model* modelCB808;
extern Model* modelTM808;
extern Model* modelBD909;
extern Model* modelSD909;
extern Model* modelHH909;
extern Model* modelBD606;
extern Model* modelHH606;
extern Model* modelSD707;
extern Model* modelCB707;
extern Model* modelTM505;


namespace rddrums {


/** One-pole highpass (RC), coefficient set by cutoff in Hz. */
struct OnePoleHP {
	float y = 0.f, xPrev = 0.f, a = 0.99f;
	void setCutoff(float fc, float sr) {
		float rc = 1.f / (2.f * M_PI * fc);
		a = rc / (rc + 1.f / sr);
	}
	float process(float x) {
		y = a * (y + x - xPrev);
		xPrev = x;
		return y;
	}
};

/** One-pole lowpass (RC). */
struct OnePoleLP {
	float y = 0.f, a = 0.5f;
	void setCutoff(float fc, float sr) {
		float rc = 1.f / (2.f * M_PI * fc);
		float dt = 1.f / sr;
		a = dt / (rc + dt);
	}
	float process(float x) {
		y += a * (x - y);
		return y;
	}
};

/** Exponential decay envelope: 1 -> 0 with time constant tau seconds. */
struct ExpEnv {
	float v = 0.f;
	void trigger() { v = 1.f; }
	float process(float tau, float dt) {
		v *= std::exp(-dt / std::fmax(tau, 1e-4f));
		return v;
	}
};

/** Cheap white noise (xorshift), deterministic per instance. */
struct Noise {
	uint32_t s = 0x9E3779B9u;
	float next() {
		s ^= s << 13; s ^= s >> 17; s ^= s << 5;
		return (float) (int32_t) s / 2147483648.f;
	}
};


} // namespace rddrums
