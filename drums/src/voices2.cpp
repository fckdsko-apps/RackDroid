/* Second wave of voices: 909 / 707 / 606 / 505 architectures. Same original
 * DSP approach and widget scaffolding as voices.cpp — these model the later
 * machines' character: the 909's punchier click-and-drive kick and brighter
 * metal, the 606's thin fizzy hats, the 707's mid-forward snare and bell,
 * the 505's plasticky swept tom. */
#include "plugin.hpp"

using namespace rddrums;


// The shared 6HP layout template lives in voices.cpp; duplicated typedef
// here would collide, so voices.cpp exposes it via plugin.hpp? No — it is
// a template defined in that TU only. Redefine the same scaffolding under
// a distinct name (identical geometry keeps the packs visually uniform).
template <typename TModule>
struct DrumWidget2 : ModuleWidget {
	static constexpr float W = 90.f;

	DrumWidget2(TModule* module, const char* panel) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, panel)));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}

	void addKnobRow(int paramId, int cvInputId, float y) {
		addParam(createParamCentered<RoundBlackKnob>(Vec(W * 0.35f, y), module, paramId));
		if (cvInputId >= 0)
			addInput(createInputCentered<PJ301MPort>(Vec(W * 0.75f, y), module, cvInputId));
	}

	void addBottomRow(int trigInputId, int outId, int trig2InputId = -1) {
		addInput(createInputCentered<PJ301MPort>(Vec(W * 0.25f, 330), module, trigInputId));
		if (trig2InputId >= 0)
			addInput(createInputCentered<PJ301MPort>(Vec(W * 0.5f, 330), module, trig2InputId));
		addOutput(createOutputCentered<PJ301MPort>(Vec(W * 0.75f, 330), module, outId));
	}
};


// ---- BD-909: bass drum (punchier click + drive than the 808) ---------------

struct BD909 : Module {
	enum ParamId { TUNE_PARAM, DECAY_PARAM, PUNCH_PARAM, NUM_PARAMS };
	enum InputId { TRIG_INPUT, TUNE_INPUT, DECAY_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	dsp::SchmittTrigger trig;
	float phase = 0.f;
	ExpEnv amp, pitch, click;
	Noise noise;
	OnePoleLP clickLp;

	BD909() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(TUNE_PARAM, 35.f, 110.f, 56.f, "Tune", " Hz");
		configParam(DECAY_PARAM, 0.05f, 1.2f, 0.35f, "Decay", " s");
		configParam(PUNCH_PARAM, 0.f, 1.f, 0.5f, "Punch");
		configInput(TRIG_INPUT, "Trigger");
		configInput(TUNE_INPUT, "Tune CV");
		configInput(DECAY_INPUT, "Decay CV");
		configOutput(OUT_OUTPUT, "Audio");
	}

	void process(const ProcessArgs& args) override {
		if (trig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
			amp.trigger();
			pitch.trigger();
			click.trigger();
		}
		float f0 = params[TUNE_PARAM].getValue() * std::pow(2.f, inputs[TUNE_INPUT].getVoltage() / 5.f);
		float decay = clamp(params[DECAY_PARAM].getValue() + inputs[DECAY_INPUT].getVoltage() * 0.1f, 0.05f, 2.f);
		float punch = params[PUNCH_PARAM].getValue();

		// Faster, deeper sweep than the 808 and a hard filtered-noise click
		// riding the first couple of milliseconds.
		float p = pitch.process(0.018f, args.sampleTime);
		float a = amp.process(decay, args.sampleTime);
		float c = click.process(0.004f, args.sampleTime);

		float f = f0 * (1.f + 6.f * p * p);
		phase += f * args.sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;

		clickLp.setCutoff(6000.f, args.sampleRate);
		float attack = clickLp.process(noise.next()) * c * punch * 3.f;

		float body = std::sin(2.f * M_PI * phase) * a;
		float out = std::tanh((1.4f + punch * 1.4f) * (body + attack)) * 5.f;
		outputs[OUT_OUTPUT].setVoltage(out);
	}
};

struct BD909Widget : DrumWidget2<BD909> {
	BD909Widget(BD909* module) : DrumWidget2(module, "res/BD909.svg") {
		addKnobRow(BD909::TUNE_PARAM, BD909::TUNE_INPUT, 90);
		addKnobRow(BD909::DECAY_PARAM, BD909::DECAY_INPUT, 165);
		addKnobRow(BD909::PUNCH_PARAM, -1, 240);
		addBottomRow(BD909::TRIG_INPUT, BD909::OUT_OUTPUT);
	}
};

Model* modelBD909 = createModel<BD909, BD909Widget>("BD909");


// ---- SD-909: snare drum (brighter, tone-controlled noise) ------------------

struct SD909 : Module {
	enum ParamId { TUNE_PARAM, TONE_PARAM, SNAPPY_PARAM, NUM_PARAMS };
	enum InputId { TRIG_INPUT, TUNE_INPUT, SNAPPY_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	dsp::SchmittTrigger trig;
	float ph1 = 0.f, ph2 = 0.f;
	ExpEnv shellEnv, noiseEnv;
	Noise noise;
	OnePoleHP noiseHp;
	OnePoleLP noiseLp;

	SD909() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(TUNE_PARAM, 130.f, 330.f, 195.f, "Tune", " Hz");
		configParam(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone");
		configParam(SNAPPY_PARAM, 0.f, 1.f, 0.7f, "Snappy");
		configInput(TRIG_INPUT, "Trigger");
		configInput(TUNE_INPUT, "Tune CV");
		configInput(SNAPPY_INPUT, "Snappy CV");
		configOutput(OUT_OUTPUT, "Audio");
	}

	void process(const ProcessArgs& args) override {
		if (trig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
			shellEnv.trigger();
			noiseEnv.trigger();
		}
		float f0 = params[TUNE_PARAM].getValue() * std::pow(2.f, inputs[TUNE_INPUT].getVoltage() / 5.f);
		float tone = params[TONE_PARAM].getValue();
		float snappy = clamp(params[SNAPPY_PARAM].getValue() + inputs[SNAPPY_INPUT].getVoltage() * 0.1f, 0.f, 1.f);

		// Shorter shell than the 808, second partial slightly sharper.
		float shell = shellEnv.process(0.09f, args.sampleTime);
		ph1 += f0 * args.sampleTime;          if (ph1 >= 1.f) ph1 -= 1.f;
		ph2 += f0 * 1.78f * args.sampleTime;  if (ph2 >= 1.f) ph2 -= 1.f;
		float body = (std::sin(2.f * M_PI * ph1) * 0.65f + std::sin(2.f * M_PI * ph2) * 0.35f) * shell;

		// Tone opens the noise band upward: the 909's crack.
		noiseHp.setCutoff(900.f + tone * 2400.f, args.sampleRate);
		noiseLp.setCutoff(6000.f + tone * 8000.f, args.sampleRate);
		float snap = noiseLp.process(noiseHp.process(noise.next()))
			* noiseEnv.process(0.16f, args.sampleTime) * snappy * 2.6f;

		outputs[OUT_OUTPUT].setVoltage(std::tanh(1.5f * (body + snap)) * 5.f);
	}
};

struct SD909Widget : DrumWidget2<SD909> {
	SD909Widget(SD909* module) : DrumWidget2(module, "res/SD909.svg") {
		addKnobRow(SD909::TUNE_PARAM, SD909::TUNE_INPUT, 90);
		addKnobRow(SD909::TONE_PARAM, -1, 165);
		addKnobRow(SD909::SNAPPY_PARAM, SD909::SNAPPY_INPUT, 240);
		addBottomRow(SD909::TRIG_INPUT, SD909::OUT_OUTPUT);
	}
};

Model* modelSD909 = createModel<SD909, SD909Widget>("SD909");


// ---- HH-909: hi-hats (brighter bank, closed chokes open) -------------------

struct HH909 : Module {
	enum ParamId { TONE_PARAM, DECAY_PARAM, NUM_PARAMS };
	enum InputId { CH_INPUT, OH_INPUT, TONE_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	dsp::SchmittTrigger chTrig, ohTrig;
	float phases[6] = {};
	ExpEnv env;
	bool open = false;
	Noise noise;
	OnePoleHP hp1, hp2;
	OnePoleLP lp;

	static constexpr float RATIOS[6] = {1.0f, 1.1873f, 1.4471f, 1.7038f, 1.9391f, 2.2141f};

	HH909() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(TONE_PARAM, 0.5f, 2.f, 1.f, "Tone");
		configParam(DECAY_PARAM, 0.08f, 1.f, 0.3f, "Open decay", " s");
		configInput(CH_INPUT, "Closed trigger");
		configInput(OH_INPUT, "Open trigger");
		configInput(TONE_INPUT, "Tone CV");
		configOutput(OUT_OUTPUT, "Audio");
	}

	void process(const ProcessArgs& args) override {
		if (ohTrig.process(inputs[OH_INPUT].getVoltage(), 0.1f, 1.f)) {
			env.trigger();
			open = true;
		}
		if (chTrig.process(inputs[CH_INPUT].getVoltage(), 0.1f, 1.f)) {
			env.trigger();
			open = false;
		}
		float tone = params[TONE_PARAM].getValue() * std::pow(2.f, inputs[TONE_INPUT].getVoltage() / 5.f);
		float base = 410.f * tone;

		float bank = 0.f;
		for (int i = 0; i < 6; i++) {
			phases[i] += base * RATIOS[i] * args.sampleTime;
			if (phases[i] >= 1.f)
				phases[i] -= 1.f;
			bank += (phases[i] < 0.5f) ? 1.f : -1.f;
		}
		bank /= 6.f;
		// A whisper of noise gives the 909's sizzling top end.
		bank += noise.next() * 0.12f;

		hp1.setCutoff(6500.f, args.sampleRate);
		hp2.setCutoff(9000.f, args.sampleRate);
		lp.setCutoff(15000.f, args.sampleRate);
		float metal = lp.process(hp2.process(hp1.process(bank)));

		float tau = open ? params[DECAY_PARAM].getValue() : 0.035f;
		float out = metal * env.process(tau, args.sampleTime) * 6.5f;
		outputs[OUT_OUTPUT].setVoltage(std::tanh(out) * 5.f);
	}
};

constexpr float HH909::RATIOS[6];

struct HH909Widget : DrumWidget2<HH909> {
	HH909Widget(HH909* module) : DrumWidget2(module, "res/HH909.svg") {
		addKnobRow(HH909::TONE_PARAM, HH909::TONE_INPUT, 90);
		addKnobRow(HH909::DECAY_PARAM, -1, 165);
		addBottomRow(HH909::CH_INPUT, HH909::OUT_OUTPUT, HH909::OH_INPUT);
	}
};

Model* modelHH909 = createModel<HH909, HH909Widget>("HH909");


// ---- BD-606: bass drum (small, tight, higher-pitched) ----------------------

struct BD606 : Module {
	enum ParamId { TUNE_PARAM, DECAY_PARAM, NUM_PARAMS };
	enum InputId { TRIG_INPUT, TUNE_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	dsp::SchmittTrigger trig;
	float phase = 0.f;
	ExpEnv amp, pitch;

	BD606() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(TUNE_PARAM, 45.f, 140.f, 68.f, "Tune", " Hz");
		configParam(DECAY_PARAM, 0.05f, 0.6f, 0.22f, "Decay", " s");
		configInput(TRIG_INPUT, "Trigger");
		configInput(TUNE_INPUT, "Tune CV");
		configOutput(OUT_OUTPUT, "Audio");
	}

	void process(const ProcessArgs& args) override {
		if (trig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
			amp.trigger();
			pitch.trigger();
		}
		float f0 = params[TUNE_PARAM].getValue() * std::pow(2.f, inputs[TUNE_INPUT].getVoltage() / 5.f);
		float p = pitch.process(0.02f, args.sampleTime);
		float a = amp.process(params[DECAY_PARAM].getValue(), args.sampleTime);

		float f = f0 * (1.f + 2.2f * p);
		phase += f * args.sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;

		// Light drive only: the small boxy thump of the cheap machine.
		float out = std::tanh(1.15f * std::sin(2.f * M_PI * phase) * a) * 5.f;
		outputs[OUT_OUTPUT].setVoltage(out);
	}
};

struct BD606Widget : DrumWidget2<BD606> {
	BD606Widget(BD606* module) : DrumWidget2(module, "res/BD606.svg") {
		addKnobRow(BD606::TUNE_PARAM, BD606::TUNE_INPUT, 90);
		addKnobRow(BD606::DECAY_PARAM, -1, 165);
		addBottomRow(BD606::TRIG_INPUT, BD606::OUT_OUTPUT);
	}
};

Model* modelBD606 = createModel<BD606, BD606Widget>("BD606");


// ---- HH-606: hi-hats (thin fizzy four-oscillator bank) ---------------------

struct HH606 : Module {
	enum ParamId { TONE_PARAM, DECAY_PARAM, NUM_PARAMS };
	enum InputId { CH_INPUT, OH_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	dsp::SchmittTrigger chTrig, ohTrig;
	float phases[4] = {};
	ExpEnv env;
	bool open = false;
	OnePoleHP hp1, hp2;

	static constexpr float RATIOS[4] = {1.0f, 1.4147f, 1.7873f, 2.0913f};

	HH606() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(TONE_PARAM, 0.5f, 2.f, 1.f, "Tone");
		configParam(DECAY_PARAM, 0.06f, 0.8f, 0.25f, "Open decay", " s");
		configInput(CH_INPUT, "Closed trigger");
		configInput(OH_INPUT, "Open trigger");
		configOutput(OUT_OUTPUT, "Audio");
	}

	void process(const ProcessArgs& args) override {
		if (ohTrig.process(inputs[OH_INPUT].getVoltage(), 0.1f, 1.f)) {
			env.trigger();
			open = true;
		}
		if (chTrig.process(inputs[CH_INPUT].getVoltage(), 0.1f, 1.f)) {
			env.trigger();
			open = false;
		}
		float base = 480.f * params[TONE_PARAM].getValue();

		float bank = 0.f;
		for (int i = 0; i < 4; i++) {
			phases[i] += base * RATIOS[i] * args.sampleTime;
			if (phases[i] >= 1.f)
				phases[i] -= 1.f;
			bank += (phases[i] < 0.5f) ? 1.f : -1.f;
		}
		bank /= 4.f;

		hp1.setCutoff(7000.f, args.sampleRate);
		hp2.setCutoff(10000.f, args.sampleRate);
		float metal = hp2.process(hp1.process(bank));

		float tau = open ? params[DECAY_PARAM].getValue() : 0.03f;
		float out = metal * env.process(tau, args.sampleTime) * 7.f;
		outputs[OUT_OUTPUT].setVoltage(std::tanh(out) * 5.f);
	}
};

constexpr float HH606::RATIOS[4];

struct HH606Widget : DrumWidget2<HH606> {
	HH606Widget(HH606* module) : DrumWidget2(module, "res/HH606.svg") {
		addKnobRow(HH606::TONE_PARAM, -1, 90);
		addKnobRow(HH606::DECAY_PARAM, -1, 165);
		addBottomRow(HH606::CH_INPUT, HH606::OUT_OUTPUT, HH606::OH_INPUT);
	}
};

Model* modelHH606 = createModel<HH606, HH606Widget>("HH606");


// ---- SD-707: snare drum (mid-forward, hard noise burst) --------------------

struct SD707 : Module {
	enum ParamId { TUNE_PARAM, SNAPPY_PARAM, DECAY_PARAM, NUM_PARAMS };
	enum InputId { TRIG_INPUT, TUNE_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	dsp::SchmittTrigger trig;
	float ph = 0.f;
	ExpEnv shellEnv, noiseEnv;
	Noise noise;
	OnePoleHP noiseHp;
	OnePoleLP noiseLp;

	SD707() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(TUNE_PARAM, 150.f, 350.f, 220.f, "Tune", " Hz");
		configParam(SNAPPY_PARAM, 0.f, 1.f, 0.65f, "Snappy");
		configParam(DECAY_PARAM, 0.05f, 0.4f, 0.14f, "Decay", " s");
		configInput(TRIG_INPUT, "Trigger");
		configInput(TUNE_INPUT, "Tune CV");
		configOutput(OUT_OUTPUT, "Audio");
	}

	void process(const ProcessArgs& args) override {
		if (trig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
			shellEnv.trigger();
			noiseEnv.trigger();
		}
		float f0 = params[TUNE_PARAM].getValue() * std::pow(2.f, inputs[TUNE_INPUT].getVoltage() / 5.f);
		float snappy = params[SNAPPY_PARAM].getValue();
		float decay = params[DECAY_PARAM].getValue();

		// Single mid-forward partial: the 707's boxy, punchy body.
		float shell = shellEnv.process(decay * 0.55f, args.sampleTime);
		ph += f0 * args.sampleTime;
		if (ph >= 1.f) ph -= 1.f;
		float body = std::sin(2.f * M_PI * ph) * 0.85f * shell;

		noiseHp.setCutoff(1400.f, args.sampleRate);
		noiseLp.setCutoff(9000.f, args.sampleRate);
		float snap = noiseLp.process(noiseHp.process(noise.next()))
			* noiseEnv.process(decay, args.sampleTime) * snappy * 2.8f;

		outputs[OUT_OUTPUT].setVoltage(std::tanh(1.6f * (body + snap)) * 5.f);
	}
};

struct SD707Widget : DrumWidget2<SD707> {
	SD707Widget(SD707* module) : DrumWidget2(module, "res/SD707.svg") {
		addKnobRow(SD707::TUNE_PARAM, SD707::TUNE_INPUT, 90);
		addKnobRow(SD707::SNAPPY_PARAM, -1, 165);
		addKnobRow(SD707::DECAY_PARAM, -1, 240);
		addBottomRow(SD707::TRIG_INPUT, SD707::OUT_OUTPUT);
	}
};

Model* modelSD707 = createModel<SD707, SD707Widget>("SD707");


// ---- CB-707: cowbell (brighter pair, faster decay than the 808) ------------

struct CB707 : Module {
	enum ParamId { TUNE_PARAM, DECAY_PARAM, NUM_PARAMS };
	enum InputId { TRIG_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	dsp::SchmittTrigger trig;
	float ph1 = 0.f, ph2 = 0.f;
	ExpEnv env;
	OnePoleHP hp;
	OnePoleLP lp;

	CB707() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(TUNE_PARAM, 0.7f, 1.5f, 1.f, "Tune");
		configParam(DECAY_PARAM, 0.04f, 0.4f, 0.13f, "Decay", " s");
		configInput(TRIG_INPUT, "Trigger");
		configOutput(OUT_OUTPUT, "Audio");
	}

	void process(const ProcessArgs& args) override {
		if (trig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f))
			env.trigger();
		float tune = params[TUNE_PARAM].getValue();
		ph1 += 562.f * tune * args.sampleTime; if (ph1 >= 1.f) ph1 -= 1.f;
		ph2 += 845.f * tune * args.sampleTime; if (ph2 >= 1.f) ph2 -= 1.f;
		float squares = ((ph1 < 0.5f) ? 1.f : -1.f) * 0.5f + ((ph2 < 0.5f) ? 1.f : -1.f) * 0.5f;

		hp.setCutoff(520.f * tune, args.sampleRate);
		lp.setCutoff(2400.f * tune, args.sampleRate);
		float band = lp.process(hp.process(squares));

		float out = band * env.process(params[DECAY_PARAM].getValue(), args.sampleTime) * 4.5f;
		outputs[OUT_OUTPUT].setVoltage(std::tanh(out) * 5.f);
	}
};

struct CB707Widget : DrumWidget2<CB707> {
	CB707Widget(CB707* module) : DrumWidget2(module, "res/CB707.svg") {
		addKnobRow(CB707::TUNE_PARAM, -1, 90);
		addKnobRow(CB707::DECAY_PARAM, -1, 165);
		addBottomRow(CB707::TRIG_INPUT, CB707::OUT_OUTPUT);
	}
};

Model* modelCB707 = createModel<CB707, CB707Widget>("CB707");


// ---- TM-505: tom / conga (plastic swept body) -------------------------------

struct TM505 : Module {
	enum ParamId { TUNE_PARAM, DECAY_PARAM, NUM_PARAMS };
	enum InputId { TRIG_INPUT, TUNE_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	dsp::SchmittTrigger trig;
	float phase = 0.f;
	ExpEnv amp, pitch;

	TM505() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(TUNE_PARAM, 80.f, 500.f, 190.f, "Tune", " Hz");
		configParam(DECAY_PARAM, 0.04f, 0.5f, 0.18f, "Decay", " s");
		configInput(TRIG_INPUT, "Trigger");
		configInput(TUNE_INPUT, "Tune CV");
		configOutput(OUT_OUTPUT, "Audio");
	}

	void process(const ProcessArgs& args) override {
		if (trig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
			amp.trigger();
			pitch.trigger();
		}
		float f0 = params[TUNE_PARAM].getValue() * std::pow(2.f, inputs[TUNE_INPUT].getVoltage() / 5.f);
		float p = pitch.process(0.04f, args.sampleTime);
		float a = amp.process(params[DECAY_PARAM].getValue(), args.sampleTime);

		float f = f0 * (1.f + 0.5f * p);
		phase += f * args.sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;

		// Sine with a pinch of square: the cheap "plastic" 505 character.
		float s = std::sin(2.f * M_PI * phase);
		float sq = (phase < 0.5f) ? 1.f : -1.f;
		float out = (s * 0.85f + sq * 0.15f) * a;
		outputs[OUT_OUTPUT].setVoltage(std::tanh(1.2f * out) * 5.f);
	}
};

struct TM505Widget : DrumWidget2<TM505> {
	TM505Widget(TM505* module) : DrumWidget2(module, "res/TM505.svg") {
		addKnobRow(TM505::TUNE_PARAM, TM505::TUNE_INPUT, 90);
		addKnobRow(TM505::DECAY_PARAM, -1, 165);
		addBottomRow(TM505::TRIG_INPUT, TM505::OUT_OUTPUT);
	}
};

Model* modelTM505 = createModel<TM505, TM505Widget>("TM505");
