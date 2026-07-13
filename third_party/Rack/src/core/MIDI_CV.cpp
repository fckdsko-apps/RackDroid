#include <algorithm>

#include "plugin.hpp"


namespace rack {
namespace core {


struct MIDI_CV : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		PITCH_OUTPUT,
		GATE_OUTPUT,
		VELOCITY_OUTPUT,
		AFTERTOUCH_OUTPUT,
		PW_OUTPUT,
		MOD_OUTPUT,
		RETRIGGER_OUTPUT,
		CLOCK_OUTPUT,
		CLOCK_DIV_OUTPUT,
		START_OUTPUT,
		STOP_OUTPUT,
		CONTINUE_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	midi::InputQueue midiInput;
	dsp::MidiParser<16> midiParser;

	MIDI_CV() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configOutput(PITCH_OUTPUT, "1V/octave pitch");
		configOutput(GATE_OUTPUT, "Gate");
		configOutput(VELOCITY_OUTPUT, "Velocity");
		configOutput(AFTERTOUCH_OUTPUT, "Aftertouch");
		configOutput(PW_OUTPUT, "Pitch wheel");
		configOutput(MOD_OUTPUT, "Mod wheel");
		configOutput(RETRIGGER_OUTPUT, "Retrigger");
		configOutput(CLOCK_OUTPUT, "Clock");
		configOutput(CLOCK_DIV_OUTPUT, "Clock divider");
		configOutput(START_OUTPUT, "Start trigger");
		configOutput(STOP_OUTPUT, "Stop trigger");
		configOutput(CONTINUE_OUTPUT, "Continue trigger");
	}

	void onReset() override {
		midiParser.reset();
		midiInput.reset();
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			midiParser.processMessage(msg);
		}

		midiParser.processFilters(args.sampleTime);

		// Set note outputs
		outputs[PITCH_OUTPUT].setChannels(midiParser.channels);
		outputs[GATE_OUTPUT].setChannels(midiParser.channels);
		outputs[VELOCITY_OUTPUT].setChannels(midiParser.channels);
		outputs[AFTERTOUCH_OUTPUT].setChannels(midiParser.channels);
		outputs[RETRIGGER_OUTPUT].setChannels(midiParser.channels);
		for (uint8_t c = 0; c < midiParser.channels; c++) {
			outputs[PITCH_OUTPUT].setVoltage(midiParser.getPitchVoltage(c), c);
			outputs[GATE_OUTPUT].setVoltage(midiParser.gates[c] ? 10.f : 0.f, c);
			outputs[VELOCITY_OUTPUT].setVoltage(midiParser.velocities[c] / 127.f * 10.f, c);
			outputs[AFTERTOUCH_OUTPUT].setVoltage(midiParser.aftertouches[c] / 127.f * 10.f, c);
			outputs[RETRIGGER_OUTPUT].setVoltage(midiParser.retriggerPulses[c].isHigh() ? 10.f : 0.f, c);
		}

		// Pitch and mod wheel outputs
		uint8_t wheelChannels = midiParser.getWheelChannels();
		outputs[PW_OUTPUT].setChannels(wheelChannels);
		outputs[MOD_OUTPUT].setChannels(wheelChannels);
		for (uint8_t c = 0; c < wheelChannels; c++) {
			outputs[PW_OUTPUT].setVoltage(midiParser.getPw(c) * 5.f, c);
			outputs[MOD_OUTPUT].setVoltage(midiParser.getMod(c) * 10.f, c);
		}

		// Set clock and transport outputs
		outputs[CLOCK_OUTPUT].setVoltage(midiParser.clockPulse.isHigh() ? 10.f : 0.f);
		outputs[CLOCK_DIV_OUTPUT].setVoltage(midiParser.clockDividerPulse.isHigh() ? 10.f : 0.f);
		outputs[START_OUTPUT].setVoltage(midiParser.startPulse.isHigh() ? 10.f : 0.f);
		outputs[STOP_OUTPUT].setVoltage(midiParser.stopPulse.isHigh() ? 10.f : 0.f);
		outputs[CONTINUE_OUTPUT].setVoltage(midiParser.continuePulse.isHigh() ? 10.f : 0.f);

		midiParser.processPulses(args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* rootJ = midiParser.toJson();
		json_object_set_new(rootJ, "midi", midiInput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// For backwards compatibility, set to 0 if undefined in JSON.
		midiParser.pwRange = 0;

		midiParser.fromJson(rootJ);

		json_t* midiJ = json_object_get(rootJ, "midi");
		if (midiJ)
			midiInput.fromJson(midiJ);
	}
};


struct MIDI_CVWidget : ModuleWidget {
	MIDI_CVWidget(MIDI_CV* module) {
		setModule(module);
		setPanel(createPanel(asset::system("res/Core/MIDI_CV.svg"), asset::system("res/Core/MIDI_CV-dark.svg")));

		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(7.905, 64.347)), module, MIDI_CV::PITCH_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(20.248, 64.347)), module, MIDI_CV::GATE_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(32.591, 64.347)), module, MIDI_CV::VELOCITY_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(7.905, 80.603)), module, MIDI_CV::AFTERTOUCH_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(20.248, 80.603)), module, MIDI_CV::PW_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(32.591, 80.603)), module, MIDI_CV::MOD_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(7.905, 96.859)), module, MIDI_CV::CLOCK_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(20.248, 96.707)), module, MIDI_CV::CLOCK_DIV_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(32.591, 96.859)), module, MIDI_CV::RETRIGGER_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(7.905, 113.115)), module, MIDI_CV::START_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(20.248, 113.115)), module, MIDI_CV::STOP_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(32.591, 112.975)), module, MIDI_CV::CONTINUE_OUTPUT));

		MidiDisplay* display = createWidget<MidiDisplay>(mm2px(Vec(0.0, 13.048)));
		display->box.size = mm2px(Vec(40.64, 29.012));
		display->setMidiPort(module ? &module->midiInput : NULL);
		addChild(display);

		// MidiButton example
		// MidiButton* midiButton = createWidget<MidiButton_MIDI_DIN>(Vec(0, 0));
		// midiButton->setMidiPort(module ? &module->midiInput : NULL);
		// addChild(midiButton);
	}

	void appendContextMenu(Menu* menu) override {
		MIDI_CV* module = dynamic_cast<MIDI_CV*>(this->module);

		menu->addChild(new MenuSeparator);

		static const std::vector<float> pwRanges = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 24, 36, 48};
		auto getPwRangeLabel = [](float pwRange) -> std::string {
			if (pwRange == 0)
				return "Off";
			else if (std::abs(pwRange) < 12)
				return string::f("%g semitone", pwRange) + (pwRange == 1 ? "" : "s");
			else
				return string::f("%g octave", pwRange / 12) + (pwRange / 12 == 1 ? "" : "s");
		};
		menu->addChild(createSubmenuItem("Pitch bend range", getPwRangeLabel(module->midiParser.pwRange), [=](Menu* menu) {
			for (size_t i = 0; i < pwRanges.size(); i++) {
				menu->addChild(createCheckMenuItem(getPwRangeLabel(pwRanges[i]), "",
					[=]() {return module->midiParser.pwRange == pwRanges[i];},
					[=]() {module->midiParser.pwRange = pwRanges[i];}
				));
			}
		}));

		menu->addChild(createBoolPtrMenuItem("Smooth pitch/mod wheel", "", &module->midiParser.smooth));

		static const std::vector<uint32_t> clockDivisions = {24 * 4, 24 * 2, 24, 24 / 2, 24 / 4, 24 / 8, 2, 1};
		static const std::vector<std::string> clockDivisionLabels = {"Whole", "Half", "Quarter", "8th", "16th", "32nd", "12 PPQN", "24 PPQN"};
		size_t clockDivisionIndex = std::find(clockDivisions.begin(), clockDivisions.end(), module->midiParser.clockDivision) - clockDivisions.begin();
		std::string clockDivisionLabel = (clockDivisionIndex < clockDivisionLabels.size()) ? clockDivisionLabels[clockDivisionIndex] : "";
		menu->addChild(createSubmenuItem("CLK/N divider", clockDivisionLabel, [=](Menu* menu) {
			for (size_t i = 0; i < clockDivisions.size(); i++) {
				menu->addChild(createCheckMenuItem(clockDivisionLabels[i], "",
					[=]() {return module->midiParser.clockDivision == clockDivisions[i];},
					[=]() {module->midiParser.clockDivision = clockDivisions[i];}
				));
			}
		}));

		menu->addChild(createSubmenuItem("Polyphony channels", string::f("%d", module->midiParser.channels), [=](Menu* menu) {
			for (int c = 1; c <= 16; c++) {
				std::string channelsLabel = (c == 1) ? "Monophonic" : string::f("%d", c);
				menu->addChild(createCheckMenuItem(channelsLabel, "",
					[=]() {return module->midiParser.channels == c;},
					[=]() {module->midiParser.setChannels(c);}
				));
			}
		}));

		menu->addChild(createIndexPtrSubmenuItem("Polyphony mode", {
			"Rotate",
			"Reuse",
			"Reset",
			"MPE",
		}, &module->midiParser.polyMode));

		menu->addChild(createMenuItem("Panic", "",
			[=]() {module->midiParser.panic();}
		));

		// Example of using appendMidiMenu()
		// menu->addChild(new MenuSeparator);
		// appendMidiMenu(menu, &module->midiInput);
	}
};


// Use legacy slug for compatibility
Model* modelMIDI_CV = createModel<MIDI_CV, MIDI_CVWidget>("MIDIToCVInterface");


} // namespace core
} // namespace rack
