/**************************************************************************/
/*  os_switch.h                                                           */
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

#include "audio_driver_switch.h"

#include "drivers/unix/os_unix.h"
#include "servers/audio/audio_server.h"

class OS_Switch : public OS_Unix {
	MainLoop *main_loop = nullptr;
	AudioDriverSwitch audio_driver;
	String executable_path;

protected:
	void initialize() override;
	void initialize_joypads() override {}

	void set_main_loop(MainLoop *p_main_loop) override;
	void delete_main_loop() override;

	void finalize() override;

	bool _check_internal_feature_support(const String &p_feature) override;

public:
	static OS_Switch *get_singleton();

	void set_executable_path(const String &p_path) { executable_path = p_path; }
	String get_executable_path() const override;

	MainLoop *get_main_loop() const override;
	void run();

	String get_name() const override;
	String get_distribution_name() const override;
	String get_version() const override;
	String get_model_name() const override;
	String get_processor_name() const override;
	int get_processor_count() const override;
	String get_unique_id() const override;
	String get_locale() const override;
	Dictionary get_memory_info() const override;

	void alert(const String &p_alert, const String &p_title = "ALERT!") override;

	Error get_entropy(uint8_t *r_buffer, int p_bytes) override;

	// No processes or dynamic libraries on Horizon homebrew.
	Error execute(const String &p_path, const List<String> &p_arguments, String *r_pipe = nullptr, int *r_exitcode = nullptr, bool read_stderr = false, Mutex *p_pipe_mutex = nullptr, bool p_open_console = false) override;
	Dictionary execute_with_pipe(const String &p_path, const List<String> &p_arguments, bool p_blocking = true) override;
	Error create_process(const String &p_path, const List<String> &p_arguments, ProcessID *r_child_id = nullptr, bool p_open_console = false) override;
	Error create_instance(const List<String> &p_arguments, ProcessID *r_child_id = nullptr) override;
	Error kill(const ProcessID &p_pid) override;
	int get_process_id() const override;
	bool is_process_running(const ProcessID &p_pid) const override;
	int get_process_exit_code(const ProcessID &p_pid) const override;
	Error shell_open(const String &p_uri) override;

	Error open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data = nullptr) override;
	Error close_dynamic_library(void *p_library_handle) override;
	Error get_dynamic_library_symbol_handle(void *p_library_handle, const String &p_name, void *&p_symbol_handle, bool p_optional = false) override;

	String get_data_path() const override;
	String get_config_path() const override;
	String get_cache_path() const override;
	String get_temp_path() const override;
	String get_user_data_dir(const String &p_user_dir) const override;

	void vibrate_handheld(int p_duration_ms = 500, float p_amplitude = -1.0) override;

	OS_Switch();
};
