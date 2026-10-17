/**************************************************************************/
/*  joypad_switch.cpp                                                     */
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

#include "joypad_switch.h"

static const u64 pad_masks[] = {
	(1ull << HidNpadIdType_No1) | (1ull << HidNpadIdType_Handheld),
	(1ull << HidNpadIdType_No2),
	(1ull << HidNpadIdType_No3),
	(1ull << HidNpadIdType_No4),
	(1ull << HidNpadIdType_No5),
	(1ull << HidNpadIdType_No6),
	(1ull << HidNpadIdType_No7),
	(1ull << HidNpadIdType_No8),
};

struct ButtonMapping {
	u64 npad_button;
	JoyButton godot_button;
};

// Godot uses the SDL layout: JoyButton::A is the bottom face button, which is B on Nintendo controllers.
static const ButtonMapping button_mapping[] = {
	{ HidNpadButton_B, JoyButton::A },
	{ HidNpadButton_A, JoyButton::B },
	{ HidNpadButton_Y, JoyButton::X },
	{ HidNpadButton_X, JoyButton::Y },
	{ HidNpadButton_Minus, JoyButton::BACK },
	{ HidNpadButton_Plus, JoyButton::START },
	{ HidNpadButton_StickL, JoyButton::LEFT_STICK },
	{ HidNpadButton_StickR, JoyButton::RIGHT_STICK },
	{ HidNpadButton_L, JoyButton::LEFT_SHOULDER },
	{ HidNpadButton_R, JoyButton::RIGHT_SHOULDER },
	{ HidNpadButton_Up, JoyButton::DPAD_UP },
	{ HidNpadButton_Down, JoyButton::DPAD_DOWN },
	{ HidNpadButton_Left, JoyButton::DPAD_LEFT },
	{ HidNpadButton_Right, JoyButton::DPAD_RIGHT },
	// SL/SR on a single sideways Joy-Con.
	{ HidNpadButton_AnySL, JoyButton::PADDLE1 },
	{ HidNpadButton_AnySR, JoyButton::PADDLE2 },
};

JoypadSwitch::JoypadSwitch(Input *p_input) {
	input = p_input;

	padConfigureInput(JOYPADS_MAX, HidNpadStyleSet_NpadStandard);
	for (int i = 0; i < JOYPADS_MAX; i++) {
		padInitializeWithMask(&joypads[i].pad, pad_masks[i]);
	}
}

String JoypadSwitch::_get_pad_name(u32 p_style_set) {
	if (p_style_set & HidNpadStyleTag_NpadFullKey) {
		return "Nintendo Switch Pro Controller";
	}
	if (p_style_set & HidNpadStyleTag_NpadHandheld) {
		return "Nintendo Switch Joy-Con (Handheld)";
	}
	if (p_style_set & HidNpadStyleTag_NpadJoyDual) {
		return "Nintendo Switch Joy-Con (Dual)";
	}
	if (p_style_set & HidNpadStyleTag_NpadJoyLeft) {
		return "Nintendo Switch Joy-Con (L)";
	}
	if (p_style_set & HidNpadStyleTag_NpadJoyRight) {
		return "Nintendo Switch Joy-Con (R)";
	}
	return "Nintendo Switch Controller";
}

void JoypadSwitch::_update_connection(int p_index) {
	Joypad &joy = joypads[p_index];
	bool connected = padIsConnected(&joy.pad);
	u32 style_set = padGetStyleSet(&joy.pad);

	if (connected == joy.connected && style_set == joy.style_set) {
		return;
	}

	if (joy.connected) {
		input->joy_connection_changed(p_index, false, "");
	}

	joy.connected = connected;
	joy.style_set = style_set;
	joy.last_buttons = 0;

	if (connected) {
		// A GUID without a gamecontrollerdb entry makes Input use our button indices as-is.
		input->joy_connection_changed(p_index, true, _get_pad_name(style_set), "__SWITCH_NPAD__");
	}
}

void JoypadSwitch::process() {
	for (int index = 0; index < JOYPADS_MAX; index++) {
		Joypad &joy = joypads[index];
		padUpdate(&joy.pad);
		_update_connection(index);

		if (!joy.connected) {
			continue;
		}

		u64 buttons = padGetButtons(&joy.pad);
		u64 changed = buttons ^ joy.last_buttons;
		if (changed) {
			for (const ButtonMapping &mapping : button_mapping) {
				if (changed & mapping.npad_button) {
					input->joy_button(index, mapping.godot_button, buttons & mapping.npad_button);
				}
			}
			// ZL/ZR are digital on Switch controllers, report them as full-range triggers.
			if (changed & HidNpadButton_ZL) {
				input->joy_axis(index, JoyAxis::TRIGGER_LEFT, (buttons & HidNpadButton_ZL) ? 1.0f : 0.0f);
			}
			if (changed & HidNpadButton_ZR) {
				input->joy_axis(index, JoyAxis::TRIGGER_RIGHT, (buttons & HidNpadButton_ZR) ? 1.0f : 0.0f);
			}
			joy.last_buttons = buttons;
		}

		for (int stick = 0; stick < 2; stick++) {
			HidAnalogStickState pos = padGetStickPos(&joy.pad, stick);
			if (pos.x == joy.last_sticks[stick].x && pos.y == joy.last_sticks[stick].y) {
				continue;
			}
			joy.last_sticks[stick] = pos;
			JoyAxis axis_x = stick == 0 ? JoyAxis::LEFT_X : JoyAxis::RIGHT_X;
			JoyAxis axis_y = stick == 0 ? JoyAxis::LEFT_Y : JoyAxis::RIGHT_Y;
			// HID reports Y up as positive, Godot expects down to be positive.
			input->joy_axis(index, axis_x, CLAMP(pos.x / (float)JOYSTICK_MAX, -1.0f, 1.0f));
			input->joy_axis(index, axis_y, CLAMP(-pos.y / (float)JOYSTICK_MAX, -1.0f, 1.0f));
		}
	}
}
