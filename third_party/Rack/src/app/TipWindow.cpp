#include <thread>

#include <app/TipWindow.hpp>
#include <widget/OpaqueWidget.hpp>
#include <ui/MenuOverlay.hpp>
#include <ui/Label.hpp>
#include <ui/Button.hpp>
#include <ui/OptionButton.hpp>
#include <ui/MenuItem.hpp>
#include <ui/SequentialLayout.hpp>
#include <settings.hpp>
#include <system.hpp>


namespace rack {
namespace app {


struct UrlButton : ui::Button {
	std::string url;
	void onAction(const ActionEvent& e) override {
		system::openBrowser(url);
	}
};


struct TipInfo {
	std::string text;
	std::string linkText;
	std::string linkUrl;
};


static std::vector<TipInfo> getTipInfos() {
	// Remember to use “smart quotes.”
	return {
		{string::translate("TipWindow.addModule"), "", ""},
		{string::f(string::translate("TipWindow.moveRack"),
			widget::getKeyCommandName(0, RACK_MOD_ALT) + string::translate("key.click"),
			string::translate("key.ctrl"),
			string::translate("key.shift"),
			string::translate("key.ctrl"),
			string::translate("key.shift"),
			string::translate("key.ctrl"),
			widget::getKeyCommandName(GLFW_KEY_EQUAL, RACK_MOD_CTRL),
			widget::getKeyCommandName(GLFW_KEY_MINUS, RACK_MOD_CTRL)),
		"", ""},
		{string::translate("TipWindow.fullscreen"), string::translate("TipWindow.fullscreenButton"), "https://vcvrack.com/Recorder"},
		{string::translate("TipWindow.library"), string::translate("TipWindow.libraryButton"), "https://library.vcvrack.com/"},
		{string::translate("TipWindow.donation"), string::translate("TipWindow.libraryButton"), "https://library.vcvrack.com/"},
		{string::translate("TipWindow.daw"), string::translate("TipWindow.learnMore"), "https://vcvrack.com/Rack"},
		{string::translate("TipWindow.manual"), string::translate("TipWindow.manualButton"), "https://vcvrack.com/manual/"},
		{string::translate("TipWindow.twitter"), string::translate("TipWindow.twitterHandle"), "https://twitter.com/vcvrack"},
		{string::translate("TipWindow.polyphony"), string::translate("TipWindow.polyphonyButton"), "https://vcvrack.com/manual/Polyphony"},
		{string::translate("TipWindow.develop"), string::translate("TipWindow.developButton"), "https://vcvrack.com/manual/PluginDevelopmentTutorial"},
		{string::translate("TipWindow.moduleManual"), "", ""},
		{string::translate("TipWindow.modularGrid"), string::translate("TipWindow.modularGridButton"), "https://www.modulargrid.net/e/grayscale-permutation-18hp"},
		{string::f(string::translate("TipWindow.menuKeepOpen"),
			widget::getKeyCommandName(0, RACK_MOD_CTRL) + string::translate("key.click")),
		"", ""},
		// {"", "", ""},
	};
}


struct TipWindow : widget::OpaqueWidget {
	ui::SequentialLayout* layout;
	ui::SequentialLayout* buttonLayout;
	ui::Label* label;
	UrlButton* linkButton;

	TipWindow() {
		box.size = math::Vec(550, 200);
		const float margin = 10;
		const float buttonWidth = 100;

		layout = new ui::SequentialLayout;
		layout->box.pos = math::Vec(0, 10);
		layout->box.size = box.size;
		layout->orientation = ui::SequentialLayout::VERTICAL_ORIENTATION;
		layout->margin = math::Vec(margin, margin);
		layout->spacing = math::Vec(margin, margin);
		layout->wrap = false;
		addChild(layout);

		ui::Label* header = new ui::Label;
		// header->box.size.x = box.size.x - 2*margin;
		header->box.size.y = 20;
		header->fontSize = 20;
		header->text = string::f(string::translate("TipWindow.welcome"), APP_NAME + " " + APP_EDITION_NAME + " " + APP_VERSION);
		layout->addChild(header);

		label = new ui::Label;
		label->box.size.y = 80;
		label->box.size.x = box.size.x - 2*margin;
		layout->addChild(label);

		// Container for link button so hiding it won't shift layout
		widget::Widget* linkPlaceholder = new widget::Widget;
		layout->addChild(linkPlaceholder);

		linkButton = new UrlButton;
		linkButton->box.size.x = box.size.x - 2*margin;
		linkPlaceholder->box.size = linkButton->box.size;
		linkPlaceholder->addChild(linkButton);

		buttonLayout = new ui::SequentialLayout;
		buttonLayout->box.size.x = box.size.x - 2*margin;
		buttonLayout->spacing = math::Vec(margin, margin);
		layout->addChild(buttonLayout);

		struct ShowQuantity : Quantity {
			void setValue(float value) override {
				settings::showTipsOnLaunch = (value > 0.f);
			}
			float getValue() override {
				return settings::showTipsOnLaunch ? 1.f : 0.f;
			}
		};
		static ShowQuantity showQuantity;

		ui::OptionButton* showButton = new ui::OptionButton;
		showButton->box.size.x = 200;
		showButton->text = string::translate("TipWindow.startup");
		showButton->quantity = &showQuantity;
		buttonLayout->addChild(showButton);

		struct PreviousButton : ui::Button {
			TipWindow* tipWindow;
			void onAction(const ActionEvent& e) override {
				tipWindow->advanceTip(-1);
			}
		};
		PreviousButton* prevButton = new PreviousButton;
		prevButton->box.size.x = buttonWidth;
		prevButton->text = "◀  " + string::translate("TipWindow.previous");
		prevButton->tipWindow = this;
		buttonLayout->addChild(prevButton);

		struct NextButton : ui::Button {
			TipWindow* tipWindow;
			void onAction(const ActionEvent& e) override {
				tipWindow->advanceTip();
			}
		};
		NextButton* nextButton = new NextButton;
		nextButton->box.size.x = buttonWidth;
		nextButton->text = "▶  " + string::translate("TipWindow.next");
		nextButton->tipWindow = this;
		buttonLayout->addChild(nextButton);

		struct CloseButton : ui::Button {
			TipWindow* tipWindow;
			void onAction(const ActionEvent& e) override {
				tipWindow->getParent()->requestDelete();
			}
		};
		CloseButton* closeButton = new CloseButton;
		closeButton->box.size.x = buttonWidth;
		closeButton->text = "✖  " + string::translate("TipWindow.close");
		closeButton->tipWindow = this;
		buttonLayout->addChild(closeButton);

		buttonLayout->box.size.y = closeButton->box.size.y;

		// When the TipWindow is created, choose the next tip
		advanceTip();
	}

	void advanceTip(int delta = 1) {
		std::vector<TipInfo> tipInfos = getTipInfos();

		// Increment tip index
		settings::tipIndex = math::eucMod(settings::tipIndex + delta, (int) tipInfos.size());

		const TipInfo& tipInfo = tipInfos[settings::tipIndex];
		label->text = tipInfo.text;
		linkButton->setVisible(tipInfo.linkText != "");
		linkButton->text = tipInfo.linkText;
		linkButton->url = tipInfo.linkUrl;
	}

	void step() override {
		OpaqueWidget::step();

		box.pos = parent->box.size.minus(box.size).div(2).round();
	}

	void draw(const DrawArgs& args) override {
		bndMenuBackground(args.vg, 0.0, 0.0, box.size.x, box.size.y, 0);
		Widget::draw(args);
	}
};


widget::Widget* tipWindowCreate() {
	ui::MenuOverlay* overlay = new ui::MenuOverlay;
	overlay->bgColor = nvgRGBAf(0, 0, 0, 0.33);

	TipWindow* tipWindow = new TipWindow;
	overlay->addChild(tipWindow);

	return overlay;
}


} // namespace app
} // namespace rack
