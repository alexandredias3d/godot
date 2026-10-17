/**************************************************************************/
/*  os_switch.cpp                                                         */
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

#include "os_switch.h"

#include "display_server_switch.h"
#include "switch_wrapper.h"

#include "core/config/project_settings.h"
#include "core/os/main_loop.h"
#include "main/main.h"

#include <cstdio>

OS_Switch *OS_Switch::get_singleton() {
	return static_cast<OS_Switch *>(OS::get_singleton());
}

void OS_Switch::initialize() {
	OS_Unix::initialize_core();
	DisplayServerSwitch::register_switch_driver();
}

void OS_Switch::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

MainLoop *OS_Switch::get_main_loop() const {
	return main_loop;
}

void OS_Switch::delete_main_loop() {
	if (main_loop) {
		memdelete(main_loop);
	}
	main_loop = nullptr;
}

void OS_Switch::finalize() {
	delete_main_loop();
}

void OS_Switch::run() {
	if (!main_loop) {
		return;
	}

	main_loop->initialize();

	// appletMainLoop() returns false when the system asks the application to exit (HOME > Close).
	while (appletMainLoop()) {
		DisplayServer::get_singleton()->process_events();
		if (Main::iteration()) {
			break;
		}
	}

	main_loop->finalize();
}

bool OS_Switch::_check_internal_feature_support(const String &p_feature) {
	return p_feature == "switch" || p_feature == "horizon";
}

String OS_Switch::get_executable_path() const {
	return executable_path;
}

String OS_Switch::get_name() const {
	return "Switch";
}

String OS_Switch::get_distribution_name() const {
	return "Horizon";
}

String OS_Switch::get_version() const {
	if (!hosversionIsAtmosphere() && hosversionGet() == 0) {
		return "";
	}
	u32 v = hosversionGet();
	return vformat("%d.%d.%d", HOSVER_MAJOR(v), HOSVER_MINOR(v), HOSVER_MICRO(v));
}

String OS_Switch::get_model_name() const {
	return "Nintendo Switch";
}

String OS_Switch::get_processor_name() const {
	return "NVIDIA Tegra X1";
}

int OS_Switch::get_processor_count() const {
	// Applications get three of the four Cortex-A57 cores (the fourth is reserved for the system).
	return 3;
}

String OS_Switch::get_unique_id() const {
	return "";
}

String OS_Switch::get_locale() const {
	u64 language_code = 0;
	if (R_SUCCEEDED(setInitialize())) {
		if (R_SUCCEEDED(setGetSystemLanguage(&language_code))) {
			setExit();
			// The language code is a NUL-padded string such as "en-US" or "pt-BR".
			char code[sizeof(u64) + 1] = {};
			memcpy(code, &language_code, sizeof(u64));
			return String(code).replace("-", "_");
		}
		setExit();
	}
	return "en";
}

Dictionary OS_Switch::get_memory_info() const {
	Dictionary meminfo;
	meminfo["physical"] = -1;
	meminfo["free"] = -1;
	meminfo["available"] = -1;
	meminfo["stack"] = -1;

	// Memory available to this process (the application pool), not the whole console.
	u64 total = 0, used = 0;
	if (R_SUCCEEDED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)) &&
			R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))) {
		meminfo["physical"] = (int64_t)total;
		meminfo["free"] = (int64_t)(total - used);
		meminfo["available"] = (int64_t)(total - used);
	}
	return meminfo;
}

void OS_Switch::alert(const String &p_alert, const String &p_title) {
	CharString title = p_title.utf8();
	CharString text = p_alert.utf8();
	printf("%s: %s\n", title.get_data(), text.get_data());

	ErrorApplicationConfig config;
	if (R_SUCCEEDED(errorApplicationCreate(&config, title.get_data(), text.get_data()))) {
		errorApplicationShow(&config);
	}
}

Error OS_Switch::get_entropy(uint8_t *r_buffer, int p_bytes) {
	// Kernel-provided CSPRNG.
	randomGet(r_buffer, p_bytes);
	return OK;
}

Error OS_Switch::execute(const String &p_path, const List<String> &p_arguments, String *r_pipe, int *r_exitcode, bool read_stderr, Mutex *p_pipe_mutex, bool p_open_console) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "OS::execute() is not available on Switch.");
}

Dictionary OS_Switch::execute_with_pipe(const String &p_path, const List<String> &p_arguments, bool p_blocking) {
	ERR_FAIL_V_MSG(Dictionary(), "OS::execute_with_pipe() is not available on Switch.");
}

Error OS_Switch::create_process(const String &p_path, const List<String> &p_arguments, ProcessID *r_child_id, bool p_open_console) {
	// Homebrew can only chain-load another NRO once this one exits (hbloader's "next load").
	String argv = "\"" + p_path + "\"";
	for (const String &arg : p_arguments) {
		argv += " \"" + arg + "\"";
	}
	Result rc = envSetNextLoad(p_path.utf8().get_data(), argv.utf8().get_data());
	ERR_FAIL_COND_V_MSG(R_FAILED(rc), ERR_UNAVAILABLE, "Chain-loading another application is not supported by the current loader.");
	if (r_child_id) {
		*r_child_id = 0;
	}
	return OK;
}

Error OS_Switch::create_instance(const List<String> &p_arguments, ProcessID *r_child_id) {
	return create_process(get_executable_path(), p_arguments, r_child_id);
}

Error OS_Switch::kill(const ProcessID &p_pid) {
	return ERR_UNAVAILABLE;
}

int OS_Switch::get_process_id() const {
	u64 pid = 0;
	svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
	return (int)pid;
}

bool OS_Switch::is_process_running(const ProcessID &p_pid) const {
	return false;
}

int OS_Switch::get_process_exit_code(const ProcessID &p_pid) const {
	return -1;
}

Error OS_Switch::shell_open(const String &p_uri) {
	if (!p_uri.begins_with("http://") && !p_uri.begins_with("https://")) {
		return ERR_UNAVAILABLE;
	}
	WebCommonConfig config;
	Result rc = webPageCreate(&config, p_uri.utf8().get_data());
	if (R_SUCCEEDED(rc)) {
		rc = webConfigShow(&config, nullptr);
	}
	return R_SUCCEEDED(rc) ? OK : ERR_UNAVAILABLE;
}

Error OS_Switch::open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Dynamic libraries (GDExtension) are not supported on Switch. Link extensions statically into the export template.");
}

Error OS_Switch::close_dynamic_library(void *p_library_handle) {
	return ERR_UNAVAILABLE;
}

Error OS_Switch::get_dynamic_library_symbol_handle(void *p_library_handle, const String &p_name, void *&p_symbol_handle, bool p_optional) {
	return ERR_UNAVAILABLE;
}

String OS_Switch::get_data_path() const {
	return "sdmc:/switch";
}

String OS_Switch::get_config_path() const {
	return get_data_path();
}

String OS_Switch::get_cache_path() const {
	return get_data_path().path_join(get_godot_dir_name()).path_join("cache");
}

String OS_Switch::get_temp_path() const {
	return get_data_path().path_join(get_godot_dir_name()).path_join("tmp");
}

String OS_Switch::get_user_data_dir(const String &p_user_dir) const {
	return get_data_path().path_join(get_godot_dir_name()).path_join("app_userdata").path_join(p_user_dir);
}

void OS_Switch::vibrate_handheld(int p_duration_ms, float p_amplitude) {
	// Rumble needs HD Rumble vibration handles per controller; not implemented yet.
}

OS_Switch::OS_Switch() {
	AudioDriverManager::add_driver(&audio_driver);
}
