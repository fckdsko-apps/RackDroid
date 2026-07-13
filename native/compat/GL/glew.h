#pragma once
/* GLEW compat shim for the RackDroid build.
 *
 * Rack's include/window/Window.hpp includes <GL/glew.h> for GL types, but
 * GLEW is a desktop-GL loader and does not exist on Android (GLES functions
 * are linked directly from libGLESv3). This shim satisfies the include with
 * the platform's real GL headers where present, or the bare typedefs needed
 * for headless compilation where not.
 */

#if defined(__ANDROID__)
	#include <GLES3/gl3.h>
#elif defined(__has_include) && __has_include(<GLES3/gl3.h>)
	/* Host UI smoke test: same GLES3 headers as Android (Mesa). */
	#include <GLES3/gl3.h>
#elif defined(__has_include) && __has_include(<GL/gl.h>)
	#include <GL/gl.h>
#else
	/* Headless build without any GL SDK: only type declarations. */
	typedef unsigned int GLenum;
	typedef unsigned int GLuint;
	typedef int GLint;
	typedef int GLsizei;
	typedef unsigned char GLboolean;
	typedef float GLfloat;
#endif

/* GLEW API surface used by Rack's Window.cpp (not compiled headless). */
#ifndef GLEW_OK
	#define GLEW_OK 0
#endif
