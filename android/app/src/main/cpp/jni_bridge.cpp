/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "android_global_equalizer.hpp"
#include "android_platform_effects_backend.hpp"
#include "android_pcm_preview.hpp"
#include "parametric_equalizer.hpp"

#include <jni.h>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>

namespace {
auto engine_from(const jlong handle) noexcept -> ee::mobile::ParametricEqualizer* {
  return reinterpret_cast<ee::mobile::ParametricEqualizer*>(static_cast<std::uintptr_t>(handle));
}

void throw_illegal_state(JNIEnv* env, const std::string& message) {
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  jclass exception_type = env->FindClass("java/lang/IllegalStateException");
  if (exception_type != nullptr) {
    env->ThrowNew(exception_type, message.c_str());
    env->DeleteLocalRef(exception_type);
  }
}
}  // namespace

extern "C" JNIEXPORT jlong JNICALL Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeCreate(JNIEnv*, jobject) {
  auto engine = std::make_unique<ee::mobile::ParametricEqualizer>();
  return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(engine.release()));
}

extern "C" JNIEXPORT void JNICALL Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeDestroy(JNIEnv* env,
                                                                                                 jobject,
                                                                                                 const jlong handle) {
  auto* const engine = engine_from(handle);
  if (engine != nullptr) {
    ee::mobile::android_global_equalizer::disable(env, engine);
    delete engine;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeSetBand(JNIEnv*,
                                                               jobject,
                                                               const jlong handle,
                                                               const jint index,
                                                               const jint type,
                                                               const jfloat frequency,
                                                               const jfloat q,
                                                               const jfloat gain,
                                                               const jboolean enabled,
                                                               const jint channel_mode) {
  auto* const engine = engine_from(handle);
  if (engine == nullptr || index < 0) {
    return;
  }
  engine->set_band(static_cast<std::size_t>(index), {.type = static_cast<ee::mobile::FilterType>(type),
                                                     .frequency_hz = frequency,
                                                     .q = q,
                                                     .gain_db = gain,
                                                     .enabled = enabled == JNI_TRUE,
                                                     .channel_mode = channel_mode});
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeSetBypass(JNIEnv*,
                                                                 jobject,
                                                                 const jlong handle,
                                                                 const jboolean bypass) {
  if (auto* const engine = engine_from(handle); engine != nullptr) {
    engine->set_bypass(bypass == JNI_TRUE);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeSetPreamp(JNIEnv*,
                                                                 jobject,
                                                                 const jlong handle,
                                                                 const jfloat gain_db) {
  if (auto* const engine = engine_from(handle); engine != nullptr) {
    engine->set_preamp(gain_db);
  }
}

extern "C" JNIEXPORT void JNICALL Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeReset(JNIEnv*,
                                                                                               jobject,
                                                                                               const jlong handle) {
  if (auto* const engine = engine_from(handle); engine != nullptr) {
    engine->reset();
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeProcessFloat(JNIEnv* env,
                                                                    jobject,
                                                                    const jlong handle,
                                                                    jobject buffer,
                                                                    const jint frames,
                                                                    const jint channels,
                                                                    const jint sample_rate) {
  auto* const engine = engine_from(handle);
  auto* const samples = static_cast<float*>(buffer == nullptr ? nullptr : env->GetDirectBufferAddress(buffer));
  const auto capacity = buffer == nullptr ? -1 : env->GetDirectBufferCapacity(buffer);
  const auto required = static_cast<jlong>(frames) * static_cast<jlong>(channels) * static_cast<jlong>(sizeof(float));
  if (engine == nullptr || samples == nullptr || frames < 0 || channels < 1 || channels > 8 || sample_rate < 1 ||
      required > capacity) {
    return;
  }
  engine->process(samples, static_cast<std::size_t>(frames), channels, static_cast<float>(sample_rate));
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeProcessPcm16(JNIEnv* env,
                                                                    jobject,
                                                                    const jlong handle,
                                                                    jobject buffer,
                                                                    const jint frames,
                                                                    const jint channels,
                                                                    const jint sample_rate) {
  auto* const engine = engine_from(handle);
  auto* const samples = static_cast<std::int16_t*>(buffer == nullptr ? nullptr : env->GetDirectBufferAddress(buffer));
  const auto capacity = buffer == nullptr ? -1 : env->GetDirectBufferCapacity(buffer);
  const auto required =
      static_cast<jlong>(frames) * static_cast<jlong>(channels) * static_cast<jlong>(sizeof(std::int16_t));
  if (engine == nullptr || samples == nullptr || frames < 0 || channels < 1 || channels > 8 || sample_rate < 1 ||
      required > capacity) {
    return;
  }
  engine->process(samples, static_cast<std::size_t>(frames), channels, static_cast<float>(sample_rate));
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeResponseDb(JNIEnv*,
                                                                  jobject,
                                                                  const jlong handle,
                                                                  const jfloat frequency,
                                                                  const jfloat sample_rate,
                                                                  const jint channel) {
  const auto* const engine = engine_from(handle);
  return engine == nullptr ? 0.0F : engine->response_db(frequency, sample_rate, channel);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeResponseCurve(JNIEnv* env,
                                                                     jobject,
                                                                     const jlong handle,
                                                                     const jint count,
                                                                     const jfloat sample_rate,
                                                                     const jint channel) {
  const auto* const engine = engine_from(handle);
  if (engine == nullptr || count < 1 || count > 256) {
    return nullptr;
  }
  std::array<float, 256U> values{};
  engine->response_curve(values.data(), static_cast<std::size_t>(count), sample_rate, channel);
  jfloatArray result = env->NewFloatArray(count);
  if (result != nullptr) {
    env->SetFloatArrayRegion(result, 0, count, values.data());
  }
  return result;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeGraphFrequencyFromX(JNIEnv*, jobject, const jfloat normalized_x) {
  return ee::mobile::graph_frequency_from_x(normalized_x);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeGraphGainForResponse(JNIEnv*,
                                                                           jobject,
                                                                           const jfloat current_band_gain_db,
                                                                           const jfloat current_total_response_db,
                                                                           const jfloat target_total_response_db) {
  return ee::mobile::graph_gain_for_response(
      current_band_gain_db, current_total_response_db, target_total_response_db);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeEnableGlobalOutputEqualizer(JNIEnv* env,
                                                                                    jobject,
                                                                                    const jlong handle,
                                                                                    const jboolean enabled) {
  auto* const engine = engine_from(handle);
  if (engine == nullptr) {
    throw_illegal_state(env, "Native equalizer is not initialized");
    return 0;
  }
  try {
    return ee::mobile::android_global_equalizer::enable(env, *engine, enabled == JNI_TRUE);
  } catch (const std::exception& failure) {
    throw_illegal_state(env, failure.what());
    return 0;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeUpdateGlobalOutputEqualizer(JNIEnv* env,
                                                                                    jobject,
                                                                                    const jlong handle,
                                                                                    const jboolean enabled) {
  auto* const engine = engine_from(handle);
  if (engine == nullptr) {
    return;
  }
  try {
    ee::mobile::android_global_equalizer::update(env, *engine, enabled == JNI_TRUE);
  } catch (const std::exception& failure) {
    throw_illegal_state(env, failure.what());
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeDisableGlobalOutputEqualizer(JNIEnv* env,
                                                                                     jobject,
                                                                                     const jlong handle) {
  if (auto* const engine = engine_from(handle); engine != nullptr) {
    ee::mobile::android_global_equalizer::disable(env, engine);
  }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeEnableSessionEqualizer(JNIEnv* env, jobject, const jlong handle, const jint session_id) {
  auto* const engine = engine_from(handle);
  if (engine == nullptr) return 0;
  try {
    return ee::mobile::android_global_equalizer::enable_session(env, *engine, session_id);
  } catch (const std::exception& failure) {
    throw_illegal_state(env, failure.what());
    return 0;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeDisableSessionEqualizer(JNIEnv* env, jobject, const jlong handle, const jint session_id) {
  if (auto* const engine = engine_from(handle); engine != nullptr) {
    ee::mobile::android_global_equalizer::disable_session(env, engine, session_id);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeDisableSessionEqualizers(JNIEnv* env, jobject, const jlong handle) {
  if (auto* const engine = engine_from(handle); engine != nullptr) {
    ee::mobile::android_global_equalizer::disable_sessions(env, engine);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeUpdateSessionEqualizers(JNIEnv* env, jobject, const jlong handle) {
  if (auto* const engine = engine_from(handle); engine != nullptr) {
    try {
      ee::mobile::android_global_equalizer::update_sessions(env, *engine);
    } catch (const std::exception& failure) {
      throw_illegal_state(env, failure.what());
    }
  }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeActiveSessionEqualizerCount(JNIEnv*, jobject, const jlong handle) {
  return ee::mobile::android_global_equalizer::active_session_count(engine_from(handle));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeIsGlobalOutputEqualizerEnabled(JNIEnv* env,
                                                                                       jobject,
                                                                                       const jlong handle) {
  return ee::mobile::android_global_equalizer::is_enabled(env, engine_from(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeGlobalOutputEqualizerBandCount(JNIEnv*,
                                                                                       jobject,
                                                                                       const jlong handle) {
  return ee::mobile::android_global_equalizer::active_band_count(engine_from(handle));
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeGlobalOutputEqualizerBandGainDb(JNIEnv* env,
                                                                                         jobject,
                                                                                         const jlong handle,
                                                                                         const jint channel,
                                                                                         const jint band) {
  const auto* const engine = engine_from(handle);
  if (engine == nullptr) {
    throw_illegal_state(env, "Native equalizer is not initialized");
    return 0.0F;
  }
  try {
    return ee::mobile::android_global_equalizer::configured_band_gain_db(env, engine, channel, band);
  } catch (const std::exception& failure) {
    throw_illegal_state(env, failure.what());
    return 0.0F;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeConfigureAndroidPlatformEffect(JNIEnv* env,
                                                                                      jobject,
                                                                                      const jlong handle,
                                                                                      const jint effect,
                                                                                      const jboolean enabled,
                                                                                      const jint value) {
  const auto* const engine = engine_from(handle);
  if (engine == nullptr) {
    throw_illegal_state(env, "Native equalizer is not initialized");
    return;
  }
  try {
    ee::mobile::android_platform_effects_backend::configure(
        env, engine, effect, enabled == JNI_TRUE, value);
  } catch (const std::exception& failure) {
    throw_illegal_state(env, failure.what());
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeConfigureDynamicsStage(JNIEnv* env,
                                                                               jobject,
                                                                               const jlong handle,
                                                                               const jint stage,
                                                                               const jboolean enabled,
                                                                               const jfloat threshold_db,
                                                                               const jfloat ratio) {
  const auto* const engine = engine_from(handle);
  if (engine == nullptr) {
    throw_illegal_state(env, "Native equalizer is not initialized");
    return;
  }
  try {
    ee::mobile::android_global_equalizer::configure_dynamics_stage(
        env, engine, stage, enabled == JNI_TRUE, threshold_db, ratio);
  } catch (const std::exception& failure) {
    throw_illegal_state(env, failure.what());
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_github_wwmm_easyeffects_NativeEqualizer_nativeRunPcmPlaybackTest(JNIEnv* env, jobject) {
  return ee::mobile::run_pcm_playback_test(env);
}
