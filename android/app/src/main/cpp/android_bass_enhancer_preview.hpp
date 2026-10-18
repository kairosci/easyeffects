/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "bass_enhancer_processor.hpp"

#include <atomic>
#include <cstdint>
#include <jni.h>

namespace ee::mobile {

/**
 * App-owned AAudio preview for the Bass Enhancer processor. It plays a generated
 * 60 Hz stereo tone through BassEnhancerProcessor on the real-time callback and
 * renders the result to the active Android output route; it never touches
 * another application's PCM.
 */
class BassEnhancerPreviewEngine final {
 public:
  BassEnhancerPreviewEngine() = default;
  ~BassEnhancerPreviewEngine();
  BassEnhancerPreviewEngine(const BassEnhancerPreviewEngine&) = delete;
  auto operator=(const BassEnhancerPreviewEngine&) -> BassEnhancerPreviewEngine& = delete;
  BassEnhancerPreviewEngine(BassEnhancerPreviewEngine&&) = delete;
  auto operator=(BassEnhancerPreviewEngine&&) -> BassEnhancerPreviewEngine& = delete;

  /** Opens and starts the preview stream. Returns 0 on success or a negative error code. */
  [[nodiscard]] auto start(float desired_sample_rate) noexcept -> int;
  void stop() noexcept;
  [[nodiscard]] auto is_running() const noexcept -> bool;

  void set_parameters(const BassEnhancerParameters& parameters) noexcept;
  [[nodiscard]] auto parameters() const noexcept -> BassEnhancerParameters;
  [[nodiscard]] auto levels() const noexcept -> BassEnhancerLevels;
  [[nodiscard]] auto rendered_frames() const noexcept -> std::uint64_t;

  /** Generates one preview-tone block and runs it through the processor. Public for the AAudio callback. */
  void process_block(float* interleaved_stereo, int32_t frames) noexcept;

 private:
  BassEnhancerProcessor processor_;
  void* stream_ = nullptr;
  double phase_ = 0.0;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> rendered_frames_{0U};
};

/** Debug-only device check: runs the preview and reports whether the DSP produced harmonics. */
auto run_bass_enhancer_preview_test(JNIEnv* env) -> jstring;

}  // namespace ee::mobile
