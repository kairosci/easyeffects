/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "parametric_equalizer.hpp"

#include <jni.h>

namespace ee::mobile::android_platform_effects_backend {
void configure(JNIEnv* env, const ParametricEqualizer* owner, int effect, bool enabled, int value);
void release(JNIEnv* env, const ParametricEqualizer* owner) noexcept;
}  // namespace ee::mobile::android_platform_effects_backend
