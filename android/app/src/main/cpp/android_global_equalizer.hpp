/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "parametric_equalizer.hpp"

#include <jni.h>

namespace ee::mobile::android_global_equalizer {

auto enable(JNIEnv* env, const ParametricEqualizer& equalizer, bool enabled) -> int;
void update(JNIEnv* env, const ParametricEqualizer& equalizer, bool enabled);
void disable(JNIEnv* env, const ParametricEqualizer* equalizer) noexcept;
auto enable_session(JNIEnv* env, const ParametricEqualizer& equalizer, int session_id) -> int;
void update_sessions(JNIEnv* env, const ParametricEqualizer& equalizer);
void disable_sessions(JNIEnv* env, const ParametricEqualizer* equalizer) noexcept;
void disable_session(JNIEnv* env, const ParametricEqualizer* equalizer, int session_id) noexcept;
[[nodiscard]] auto active_session_count(const ParametricEqualizer* equalizer) noexcept -> int;
[[nodiscard]] auto is_enabled(JNIEnv* env, const ParametricEqualizer* equalizer) noexcept -> bool;
[[nodiscard]] auto is_effect_active(JNIEnv* env, const ParametricEqualizer* equalizer) noexcept -> bool;
[[nodiscard]] auto active_band_count(const ParametricEqualizer* equalizer) noexcept -> int;
auto configured_band_gain_db(JNIEnv* env, const ParametricEqualizer* equalizer, int channel, int band) -> float;
void configure_dynamics_stage(JNIEnv* env,
                              const ParametricEqualizer* equalizer,
                              int stage,
                              bool enabled,
                              float threshold_db,
                              float ratio);

}  // namespace ee::mobile::android_global_equalizer
