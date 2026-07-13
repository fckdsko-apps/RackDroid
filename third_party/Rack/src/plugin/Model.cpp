#include <algorithm>

#include <plugin/Model.hpp>
#include <plugin.hpp>
#include <asset.hpp>
#include <system.hpp>
#include <settings.hpp>
#include <string.hpp>
#include <tag.hpp>
#include <ui/Menu.hpp>
#include <ui/MenuSeparator.hpp>
#include <helpers.hpp>
#include <window/Window.hpp>


namespace rack {
namespace plugin {


void Model::fromJson(json_t* rootJ) {
	assert(plugin);

	json_t* nameJ = json_object_get(rootJ, "name");
	if (nameJ)
		name = json_string_value(nameJ);
	if (name == "")
		throw Exception("Module %s/%s has no name", plugin->slug.c_str(), slug.c_str());

	json_t* descriptionJ = json_object_get(rootJ, "description");
	if (descriptionJ)
		description = json_string_value(descriptionJ);

	// Tags
	tagIds.clear();
	json_t* tagsJ = json_object_get(rootJ, "tags");
	if (tagsJ) {
		size_t i;
		json_t* tagJ;
		json_array_foreach(tagsJ, i, tagJ) {
			std::string tag = json_string_value(tagJ);
			int tagId = tag::findId(tag);
			if (tagId < 0) {
				// WARN("Module %s/%s has invalid tag \"%s\"", plugin->slug.c_str(), slug.c_str(), tag.c_str());
				continue;
			}

			// Omit duplicates
			auto it = std::find(tagIds.begin(), tagIds.end(), tagId);
			if (it != tagIds.end()) {
				// WARN("Module %s/%s has duplicate tag \"%s\"", plugin->slug.c_str(), slug.c_str(), tag.c_str());
				continue;
			}

			tagIds.push_back(tagId);
		}
	}

	// manualUrl
	json_t* manualUrlJ = json_object_get(rootJ, "manualUrl");
	if (manualUrlJ)
		manualUrl = json_string_value(manualUrlJ);

	// modularGridUrl
	json_t* modularGridUrlJ = json_object_get(rootJ, "modularGridUrl");
	if (modularGridUrlJ)
		modularGridUrl = json_string_value(modularGridUrlJ);

	// hidden
	json_t* hiddenJ = json_object_get(rootJ, "hidden");
	// "disabled" was a deprecated alias in Rack <2
	if (!hiddenJ)
		hiddenJ = json_object_get(rootJ, "disabled");
	// "deprecated" was a deprecated alias in Rack <2.2.4
	if (!hiddenJ)
		hiddenJ = json_object_get(rootJ, "deprecated");
	if (hiddenJ) {
		// Don't un-hide Model if already hidden by C++
		if (json_boolean_value(hiddenJ))
			hidden = true;
	}
}


std::string Model::getFullName() {
	assert(plugin);
	return plugin->getBrand() + " " + name;
}


std::string Model::getFactoryPresetDirectory() {
	return asset::plugin(plugin, system::join("presets", slug));
}


std::string Model::getUserPresetDirectory() {
	return asset::user(system::join("presets", plugin->slug, slug));
}


std::string Model::getManualUrl() {
	if (!manualUrl.empty())
		return manualUrl;
	return plugin->manualUrl;
}


void Model::appendContextMenu(ui::Menu* menu, bool inBrowser) {
	// plugin
	menu->addChild(createMenuItem(string::translate("Model.plugin") + plugin->name, "", [=]() {
		system::openBrowser(plugin->pluginUrl);
	}, plugin->pluginUrl == ""));

	// version
	menu->addChild(createMenuLabel(string::translate("Model.version") + plugin->version));

	// author
	if (plugin->author != "") {
		menu->addChild(createMenuItem(string::translate("Model.author") + plugin->author, "", [=]() {
			system::openBrowser(plugin->authorUrl);
		}, plugin->authorUrl.empty()));
	}

	// license
	std::string license = plugin->license;
	if (string::startsWith(license, "https://") || string::startsWith(license, "http://")) {
		menu->addChild(createMenuItem(string::translate("Model.licenseBrowser"), "", [=]() {
			system::openBrowser(license);
		}));
	}
	else if (license != "") {
		menu->addChild(createMenuLabel(string::translate("Model.license") + license));
	}

	// tags
	if (!tagIds.empty()) {
		menu->addChild(createMenuLabel(string::translate("Model.tags")));
		for (int tagId : tagIds) {
			std::string tag = string::translate("tag." + tag::getTag(tagId));
			menu->addChild(createMenuLabel("• " + tag));
		}
	}

	menu->addChild(new ui::MenuSeparator);

	// VCV Library page
	menu->addChild(createMenuItem(string::translate("Model.library"), "", [=]() {
		system::openBrowser("https://library.vcvrack.com/" + plugin->slug + "/" + slug);
	}));

	// modularGridUrl
	if (modularGridUrl != "") {
		menu->addChild(createMenuItem(string::translate("Model.modularGrid"), "", [=]() {
			system::openBrowser(modularGridUrl);
		}));
	}

	// manual
	std::string manualUrl = getManualUrl();
	if (manualUrl != "") {
		menu->addChild(createMenuItem(string::translate("Model.manual"), widget::getKeyCommandName(GLFW_KEY_F1, RACK_MOD_CTRL), [=]() {
			system::openBrowser(manualUrl);
		}));
	}

	// donate
	if (plugin->donateUrl != "") {
		menu->addChild(createMenuItem(string::translate("Model.donate"), "", [=]() {
			system::openBrowser(plugin->donateUrl);
		}));
	}

	// source code
	if (plugin->sourceUrl != "") {
		menu->addChild(createMenuItem(string::translate("Model.source"), "", [=]() {
			system::openBrowser(plugin->sourceUrl);
		}));
	}

	// changelog
	if (plugin->changelogUrl != "") {
		menu->addChild(createMenuItem(string::translate("Model.changelog"), "", [=]() {
			system::openBrowser(plugin->changelogUrl);
		}));
	}

	// author email
	if (plugin->authorEmail != "") {
		menu->addChild(createMenuItem(string::translate("Model.authorEmail"), string::translate("Model.authorEmailCopy"), [=]() {
			glfwSetClipboardString(APP->window->win, plugin->authorEmail.c_str());
		}));
	}

	// plugin folder
	if (plugin->path != "") {
		menu->addChild(createMenuItem(string::translate("Model.pluginDir"), "", [=]() {
			system::openDirectory(plugin->path);
		}));
	}

	// Favorite
	std::string favoriteRightText;
	if (inBrowser)
		favoriteRightText = widget::getKeyCommandName(0, RACK_MOD_CTRL) + string::translate("key.click");
	if (isFavorite())
		favoriteRightText += " " CHECKMARK_STRING;
	menu->addChild(createMenuItem(string::translate("Model.favorite"), favoriteRightText,
		[=]() {
			setFavorite(!isFavorite());
		}
	));
}


bool Model::isFavorite() {
	const settings::ModuleInfo* mi = settings::getModuleInfo(plugin->slug, slug);
	return mi && mi->favorite;
}


void Model::setFavorite(bool favorite) {
	settings::ModuleInfo& mi = settings::moduleInfos[plugin->slug][slug];
	mi.favorite = favorite;
}


} // namespace plugin
} // namespace rack
