/* osdialog backend for platforms without desktop dialogs (Android, headless).
 *
 * Messages are logged; prompts and file dialogs report "cancelled". Phase 3
 * replaces the file dialog with the Android Storage Access Framework via JNI.
 */
#include <stdio.h>
#include <string.h>
#include <osdialog.h>

#if defined(__ANDROID__)
	#include <android/log.h>
#endif


static void log_message(const char* message) {
#if defined(__ANDROID__)
	__android_log_print(ANDROID_LOG_INFO, "rackdroid", "[osdialog] %s", message);
#else
	fprintf(stderr, "[osdialog] %s\n", message);
#endif
}

int osdialog_message(osdialog_message_level level, osdialog_message_buttons buttons, const char* message) {
	(void) level;
	(void) buttons;
	log_message(message ? message : "");
	/* Behave as if the user pressed OK/Yes so non-interactive flows continue. */
	return 1;
}

char* osdialog_prompt(osdialog_message_level level, const char* message, const char* text) {
	(void) level;
	log_message(message ? message : "");
	(void) text;
	return NULL;
}

char* osdialog_file(osdialog_file_action action, const char* path, const char* filename, osdialog_filters* filters) {
	(void) action;
	(void) path;
	(void) filename;
	(void) filters;
	log_message("file dialog unavailable in this build");
	return NULL;
}

int osdialog_color_picker(osdialog_color* color, int opacity) {
	(void) color;
	(void) opacity;
	return 0;
}
