/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "android_pcm_preview.hpp"

#include "parametric_equalizer.hpp"

#include <aaudio/AAudio.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <thread>

namespace ee::mobile {
namespace {
constexpr char log_tag[] = "EasyEffectsPcmPreview";
constexpr float tone_frequency_hz = 1000.0F;
constexpr float tone_amplitude = 0.08F;
constexpr float test_boost_db = 12.0F;
constexpr int channel_count = 2;
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

struct Preview final {
  ParametricEqualizer equalizer;
  double phase = 0.0;
  std::atomic<float> input_energy{0.0F};
  std::atomic<float> output_energy{0.0F};
  std::atomic<std::uint32_t> frame_count{0U};

  Preview() {
    for (std::size_t band = 0; band < ParametricEqualizer::band_count; ++band) {
      equalizer.set_band(band, {.type = FilterType::off, .enabled = false});
    }
    equalizer.set_band(0U, {.type = FilterType::peaking,
                            .frequency_hz = tone_frequency_hz,
                            .q = 1.4F,
                            .gain_db = test_boost_db,
                            .enabled = true});
  }
};

auto on_audio_ready(AAudioStream* const stream,
                    void* user_data,
                    void* audio_data,
                    const int32_t frames) -> aaudio_data_callback_result_t {
  auto& preview = *static_cast<Preview*>(user_data);
  auto* const audio = static_cast<float*>(audio_data);
  const double sample_rate = static_cast<double>(AAudioStream_getSampleRate(stream));
  const double angular_step = 2.0 * std::numbers::pi * static_cast<double>(tone_frequency_hz) / sample_rate;
  double input_energy = 0.0;
  for (int32_t frame = 0; frame < frames; ++frame) {
    const float sample = tone_amplitude * static_cast<float>(std::sin(preview.phase));
    preview.phase += angular_step;
    if (preview.phase >= 2.0 * std::numbers::pi) preview.phase -= 2.0 * std::numbers::pi;
    audio[frame * channel_count] = sample;
    audio[frame * channel_count + 1] = sample;
    input_energy += static_cast<double>(sample) * sample * channel_count;
  }
  preview.equalizer.process(audio, static_cast<std::size_t>(frames), channel_count,
                           static_cast<float>(sample_rate));
  double output_energy = 0.0;
  for (int32_t sample = 0; sample < frames * channel_count; ++sample) {
    const double value = audio[sample];
    output_energy += value * value;
  }
  preview.input_energy.fetch_add(static_cast<float>(input_energy), std::memory_order_relaxed);
  preview.output_energy.fetch_add(static_cast<float>(output_energy), std::memory_order_relaxed);
  preview.frame_count.fetch_add(static_cast<std::uint32_t>(frames), std::memory_order_relaxed);
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
}  // namespace

auto run_pcm_playback_test(JNIEnv* const env) -> jstring {
  auto preview = std::make_unique<Preview>();
  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t result = AAudio_createStreamBuilder(&builder);
  if (result != AAUDIO_OK || builder == nullptr) {
    return env->NewStringUTF("FAIL: AAudio stream builder unavailable");
  }
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(builder, channel_count);
  AAudioStreamBuilder_setSampleRate(builder, 48000);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setDataCallback(builder, on_audio_ready, preview.get());
  AAudioStream* stream = nullptr;
  result = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if (result != AAUDIO_OK || stream == nullptr) {
    const std::string message = "FAIL: AAudio output stream open: " + std::string(AAudio_convertResultToText(result));
    return env->NewStringUTF(message.c_str());
  }

  const int32_t sample_rate = AAudioStream_getSampleRate(stream);
  const int32_t device_id = AAudioStream_getDeviceId(stream);
  if (sample_rate != 48000) {
    // The callback is deliberately validated against the rate at which its
    // generated tone and the DSP are configured.
    AAudioStream_close(stream);
    const std::string message = "FAIL: expected 48000 Hz output, got " + std::to_string(sample_rate);
    return env->NewStringUTF(message.c_str());
  }
  result = AAudioStream_requestStart(stream);
  if (result != AAUDIO_OK) {
    AAudioStream_close(stream);
    const std::string message = "FAIL: AAudio start: " + std::string(AAudio_convertResultToText(result));
    return env->NewStringUTF(message.c_str());
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(1600));
  result = AAudioStream_requestStop(stream);
  if (result != AAUDIO_OK) {
    AAudioStream_close(stream);
    const std::string message = "FAIL: AAudio stop request: " + std::string(AAudio_convertResultToText(result));
    return env->NewStringUTF(message.c_str());
  }

  auto stream_state = AAudioStream_getState(stream);
  const auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (stream_state != AAUDIO_STREAM_STATE_STOPPED && stream_state != AAUDIO_STREAM_STATE_DISCONNECTED) {
    const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
        drain_deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) break;
    aaudio_stream_state_t next_state = stream_state;
    result = AAudioStream_waitForStateChange(stream, stream_state, &next_state, remaining);
    if (result != AAUDIO_OK || next_state == stream_state) break;
    stream_state = next_state;
  }
  const bool output_drained = stream_state == AAUDIO_STREAM_STATE_STOPPED;
  const int64_t submitted_frames = AAudioStream_getFramesWritten(stream);
  AAudioStream_close(stream);

  const double input = preview->input_energy.load(std::memory_order_relaxed);
  const double output = preview->output_energy.load(std::memory_order_relaxed);
  const auto frames = preview->frame_count.load(std::memory_order_relaxed);
  if (!output_drained || submitted_frames < static_cast<int64_t>(sample_rate / 2)) {
    const std::string message = "FAIL: AAudio did not confirm playback drain; state=" +
                                std::to_string(static_cast<int>(stream_state)) + "; frames submitted=" +
                                std::to_string(submitted_frames);
    __android_log_print(ANDROID_LOG_ERROR, log_tag, "%s", message.c_str());
    return env->NewStringUTF(message.c_str());
  }
  if (frames < static_cast<std::uint32_t>(sample_rate / 2) || input <= 0.0 || output <= 0.0 ||
      !std::isfinite(input) || !std::isfinite(output)) {
    const std::string message = "FAIL: processed PCM was not rendered with finite energy; frames=" +
                                std::to_string(frames);
    __android_log_print(ANDROID_LOG_ERROR, log_tag, "%s", message.c_str());
    return env->NewStringUTF(message.c_str());
  }
  const double gain_db = 10.0 * std::log10(output / input);
  if (!std::isfinite(gain_db) || std::abs(gain_db - static_cast<double>(test_boost_db)) > 1.0) {
    const std::string message = "FAIL: expected +" + std::to_string(test_boost_db) +
                                " dB PCM energy gain within 1 dB; measured " + std::to_string(gain_db) +
                                " dB across " + std::to_string(frames) + " frames";
    __android_log_print(ANDROID_LOG_ERROR, log_tag, "%s", message.c_str());
    return env->NewStringUTF(message.c_str());
  }
  const std::string message = "PASS: AAudio drained " + std::to_string(submitted_frames) +
                              " output frames through C++ ParametricEqualizer on output device " +
                              std::to_string(device_id) + "; 1 kHz test tone, +12 dB band; " +
                              std::to_string(gain_db) + " dB measured PCM energy gain";
  __android_log_print(ANDROID_LOG_INFO, log_tag, "%s", message.c_str());
  return env->NewStringUTF(message.c_str());
}

}  // namespace ee::mobile
