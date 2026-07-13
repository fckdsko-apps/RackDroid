/* osdialog backend on Android system dialogs (replaces osdialog_stub.c in
 * the Android build). Runs the AlertDialog on the Java UI thread while the
 * calling native thread blocks — same synchronous contract as desktop
 * osdialog backends.
 */
#include <cstdlib>
#include <cstring>
#include <string>

#include <osdialog.h>

#include "jni_bridge.hpp"


extern "C" {

int osdialog_message(osdialog_message_level level, osdialog_message_buttons buttons, const char* message) {
	return rackdroid::dialogMessage(level, buttons, message ? message : "");
}

char* osdialog_prompt(osdialog_message_level level, const char* message, const char* text) {
	(void) level;
	std::string result;
	if (!rackdroid::dialogPrompt(message ? message : "", text ? text : "", result))
		return NULL;
	return strdup(result.c_str());
}

char* osdialog_file(osdialog_file_action action, const char* path, const char* filename, osdialog_filters* filters) {
	(void) filters;
	if (action == OSDIALOG_OPEN_DIR)
		return NULL; // No directory picker; not used by Rack core flows
	std::string result;
	bool save = (action == OSDIALOG_SAVE);
	if (!rackdroid::dialogFile(save, path ? path : "", filename ? filename : "", result))
		return NULL;
	return strdup(result.c_str());
}

int osdialog_color_picker(osdialog_color* color, int opacity) {
	(void) color;
	(void) opacity;
	return 0;
}

} // extern "C"
