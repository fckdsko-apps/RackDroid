/* JNI bridge to MainActivity for clipboard and dialogs.
 *
 * All entry points may be called from the native glue thread (or Rack's
 * threads for the clipboard): the thread is attached to the VM on demand.
 *
 * Dialogs keep osdialog's synchronous contract WITHOUT blocking the glue
 * thread's event processing: the Java side shows the dialog and returns
 * immediately; the native caller then pumps the glue looper (lifecycle
 * cmds keep flowing, input is dropped by handleInput while pumping — see
 * dialogIsPumping) until the result arrives via nativeDialogInt/String.
 * A plain latch-blocked wait here left NativeActivity's input queue
 * unread, which Android reports as an ANR after a few seconds of touches.
 */
#include <atomic>
#include <thread>
#include <chrono>

#include <android/native_activity.h>
#include <android/log.h>
#include <android/looper.h>
#include <jni.h>

#include <system.hpp>

#include "jni_bridge.hpp"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "rackdroid", __VA_ARGS__)


namespace rackdroid {


static JavaVM* vm = NULL;
static jobject activityObj = NULL; // global ref
static jclass activityCls = NULL;  // global ref
static jclass stringCls = NULL;    // global ref (java/lang/String)
static jmethodID midClipboardSet;
static jmethodID midClipboardGet;
static jmethodID midDialogMessage;
static jmethodID midDialogPrompt;
static jmethodID midDialogFile;
static jmethodID midMenuShow;
static jmethodID midMenuDismiss;
static jmethodID midBrowserShow;
static jmethodID midSharePatch;
static jmethodID midShowHelp;

// ---- dialog result handoff (UI thread -> pumping glue thread) ----
static void (*pumpOnce)(int timeoutMs) = NULL; // installed by main_android
static std::atomic<bool> dialogDone{false};
static std::atomic<bool> pumping{false};
static int dialogResultInt = 0;
static bool dialogResultHasStr = false;
static std::string dialogResultStr;


void jniSetPump(void (*pump)(int timeoutMs)) {
	pumpOnce = pump;
}


bool dialogIsPumping() {
	return pumping.load();
}


/** Pumps glue events until the Java side reports the dialog result.
Returns false on timeout/reentry (callers fall back to their default). */
static bool pumpUntilDialogDone() {
	if (!pumpOnce)
		return false;
	if (pumping.exchange(true))
		return false; // nested dialog from a pumped event: refuse
	// Callers off the glue thread (a plugin's worker/audio thread) have no
	// looper to pump; a plain wait is correct there — only the glue thread
	// owns the input queue whose starvation causes the ANR.
	bool onGlueThread = ALooper_forThread() != NULL;
	double start = rack::system::getTime();
	while (!dialogDone.load()) {
		if (onGlueThread)
			pumpOnce(50);
		else
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		// Safety valve: a lost dialog (activity recreated, Java exception)
		// must not wedge the render thread forever.
		if (rack::system::getTime() - start > 300.0) {
			LOGE("dialog result never arrived; abandoning");
			pumping = false;
			return false;
		}
	}
	pumping = false;
	return true;
}


void jniInit(ANativeActivity* activity) {
	vm = activity->vm;
	JNIEnv* env = NULL;
	vm->AttachCurrentThread(&env, NULL);
	activityObj = env->NewGlobalRef(activity->clazz);
	jclass cls = env->GetObjectClass(activityObj);
	activityCls = (jclass) env->NewGlobalRef(cls);
	jclass sc = env->FindClass("java/lang/String");
	stringCls = (jclass) env->NewGlobalRef(sc);

	midClipboardSet = env->GetMethodID(activityCls, "clipboardSet", "(Ljava/lang/String;)V");
	midClipboardGet = env->GetMethodID(activityCls, "clipboardGet", "()Ljava/lang/String;");
	midDialogMessage = env->GetMethodID(activityCls, "dialogMessageAsync", "(IILjava/lang/String;)V");
	midDialogPrompt = env->GetMethodID(activityCls, "dialogPromptAsync", "(Ljava/lang/String;Ljava/lang/String;)V");
	midDialogFile = env->GetMethodID(activityCls, "dialogFileAsync", "(ZLjava/lang/String;Ljava/lang/String;)V");
	midMenuShow = env->GetMethodID(activityCls, "showNativeMenu", "([Ljava/lang/String;[Ljava/lang/String;[I)V");
	midMenuDismiss = env->GetMethodID(activityCls, "dismissNativeMenu", "()V");
	midBrowserShow = env->GetMethodID(activityCls, "showNativeBrowser", "()V");
	midSharePatch = env->GetMethodID(activityCls, "sharePatchFromNative", "(Ljava/lang/String;)V");
	midShowHelp = env->GetMethodID(activityCls, "showHelpFromNative", "(I)V");
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		LOGE("jniInit: MainActivity methods missing; dialogs/clipboard disabled");
		midClipboardSet = NULL;
	}
}


static JNIEnv* getEnv() {
	if (!vm)
		return NULL;
	JNIEnv* env = NULL;
	if (vm->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK)
		vm->AttachCurrentThread(&env, NULL);
	return env;
}


static jobjectArray toStringArray(JNIEnv* env, const std::vector<std::string>& v) {
	jobjectArray arr = env->NewObjectArray(v.size(), stringCls, NULL);
	if (!arr)
		return NULL;
	for (size_t i = 0; i < v.size(); i++) {
		jstring s = env->NewStringUTF(v[i].c_str());
		env->SetObjectArrayElement(arr, i, s);
		if (s)
			env->DeleteLocalRef(s);
	}
	return arr;
}


void nativeMenuShow(const std::vector<std::string>& labels,
		const std::vector<std::string>& rights, const std::vector<int>& flags) {
	JNIEnv* env = getEnv();
	if (!env || !midMenuShow || !stringCls)
		return;
	jobjectArray jLabels = toStringArray(env, labels);
	jobjectArray jRights = toStringArray(env, rights);
	jintArray jFlags = env->NewIntArray(flags.size());
	if (!jLabels || !jRights || !jFlags) {
		if (env->ExceptionCheck())
			env->ExceptionClear();
		return;
	}
	if (!flags.empty())
		env->SetIntArrayRegion(jFlags, 0, flags.size(), flags.data());
	env->CallVoidMethod(activityObj, midMenuShow, jLabels, jRights, jFlags);
	env->DeleteLocalRef(jLabels);
	env->DeleteLocalRef(jRights);
	env->DeleteLocalRef(jFlags);
	if (env->ExceptionCheck())
		env->ExceptionClear();
}


void nativeMenuDismiss() {
	JNIEnv* env = getEnv();
	if (!env || !midMenuDismiss)
		return;
	env->CallVoidMethod(activityObj, midMenuDismiss);
	if (env->ExceptionCheck())
		env->ExceptionClear();
}


void nativeBrowserShow() {
	JNIEnv* env = getEnv();
	if (!env || !midBrowserShow)
		return;
	env->CallVoidMethod(activityObj, midBrowserShow);
	if (env->ExceptionCheck())
		env->ExceptionClear();
}


void nativeShowHelp(int which) {
	JNIEnv* env = getEnv();
	if (!env || !midShowHelp)
		return;
	env->CallVoidMethod(activityObj, midShowHelp, (jint) which);
	if (env->ExceptionCheck())
		env->ExceptionClear();
}


void nativeSharePatch(const std::string& path) {
	JNIEnv* env = getEnv();
	if (!env || !midSharePatch)
		return;
	jstring js = env->NewStringUTF(path.c_str());
	env->CallVoidMethod(activityObj, midSharePatch, js);
	if (js)
		env->DeleteLocalRef(js);
	if (env->ExceptionCheck())
		env->ExceptionClear();
}


static std::string jstringToStd(JNIEnv* env, jstring js) {
	if (!js)
		return "";
	const char* chars = env->GetStringUTFChars(js, NULL);
	std::string s = chars ? chars : "";
	env->ReleaseStringUTFChars(js, chars);
	return s;
}


void clipboardSet(const std::string& text) {
	JNIEnv* env = getEnv();
	if (!env || !midClipboardSet)
		return;
	jstring js = env->NewStringUTF(text.c_str());
	env->CallVoidMethod(activityObj, midClipboardSet, js);
	env->DeleteLocalRef(js);
	if (env->ExceptionCheck())
		env->ExceptionClear();
}


std::string clipboardGet() {
	JNIEnv* env = getEnv();
	if (!env || !midClipboardSet)
		return "";
	jstring js = (jstring) env->CallObjectMethod(activityObj, midClipboardGet);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return "";
	}
	std::string s = jstringToStd(env, js);
	if (js)
		env->DeleteLocalRef(js);
	return s;
}


int dialogMessage(int level, int buttons, const std::string& message) {
	JNIEnv* env = getEnv();
	if (!env || !midClipboardSet)
		return 1; // behave like the headless stub: proceed with OK
	dialogDone = false;
	jstring js = env->NewStringUTF(message.c_str());
	env->CallVoidMethod(activityObj, midDialogMessage, level, buttons, js);
	env->DeleteLocalRef(js);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return 1;
	}
	if (!pumpUntilDialogDone())
		return 1;
	return dialogResultInt;
}


bool dialogPrompt(const std::string& title, const std::string& text, std::string& result) {
	JNIEnv* env = getEnv();
	if (!env || !midClipboardSet)
		return false;
	dialogDone = false;
	jstring jTitle = env->NewStringUTF(title.c_str());
	jstring jText = env->NewStringUTF(text.c_str());
	env->CallVoidMethod(activityObj, midDialogPrompt, jTitle, jText);
	env->DeleteLocalRef(jTitle);
	env->DeleteLocalRef(jText);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	if (!pumpUntilDialogDone() || !dialogResultHasStr)
		return false;
	result = dialogResultStr;
	return true;
}


bool dialogFile(bool save, const std::string& dir, const std::string& filename, std::string& path) {
	JNIEnv* env = getEnv();
	if (!env || !midClipboardSet)
		return false;
	dialogDone = false;
	jstring jDir = env->NewStringUTF(dir.c_str());
	jstring jName = env->NewStringUTF(filename.c_str());
	env->CallVoidMethod(activityObj, midDialogFile, (jboolean) save, jDir, jName);
	env->DeleteLocalRef(jDir);
	env->DeleteLocalRef(jName);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	if (!pumpUntilDialogDone() || !dialogResultHasStr)
		return false;
	path = dialogResultStr;
	return true;
}


// ---- results posted by MainActivity's dialog handlers (UI thread) ----

extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeDialogInt(JNIEnv* env, jobject thiz, jint result) {
	dialogResultInt = result;
	dialogResultHasStr = false;
	dialogDone = true;
}

extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeDialogString(JNIEnv* env, jobject thiz, jstring js) {
	if (js) {
		dialogResultStr = jstringToStd(env, js);
		dialogResultHasStr = true;
	}
	else {
		dialogResultHasStr = false;
	}
	dialogDone = true;
}


} // namespace rackdroid
