/**************************************************************************/
/*  godot_switch.cpp                                                      */
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
#include "switch_wrapper.h"

#include "main/main.h"

#include <unistd.h>

#include <climits>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <zlib.h>

#define SPLASH_WIDTH 1280
#define SPLASH_HEIGHT 720

// Applets (album/hbmenu launched without title override) only get a tiny memory pool,
// not enough to run Godot. Show a splash asking the user to launch through a title instead.
static void show_applet_splash() {
	NWindow *win = nwindowGetDefault();
	Framebuffer fb;
	framebufferCreate(&fb, win, SPLASH_WIDTH, SPLASH_HEIGHT, PIXEL_FORMAT_RGBA_8888, 1);
	framebufferMakeLinear(&fb);

	u32 stride;
	u32 *framebuf = (u32 *)framebufferBegin(&fb, &stride);
	memset(framebuf, 0, stride * SPLASH_HEIGHT);

	FILE *splash = fopen("romfs:/applet_splash.rgba.gz", "rb");
	if (splash) {
		fseek(splash, 0, SEEK_END);
		size_t splash_size = ftell(splash);
		fseek(splash, 0, SEEK_SET);

		u8 *compressed_splash = (u8 *)malloc(splash_size);
		if (compressed_splash && fread(compressed_splash, 1, splash_size, splash) == splash_size) {
			z_stream stream = {};
			stream.next_in = compressed_splash;
			stream.avail_in = splash_size;
			stream.next_out = (u8 *)framebuf;
			stream.avail_out = stride * SPLASH_HEIGHT;

			if (inflateInit2(&stream, 16 + MAX_WBITS) == Z_OK) {
				inflate(&stream, Z_FINISH);
				inflateEnd(&stream);
			}
		}
		free(compressed_splash);
		fclose(splash);
	}

	framebufferEnd(&fb);

	PadState pad;
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	while (appletMainLoop()) {
		padUpdate(&pad);
		if (padGetButtonsDown(&pad) != 0) {
			break;
		}
		svcSleepThread(16000000ll);
	}

	framebufferClose(&fb);
}

int main(int argc, char *argv[]) {
	socketInitializeDefault();
#ifdef SWITCH_NXLINK_ENABLED
	int nxlink_sock = nxlinkStdio();
#endif
	romfsInit();

	AppletType applet_type = appletGetAppletType();
	if (applet_type != AppletType_Application && applet_type != AppletType_SystemApplication) {
		show_applet_splash();
		romfsExit();
		socketExit();
		return 0;
	}

	// Keep running (audio, network) while the HOME menu is shown, and get notified of focus changes.
	appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);

	OS_Switch os;
	os.set_executable_path(argc > 0 ? argv[0] : "sdmc:/switch/godot.nro");

	setlocale(LC_CTYPE, "");

	char *cwd = (char *)malloc(PATH_MAX);
	ERR_FAIL_NULL_V(cwd, ERR_OUT_OF_MEMORY);
	char *ret = getcwd(cwd, PATH_MAX);

	Error err = Main::setup(argc > 0 ? argv[0] : "godot", argc > 0 ? argc - 1 : 0, argc > 0 ? &argv[1] : nullptr);
	if (err != OK) {
		free(cwd);
		romfsExit();
		socketExit();
		if (err == ERR_HELP) {
			return EXIT_SUCCESS;
		}
		return EXIT_FAILURE;
	}

	if (Main::start() == EXIT_SUCCESS) {
		os.run();
	} else {
		os.set_exit_code(EXIT_FAILURE);
	}
	Main::cleanup();

	if (ret) {
		chdir(cwd);
	}
	free(cwd);

#ifdef SWITCH_NXLINK_ENABLED
	if (nxlink_sock >= 0) {
		close(nxlink_sock);
	}
#endif
	romfsExit();
	socketExit();
	return os.get_exit_code();
}
