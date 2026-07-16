#pragma once
/* execinfo.h compat shim for the RackDroid build.
 *
 * third_party/Rack/src/system.cpp (upstream, unmodified) includes
 * <execinfo.h> and calls backtrace() for getStackTrace(). Bionic only
 * declares backtrace()/backtrace_symbols()/backtrace_symbols_fd() starting
 * at API 33 (see the NDK sysroot's execinfo.h, guarded by
 * `#if __ANDROID_API__ >= 33`), which is why PORTING.md pinned minSdk to 33.
 *
 * This header sits earlier on the include path (native/compat is added
 * before the sysroot in CMakeLists.txt) and is picked up by the same
 * `#include <execinfo.h>`. Below API 33 it implements backtrace() itself
 * via the ARM EH unwinder (<unwind.h>, available at every API level,
 * the standard workaround for this bionic gap). At API 33+, and on the
 * non-Android host build, it steps out of the way via #include_next so the
 * platform's real header (bionic 33+, or glibc on host) is used unchanged.
 */

#if defined(__ANDROID__) && __ANDROID_API__ < 33

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <unwind.h>

namespace rackdroid_compat {

struct BacktraceState {
	void** current;
	void** end;
};

inline _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
	auto* state = static_cast<BacktraceState*>(arg);
	uintptr_t pc = _Unwind_GetIP(context);
	if (pc) {
		if (state->current == state->end)
			return _URC_END_OF_STACK;
		*state->current++ = reinterpret_cast<void*>(pc);
	}
	return _URC_NO_REASON;
}

} // namespace rackdroid_compat

inline int backtrace(void** buffer, int size) {
	rackdroid_compat::BacktraceState state{buffer, buffer + size};
	_Unwind_Backtrace(rackdroid_compat::unwindCallback, &state);
	return static_cast<int>(state.current - buffer);
}

// Not called by Rack (getStackTrace formats each frame itself via dladdr),
// but implemented for header-contract completeness: one malloc, matching
// glibc's "single free() by the caller" ownership.
inline char** backtrace_symbols(void* const* buffer, int size) {
	size_t total = size * sizeof(char*);
	for (int i = 0; i < size; i++) total += 32;
	char** result = static_cast<char**>(malloc(total));
	if (!result) return nullptr;
	char* strings = reinterpret_cast<char*>(result + size);
	for (int i = 0; i < size; i++) {
		result[i] = strings;
		int n = snprintf(strings, 32, "%p", buffer[i]);
		strings += n + 1;
	}
	return result;
}

inline void backtrace_symbols_fd(void* const* buffer, int size, int fd) {
	char** syms = backtrace_symbols(buffer, size);
	if (!syms) return;
	for (int i = 0; i < size; i++) {
		write(fd, syms[i], strlen(syms[i]));
		write(fd, "\n", 1);
	}
	free(syms);
}

#else

#include_next <execinfo.h>

#endif // __ANDROID_API__ < 33
