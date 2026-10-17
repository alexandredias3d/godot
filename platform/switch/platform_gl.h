/**************************************************************************/
/*  platform_gl.h                                                         */
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

#ifndef GLES_API_ENABLED
#define GLES_API_ENABLED // Allow using GLES.
#endif

// devkitPro's Mesa port links GLES 3.2 statically, so no loader (GLAD) is needed.
// IWYU pragma: begin_exports.
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
// IWYU pragma: end_exports.

#include <EGL/egl.h>

// GLES has no glTexImage3DMultisample, but the GLES 3.1 storage variant has the same signature.
#define glTexImage3DMultisample glTexStorage3DMultisample

// OVR_multiview is not exported by the static GLESv2 library; resolve it at runtime
// (only used for XR, which Switch doesn't support anyway).
static inline void godot_switch_glFramebufferTextureMultiviewOVR(GLenum p_target, GLenum p_attachment, GLuint p_texture, GLint p_level, GLint p_base_view_index, GLsizei p_num_views) {
	static PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC proc = (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)eglGetProcAddress("glFramebufferTextureMultiviewOVR");
	if (proc) {
		proc(p_target, p_attachment, p_texture, p_level, p_base_view_index, p_num_views);
	}
}
#define glFramebufferTextureMultiviewOVR godot_switch_glFramebufferTextureMultiviewOVR
