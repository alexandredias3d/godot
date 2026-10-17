/**************************************************************************/
/*  display_server_switch.cpp                                             */
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

#include "display_server_switch.h"

#include "joypad_switch.h"
#include "os_switch.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/os/main_loop.h"
#include "servers/display/native_menu.h"

#ifdef GLES3_ENABLED
#include "drivers/gles3/rasterizer_gles3.h"
#endif

#define TOUCH_SCREEN_WIDTH 1280
#define TOUCH_SCREEN_HEIGHT 720

DisplayServerSwitch *DisplayServerSwitch::get_singleton() {
	return static_cast<DisplayServerSwitch *>(DisplayServer::get_singleton());
}

Size2i DisplayServerSwitch::_get_size_for_mode(AppletOperationMode p_mode) {
	if (p_mode == AppletOperationMode_Console) {
		return Size2i(1920, 1080);
	}
	return Size2i(1280, 720);
}

bool DisplayServerSwitch::has_feature(DisplayServerEnums::Feature p_feature) const {
	switch (p_feature) {
		case DisplayServerEnums::FEATURE_TOUCHSCREEN:
		case DisplayServerEnums::FEATURE_VIRTUAL_KEYBOARD:
		case DisplayServerEnums::FEATURE_SWAP_BUFFERS:
		case DisplayServerEnums::FEATURE_KEEP_SCREEN_ON:
			return true;
		default:
			return false;
	}
}

int DisplayServerSwitch::screen_get_dpi(int p_screen) const {
	// Handheld screen: 6.2" at 1280x720 (~237 DPI). Docked, the TV size is unknown.
	return operation_mode == AppletOperationMode_Handheld ? 237 : 96;
}

void DisplayServerSwitch::screen_set_keep_on(bool p_enable) {
	appletSetMediaPlaybackState(p_enable);
}

bool DisplayServerSwitch::is_touchscreen_available() const {
	// The touch screen only works in handheld mode.
	return operation_mode == AppletOperationMode_Handheld;
}

Vector<DisplayServerEnums::WindowID> DisplayServerSwitch::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> ret;
	ret.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return ret;
}

void DisplayServerSwitch::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
	vsync_mode = p_vsync_mode;
#ifdef GLES3_ENABLED
	if (gl_manager) {
		gl_manager->set_use_vsync(p_vsync_mode != DisplayServerEnums::VSYNC_DISABLED);
	}
#endif
}

int64_t DisplayServerSwitch::window_get_native_handle(DisplayServerEnums::HandleType p_handle_type, DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(p_window != DisplayServerEnums::MAIN_WINDOW_ID, 0);
	switch (p_handle_type) {
		case DisplayServerEnums::WINDOW_HANDLE:
			return reinterpret_cast<int64_t>(nwindowGetDefault());
#ifdef GLES3_ENABLED
		case DisplayServerEnums::DISPLAY_HANDLE:
			return reinterpret_cast<int64_t>(eglGetCurrentDisplay());
		case DisplayServerEnums::OPENGL_CONTEXT:
			return reinterpret_cast<int64_t>(eglGetCurrentContext());
#endif
		default:
			return 0;
	}
}

void DisplayServerSwitch::_send_window_event(DisplayServerEnums::WindowEvent p_event) const {
	if (window_event_callback.is_valid()) {
		window_event_callback.call((int)p_event);
	}
}

void DisplayServerSwitch::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	get_singleton()->_dispatch_input_event(p_event);
}

void DisplayServerSwitch::_dispatch_input_event(const Ref<InputEvent> &p_event) {
	if (input_event_callback.is_valid()) {
		input_event_callback.call(p_event);
	}
}

void DisplayServerSwitch::_update_operation_mode() {
	AppletOperationMode mode = appletGetOperationMode();
	if (mode == operation_mode) {
		return;
	}
	operation_mode = mode;
	window_size = _get_size_for_mode(mode);

#ifdef GLES3_ENABLED
	if (gl_manager) {
		gl_manager->resize(window_size);
	}
#endif

	if (rect_changed_callback.is_valid()) {
		rect_changed_callback.call(Rect2i(Point2i(), window_size));
	}
}

void DisplayServerSwitch::_update_focus_state() {
	AppletFocusState state = appletGetFocusState();
	if (state == focus_state) {
		return;
	}
	bool was_focused = focus_state == AppletFocusState_InFocus;
	focus_state = state;
	bool focused = focus_state == AppletFocusState_InFocus;
	if (was_focused == focused) {
		return;
	}

	MainLoop *main_loop = OS::get_singleton()->get_main_loop();
	if (focused) {
		_send_window_event(DisplayServerEnums::WINDOW_EVENT_FOCUS_IN);
		if (main_loop) {
			main_loop->notification(MainLoop::NOTIFICATION_APPLICATION_RESUMED);
		}
	} else {
		if (main_loop) {
			main_loop->notification(MainLoop::NOTIFICATION_APPLICATION_PAUSED);
		}
		_send_window_event(DisplayServerEnums::WINDOW_EVENT_FOCUS_OUT);
	}
}

void DisplayServerSwitch::_process_touch() {
	Input *input = Input::get_singleton();

	// The software keyboard draws over the game and handles touches itself.
	if (swkbd_open || operation_mode != AppletOperationMode_Handheld) {
		for (int i = 0; i < last_touch_count; i++) {
			Ref<InputEventScreenTouch> st;
			st.instantiate();
			st->set_index(i);
			st->set_position(last_touch_pos[i]);
			st->set_pressed(false);
			st->set_canceled(true);
			input->parse_input_event(st);
		}
		last_touch_count = 0;
		return;
	}

	HidTouchScreenState touch_state = {};
	if (!hidGetTouchScreenStates(&touch_state, 1)) {
		return;
	}

	int count = MIN((int)touch_state.count, MAX_TOUCHES);
	// Touch coordinates are always in the 1280x720 panel space.
	Vector2 scale = Vector2(window_size) / Vector2(TOUCH_SCREEN_WIDTH, TOUCH_SCREEN_HEIGHT);

	for (int i = 0; i < count; i++) {
		Vector2 pos = Vector2(touch_state.touches[i].x, touch_state.touches[i].y) * scale;
		if (i >= last_touch_count) {
			Ref<InputEventScreenTouch> st;
			st.instantiate();
			st->set_index(i);
			st->set_position(pos);
			st->set_pressed(true);
			input->parse_input_event(st);
		} else if (pos != last_touch_pos[i]) {
			Ref<InputEventScreenDrag> sd;
			sd.instantiate();
			sd->set_index(i);
			sd->set_position(pos);
			sd->set_relative(pos - last_touch_pos[i]);
			sd->set_relative_screen_position(sd->get_relative());
			input->parse_input_event(sd);
		}
		last_touch_pos[i] = pos;
	}

	for (int i = count; i < last_touch_count; i++) {
		Ref<InputEventScreenTouch> st;
		st.instantiate();
		st->set_index(i);
		st->set_position(last_touch_pos[i]);
		st->set_pressed(false);
		input->parse_input_event(st);
	}

	last_touch_count = count;
}

void DisplayServerSwitch::process_events() {
	_update_operation_mode();
	_update_focus_state();

#ifdef TOUCH_ENABLED
	_process_touch();
#endif

	if (joypad) {
		joypad->process();
	}

	if (swkbd_created) {
		swkbdInlineUpdate(&inline_keyboard, nullptr);
	}

	Input::get_singleton()->flush_buffered_events();
}

// Software keyboard.

void DisplayServerSwitch::_send_key(Key p_keycode, char32_t p_unicode) {
	for (int pressed = 1; pressed >= 0; pressed--) {
		Ref<InputEventKey> ev;
		ev.instantiate();
		ev->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
		ev->set_echo(false);
		ev->set_pressed(pressed);
		ev->set_keycode(p_keycode);
		ev->set_physical_keycode(p_keycode);
		ev->set_key_label(p_keycode);
		if (pressed) {
			ev->set_unicode(p_unicode);
		}
		Input::get_singleton()->parse_input_event(ev);
	}
}

void DisplayServerSwitch::_swkbd_string_changed(const char *p_str, SwkbdChangedStringArg *p_arg) {
	DisplayServerSwitch *ds = get_singleton();
	// We get a "string changed" event when the keyboard appears, and another one when setting the text.
	if (ds->swkbd_eat_string_events > 0) {
		ds->swkbd_last_len = p_arg->stringLen;
		ds->swkbd_eat_string_events--;
		return;
	}

	if (p_arg->stringLen < ds->swkbd_last_len) {
		for (uint32_t i = p_arg->stringLen; i < ds->swkbd_last_len; i++) {
			ds->_send_key(Key::BACKSPACE, 0);
		}
	} else if (p_arg->stringLen > ds->swkbd_last_len) {
		// stringLen counts UTF-16 code units; the typed characters are at the end of the UTF-8 string.
		String text = String::utf8(p_str);
		int added = p_arg->stringLen - ds->swkbd_last_len;
		for (int i = MAX(0, text.length() - added); i < text.length(); i++) {
			ds->_send_key(Key::NONE, text[i]);
		}
	}
	ds->swkbd_last_len = p_arg->stringLen;
}

void DisplayServerSwitch::_swkbd_moved_cursor(const char *p_str, SwkbdMovedCursorArg *p_arg) {
	DisplayServerSwitch *ds = get_singleton();
	if (p_arg->cursorPos < ds->swkbd_last_cursor) {
		ds->_send_key(Key::LEFT, 0);
	} else if (p_arg->cursorPos > ds->swkbd_last_cursor) {
		ds->_send_key(Key::RIGHT, 0);
	}
	ds->swkbd_last_cursor = p_arg->cursorPos;
}

void DisplayServerSwitch::_swkbd_decided_enter(const char *p_str, SwkbdDecidedEnterArg *p_arg) {
	DisplayServerSwitch *ds = get_singleton();
	ds->swkbd_open = false;
	ds->_send_key(Key::ENTER, 0);
}

void DisplayServerSwitch::_swkbd_decided_cancel() {
	get_singleton()->swkbd_open = false;
}

void DisplayServerSwitch::virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect, DisplayServerEnums::VirtualKeyboardType p_type, int p_max_length, int p_cursor_start, int p_cursor_end) {
	if (!swkbd_created || swkbd_open) {
		return;
	}
	swkbd_open = true;

	SwkbdType type = SwkbdType_Normal;
	switch (p_type) {
		case DisplayServerEnums::KEYBOARD_TYPE_NUMBER:
		case DisplayServerEnums::KEYBOARD_TYPE_NUMBER_DECIMAL:
		case DisplayServerEnums::KEYBOARD_TYPE_PHONE:
			type = SwkbdType_NumPad;
			break;
		default:
			break;
	}

	SwkbdAppearArg appear_arg;
	swkbdInlineMakeAppearArg(&appear_arg, type);
	if (p_max_length > 0) {
		appear_arg.stringLenMax = p_max_length;
	}
	CharString utf8 = p_existing_text.utf8();
	swkbdInlineSetInputText(&inline_keyboard, utf8.get_data());
	int cursor = p_cursor_start >= 0 ? p_cursor_start : p_existing_text.length();
	swkbdInlineSetCursorPos(&inline_keyboard, cursor);
	swkbd_last_cursor = cursor;

	swkbd_eat_string_events = 2;
	swkbdInlineAppear(&inline_keyboard, &appear_arg);
}

void DisplayServerSwitch::virtual_keyboard_hide() {
	if (!swkbd_created) {
		return;
	}
	swkbd_open = false;
	swkbdInlineDisappear(&inline_keyboard);
}

int DisplayServerSwitch::virtual_keyboard_get_height() const {
	// The inline keyboard covers roughly the lower 40% of the screen.
	return swkbd_open ? (int)(window_size.height * 0.4f) : 0;
}

// Rendering.

void DisplayServerSwitch::release_rendering_thread() {
#ifdef GLES3_ENABLED
	if (gl_manager) {
		gl_manager->release_current();
	}
#endif
}

void DisplayServerSwitch::swap_buffers() {
#ifdef GLES3_ENABLED
	if (gl_manager) {
		gl_manager->swap_buffers();
	}
#endif
}

Vector<String> DisplayServerSwitch::get_rendering_drivers_func() {
	Vector<String> drivers;
#ifdef GLES3_ENABLED
	drivers.push_back("opengl3");
	drivers.push_back("opengl3_es");
#endif
	return drivers;
}

DisplayServer *DisplayServerSwitch::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	DisplayServer *ds = memnew(DisplayServerSwitch(p_rendering_driver, p_vsync_mode, r_error));
	if (r_error != OK) {
		OS::get_singleton()->alert("Could not initialize the OpenGL ES 3 renderer.", "Unable to initialize video driver");
	}
	return ds;
}

void DisplayServerSwitch::register_switch_driver() {
	register_create_function("switch", create_func, get_rendering_drivers_func);
}

DisplayServerSwitch::DisplayServerSwitch(const String &p_rendering_driver, DisplayServerEnums::VSyncMode p_vsync_mode, Error &r_error) {
	rendering_driver = p_rendering_driver;
	vsync_mode = p_vsync_mode;
	native_menu = memnew(NativeMenu);

	operation_mode = appletGetOperationMode();
	focus_state = appletGetFocusState();
	window_size = _get_size_for_mode(operation_mode);

	r_error = ERR_UNAVAILABLE;

#ifdef GLES3_ENABLED
	if (rendering_driver == "opengl3" || rendering_driver == "opengl3_es") {
		// Projects made with the Compatibility renderer ask for "opengl3" (desktop GL on PC);
		// on Switch it is always GLES.
		rendering_driver = "opengl3_es";
		OS::get_singleton()->set_current_rendering_driver_name(rendering_driver, OS::RENDERING_SOURCE_FALLBACK);

		gl_manager = memnew(GLManagerSwitchEGL);
		gl_manager->set_use_vsync(p_vsync_mode != DisplayServerEnums::VSYNC_DISABLED);
		if (gl_manager->initialize(window_size) != OK) {
			memdelete(gl_manager);
			gl_manager = nullptr;
			ERR_FAIL_MSG("Could not initialize EGL / OpenGL ES 3.");
		}
		RasterizerGLES3::make_current(false);
	}
#endif

	if (rendering_driver != "opengl3_es") {
		ERR_FAIL_MSG(vformat("Unsupported rendering driver '%s' on Switch. Use the Compatibility renderer.", rendering_driver));
	}

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

#ifdef TOUCH_ENABLED
	hidInitializeTouchScreen();
#endif
	joypad = memnew(JoypadSwitch(Input::get_singleton()));

	if (R_SUCCEEDED(swkbdInlineCreate(&inline_keyboard))) {
		if (R_SUCCEEDED(swkbdInlineLaunchForLibraryApplet(&inline_keyboard, SwkbdInlineMode_AppletDisplay, 0))) {
			swkbdInlineSetChangedStringCallback(&inline_keyboard, _swkbd_string_changed);
			swkbdInlineSetMovedCursorCallback(&inline_keyboard, _swkbd_moved_cursor);
			swkbdInlineSetDecidedEnterCallback(&inline_keyboard, _swkbd_decided_enter);
			swkbdInlineSetDecidedCancelCallback(&inline_keyboard, _swkbd_decided_cancel);
			swkbd_created = true;
		} else {
			swkbdInlineClose(&inline_keyboard);
		}
	}

	r_error = OK;
}

DisplayServerSwitch::~DisplayServerSwitch() {
	if (swkbd_created) {
		swkbdInlineClose(&inline_keyboard);
		swkbd_created = false;
	}
	if (joypad) {
		memdelete(joypad);
		joypad = nullptr;
	}
#ifdef GLES3_ENABLED
	if (gl_manager) {
		memdelete(gl_manager);
		gl_manager = nullptr;
	}
#endif
	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}
}
