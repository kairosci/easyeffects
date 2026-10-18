/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "android_bass_enhancer_preview.hpp"

#include <aaudio/AAudio.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <thread>

namespace ee::mobile {
namespace {
constexpr char log_tag[] = "EasyEffectsBassEnhancerPreview";
constexpr float tone_frequency_hz = 60.0F;
constexpr float tone_amplitude = 0.05F;
constexpr int channel_count = 2;
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

auto on_audio_ready(AAudioStream* /*stream*/,
                    void* user_data,
                    void* audio_data,
                    const int32_t frames) -> aaudio_data_callback_result_t {
  if (auto* const engine = static_cast<BassEnhancerPreviewEngine*>(user_data); engine != nullptr) {
    engine->process_block(static_cast<float*>(audio_data), frames);
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
}  // namespace

BassEnhancerPreviewEngine::~BassEnhancerPreviewEngine() { stop(); }

auto BassEnhancerPreviewEngine::start(const float desired_sample_rate) noexcept -> int {
  if (stream_ != nullptr) {
    return 0;
  }
  const int requested_rate =
      desired_sample_rate > 0.0F ? static_cast<int>(std::lround(desired_sample_rate)) : 48000;

  AAudioStreamBuilder* builder = nullptr;
  if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
    return -1;
  }
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(builder, channel_count);
  AAudioStreamBuilder_setSampleRate(builder, requested_rate);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setDataCallback(builder, on_audio_ready, this);

  AAudioStream* stream = nullptr;
  const aaudio_result_t open_result = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if (open_result != AAUDIO_OK || stream == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, log_tag, "AAudio output stream open failed: %s",
                        AAudio_convertResultToText(open_result));
    return -2;
  }

  const auto actual_rate = static_cast<float>(AAudioStream_getSampleRate(stream));
  if (!processor_.prepare(actual_rate)) {
    AAudioStream_close(stream);
    return -3;
  }

  phase_ = 0.0;
  rendered_frames_.store(0U, std::memory_order_relaxed);

  const aaudio_result_t start_result = AAudioStream_requestStart(stream);
  if (start_result != AAUDIO_OK) {
    __android_log_print(ANDROID_LOG_ERROR, log_tag, "AAudio start failed: %s",
                        AAudio_convertResultToText(start_result));
    AAudioStream_close(stream);
    return -4;
  }

  stream_ = stream;
  running_.store(true, std::memory_order_relaxed);
  return 0;
}

void BassEnhancerPreviewEngine::stop() noexcept {
  running_.store(false, std::memory_order_relaxed);
  if (stream_ == nullptr) {
    return;
  }
  auto* const stream = static_cast<AAudioStream*>(stream_);
  AAudioStream_requestStop(stream);
  AAudioStream_close(stream);
  stream_ = nullptr;
}

auto BassEnhancerPreviewEngine::is_running() const noexcept -> bool { return running_.load(std::memory_order_relaxed); }

void BassEnhancerPreviewEngine::process_block(float* const audio, const int32_t frames) noexcept {
  if (audio == nullptr || frames <= 0) {
    return;
  }
  const float rate = processor_.sample_rate();
  if (!(rate > 0.0F) || !std::isfinite(rate)) {
    return;
  }

  const double angular_step =
      2.0 * std::numbers::pi * static_cast<double>(tone_frequency_hz) / static_cast<double>(rate);
  const double two_pi = 2.0 * std::numbers::pi;
  for (int32_t frame = 0; frame < frames; ++frame) {
    const float sample = tone_amplitude * static_cast<float>(std::sin(phase_));
    phase_ += angular_step;
    if (phase_ >= two_pi) {
      phase_ -= two_pi;
    }
    audio[frame * channel_count] = sample;
    audio[frame * channel_count + 1] = sample;
  }

  processor_.process(audio, static_cast<std::size_t>(frames));
  rendered_frames_.fetch_add(static_cast<std::uint64_t>(frames), std::memory_order_relaxed);
}

void BassEnhancerPreviewEngine::set_parameters(const BassEnhancerParameters& parameters) noexcept {
  processor_.set_parameters(parameters);
}

auto BassEnhancerPreviewEngine::parameters() const noexcept -> BassEnhancerParameters {
  return processor_.parameters();
}

auto BassEnhancerPreviewEngine::levels() const noexcept -> BassEnhancerLevels { return processor_.levels(); }

auto BassEnhancerPreviewEngine::rendered_frames() const noexcept -> std::uint64_t {
  return rendered_frames_.load(std::memory_order_relaxed);
}

auto run_bass_enhancer_preview_test(JNIEnv* const env) -> jstring {
  BassEnhancerPreviewEngine engine;
  const BassEnhancerParameters parameters;
  engine.set_parameters(parameters);

  const int start_result = engine.start(48000.0F);
  if (start_result != 0) {
    const std::string message =
        "FAIL: AAudio bass enhancer preview did not start; code " + std::to_string(start_result);
    __android_log_print(ANDROID_LOG_ERROR, log_tag, "%s", message.c_str());
    return env->NewStringUTF(message.c_str());
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  float harmonics = -100.0F;
  float output_peak = -100.0F;
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto levels = engine.levels();
    harmonics = levels.harmonics_level_db;
    output_peak = std::max(levels.output_left_db, levels.output_right_db);
    if (harmonics > -100.0F && output_peak > -60.0F) {
      break;
    }
  }
  const auto frames = engine.rendered_frames();
  engine.stop();

  if (frames < 48000ULL) {
    const std::string message =
        "FAIL: AAudio bass enhancer preview rendered too few frames: " + std::to_string(frames);
    __android_log_print(ANDROID_LOG_ERROR, log_tag, "%s", message.c_str());
    return env->NewStringUTF(message.c_str());
  }
  if (!(harmonics > -100.0F) || !(output_peak > -60.0F)) {
    const std::string message = "FAIL: bass enhancer preview produced no harmonics; harmonics " +
                                std::to_string(harmonics) + " dB, output peak " +
                                std::to_string(output_peak) + " dB";
    __android_log_print(ANDROID_LOG_ERROR, log_tag, "%s", message.c_str());
    return env->NewStringUTF(message.c_str());
  }
  const std::string message = "PASS: AAudio bass enhancer preview rendered " + std::to_string(frames) +
                              " frames of a 60 Hz tone through C++ BassEnhancerProcessor; harmonics " +
                              std::to_string(harmonics) + " dB, output peak " + std::to_string(output_peak) + " dB";
  __android_log_print(ANDROID_LOG_INFO, log_tag, "%s", message.c_str());
  return env->NewStringUTF(message.c_str());
}

}  // namespace ee::mobile
