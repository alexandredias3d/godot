/**************************************************************************/
/*  gl_manager_switch_egl.h                                               */
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

#pragma once

#ifdef GLES3_ENABLED

#include "core/error/error_list.h"
#include "core/math/vector2i.h"

#include <EGL/egl.h>

// Minimal EGL context for the default NWindow, using devkitPro's Mesa (nouveau) port.
// Horizon only has one native window, so this does not try to be a multi-window manager
// like drivers/egl/EGLManager.
class GLManagerSwitchEGL {
	EGLDisplay display = EGL_NO_DISPLAY;
	EGLContext context = EGL_NO_CONTEXT;
	EGLSurface surface = EGL_NO_SURFACE;
	EGLConfig config = nullptr;

	Size2i size;
	bool use_vsync = true;

	Error _create_surface();
	void _destroy_surface();

public:
	Error initialize(const Size2i &p_size);
	void cleanup();

	// Recreates the window surface with new dimensions (e.g. when docking or undocking).
	Error resize(const Size2i &p_size);
	Size2i get_size() const { return size; }

	void make_current();
	void release_current();
	void swap_buffers();

	void set_use_vsync(bool p_use);
	bool is_using_vsync() const { return use_vsync; }

	~GLManagerSwitchEGL();
};

#endif // GLES3_ENABLED
