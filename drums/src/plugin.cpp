#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelBD808);
	p->addModel(modelSD808);
	p->addModel(modelHH808);
	p->addModel(modelCP808);
	p->addModel(modelCB808);
	p->addModel(modelTM808);
	p->addModel(modelBD909);
	p->addModel(modelSD909);
	p->addModel(modelHH909);
	p->addModel(modelBD606);
	p->addModel(modelHH606);
	p->addModel(modelSD707);
	p->addModel(modelCB707);
	p->addModel(modelTM505);
}
