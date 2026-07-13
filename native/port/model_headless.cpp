/* Headless replacement for src/plugin/Model.cpp.
 *
 * Upstream Model.cpp builds the module context menu (ui::Menu widgets), which
 * links the whole widget/ui stack. The headless subset needs only the
 * manifest/preset logic, reproduced here from the upstream file (GPLv3, same
 * license as this port). appendContextMenu() becomes a no-op until the UI
 * phases compile the upstream file instead.
 */
#include <algorithm>

#include <plugin/Model.hpp>
#include <plugin.hpp>
#include <asset.hpp>
#include <system.hpp>
#include <settings.hpp>
#include <string.hpp>
#include <tag.hpp>


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
			if (tagId < 0)
				continue;
			// Omit duplicates
			auto it = std::find(tagIds.begin(), tagIds.end(), tagId);
			if (it != tagIds.end())
				continue;
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

	// hidden ("disabled" and "deprecated" are deprecated aliases)
	json_t* hiddenJ = json_object_get(rootJ, "hidden");
	if (!hiddenJ)
		hiddenJ = json_object_get(rootJ, "disabled");
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
	// UI not compiled in the headless subset.
	(void) menu;
	(void) inBrowser;
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
