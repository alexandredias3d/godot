/**************************************************************************/
/*  export_plugin.cpp                                                     */
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

#include "export_plugin.h"

#include "logo_svg.gen.h"
#include "run_icon_svg.gen.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/ip.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/file_system/editor_paths.h"
#include "editor/import/resource_importer_texture_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "modules/svg/image_loader_svg.h"

#define TEMPLATE_RELEASE "switch_release.nro"
#define TEMPLATE_DEBUG "switch_debug.nro"

// Port used by hbmenu's netloader (nxlink) for discovery and transfers.
#define NXLINK_SERVER_PORT 28280
#define NXLINK_CLIENT_PORT 28771

// NRO / NACP layouts, from libnx (switch/nro.h and switch/nacp.h). Only the parts we touch.
namespace SwitchFormats {

struct NroHeader {
	uint32_t start_unused;
	uint32_t start_mod_offset;
	uint8_t start_padding[8];
	uint32_t magic; // "NRO0"
	uint32_t version;
	uint32_t size;
	uint32_t flags;
	uint32_t segments[6];
	uint32_t bss_size;
	uint32_t reserved;
	uint8_t build_id[0x20];
	uint8_t padding[0x20];
};
static_assert(sizeof(NroHeader) == 0x80);

struct NroAssetSection {
	uint64_t offset;
	uint64_t size;
};

struct NroAssetHeader {
	uint32_t magic; // "ASET"
	uint32_t version;
	NroAssetSection icon;
	NroAssetSection nacp;
	NroAssetSection romfs;
};
static_assert(sizeof(NroAssetHeader) == 0x38);

constexpr int NACP_SIZE = 0x4000;
constexpr int NACP_LANGUAGE_COUNT = 16;
constexpr int NACP_NAME_SIZE = 0x200;
constexpr int NACP_AUTHOR_SIZE = 0x100;
constexpr int NACP_DISPLAY_VERSION_OFFSET = 0x3060;
constexpr int NACP_DISPLAY_VERSION_SIZE = 0x10;

constexpr int ICON_SIZE = 256;

} // namespace SwitchFormats

using namespace SwitchFormats;

static void _copy_utf8(uint8_t *r_dst, int p_dst_size, const String &p_src) {
	CharString utf8 = p_src.utf8();
	memset(r_dst, 0, p_dst_size);
	memcpy(r_dst, utf8.get_data(), MIN(utf8.length(), p_dst_size - 1));
}

static Error _copy_range(const Ref<FileAccess> &p_src, const Ref<FileAccess> &p_dst, uint64_t p_len) {
	const uint64_t chunk_size = 0x100000;
	Vector<uint8_t> buffer;
	buffer.resize(chunk_size);
	while (p_len > 0) {
		uint64_t amount = p_src->get_buffer(buffer.ptrw(), MIN(chunk_size, p_len));
		ERR_FAIL_COND_V(amount == 0, ERR_FILE_EOF);
		p_dst->store_buffer(buffer.ptr(), amount);
		p_len -= amount;
	}
	return OK;
}

void EditorExportPlatformSwitch::_device_poll_thread(void *p_ud) {
	EditorExportPlatformSwitch *ea = static_cast<EditorExportPlatformSwitch *>(p_ud);

	Ref<PacketPeerUDP> peer;
	peer.instantiate();
	if (peer->bind(NXLINK_CLIENT_PORT) != OK) {
		print_verbose("Switch export: couldn't bind UDP port for nxlink discovery, one-click deploy disabled.");
		return;
	}
	peer->set_broadcast_enabled(true);
	peer->set_dest_address(IPAddress("255.255.255.255"), NXLINK_SERVER_PORT);

	static const char ping[] = "nxboot";

	while (!ea->quit_request.is_set()) {
		peer->put_packet((const uint8_t *)ping, strlen(ping));
		// Give consoles time to answer.
		OS::get_singleton()->delay_usec(500000);

		Vector<String> ndevices;
		while (peer->get_available_packet_count() > 0) {
			const uint8_t *buffer;
			int len;
			if (peer->get_packet(&buffer, len) != OK) {
				break;
			}
			String address = String(peer->get_packet_address());
			if (!ndevices.has(address)) {
				ndevices.push_back(address);
			}
		}
		ndevices.sort();

		{
			MutexLock lock(ea->device_lock);
			if (ndevices != ea->devices) {
				ea->devices = ndevices;
				ea->devices_changed.set();
			}
		}

		for (int i = 0; i < 30 && !ea->quit_request.is_set(); i++) {
			OS::get_singleton()->delay_usec(100000);
		}
	}

	peer->close();
}

String EditorExportPlatformSwitch::_get_template_name(bool p_debug) const {
	return p_debug ? TEMPLATE_DEBUG : TEMPLATE_RELEASE;
}

void EditorExportPlatformSwitch::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	// The Tegra X1 (Maxwell) decodes both desktop (BCn) and mobile (ETC2/ASTC) formats.
	r_features->push_back("s3tc");
	r_features->push_back("bptc");
	if (p_preset->get("texture_format/etc2_astc")) {
		r_features->push_back("etc2");
		r_features->push_back("astc");
	}
	r_features->push_back("arm64");
}

void EditorExportPlatformSwitch::get_platform_features(List<String> *r_features) const {
	r_features->push_back("switch");
	r_features->push_back("horizon");
}

void EditorExportPlatformSwitch::get_export_options(List<ExportOption> *r_options) const {
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE, "*.nro"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE, "*.nro"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "binary_format/embed_pck"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "texture_format/etc2_astc"), false));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/title", PROPERTY_HINT_PLACEHOLDER_TEXT, "Game Name [default if blank]"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/author", PROPERTY_HINT_PLACEHOLDER_TEXT, "Game Author"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/version", PROPERTY_HINT_PLACEHOLDER_TEXT, "Leave empty to use project version"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/icon", PROPERTY_HINT_FILE, "*.png,*.jpg,*.jpeg,*.webp,*.svg"), ""));
}

bool EditorExportPlatformSwitch::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug) const {
	String err;
	bool valid = true;

	String custom = p_preset->get(p_debug ? "custom_template/debug" : "custom_template/release");
	if (!custom.is_empty()) {
		if (!FileAccess::exists(custom)) {
			err += TTR("Custom export template not found:") + " " + custom + "\n";
			valid = false;
		}
		r_missing_templates = false;
	} else {
		String template_err;
		r_missing_templates = find_export_template(_get_template_name(p_debug), &template_err).is_empty();
		if (r_missing_templates) {
			err += template_err;
			valid = false;
		}
	}

	if (!err.is_empty()) {
		r_error = err;
	}
	return valid;
}

bool EditorExportPlatformSwitch::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	String err;
	bool valid = true;

	String rendering_method = get_project_setting(p_preset, "rendering/renderer/rendering_method");
	if (rendering_method != "gl_compatibility") {
		err += TTR("The Switch export only supports the Compatibility renderer (Project Settings > Rendering > Renderer > Rendering Method).") + "\n";
		valid = false;
	}

	if (p_preset->get("texture_format/etc2_astc") && !ResourceImporterTextureSettings::should_import_etc2_astc()) {
		valid = false;
	}

	if (!err.is_empty()) {
		r_error = err;
	}
	return valid;
}

List<String> EditorExportPlatformSwitch::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> list;
	list.push_back("nro");
	return list;
}

Error EditorExportPlatformSwitch::_create_nro(const String &p_template_path, const String &p_output_path, const String &p_title, const String &p_author, const String &p_version, const String &p_icon_path) {
	Error err;
	Ref<FileAccess> src = FileAccess::open(p_template_path, FileAccess::READ, &err);
	if (src.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not open template \"%s\"."), p_template_path));
		return err;
	}

	NroHeader nro_header;
	src->get_buffer((uint8_t *)&nro_header, sizeof(NroHeader));
	if (memcmp(&nro_header.magic, "NRO0", 4) != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("\"%s\" is not a valid NRO file."), p_template_path));
		return ERR_FILE_CORRUPT;
	}

	NroAssetHeader asset_header;
	src->seek(nro_header.size);
	src->get_buffer((uint8_t *)&asset_header, sizeof(NroAssetHeader));
	if (memcmp(&asset_header.magic, "ASET", 4) != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Template \"%s\" has no asset section (built without elf2nro --icon/--nacp?)."), p_template_path));
		return ERR_FILE_CORRUPT;
	}

	// NACP: start from the template's one so unknown fields keep sane defaults.
	Vector<uint8_t> nacp;
	nacp.resize(NACP_SIZE);
	memset(nacp.ptrw(), 0, NACP_SIZE);
	if (asset_header.nacp.size >= (uint64_t)NACP_SIZE) {
		src->seek(nro_header.size + asset_header.nacp.offset);
		src->get_buffer(nacp.ptrw(), NACP_SIZE);
	}
	uint8_t *nacp_ptr = nacp.ptrw();
	for (int i = 0; i < NACP_LANGUAGE_COUNT; i++) {
		uint8_t *entry = nacp_ptr + i * (NACP_NAME_SIZE + NACP_AUTHOR_SIZE);
		_copy_utf8(entry, NACP_NAME_SIZE, p_title);
		_copy_utf8(entry + NACP_NAME_SIZE, NACP_AUTHOR_SIZE, p_author);
	}
	_copy_utf8(nacp_ptr + NACP_DISPLAY_VERSION_OFFSET, NACP_DISPLAY_VERSION_SIZE, p_version);

	// Icon: hbmenu wants a 256x256 JPEG.
	Vector<uint8_t> icon_data;
	if (!p_icon_path.is_empty()) {
		Error icon_err = OK;
		Ref<Image> icon = _load_icon_or_splash_image(p_icon_path, &icon_err);
		if (icon_err == OK && icon.is_valid() && !icon->is_empty()) {
			icon->convert(Image::FORMAT_RGB8);
			icon->resize(ICON_SIZE, ICON_SIZE, Image::INTERPOLATE_LANCZOS);
			icon_data = icon->save_jpg_to_buffer(0.9);
		} else {
			add_message(EXPORT_MESSAGE_WARNING, TTR("Export"), vformat(TTR("Could not load icon \"%s\", using the template's icon."), p_icon_path));
		}
	}
	if (icon_data.is_empty() && asset_header.icon.size > 0) {
		icon_data.resize(asset_header.icon.size);
		src->seek(nro_header.size + asset_header.icon.offset);
		src->get_buffer(icon_data.ptrw(), icon_data.size());
	}

	Ref<FileAccess> dst = FileAccess::open(p_output_path, FileAccess::WRITE, &err);
	if (dst.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not create \"%s\"."), p_output_path));
		return err;
	}

	// Executable part.
	src->seek(0);
	err = _copy_range(src, dst, nro_header.size);
	ERR_FAIL_COND_V(err != OK, err);

	NroAssetHeader new_asset_header = {};
	memcpy(&new_asset_header.magic, "ASET", 4);
	new_asset_header.version = 0;
	dst->store_buffer((const uint8_t *)&new_asset_header, sizeof(NroAssetHeader));

	new_asset_header.icon.offset = dst->get_position() - nro_header.size;
	new_asset_header.icon.size = icon_data.size();
	dst->store_buffer(icon_data);

	new_asset_header.nacp.offset = dst->get_position() - nro_header.size;
	new_asset_header.nacp.size = NACP_SIZE;
	dst->store_buffer(nacp);

	// Keep the template's romfs (it holds the applet-mode splash).
	new_asset_header.romfs.offset = dst->get_position() - nro_header.size;
	new_asset_header.romfs.size = asset_header.romfs.size;
	if (asset_header.romfs.size > 0) {
		src->seek(nro_header.size + asset_header.romfs.offset);
		err = _copy_range(src, dst, asset_header.romfs.size);
		ERR_FAIL_COND_V(err != OK, err);
	}

	uint64_t end = dst->get_position();
	dst->seek(nro_header.size);
	dst->store_buffer((const uint8_t *)&new_asset_header, sizeof(NroAssetHeader));
	dst->seek(end);

	return OK;
}

Error EditorExportPlatformSwitch::_export_nro(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, bool p_embed_pck) {
	String template_path = p_preset->get(p_debug ? "custom_template/debug" : "custom_template/release");
	if (template_path.is_empty()) {
		template_path = find_export_template(_get_template_name(p_debug));
	}
	if (template_path.is_empty() || !FileAccess::exists(template_path)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Template file not found: \"%s\"."), template_path.is_empty() ? _get_template_name(p_debug) : template_path));
		return ERR_FILE_NOT_FOUND;
	}

	String title = p_preset->get("application/title");
	if (title.is_empty()) {
		title = get_project_setting(p_preset, "application/config/name");
	}
	if (title.is_empty()) {
		title = "Godot Game";
	}
	String author = p_preset->get("application/author");
	if (author.is_empty()) {
		author = "Unknown";
	}
	String version = p_preset->get("application/version");
	if (version.is_empty()) {
		version = get_project_setting(p_preset, "application/config/version");
	}
	if (version.is_empty()) {
		version = "1.0";
	}
	String icon = p_preset->get("application/icon");
	if (icon.is_empty()) {
		icon = String(get_project_setting(p_preset, "application/config/icon")).strip_edges();
	}

	Error err = _create_nro(template_path, p_path, title, author, version, icon);
	if (err != OK) {
		return err;
	}

	if (p_embed_pck) {
		// The PCK goes at the end of the NRO; the loader ignores trailing data and Godot
		// finds the pack through its footer.
		err = save_pack(p_preset, p_debug, p_path, nullptr, nullptr, nullptr, true);
	} else {
		err = save_pack(p_preset, p_debug, p_path.get_basename() + ".pck");
	}
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Save PCK"), TTR("Could not save the PCK file."));
	}
	return err;
}

Error EditorExportPlatformSwitch::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags, bool p_notify) {
	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags, p_notify);

	String base_dir = p_path.get_base_dir();
	if (!DirAccess::exists(base_dir)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Target folder does not exist or is inaccessible: \"%s\""), base_dir));
		return ERR_FILE_BAD_PATH;
	}

	return _export_nro(p_preset, p_debug, p_path, p_preset->get("binary_format/embed_pck"));
}

bool EditorExportPlatformSwitch::poll_export() {
	bool changed = devices_changed.is_set();
	if (changed) {
		devices_changed.clear();
	}
	return changed;
}

int EditorExportPlatformSwitch::get_options_count() const {
	MutexLock lock(device_lock);
	return devices.size();
}

String EditorExportPlatformSwitch::get_options_tooltip() const {
	return TTR("Select a console running the hbmenu netloader (press Y in hbmenu).");
}

String EditorExportPlatformSwitch::get_option_label(int p_index) const {
	MutexLock lock(device_lock);
	ERR_FAIL_INDEX_V(p_index, devices.size(), "");
	return "Switch (" + devices[p_index] + ")";
}

String EditorExportPlatformSwitch::get_option_tooltip(int p_index) const {
	MutexLock lock(device_lock);
	ERR_FAIL_INDEX_V(p_index, devices.size(), "");
	return TTR("Send and run on") + " " + devices[p_index];
}

Error EditorExportPlatformSwitch::run(const Ref<EditorExportPreset> &p_preset, int p_device, BitField<EditorExportPlatform::DebugFlags> p_debug_flags) {
	String device;
	{
		MutexLock lock(device_lock);
		ERR_FAIL_INDEX_V(p_device, devices.size(), ERR_INVALID_PARAMETER);
		device = devices[p_device];
	}

	String nxlink = EDITOR_GET("export/switch/nxlink");
	if (nxlink.is_empty()) {
		// Look for a copy next to the editor, then in devkitPro's default location.
		String exe_ext = OS::get_singleton()->get_name() == "Windows" ? ".exe" : "";
		String bundled = OS::get_singleton()->get_executable_path().get_base_dir().path_join("nxlink" + exe_ext);
		String devkitpro = OS::get_singleton()->get_environment("DEVKITPRO");
		if (FileAccess::exists(bundled)) {
			nxlink = bundled;
		} else if (!devkitpro.is_empty() && FileAccess::exists(devkitpro.path_join("tools/bin/nxlink" + exe_ext))) {
			nxlink = devkitpro.path_join("tools/bin/nxlink" + exe_ext);
		}
	}
	if (nxlink.is_empty() || !FileAccess::exists(nxlink)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), TTR("nxlink not found. Set its path in Editor Settings > Export > Switch > nxlink."));
		return ERR_FILE_NOT_FOUND;
	}

	EditorProgress ep("run", TTR("Running on Switch"), 2);

	if (ep.step(TTR("Exporting project..."), 0)) {
		return ERR_SKIP;
	}

	String tmp_nro = EditorPaths::get_singleton()->get_temp_dir().path_join("switch_run.nro");
	Error err;
	{
		// nxlink sends a single file, so always embed the PCK here.
		ExportNotifier notifier(*this, p_preset, true, tmp_nro, p_debug_flags);
		err = _export_nro(p_preset, true, tmp_nro, true);
	}
	if (err != OK) {
		DirAccess::remove_file_or_error(tmp_nro);
		return err;
	}

	if (ep.step(TTR("Sending to the console..."), 1)) {
		DirAccess::remove_file_or_error(tmp_nro);
		return ERR_SKIP;
	}

	// The console can't reach "localhost": point the remote debugger at this machine's LAN address.
	Vector<String> flags = gen_export_flags(p_debug_flags);
	String remote_host = EDITOR_GET("network/debug/remote_host");
	if (remote_host == "127.0.0.1" || remote_host == "localhost") {
		List<IPAddress> addresses;
		IP::get_singleton()->get_local_addresses(&addresses);
		for (const IPAddress &address : addresses) {
			String addr = String(address);
			if (address.is_ipv4() && !addr.begins_with("127.")) {
				for (int i = 0; i < flags.size(); i++) {
					flags.write[i] = flags[i].replace("127.0.0.1", addr).replace("localhost", addr);
				}
				break;
			}
		}
	}

	List<String> args;
	args.push_back("-a");
	args.push_back(device);
	args.push_back("-p");
	args.push_back("godot/" + String(get_project_setting(p_preset, "application/config/name")).validate_filename() + ".nro");
	args.push_back(tmp_nro);
	for (const String &flag : flags) {
		args.push_back(flag);
	}

	String output;
	int exit_code = 0;
	err = OS::get_singleton()->execute(nxlink, args, &output, &exit_code, true);
	DirAccess::remove_file_or_error(tmp_nro);

	if (err != OK || exit_code != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), TTR("nxlink failed:") + "\n" + output);
		return err != OK ? err : FAILED;
	}
	print_line(output);
	return OK;
}

void EditorExportPlatformSwitch::initialize() {
	if (EditorNode::get_singleton()) {
		Ref<Image> img = memnew(Image);
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);

		ImageLoaderSVG::create_image_from_string(img, _switch_logo_svg, EDSCALE, upsample, false);
		logo = ImageTexture::create_from_image(img);

		ImageLoaderSVG::create_image_from_string(img, _switch_run_icon_svg, EDSCALE, upsample, false);
		run_icon = ImageTexture::create_from_image(img);

		devices_changed.set();
		quit_request.clear();
		device_thread.start(_device_poll_thread, this);
	}
}

EditorExportPlatformSwitch::~EditorExportPlatformSwitch() {
	quit_request.set();
	if (device_thread.is_started()) {
		device_thread.wait_to_finish();
	}
}
