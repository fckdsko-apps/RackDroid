/******************************************************************************
 * Copyright 2017-2023 Valerio Orlandini / Sonus Dept. <sonusdept@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/


#include "sonusmodular.hpp"


struct Fluidmix : Module
{
    enum ParamIds
    {
        MIX_POS,
        NUM_PARAMS
    };
    enum InputIds
    {
        CV_MIX_POS,
        INPUT_1,
        INPUT_2,
        INPUT_3,
        INPUT_4,
        NUM_INPUTS
    };
    enum OutputIds
    {
        OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds
    {
        NUM_LIGHTS
    };

    Fluidmix()
    {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(Fluidmix::MIX_POS, 1.0f, 4.0f, 1.0f, "");
    }

    void process(const ProcessArgs &args) override;
};


void Fluidmix::process(const ProcessArgs &args)
{
    float position = params[MIX_POS].getValue();
    position += inputs[CV_MIX_POS].getVoltage() * 0.3f;
    position = clamp(position, 1.0f, 4.0f);

    float a_in = floor(position);
    float b_in = ceil(position);

    float b_gain = position - a_in;
    float a_gain = 1.0f - b_gain;

    float out = inputs[(int)a_in].getVoltage() * a_gain;
    out += inputs[(int)b_in].getVoltage() * b_gain;

    outputs[OUTPUT].setVoltage(out);
}

struct FluidmixWidget : ModuleWidget
{
    FluidmixWidget(Fluidmix *module)
    {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/fluidmix.svg")));

        addChild(createWidget<SonusScrew>(Vec(0, 0)));
        addChild(createWidget<SonusScrew>(Vec(box.size.x - 15, 0)));
        addChild(createWidget<SonusScrew>(Vec(0, 365)));
        addChild(createWidget<SonusScrew>(Vec(box.size.x - 15, 365)));

        addInput(createInput<PJ301MPort>(Vec(14, 67), module, Fluidmix::CV_MIX_POS));
        addParam(createParam<SonusKnob>(Vec(46, 61), module, Fluidmix::MIX_POS));
        addInput(createInput<PJ301MPort>(Vec(14, 132), module, Fluidmix::INPUT_1));
        addInput(createInput<PJ301MPort>(Vec(52, 132), module, Fluidmix::INPUT_2));
        addInput(createInput<PJ301MPort>(Vec(14, 197), module, Fluidmix::INPUT_3));
        addInput(createInput<PJ301MPort>(Vec(52, 197), module, Fluidmix::INPUT_4));
        addOutput(createOutput<PJ301MPort>(Vec(33, 262), module, Fluidmix::OUTPUT));
    }
};

Model *modelFluidmix = createModel<Fluidmix, FluidmixWidget>("Fluidmix");
