#pragma once
#include <string>
#include <vector>

struct ANativeActivity;

namespace rackdroid {

/** Stores the activity/VM for later JNI calls. Call once from android_main. */
void jniInit(ANativeActivity* activity);

/** Installs the glue-thread event pump used while a dialog is open (see
main_android.cpp). Must be called once from android_main. */
void jniSetPump(void (*pump)(int timeoutMs));

/** True while a dialog result is being waited on; handleInput drops touch
events during this window (the dialog is modal anyway). */
bool dialogIsPumping();

/** Shows the native (Android bottom-sheet) menu with the given rows.
Non-blocking: taps are delivered back via the nativeMenuSelect JNI callback. */
void nativeMenuShow(const std::vector<std::string>& labels,
	const std::vector<std::string>& rights,
	const std::vector<int>& flags);
/** Dismisses the native menu sheet, if shown. */
void nativeMenuDismiss();

/** Tells Java the module browser was opened (canvas browser already hidden
by the caller); MainActivity pulls the model list itself via the
nativeBrowserModelsJson JNI callback and shows the sheet. */
void nativeBrowserShow();

/** Hand a saved .vcv to Java for the system share sheet. */
void nativeSharePatch(const std::string& path);

/** Open the Java help UI: 0 = guide sheet, 1 = step-by-step wizard. */
void nativeShowHelp(int which);

// Clipboard (thread-safe; dispatches to the Java UI thread internally)
void clipboardSet(const std::string& text);
std::string clipboardGet();

/** Synchronous dialogs, called from the native glue thread. The thread blocks
while the dialog is shown on the Java UI thread. Levels/buttons use the
osdialog enum values. Returns 1 for OK/Yes. */
int dialogMessage(int level, int buttons, const std::string& message);

/** Text prompt. Returns true and fills `result` if confirmed. */
bool dialogPrompt(const std::string& title, const std::string& text, std::string& result);

/** Patch file picker. save=false lists *.vcv files in `dir` for opening;
save=true asks for a filename (suggesting `filename`). Returns true and fills
`path` (absolute) if confirmed. */
bool dialogFile(bool save, const std::string& dir, const std::string& filename, std::string& path);

} // namespace rackdroid
