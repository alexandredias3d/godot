/**************************************************************************/
/*  display_server_switch.h                                               */
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

#include "switch_wrapper.h"

#include "core/input/input.h"
#include "servers/display/display_server.h"

#ifdef GLES3_ENABLED
#include "gl_manager_switch_egl.h"
#endif

class JoypadSwitch;
class NativeMenu;

// Horizon has a single fullscreen "window" (the default NWindow), whose resolution
// depends on the console being docked (1920x1080) or handheld (1280x720).
class DisplayServerSwitch : public DisplayServer {
	GDSOFTCLASS(DisplayServerSwitch, DisplayServer);

	static constexpr int MAX_TOUCHES = 16;

	String rendering_driver;
#ifdef GLES3_ENABLED
	GLManagerSwitchEGL *gl_manager = nullptr;
#endif
	NativeMenu *native_menu = nullptr;
	JoypadSwitch *joypad = nullptr;

	Size2i window_size;
	AppletOperationMode operation_mode = AppletOperationMode_Handheld;
	AppletFocusState focus_state = AppletFocusState_InFocus;
	DisplayServerEnums::VSyncMode vsync_mode = DisplayServerEnums::VSYNC_ENABLED;

	ObjectID window_attached_instance_id;
	Callable window_event_callback;
	Callable input_event_callback;
	Callable input_text_callback;
	Callable rect_changed_callback;

	// Touch screen state.
	int last_touch_count = 0;
	Vector2 last_touch_pos[MAX_TOUCHES];

	// Inline software keyboard.
	SwkbdInline inline_keyboard;
	bool swkbd_created = false;
	bool swkbd_open = false;
	int swkbd_eat_string_events = 0;
	uint32_t swkbd_last_len = 0;
	int32_t swkbd_last_cursor = 0;

	static Size2i _get_size_for_mode(AppletOperationMode p_mode);
	void _update_operation_mode();
	void _update_focus_state();
	void _process_touch();
	void _send_window_event(DisplayServerEnums::WindowEvent p_event) const;

	static void _dispatch_input_events(const Ref<InputEvent> &p_event);
	void _dispatch_input_event(const Ref<InputEvent> &p_event);

	static void _swkbd_string_changed(const char *p_str, SwkbdChangedStringArg *p_arg);
	static void _swkbd_moved_cursor(const char *p_str, SwkbdMovedCursorArg *p_arg);
	static void _swkbd_decided_enter(const char *p_str, SwkbdDecidedEnterArg *p_arg);
	static void _swkbd_decided_cancel();
	void _send_key(Key p_keycode, char32_t p_unicode);

	static Vector<String> get_rendering_drivers_func();
	static DisplayServer *create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error);

public:
	static DisplayServerSwitch *get_singleton();
	static void register_switch_driver();

	bool has_feature(DisplayServerEnums::Feature p_feature) const override;
	String get_name() const override { return "Switch"; }

	int get_screen_count() const override { return 1; }
	int get_primary_screen() const override { return 0; }
	Point2i screen_get_position(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return Point2i(); }
	Size2i screen_get_size(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return window_size; }
	Rect2i screen_get_usable_rect(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return Rect2i(Point2i(), window_size); }
	int screen_get_dpi(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	float screen_get_scale(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return 1.0f; }
	float screen_get_refresh_rate(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return 60.0f; }
	void screen_set_keep_on(bool p_enable) override;
	bool is_touchscreen_available() const override;

	Vector<DisplayServerEnums::WindowID> get_window_list() const override;
	DisplayServerEnums::WindowID get_window_at_screen_position(const Point2i &p_position) const override { return DisplayServerEnums::MAIN_WINDOW_ID; }

	void window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override { window_attached_instance_id = p_instance; }
	ObjectID window_get_attached_instance_id(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return window_attached_instance_id; }

	void window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override { rect_changed_callback = p_callable; }
	void window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override { window_event_callback = p_callable; }
	void window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override { input_event_callback = p_callable; }
	void window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override { input_text_callback = p_callable; }
	void window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	void window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	int window_get_current_screen(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return 0; }
	void window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	Point2i window_get_position(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return Point2i(); }
	Point2i window_get_position_with_decorations(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return Point2i(); }
	void window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}

	void window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) override {}

	void window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	Size2i window_get_max_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return Size2i(); }
	void window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	Size2i window_get_min_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return Size2i(); }

	void window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	Size2i window_get_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return window_size; }
	Size2i window_get_size_with_decorations(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return window_size; }

	void window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	DisplayServerEnums::WindowMode window_get_mode(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return DisplayServerEnums::WINDOW_MODE_EXCLUSIVE_FULLSCREEN; }

	void window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	DisplayServerEnums::VSyncMode window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const override { return vsync_mode; }

	bool window_is_maximize_allowed(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return false; }

	void window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	bool window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return false; }

	void window_request_attention(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	void window_move_to_foreground(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override {}
	bool window_is_focused(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return focus_state == AppletFocusState_InFocus; }

	bool window_can_draw(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return true; }
	bool can_any_window_draw() const override { return true; }

	int64_t window_get_native_handle(DisplayServerEnums::HandleType p_handle_type, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;

	void process_events() override;

	void virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect = Rect2(), DisplayServerEnums::VirtualKeyboardType p_type = DisplayServerEnums::KEYBOARD_TYPE_DEFAULT, int p_max_length = -1, int p_cursor_start = -1, int p_cursor_end = -1) override;
	void virtual_keyboard_hide() override;
	int virtual_keyboard_get_height() const override;
	bool has_hardware_keyboard() const override { return false; }

	void release_rendering_thread() override;
	void swap_buffers() override;

	DisplayServerSwitch(const String &p_rendering_driver, DisplayServerEnums::VSyncMode p_vsync_mode, Error &r_error);
	~DisplayServerSwitch();
};
