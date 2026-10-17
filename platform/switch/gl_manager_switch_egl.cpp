/**************************************************************************/
/*  gl_manager_switch_egl.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "gl_manager_switch_egl.h"

#ifdef GLES3_ENABLED

#include "switch_wrapper.h"

#include "core/error/error_macros.h"
#include "core/string/print_string.h"
#include "core/variant/variant.h"

Error GLManagerSwitchEGL::initialize(const Size2i &p_size) {
	size = p_size;

	display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	ERR_FAIL_COND_V_MSG(display == EGL_NO_DISPLAY, ERR_UNAVAILABLE, vformat("Could not connect to the EGL display (error 0x%x).", eglGetError()));

	if (!eglInitialize(display, nullptr, nullptr)) {
		ERR_PRINT(vformat("Could not initialize EGL (error 0x%x).", eglGetError()));
		display = EGL_NO_DISPLAY;
		return ERR_UNAVAILABLE;
	}

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		ERR_PRINT(vformat("Could not bind the OpenGL ES API (error 0x%x).", eglGetError()));
		cleanup();
		return ERR_UNAVAILABLE;
	}

	static const EGLint attribute_list[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};

	EGLint num_configs = 0;
	eglChooseConfig(display, attribute_list, &config, 1, &num_configs);
	if (num_configs == 0) {
		ERR_PRINT(vformat("No suitable EGL config found (error 0x%x).", eglGetError()));
		cleanup();
		return ERR_UNAVAILABLE;
	}

	static const EGLint context_attribute_list[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 0,
		EGL_NONE
	};

	context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list);
	if (context == EGL_NO_CONTEXT) {
		ERR_PRINT(vformat("Could not create an OpenGL ES 3 context (error 0x%x).", eglGetError()));
		cleanup();
		return ERR_UNAVAILABLE;
	}

	Error err = _create_surface();
	if (err != OK) {
		cleanup();
		return err;
	}

	make_current();
	set_use_vsync(use_vsync);

	print_verbose(vformat("EGL: %s %s", eglQueryString(display, EGL_VENDOR), eglQueryString(display, EGL_VERSION)));
	return OK;
}

Error GLManagerSwitchEGL::_create_surface() {
	NWindow *win = nwindowGetDefault();
	Result rc = nwindowSetDimensions(win, size.width, size.height);
	ERR_FAIL_COND_V_MSG(R_FAILED(rc), ERR_CANT_CREATE, vformat("Could not set the NWindow dimensions (0x%x).", rc));

	surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)win, nullptr);
	ERR_FAIL_COND_V_MSG(surface == EGL_NO_SURFACE, ERR_CANT_CREATE, vformat("Could not create the EGL window surface (error 0x%x).", eglGetError()));
	return OK;
}

void GLManagerSwitchEGL::_destroy_surface() {
	if (surface != EGL_NO_SURFACE) {
		eglDestroySurface(display, surface);
		surface = EGL_NO_SURFACE;
	}
}

Error GLManagerSwitchEGL::resize(const Size2i &p_size) {
	if (p_size == size || display == EGL_NO_DISPLAY) {
		return OK;
	}

	size = p_size;
	release_current();
	_destroy_surface();
	Error err = _create_surface();
	ERR_FAIL_COND_V(err != OK, err);
	make_current();
	// The swap interval belongs to the surface, apply it again.
	set_use_vsync(use_vsync);
	return OK;
}

void GLManagerSwitchEGL::cleanup() {
	if (display == EGL_NO_DISPLAY) {
		return;
	}

	eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (context != EGL_NO_CONTEXT) {
		eglDestroyContext(display, context);
		context = EGL_NO_CONTEXT;
	}
	_destroy_surface();
	eglTerminate(display);
	display = EGL_NO_DISPLAY;
}

void GLManagerSwitchEGL::make_current() {
	eglMakeCurrent(display, surface, surface, context);
}

void GLManagerSwitchEGL::release_current() {
	eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void GLManagerSwitchEGL::swap_buffers() {
	eglSwapBuffers(display, surface);
}

void GLManagerSwitchEGL::set_use_vsync(bool p_use) {
	use_vsync = p_use;
	if (display != EGL_NO_DISPLAY) {
		eglSwapInterval(display, p_use ? 1 : 0);
	}
}

GLManagerSwitchEGL::~GLManagerSwitchEGL() {
	cleanup();
}

#endif // GLES3_ENABLED
