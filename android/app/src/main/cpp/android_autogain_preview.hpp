/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "autogain_processor.hpp"

#include <atomic>
#include <cstdint>
#include <jni.h>

namespace ee::mobile {

/**
 * App-owned AAudio preview for the Autogain processor. It plays a generated
 * 997 Hz stereo tone through AutogainProcessor on the real-time callback and
 * renders the result to the active Android output route; it never touches
 * another application's PCM.
 */
class AutogainPreviewEngine final {
 public:
  AutogainPreviewEngine() = default;
  ~AutogainPreviewEngine();
  AutogainPreviewEngine(const AutogainPreviewEngine&) = delete;
  auto operator=(const AutogainPreviewEngine&) -> AutogainPreviewEngine& = delete;
  AutogainPreviewEngine(AutogainPreviewEngine&&) = delete;
  auto operator=(AutogainPreviewEngine&&) -> AutogainPreviewEngine& = delete;

  /** Opens and starts the preview stream. Returns 0 on success or a negative error code. */
  [[nodiscard]] auto start(float desired_sample_rate) noexcept -> int;
  void stop() noexcept;
  [[nodiscard]] auto is_running() const noexcept -> bool;

  void set_parameters(const AutogainParameters& parameters) noexcept;
  [[nodiscard]] auto parameters() const noexcept -> AutogainParameters;
  void reset_history() noexcept;
  [[nodiscard]] auto levels() const noexcept -> AutogainLevels;
  [[nodiscard]] auto rendered_frames() const noexcept -> std::uint64_t;

  /** Generates one preview-tone block and runs it through the processor. Public for the AAudio callback. */
  void process_block(float* interleaved_stereo, int32_t frames) noexcept;

 private:
  AutogainProcessor processor_;
  void* stream_ = nullptr;
  double phase_ = 0.0;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> rendered_frames_{0U};
};

/** Debug-only device check: runs the preview and reports whether it reached the target. */
auto run_autogain_preview_test(JNIEnv* env) -> jstring;

}  // namespace ee::mobile
