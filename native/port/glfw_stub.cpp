/* Implementations for the few GLFW calls that leak outside
 * src/window/Window.cpp (clipboard in menus/TextField, key names in
 * widget/event.cpp, timing in some plugins e.g. computerscare). On Android
 * the clipboard is real (JNI bridge to ClipboardManager); on the headless
 * host build they are no-ops.
 */
#include <string>

#include <GLFW/glfw3.h>

#include <system.hpp>
#include <context.hpp>
#include <window/Window.hpp>

#if defined(__ANDROID__)
	#include "jni_bridge.hpp"
#endif


extern "C" {

double glfwGetTime(void) {
	// Same monotonic clock Rack's own timing uses.
	return rack::system::getTime();
}

// Cursor management: no OS cursor exists on a touch screen; plugins that
// customize it (e.g. computerscare's drag helpers) just need the calls to
// resolve and be harmless.
GLFWcursor* glfwCreateStandardCursor(int shape) {
	(void) shape;
	return NULL;
}

void glfwSetCursor(GLFWwindow* window, GLFWcursor* cursor) {
	(void) window;
	(void) cursor;
}

void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height) {
	(void) window;
	// Answered from Rack's own window so callers see the real surface.
	rack::math::Vec size = APP && APP->window ? APP->window->getSize() : rack::math::Vec(0, 0);
	if (width)
		*width = (int) size.x;
	if (height)
		*height = (int) size.y;
}

void glfwSetClipboardString(GLFWwindow* window, const char* string) {
	(void) window;
#if defined(__ANDROID__)
	rackdroid::clipboardSet(string ? string : "");
#else
	(void) string;
#endif
}

const char* glfwGetClipboardString(GLFWwindow* window) {
	(void) window;
#if defined(__ANDROID__)
	// GLFW contract: pointer valid until the next call.
	static std::string clip;
	clip = rackdroid::clipboardGet();
	return clip.empty() ? NULL : clip.c_str();
#else
	return NULL;
#endif
}

// Key-state polling (Venom checks modifier keys mid-drag): no keyboard
// state exists outside the event stream on Android; report released.
int glfwGetKey(GLFWwindow* window, int key) {
	(void) window;
	(void) key;
	return GLFW_RELEASE;
}

// Window minimize (PackOne's MEM/screenshot flows): meaningless for a
// fullscreen NativeActivity surface.
void glfwIconifyWindow(GLFWwindow* window) {
	(void) window;
}

int glfwGetKeyScancode(int key) {
	(void) key;
	return -1;
}

const char* glfwGetKeyName(int key, int scancode) {
	(void) key;
	(void) scancode;
	return NULL;
}

} // extern "C"
