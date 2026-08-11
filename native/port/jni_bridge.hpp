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

/** One-shot options set by MainActivity before NativeActivity starts the
native glue thread. Used for automatic recovery after repeated failed starts. */
void setStartupOptions(bool safeMode, bool skipUserPlugins);
bool startupSafeModeRequested();
bool userPluginsDisabled();

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

/** Tells Java the patch is restored and the engine is running, so it can
build the model list and raise the palette. Non-blocking. */
void nativePatchReady();

/** The user picked a different interface language from Rack's own Help menu.
Java owns the other half of the app's strings -- everything in the toolbar,
the palette, the tour -- and those come from Android resources, which follow
the DEVICE locale and know nothing about Rack's setting. So it is told, it
remembers the choice, and it restarts: Rack asks for a restart anyway, having
no way to relabel widgets already on screen, and on a phone there is no reason
to make the user do it by hand. */
void nativeLanguageChanged(const std::string& code);

/** Runs MainActivity.loadUserPluginsFromNative() and BLOCKS the calling glue
thread until it reports back (pumping the looper meanwhile), so side-loaded
.rdmod packs are registered before the patch is restored. A pack's .so can
only be brought in by Java System.load() (Android linker namespaces), so this
has to round-trip through the UI thread rather than being done natively. */
void loadUserPluginsBlocking();

// Clipboard (thread-safe; dispatches to the Java UI thread internally)
void clipboardSet(const std::string& text);
std::string clipboardGet();

/** Synchronous dialogs, called from the native glue thread. The thread blocks
while the dialog is shown on the Java UI thread. Levels/buttons use the
osdialog enum values. Returns 1 for OK/Yes. */
int dialogMessage(int level, int buttons, const std::string& message);

/** Text prompt. Returns true and fills `result` if confirmed. */
bool dialogPrompt(const std::string& title, const std::string& text, std::string& result);

/** File picker bridge. `action` uses the osdialog_file_action numeric values.
`extensions` is a comma-separated list such as "wav,aiff". Java preserves
RackDroid's private .vcv patch browser, and routes other OPEN requests through
Android's Storage Access Framework before importing the selected document into
RackDroid's persistent user/imports directory. */
bool dialogFile(int action, const std::string& dir, const std::string& filename,
	const std::string& extensions, std::string& path);

} // namespace rackdroid
