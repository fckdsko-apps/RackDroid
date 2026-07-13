#include "ML_modules.hpp"
#include "SettingsHandler.hpp"


#define MAX(a,b) a>b?a:b

struct ShiftRegister : Module {
    enum ParamIds {
        NUM_PARAMS
    };

    enum InputIds {
        IN_INPUT,
        TRIGGER_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        OUT1_OUTPUT,
        OUT2_OUTPUT,
        OUT3_OUTPUT,
        OUT4_OUTPUT,
        OUT5_OUTPUT,
        OUT6_OUTPUT,
        OUT7_OUTPUT,
        OUT8_OUTPUT,
        // OUT_OUTPUT,
        NUM_OUTPUTS
    };

    enum LightIds {
        STEP1_LIGHT,
        NUM_LIGHTS = STEP1_LIGHT+16
    };


    ShiftRegister() {
        config( NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS ); 

        configInput(IN_INPUT, "CV");
        configInput(TRIGGER_INPUT, "Trigger");	
        for(int i=0; i<8; i++) {
            configOutput(OUT1_OUTPUT+i, "CV #"+std::to_string(i+1));
            configLight(STEP1_LIGHT+i, "Step "+std::to_string(i+1) + " (Channel 0)");
        }

        onReset();
    };

    json_t *dataToJson() override {

        json_t *rootJ = json_object();

        json_object_set_new(rootJ, "resetOnStart", json_integer(resetOnStart));

        json_t *valuesJ = json_array();
        for (int i = 0; i < 8*PORT_MAX_CHANNELS; i++) {
            json_t *stepJ = json_real( (float) values[i]);
            json_array_append_new(valuesJ, stepJ);
        }
        json_object_set_new(rootJ, "values", valuesJ);

        return rootJ;
    }

    void dataFromJson(json_t *rootJ) override {


        json_t *resetJ = json_object_get(rootJ, "resetOnStart");
        if(resetJ) resetOnStart =  json_integer_value(resetJ);

        json_t *valuesJ = json_object_get(rootJ, "values");
        if(!resetOnStart && valuesJ) {
            for (int i = 0; i < 8*PORT_MAX_CHANNELS; i++) {
                json_t *stepJ = json_array_get(valuesJ, i);
                values[i] = json_number_value(stepJ);
            }
        }
    }

    void process(const ProcessArgs &args) override;

    int position=0;
    bool resetOnStart = true;


    float values[8 * PORT_MAX_CHANNELS] = {};
    int channels[8];

    dsp::SchmittTrigger setTrigger[PORT_MAX_CHANNELS];

    void onReset() override {
        position=0;
        Module::onReset();
        memset(channels, 0, 8*sizeof(int));
        memset(values,   0, 8*PORT_MAX_CHANNELS*sizeof(float));
        for(int i=0; i<8; i++) {
            lights[i].value = 0.0;
        };
    };

};


void ShiftRegister::process(const ProcessArgs &args) {

    if( inputs[TRIGGER_INPUT].isConnected() ) {

        int trig_channels = inputs[TRIGGER_INPUT].getChannels();

        if(trig_channels == 1) {

            if( setTrigger[0].process(inputs[TRIGGER_INPUT].getVoltage()) ) {

                for(int i=7; i>0; i--) {
                    memcpy(values+i*PORT_MAX_CHANNELS, values+(i-1)*PORT_MAX_CHANNELS, channels[i-1]*sizeof(float));
                    channels[i] = channels[i-1];
                }
                channels[0] = inputs[IN_INPUT].getChannels();
                memcpy(values, inputs[IN_INPUT].getVoltages(), channels[0]*sizeof(float));
            }

        } else {

            for(int c=0; c < trig_channels; c++) {

                if( setTrigger[c].process(inputs[TRIGGER_INPUT].getVoltage(c)) ) {

                    for(int i=7; i>0; i--) {
                        values[i*PORT_MAX_CHANNELS + c] = values[(i-1)*PORT_MAX_CHANNELS + c];
                        channels[i] = MAX(channels[i-1], channels[i]);
                    }
                    channels[0] = inputs[IN_INPUT].getChannels();
                    values[c]   = inputs[IN_INPUT].getVoltage(c);
                }
            }
            
        }

        for(int i=0; i<8; i++) {
            outputs[OUT1_OUTPUT+i].setChannels(channels[i]);
            outputs[OUT1_OUTPUT+i].writeVoltages(values+i*PORT_MAX_CHANNELS);

//			float value = channels[i]>0?outputs[OUT1_OUTPUT+i].getVoltageSum():5.0f; //values[i*PORT_MAX_CHANNELS];
            float value = values[i*PORT_MAX_CHANNELS];
            bool positive = value>0.0f;
            lights[STEP1_LIGHT+2*i  ].setBrightness(  positive?value:0.0   );
            lights[STEP1_LIGHT+2*i+1].setBrightness( -(positive?0.0:value) );
        } // i

    }; // if trigger connected


};

struct ResetOnStartItem : MenuItem {

    ShiftRegister *module;

    void onAction(const event::Action &e) override {
        module->resetOnStart = !module->resetOnStart;
    };

    void step() override {
        rightText = (module->resetOnStart)? "✔" : "";
    };

};



struct ShiftRegisterWidget : ModuleWidget {
    ShiftRegisterWidget(ShiftRegister *module);
    void appendContextMenu(Menu *) override;
};

ShiftRegisterWidget::ShiftRegisterWidget(ShiftRegister *module) {
        setModule(module);

    box.size = Vec(15*4, 380);

    {
        SvgPanel *panel = new SvgPanel();
        panel->box.size = box.size;
        panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ShiftReg.svg")));

        addChild(panel);
    }

    addChild(createWidget<MLScrew>(Vec(15, 0)));
    addChild(createWidget<MLScrew>(Vec(15, 365)));




    const float offset_y = 140, delta_y = 26, offset_x=11;

    for( int i=0; i<8; i++) {

        addOutput(createOutput<MLPortOut>(Vec(offset_x+17, offset_y + i*delta_y  ), module, ShiftRegister::OUT1_OUTPUT+i));
        addChild(createLight<MediumLight<GreenRedLight>>(Vec(offset_x, offset_y + 8 +   i*delta_y), module, ShiftRegister::STEP1_LIGHT+2*i));
    };


    addInput(createInput<MLPort>(Vec(offset_x+17, 58), module, ShiftRegister::IN_INPUT));
    addInput(createInput<MLPort>(Vec(offset_x+17, 94), module, ShiftRegister::TRIGGER_INPUT));

}

void ShiftRegisterWidget::appendContextMenu(Menu *menu) {

    ShiftRegister *shiftReg = dynamic_cast<ShiftRegister*>(module);
    assert(shiftReg);
    
    MenuLabel *modeLabel = new MenuLabel();
    modeLabel->text = "Cloner Mode";
    menu->addChild(modeLabel);

    ResetOnStartItem *reset_Item = new ResetOnStartItem();
    reset_Item->text = "Reset on start";
    reset_Item->module = shiftReg;
    menu->addChild(reset_Item);


}


Model *modelShiftRegister = createModel<ShiftRegister, ShiftRegisterWidget>("ShiftRegister");