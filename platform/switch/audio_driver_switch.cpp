/**************************************************************************/
/*  audio_driver_switch.cpp                                               */
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

#include "audio_driver_switch.h"

#include "core/config/engine.h"
#include "core/os/os.h"

#include <malloc.h>

static const AudioRendererConfig audren_config = {
	.output_rate = AudioRendererOutputRate_48kHz,
	.num_voices = 24,
	.num_effects = 0,
	.num_sinks = 1,
	.num_mix_objs = 1,
	.num_mix_buffers = 2,
};

Error AudioDriverSwitch::init_device() {
	mix_rate = _get_configured_mix_rate();
	channels = 2;

	int latency = Engine::get_singleton()->get_audio_output_latency();
	buffer_frames = Math::closest_power_of_2(latency * mix_rate / 1000);
	samples_in.resize(buffer_frames * channels);
	samples_out.resize(buffer_frames * channels);

	Result rc = audrenInitialize(&audren_config);
	ERR_FAIL_COND_V_MSG(R_FAILED(rc), ERR_CANT_OPEN, vformat("audrenInitialize failed (0x%x).", rc));

	rc = audrvCreate(&audren_driver, &audren_config, channels);
	if (R_FAILED(rc)) {
		audrenExit();
		ERR_FAIL_V_MSG(ERR_CANT_OPEN, vformat("audrvCreate failed (0x%x).", rc));
	}
	audren_initialized = true;

	audren_buffer_size = sizeof(int16_t) * buffer_frames * channels;
	audren_pool_size = ((audren_buffer_size * BUFFER_COUNT) + 0xFFF) & ~0xFFF;
	audren_pool_ptr = memalign(0x1000, audren_pool_size);
	ERR_FAIL_NULL_V(audren_pool_ptr, ERR_OUT_OF_MEMORY);
	memset(audren_pool_ptr, 0, audren_pool_size);

	for (int i = 0; i < BUFFER_COUNT; i++) {
		audren_buffers[i] = {};
		audren_buffers[i].data_raw = audren_pool_ptr;
		audren_buffers[i].size = audren_pool_size;
		audren_buffers[i].start_sample_offset = i * buffer_frames;
		audren_buffers[i].end_sample_offset = audren_buffers[i].start_sample_offset + buffer_frames;
	}

	int mpid = audrvMemPoolAdd(&audren_driver, audren_pool_ptr, audren_pool_size);
	audrvMemPoolAttach(&audren_driver, mpid);

	static const u8 sink_channels[] = { 0, 1 };
	audrvDeviceSinkAdd(&audren_driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

	audrvUpdate(&audren_driver);
	rc = audrenStartAudioRenderer();
	ERR_FAIL_COND_V_MSG(R_FAILED(rc), ERR_CANT_OPEN, vformat("audrenStartAudioRenderer failed (0x%x).", rc));

	// The renderer resamples the voice to its 48 kHz output, so any mix rate works.
	audrvVoiceInit(&audren_driver, 0, channels, PcmFormat_Int16, mix_rate);
	audrvVoiceSetDestinationMix(&audren_driver, 0, AUDREN_FINAL_MIX_ID);
	audrvVoiceSetMixFactor(&audren_driver, 0, 1.0f, 0, 0);
	audrvVoiceSetMixFactor(&audren_driver, 0, 0.0f, 0, 1);
	audrvVoiceSetMixFactor(&audren_driver, 0, 0.0f, 1, 0);
	audrvVoiceSetMixFactor(&audren_driver, 0, 1.0f, 1, 1);

	return OK;
}

void AudioDriverSwitch::finish_device() {
	if (audren_initialized) {
		audrvClose(&audren_driver);
		audrenExit();
		audren_initialized = false;
	}
	if (audren_pool_ptr) {
		free(audren_pool_ptr);
		audren_pool_ptr = nullptr;
	}
}

Error AudioDriverSwitch::init() {
	active.clear();
	exit_thread.clear();

	Error err = init_device();
	if (err != OK) {
		finish_device();
		return err;
	}

	thread.start(AudioDriverSwitch::thread_func, this);
	return OK;
}

void AudioDriverSwitch::thread_func(void *p_udata) {
	AudioDriverSwitch *ad = static_cast<AudioDriverSwitch *>(p_udata);

	// Run above the main thread (0x2C) so audio doesn't starve when the game is busy.
	svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2B);

	while (!ad->exit_thread.is_set()) {
		ad->lock();
		ad->start_counting_ticks();

		const unsigned int sample_count = ad->buffer_frames * ad->channels;
		int16_t *out = ad->samples_out.ptrw();
		if (!ad->active.is_set()) {
			memset(out, 0, sample_count * sizeof(int16_t));
		} else {
			ad->audio_server_process(ad->buffer_frames, ad->samples_in.ptrw());
			const int32_t *in = ad->samples_in.ptr();
			for (unsigned int i = 0; i < sample_count; i++) {
				out[i] = in[i] >> 16;
			}
		}

		int free_buffer = -1;
		for (int i = 0; i < BUFFER_COUNT; i++) {
			if (ad->audren_buffers[i].state == AudioDriverWaveBufState_Free || ad->audren_buffers[i].state == AudioDriverWaveBufState_Done) {
				free_buffer = i;
				break;
			}
		}

		if (free_buffer >= 0) {
			uint8_t *ptr = (uint8_t *)ad->audren_pool_ptr + (free_buffer * ad->audren_buffer_size);
			memcpy(ptr, out, ad->audren_buffer_size);
			armDCacheFlush(ptr, ad->audren_buffer_size);
			audrvVoiceAddWaveBuf(&ad->audren_driver, 0, &ad->audren_buffers[free_buffer]);
			if (!audrvVoiceIsPlaying(&ad->audren_driver, 0)) {
				audrvVoiceStart(&ad->audren_driver, 0);
			}
		}
		audrvUpdate(&ad->audren_driver);

		ad->stop_counting_ticks();
		ad->unlock();

		// Wait for the renderer to consume a frame (5 ms) before mixing more.
		while (!ad->exit_thread.is_set()) {
			bool any_free = false;
			for (int i = 0; i < BUFFER_COUNT; i++) {
				if (ad->audren_buffers[i].state == AudioDriverWaveBufState_Free || ad->audren_buffers[i].state == AudioDriverWaveBufState_Done) {
					any_free = true;
					break;
				}
			}
			if (any_free) {
				break;
			}
			audrenWaitFrame();
			ad->lock();
			audrvUpdate(&ad->audren_driver);
			ad->unlock();
		}
	}
}

void AudioDriverSwitch::start() {
	active.set();
}

int AudioDriverSwitch::get_mix_rate() const {
	return mix_rate;
}

AudioDriver::SpeakerMode AudioDriverSwitch::get_speaker_mode() const {
	return SPEAKER_MODE_STEREO;
}

float AudioDriverSwitch::get_latency() {
	return mix_rate ? (float)(buffer_frames * BUFFER_COUNT) / mix_rate : 0.0f;
}

void AudioDriverSwitch::lock() {
	mutex.lock();
}

void AudioDriverSwitch::unlock() {
	mutex.unlock();
}

void AudioDriverSwitch::finish() {
	exit_thread.set();
	if (thread.is_started()) {
		thread.wait_to_finish();
	}
	finish_device();
}
