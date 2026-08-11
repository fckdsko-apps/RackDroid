/* osdialog backend on Android system dialogs (replaces osdialog_stub.c in
 * the Android build). Runs the AlertDialog on the Java UI thread while the
 * calling native thread blocks — same synchronous contract as desktop
 * osdialog backends.
 */
#include <cstdlib>
#include <cstring>
#include <string>

#include <osdialog.h>
#include <string.hpp>

#include "jni_bridge.hpp"


/** Rack asks "clear it and start a new patch?" before File > New, and then
 * loads the TEMPLATE -- which is a patch with modules and cables in it, not an
 * empty rack. The promise and the result do not match, and the first thing a
 * new user does with a menu is press the first item in it.
 *
 * The behaviour is right and is left alone: the same template is what a first
 * launch opens, and File > Overwrite template exists precisely to choose it.
 * So the sentence is what gets fixed. Matched against translate() rather than
 * against an English literal, so it still catches the prompt in any language,
 * and handed to Java as a marker because that is the side with the strings --
 * the same trick the menu bridge already uses for the rows we add ourselves. */
static const char* NEW_PATCH_MARKER = "@rackdroid:new_patch_confirm";


extern "C" {

int osdialog_message(osdialog_message_level level, osdialog_message_buttons buttons, const char* message) {
	std::string text = message ? message : "";
	std::string newPatch = rack::string::translate("patch.loadTemplateConfirm");
	if (!newPatch.empty() && text == newPatch)
		text = NEW_PATCH_MARKER;
	return rackdroid::dialogMessage(level, buttons, text);
}

char* osdialog_prompt(osdialog_message_level level, const char* message, const char* text) {
	(void) level;
	std::string result;
	if (!rackdroid::dialogPrompt(message ? message : "", text ? text : "", result))
		return NULL;
	return strdup(result.c_str());
}

char* osdialog_file(osdialog_file_action action, const char* path, const char* filename, osdialog_filters* filters) {
	// Android's document picker filters by MIME rather than filename glob. Pass
	// the extensions to Java so it can choose a useful MIME family (audio/* for
	// WAV, for example) and still validate the selected document's extension.
	std::string extensions;
	for (osdialog_filters* f = filters; f; f = f->next) {
		for (osdialog_filter_patterns* p = f->patterns; p; p = p->next) {
			if (!p->pattern || !*p->pattern)
				continue;
			if (!extensions.empty())
				extensions += ',';
			std::string ext = p->pattern;
			while (!ext.empty() && (ext.front() == '.' || ext.front() == '*'))
				ext.erase(ext.begin());
			extensions += ext;
		}
	}

	std::string result;
	if (!rackdroid::dialogFile((int) action, path ? path : "", filename ? filename : "",
			extensions, result))
		return NULL;
	return strdup(result.c_str());
}

int osdialog_color_picker(osdialog_color* color, int opacity) {
	(void) color;
	(void) opacity;
	return 0;
}

} // extern "C"
