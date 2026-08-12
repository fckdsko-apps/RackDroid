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
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

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
static jmethodID midLoadUserPlugins;
static jmethodID midPatchReady;
static jmethodID midLanguageChanged;

// ---- dialog result handoff (UI thread -> pumping glue thread) ----
static void (*pumpOnce)(int timeoutMs) = NULL; // installed by main_android
static std::atomic<bool> dialogDone{false};
static std::atomic<bool> userPluginsDone{false};
static std::atomic<bool> pumping{false};
static std::atomic<bool> safeStartup{false};
static std::atomic<bool> skipStartupUserPlugins{false};
static int dialogResultInt = 0;
static bool dialogResultHasStr = false;
static std::string dialogResultStr;


void jniSetPump(void (*pump)(int timeoutMs)) {
	pumpOnce = pump;
}


bool dialogIsPumping() {
	return pumping.load();
}


void setStartupOptions(bool safeMode, bool skipUserPlugins) {
	safeStartup.store(safeMode, std::memory_order_release);
	skipStartupUserPlugins.store(skipUserPlugins, std::memory_order_release);
}


bool startupSafeModeRequested() {
	return safeStartup.load(std::memory_order_acquire);
}


bool userPluginsDisabled() {
	return skipStartupUserPlugins.load(std::memory_order_acquire);
}


/** Pumps glue events until the Java side sets `done`.
Returns false on timeout/reentry (callers fall back to their default). */
static bool pumpUntilFlag(std::atomic<bool>& done, double timeoutSec, const char* what) {
	if (!pumpOnce)
		return false;
	if (pumping.exchange(true))
		return false; // nested wait from a pumped event: refuse
	// Callers off the glue thread (a plugin's worker/audio thread) have no
	// looper to pump; a plain wait is correct there — only the glue thread
	// owns the input queue whose starvation causes the ANR.
	bool onGlueThread = ALooper_forThread() != NULL;
	double start = rack::system::getTime();
	while (!done.load()) {
		if (onGlueThread)
			pumpOnce(50);
		else
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		// Safety valve: a lost reply (activity recreated, Java exception)
		// must not wedge the render thread forever.
		if (rack::system::getTime() - start > timeoutSec) {
			LOGE("%s reply never arrived; abandoning", what);
			pumping = false;
			return false;
		}
	}
	pumping = false;
	return true;
}

static bool pumpUntilDialogDone() {
	return pumpUntilFlag(dialogDone, 300.0, "dialog");
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
	midDialogFile = env->GetMethodID(activityCls, "dialogFileAsync",
		"(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	midMenuShow = env->GetMethodID(activityCls, "showNativeMenu", "([Ljava/lang/String;[Ljava/lang/String;[I)V");
	midMenuDismiss = env->GetMethodID(activityCls, "dismissNativeMenu", "()V");
	midBrowserShow = env->GetMethodID(activityCls, "showNativeBrowser", "()V");
	midSharePatch = env->GetMethodID(activityCls, "sharePatchFromNative", "(Ljava/lang/String;)V");
	midShowHelp = env->GetMethodID(activityCls, "showHelpFromNative", "(I)V");
	midLoadUserPlugins = env->GetMethodID(activityCls, "loadUserPluginsFromNative", "()V");
	midPatchReady = env->GetMethodID(activityCls, "patchReadyFromNative", "()V");
	midLanguageChanged = env->GetMethodID(activityCls, "languageChangedFromNative", "(Ljava/lang/String;)V");
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
	if (chars)
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


void nativePatchReady() {
	JNIEnv* env = getEnv();
	if (!env || !midPatchReady)
		return;
	env->CallVoidMethod(activityObj, midPatchReady);
	if (env->ExceptionCheck())
		env->ExceptionClear();
}


void nativeLanguageChanged(const std::string& code) {
	JNIEnv* env = getEnv();
	if (!env || !midLanguageChanged)
		return;
	jstring js = env->NewStringUTF(code.c_str());
	env->CallVoidMethod(activityObj, midLanguageChanged, js);
	env->DeleteLocalRef(js);
	if (env->ExceptionCheck())
		env->ExceptionClear();
}


void loadUserPluginsBlocking() {
	JNIEnv* env = getEnv();
	if (!env || !midLoadUserPlugins) {
		LOGE("cannot load user plugins: JNI bridge not ready");
		return;
	}
	userPluginsDone = false;
	env->CallVoidMethod(activityObj, midLoadUserPlugins);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return;
	}
	// Shorter leash than a dialog: this runs on the startup path, so a Java
	// side that never answers must degrade to "no side-loaded packs" quickly
	// rather than hold the patch (and the whole app) hostage.
	pumpUntilFlag(userPluginsDone, 30.0, "user plugin load");
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


bool dialogFile(int action, const std::string& dir, const std::string& filename,
		const std::string& extensions, std::string& path) {
	JNIEnv* env = getEnv();
	if (!env || !midClipboardSet)
		return false;
	dialogDone = false;
	jstring jDir = env->NewStringUTF(dir.c_str());
	jstring jName = env->NewStringUTF(filename.c_str());
	jstring jExt = env->NewStringUTF(extensions.c_str());
	env->CallVoidMethod(activityObj, midDialogFile, (jint) action, jDir, jName, jExt);
	env->DeleteLocalRef(jDir);
	env->DeleteLocalRef(jName);
	env->DeleteLocalRef(jExt);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	if (!pumpUntilDialogDone() || !dialogResultHasStr)
		return false;
	path = dialogResultStr;
	return true;
}


/** Launches a tiny helper Activity which immediately opens Android's
 * ACTION_CREATE_DOCUMENT picker. Keeping it separate avoids changing the
 * NativeActivity lifecycle/file-dialog code just to support one URI-backed
 * save operation. The helper returns an ordinary private mirror path under
 * user/patches; the path is linked to the chosen content URI in private
 * SharedPreferences. */
bool documentSaveDialog(const std::string& filename, std::string& path) {
	JNIEnv* env = getEnv();
	if (!env || !activityObj || !activityCls)
		return false;

	dialogDone = false;
	dialogResultHasStr = false;

	jclass intentCls = env->FindClass("android/content/Intent");
	if (!intentCls || env->ExceptionCheck()) {
		if (env->ExceptionCheck()) env->ExceptionClear();
		return false;
	}
	jmethodID ctor = env->GetMethodID(intentCls, "<init>", "()V");
	jmethodID setClassName = env->GetMethodID(intentCls, "setClassName",
		"(Landroid/content/Context;Ljava/lang/String;)Landroid/content/Intent;");
	jmethodID putExtra = env->GetMethodID(intentCls, "putExtra",
		"(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
	jmethodID startActivity = env->GetMethodID(activityCls, "startActivity",
		"(Landroid/content/Intent;)V");
	if (!ctor || !setClassName || !putExtra || !startActivity || env->ExceptionCheck()) {
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(intentCls);
		return false;
	}

	jobject intent = env->NewObject(intentCls, ctor);
	jstring className = env->NewStringUTF("org.rackdroid.DocumentSaveActivity");
	jstring extraKey = env->NewStringUTF("org.rackdroid.extra.SAVE_FILENAME");
	jstring extraValue = env->NewStringUTF(filename.empty() ? "Untitled.vcv" : filename.c_str());
	if (!intent || !className || !extraKey || !extraValue) {
		if (intent) env->DeleteLocalRef(intent);
		if (className) env->DeleteLocalRef(className);
		if (extraKey) env->DeleteLocalRef(extraKey);
		if (extraValue) env->DeleteLocalRef(extraValue);
		env->DeleteLocalRef(intentCls);
		return false;
	}

	jobject configured = env->CallObjectMethod(intent, setClassName, activityObj, className);
	if (configured) env->DeleteLocalRef(configured);
	jobject withExtra = env->CallObjectMethod(intent, putExtra, extraKey, extraValue);
	if (withExtra) env->DeleteLocalRef(withExtra);
	env->CallVoidMethod(activityObj, startActivity, intent);
	env->DeleteLocalRef(extraValue);
	env->DeleteLocalRef(extraKey);
	env->DeleteLocalRef(className);
	env->DeleteLocalRef(intent);
	env->DeleteLocalRef(intentCls);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}

	if (!pumpUntilDialogDone() || !dialogResultHasStr)
		return false;
	path = dialogResultStr;
	return true;
}


/** If `path` was created by DocumentSaveActivity, synchronously mirror the
 * finished local .vcv to the remembered SAF URI. Unlinked/private paths are a
 * no-op. The local file is always written first, so a provider failure cannot
 * destroy the only copy of a patch. */
bool commitDocumentSave(const std::string& path) {
	JNIEnv* env = getEnv();
	if (!env || !activityObj || !activityCls)
		return true; // no Android bridge means ordinary private save semantics

	jmethodID getPrefs = env->GetMethodID(activityCls, "getSharedPreferences",
		"(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
	jclass prefsCls = env->FindClass("android/content/SharedPreferences");
	jmethodID getString = prefsCls ? env->GetMethodID(prefsCls, "getString",
		"(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") : NULL;
	if (!getPrefs || !prefsCls || !getString || env->ExceptionCheck()) {
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (prefsCls) env->DeleteLocalRef(prefsCls);
		return false;
	}

	jstring prefsName = env->NewStringUTF("document_save_links");
	jobject prefs = env->CallObjectMethod(activityObj, getPrefs, prefsName, 0);
	jstring key = env->NewStringUTF(path.c_str());
	jstring uriJs = prefs ? (jstring) env->CallObjectMethod(prefs, getString, key, NULL) : NULL;
	env->DeleteLocalRef(key);
	env->DeleteLocalRef(prefsName);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		if (prefs) env->DeleteLocalRef(prefs);
		env->DeleteLocalRef(prefsCls);
		return false;
	}
	if (!uriJs) {
		if (prefs) env->DeleteLocalRef(prefs);
		env->DeleteLocalRef(prefsCls);
		return true; // normal internal RackDroid save
	}
	std::string uriString = jstringToStd(env, uriJs);
	env->DeleteLocalRef(uriJs);
	if (prefs) env->DeleteLocalRef(prefs);
	env->DeleteLocalRef(prefsCls);
	if (uriString.empty())
		return true;

	jclass uriCls = env->FindClass("android/net/Uri");
	jclass resolverCls = env->FindClass("android/content/ContentResolver");
	jclass pfdCls = env->FindClass("android/os/ParcelFileDescriptor");
	jmethodID parse = uriCls ? env->GetStaticMethodID(uriCls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;") : NULL;
	jmethodID getResolver = env->GetMethodID(activityCls, "getContentResolver", "()Landroid/content/ContentResolver;");
	jmethodID openPfd = resolverCls ? env->GetMethodID(resolverCls, "openFileDescriptor",
		"(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;") : NULL;
	jmethodID detachFd = pfdCls ? env->GetMethodID(pfdCls, "detachFd", "()I") : NULL;
	if (!uriCls || !resolverCls || !pfdCls || !parse || !getResolver || !openPfd || !detachFd || env->ExceptionCheck()) {
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (uriCls) env->DeleteLocalRef(uriCls);
		if (resolverCls) env->DeleteLocalRef(resolverCls);
		if (pfdCls) env->DeleteLocalRef(pfdCls);
		return false;
	}

	jstring uriText = env->NewStringUTF(uriString.c_str());
	jobject uri = env->CallStaticObjectMethod(uriCls, parse, uriText);
	jobject resolver = env->CallObjectMethod(activityObj, getResolver);
	env->DeleteLocalRef(uriText);
	if (env->ExceptionCheck() || !uri || !resolver) {
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (uri) env->DeleteLocalRef(uri);
		if (resolver) env->DeleteLocalRef(resolver);
		env->DeleteLocalRef(uriCls);
		env->DeleteLocalRef(resolverCls);
		env->DeleteLocalRef(pfdCls);
		return false;
	}

	jobject pfd = NULL;
	const char* modes[] = {"rwt", "wt", "w"};
	for (const char* mode : modes) {
		jstring modeJs = env->NewStringUTF(mode);
		pfd = env->CallObjectMethod(resolver, openPfd, uri, modeJs);
		env->DeleteLocalRef(modeJs);
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			pfd = NULL;
		}
		if (pfd)
			break;
	}

	int outFd = -1;
	if (pfd) {
		outFd = env->CallIntMethod(pfd, detachFd);
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			outFd = -1;
		}
		env->DeleteLocalRef(pfd);
	}
	env->DeleteLocalRef(resolver);
	env->DeleteLocalRef(uri);
	env->DeleteLocalRef(uriCls);
	env->DeleteLocalRef(resolverCls);
	env->DeleteLocalRef(pfdCls);
	if (outFd < 0)
		return false;

	// rwt/wt already request truncation. Repeat it at the fd layer when the
	// provider exposes a seekable descriptor; ignore EINVAL/ESPIPE for cloud
	// providers whose descriptor is stream-like and handles truncation itself.
	::ftruncate(outFd, 0);
	::lseek(outFd, 0, SEEK_SET);

	int inFd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	if (inFd < 0) {
		::close(outFd);
		return false;
	}

	bool ok = true;
	char buffer[64 * 1024];
	while (true) {
		ssize_t n = ::read(inFd, buffer, sizeof(buffer));
		if (n == 0)
			break;
		if (n < 0) {
			if (errno == EINTR) continue;
			ok = false;
			break;
		}
		ssize_t off = 0;
		while (off < n) {
			ssize_t w = ::write(outFd, buffer + off, (size_t) (n - off));
			if (w < 0 && errno == EINTR) continue;
			if (w <= 0) {
				ok = false;
				break;
			}
			off += w;
		}
		if (!ok) break;
	}
	::close(inFd);
	// Some cloud providers expose a non-fsyncable descriptor. close() is the
	// provider boundary that matters; fsync failure alone is not data failure.
	::fsync(outFd);
	if (::close(outFd) != 0)
		ok = false;
	return ok;
}


// ---- results posted by MainActivity / helper Activity (UI thread) ----

extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeSetStartupOptions(JNIEnv* env, jobject thiz,
		jboolean safeMode, jboolean skipUserPlugins) {
	setStartupOptions(safeMode, skipUserPlugins);
}

/** MainActivity signals that loadUserPlugins() has finished, so the startup
path may proceed to launch the patch with every side-loaded pack registered. */
extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_MainActivity_nativeUserPluginsLoaded(JNIEnv* env, jobject thiz) {
	userPluginsDone = true;
}

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

/** DocumentSaveActivity uses the same synchronous dialog handoff as the
 * existing MainActivity file picker, but keeps its Activity plumbing isolated
 * from NativeActivity. */
extern "C" JNIEXPORT void JNICALL
Java_org_rackdroid_DocumentSaveActivity_nativeDocumentSaveResult(JNIEnv* env, jobject thiz, jstring js) {
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
