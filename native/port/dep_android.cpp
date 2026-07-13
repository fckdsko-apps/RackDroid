/* Android equivalent of Rack's src/dep.cpp: compiles the header-only
 * libraries. Uses the nanovg GLES3 backend instead of desktop GL2 — upstream
 * already anticipates this backend (commented out in src/dep.cpp).
 * No GLEW: GLES3 functions link directly from libGLESv3.
 */
#include <common.hpp> // for fopen_u8

#include <GLES3/gl3.h>

#include <nanovg.h>
#define NANOVG_GLES3_IMPLEMENTATION
#include <nanovg_gl.h>

// Hack to get framebuffer objects working (guaranteed on ES3)
#define NANOVG_FBO_VALID
#include <nanovg_gl_utils.h>

// Some plugins (e.g. computerscare) call the desktop-GL2 nanovg entry
// points directly, matching Rack's official builds. Alias them to the
// GLES3 backend compiled above; same underlying context either way.
extern "C" GLuint nvglImageHandleGL2(NVGcontext* ctx, int image) {
	return nvglImageHandleGLES3(ctx, image);
}
extern "C" int nvglCreateImageFromHandleGL2(NVGcontext* ctx, GLuint textureId, int w, int h, int flags) {
	return nvglCreateImageFromHandleGLES3(ctx, textureId, w, h, flags);
}

// blendish is compiled separately from Rack's dep/oui-blendish/blendish.c
// (Rack's fork ships it as a .c file, not header-only).

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include <nanosvg.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
