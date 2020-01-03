#include "23volts.hpp"
#include "widgets/knobs.hpp"
#include "widgets/ports.hpp"
#include "common/mapping.hpp"

struct ParameterSnapshot {
	float values[8] = {};
};

struct Morph : Module {
	enum ParamIds {
		ENUMS(KNOB_PARAMS, 8),
		NUM_PARAMS
	};
	enum InputIds {
		X_CV_INPUT,
		Y_CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUTS, 8),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	MappingProcessor mappingProcessor;
	HandleMapCollection handleMap;

	ParameterSnapshot snapshots[4];

	float offsetX = 0;
	float offsetY = 0;
	float selectorX = 0;
	float selectorY = 0;
	float maxX;
	float maxY;

	bool inputX = false;
	bool inputY = false;
	dsp::ClockDivider XYInputCheckDivider;

	int writingSnapshot = 0;

	Morph() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for(int x = 0; x < 8; x++) {
			configParam(KNOB_PARAMS + x, -10.0, 10.0, 0.0, string::f("Knob %d", x));	
		}
		XYInputCheckDivider.setDivision(64);
		init();
	}

	void init() {
		mappingProcessor.handleMap = &handleMap;

		for(int paramId = 0; paramId < NUM_PARAMS; paramId++) {
			mappingProcessor.params[paramId] = paramQuantities[paramId];
		}
	}

	void process(const ProcessArgs& args) override {

		if (XYInputCheckDivider.process()) {
			inputX = inputs[X_CV_INPUT].isConnected();
			inputY = inputs[Y_CV_INPUT].isConnected();
		}

		if(writingSnapshot > -1) {
			updateSnapshot();
		}

		if(inputX) {
			float X_inputValue = math::clamp(inputs[X_CV_INPUT].getVoltage(), -10.f, 10.f);
			offsetX = (X_inputValue / 10.f) * maxX;
		}

		if(inputY) {
			float Y_inputValue = math::clamp(inputs[Y_CV_INPUT].getVoltage(), -10.f, 10.f);
			offsetY = - ((Y_inputValue / 10.f) * maxY);
		}

		if(inputX || inputY) updateParameters();

		for (int x = 0; x < 8; x++) {
			outputs[OUTPUTS + x].setVoltage(params[KNOB_PARAMS + x].getValue());
		}

		mappingProcessor.process();
	}

	void updateSnapshot() {
		for(int x = 0; x < 8; x++) {
			snapshots[writingSnapshot].values[x] = params[KNOB_PARAMS + x].getValue();
		}
	}

	void move(float x, float y) {
		selectorX += x;
		selectorY += y;
		if(selectorX < 0) selectorX = 0;
		if(selectorY < 0) selectorY = 0;
		if(selectorX > maxX) selectorX = maxX;
		if(selectorY > maxY) selectorY = maxY;
		writingSnapshot = getWritingSnapshot();
		updateParameters();
	}

	int getWritingSnapshot() {
		if(selectorX == 0 && selectorY == 0) {
			return 0;
		}
		if(selectorX == maxX && selectorY == 0) {
			return 1;
		}
		if(selectorX == 0 && selectorY == maxY) {
			return 2;
		}
		if(selectorX == maxX && selectorY == maxY) {
			return 3;
		}
		return -1;
	}

	float getX() const {
		return (selectorX + offsetX) < 0.f ? 0.f : 
			selectorX + offsetX > maxX ? maxX : selectorX + offsetX;
	}

	float getY() const {
		return (selectorY + offsetY) < 0.f ? 0.f : 
			selectorY + offsetY > maxY ? maxY : selectorY + offsetY;
	}

	void updateParameters() {
		float weights[4];
		float trueX = getX();
		float trueY = getY();
		float ratioX = trueX / maxX;
		float ratioY = trueY / maxY;

		weights[0] = (1.f - ratioX) * (1.f - ratioY);
		weights[1] = (0.f + ratioX) * (1.f - ratioY);
		weights[2] = (1.f - ratioX) * (0.f + ratioY);
		weights[3] = (0.f + ratioX) * (0.f + ratioY);

		for(int x = 0; x < 8; x++) {
			float knobValue = 0.f;
			for(int y = 0; y < 4; y++) {
				knobValue += snapshots[y].values[x] * weights[y];
			}
			params[KNOB_PARAMS + x].setValue(knobValue);
		}
	}

	void onReset() override {
		selectorX = 0;
		selectorY = 0;
		for(int x = 0; x < 4; x++) {
			snapshots[x] = ParameterSnapshot();
		}
		handleMap.clear();
	}

	void onRandomize() override {
		for(int x = 0; x < 4; x++) {
			for(int y = 0; y <8; y++) {
				snapshots[x].values[y] = 10.f - 20 * random::uniform();
			}
		}
		updateParameters();	
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "selectorX", json_integer(selectorX));
		json_object_set_new(rootJ, "selectorY", json_integer(selectorY));
		
		json_t* snapshotsJ = json_array();
		for (int i = 0; i < 4; i++) {
			json_t* snapshotJ = json_array();
			for(int z = 0; z < 8; z++) {
				json_array_insert_new(snapshotJ, z, json_real(snapshots[i].values[z]));	
			}
			json_array_insert_new(snapshotsJ, i, snapshotJ);
		}
		json_object_set_new(rootJ, "snapshots", snapshotsJ);
		json_object_set_new(rootJ, "handle_map", handleMap.toJson());
		json_object_set_new(rootJ, "writing_snapshot", json_integer(writingSnapshot));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* selectorXJ = json_object_get(rootJ, "selectorX");
		json_t* selectorYJ = json_object_get(rootJ, "selectorY");
		if(selectorXJ) selectorX = json_integer_value(selectorXJ);
		if(selectorYJ) selectorY = json_integer_value(selectorYJ);
			
		json_t* snapshotsJ = json_object_get(rootJ, "snapshots");
		if(snapshotsJ) {
			for (int i = 0; i < 4; i++) {
				json_t* snapshotJ = json_array_get(snapshotsJ, i);
				for(int z = 0; z < 8; z++) {
					snapshots[i].values[z] = json_real_value(json_array_get(snapshotJ, z));
				}
			}
		}

		json_t* handleMapJ = json_object_get(rootJ, "handle_map");
		if(handleMapJ) {
			handleMap.fromJson(handleMapJ);
		}

		json_t* writingSnapshotJ = json_object_get(rootJ, "writing_snapshot");
		if(writingSnapshotJ) {
			writingSnapshot = json_integer_value(writingSnapshotJ);
		}
	}
};

struct MorphDisplay : OpaqueWidget
{
	Morph* module;
	std::shared_ptr<Font> font;

	MorphDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Bebas-Regular.ttf"));
	}

	void draw(const DrawArgs &args) override {
		
		nvgFillColor(args.vg, nvgRGB(20, 30, 33));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFill(args.vg);

		NVGcolor fixedColor = nvgRGB(0x42,0x42,0x42);
		NVGcolor selectorColor = nvgRGB(0x99,0x99,0x99);

		nvgStrokeColor(args.vg, fixedColor);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, box.size.y / 2);
		nvgLineTo(args.vg, box.size.x, box.size.y / 2);
		nvgStroke(args.vg);
		nvgStrokeColor(args.vg, fixedColor);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, box.size.x / 2, 0);
		nvgLineTo(args.vg, box.size.x / 2, box.size.y);
		nvgStroke(args.vg);

		nvgFontSize(args.vg, 64);
		nvgFontFaceId(args.vg, font->handle);
		
		if(module && module->writingSnapshot == 0) {
			nvgFillColor(args.vg, selectorColor);
		}
		else {
			nvgFillColor(args.vg, fixedColor);
		}
		nvgText(args.vg, box.size.x / 5 , box.size.y / 2.7, "A", NULL);

		if(module && module->writingSnapshot == 1) {
			nvgFillColor(args.vg, selectorColor);
		}
		else {
			nvgFillColor(args.vg, fixedColor);
		}
		nvgText(args.vg, box.size.x - box.size.x / 3 , box.size.y / 2.7, "B", NULL);

		if(module && module->writingSnapshot == 2) {
			nvgFillColor(args.vg, selectorColor);
		}
		else {
			nvgFillColor(args.vg, fixedColor);
		}
		nvgText(args.vg, box.size.x / 5 , box.size.y - box.size.y / 7 , "C", NULL);
		
		if(module && module->writingSnapshot == 3) {
			nvgFillColor(args.vg, selectorColor);
		}
		else {
			nvgFillColor(args.vg, fixedColor);
		}
		nvgText(args.vg, box.size.x - box.size.x / 3  , box.size.y - box.size.y / 7 , "D", NULL);

		if(module) {
			// Offset version
			nvgStrokeColor(args.vg, SCHEME_BLUE);
			nvgStrokeWidth(args.vg, 2);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, module->getX(), module->getY(), box.size.x / 2, box.size.y / 2);
			nvgStroke(args.vg);

			nvgStrokeColor(args.vg, selectorColor);
			nvgStrokeWidth(args.vg, 2);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, module->selectorX, module->selectorY, box.size.x / 2, box.size.y / 2);
			nvgStroke(args.vg);

		}
	}

	void onDragMove(const event::DragMove &e) override {
		module->move(e.mouseDelta.x, e.mouseDelta.y);
	}
};

struct MorphWidget : ModuleWidget {
	MorphWidget(Morph* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Morph.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		{
			MorphDisplay* display = new MorphDisplay();
			display->module = module;
			float width = box.size.x;
			float height = box.size.x - 33;
			display->box.pos = Vec(0, 23);
			display->box.size = Vec(width, height);
			if(module) {
				module->maxX = width / 2;
				module->maxY = height / 2;
			}
			addChild(display);
		}

		addInput(createInputCentered<SmallPort>(mm2px(Vec(10.f, 67.5f)), module, Morph::X_CV_INPUT));
		addInput(createInputCentered<SmallPort>(mm2px(Vec(19.5f, 67.5f)), module, Morph::Y_CV_INPUT));

		ParamMapButton* button = new ParamMapButton();
		button->box.pos = mm2px(Vec(57.5f,65.2f));
		button->light->bgColor = nvgRGBA(0xff, 0xff, 0xff, 0x99);
		button->light->setColor(SCHEME_YELLOW);
		button->light->borderColor = nvgRGBA(0xff, 0xff, 0xff, 0x00);
		button->light->setBrightness(0.f);
		if(module) button->paramMap = &module->handleMap;
		addChild(button);

		createMappableParam<KnobWhite32>(mm2px(Vec(6, 75)), Morph::KNOB_PARAMS + 0);
		createMappableParam<KnobWhite32>(mm2px(Vec(21, 75)), Morph::KNOB_PARAMS + 1);
		createMappableParam<KnobWhite32>(mm2px(Vec(36, 75)), Morph::KNOB_PARAMS + 2);
		createMappableParam<KnobWhite32>(mm2px(Vec(50, 75)), Morph::KNOB_PARAMS + 3);
		createMappableParam<KnobWhite32>(mm2px(Vec(6, 99)), Morph::KNOB_PARAMS + 4);
		createMappableParam<KnobWhite32>(mm2px(Vec(21, 99)), Morph::KNOB_PARAMS + 5);
		createMappableParam<KnobWhite32>(mm2px(Vec(36, 99)), Morph::KNOB_PARAMS + 6);
		createMappableParam<KnobWhite32>(mm2px(Vec(50, 99)), Morph::KNOB_PARAMS + 7);

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(11, 92)), module, Morph::OUTPUTS + 0));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(26, 92)), module, Morph::OUTPUTS + 1));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41, 92)), module, Morph::OUTPUTS + 2));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(55, 92)), module, Morph::OUTPUTS + 3));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(11, 115)), module, Morph::OUTPUTS + 4));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(26, 115)), module, Morph::OUTPUTS + 5));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41, 115)), module, Morph::OUTPUTS + 6));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(55, 115)), module, Morph::OUTPUTS + 7));
	}

	template <class TParamWidget>
	void createMappableParam(math::Vec pos, int paramId) {
		Morph* module = dynamic_cast<Morph*>(this->module);
		MappableParameter<TParamWidget>* param = createParam<MappableParameter<TParamWidget>>(pos, module, paramId);
		param->paramId = paramId;
		if(module) {
			param->module = module;
			param->handleMap = &module->handleMap;
		}
		addParam(param);
	}
};

Model* modelMorph = createModel<Morph, MorphWidget>("Morph");
