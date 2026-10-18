/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "android_autogain_preview.hpp"
#include "android_bass_enhancer_preview.hpp"
#include "android_global_equalizer.hpp"
#include "android_platform_effects_backend.hpp"
#include "android_pcm_preview.hpp"
#include "autogain_processor.hpp"
#include "bass_enhancer_processor.hpp"
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

auto autogain_engine_from(const jlong handle) noexcept -> ee::mobile::AutogainPreviewEngine* {
  return reinterpret_cast<ee::mobile::AutogainPreviewEngine*>(static_cast<std::uintptr_t>(handle));
}

auto bass_enhancer_engine_from(const jlong handle) noexcept -> ee::mobile::BassEnhancerPreviewEngine* {
  return reinterpret_cast<ee::mobile::BassEnhancerPreviewEngine*>(static_cast<std::uintptr_t>(handle));
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

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeCreate(JNIEnv*, jobject) {
  auto engine = std::make_unique<ee::mobile::AutogainPreviewEngine>();
  return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(engine.release()));
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeDestroy(JNIEnv*, jobject, const jlong handle) {
  auto* const engine = autogain_engine_from(handle);
  if (engine != nullptr) {
    engine->stop();
    delete engine;
  }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeStart(JNIEnv*,
                                                                   jobject,
                                                                   const jlong handle,
                                                                   const jfloat desired_sample_rate) {
  auto* const engine = autogain_engine_from(handle);
  if (engine == nullptr) {
    return -1000;
  }
  return static_cast<jint>(engine->start(desired_sample_rate));
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeStop(JNIEnv*, jobject, const jlong handle) {
  if (auto* const engine = autogain_engine_from(handle); engine != nullptr) {
    engine->stop();
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeIsRunning(JNIEnv*, jobject, const jlong handle) {
  const auto* const engine = autogain_engine_from(handle);
  return engine != nullptr && engine->is_running() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeSetParameters(JNIEnv*,
                                                                           jobject,
                                                                           const jlong handle,
                                                                           const jint reference,
                                                                           const jfloat target_db,
                                                                           const jfloat silence_threshold_db,
                                                                           const jint maximum_history_seconds,
                                                                           const jboolean force_silence,
                                                                           const jfloat input_gain_db,
                                                                           const jfloat output_gain_db,
                                                                           const jboolean bypass) {
  auto* const engine = autogain_engine_from(handle);
  if (engine == nullptr) {
    return;
  }
  engine->set_parameters({.reference = static_cast<ee::mobile::AutogainReference>(reference),
                          .target_db = target_db,
                          .silence_threshold_db = silence_threshold_db,
                          .maximum_history_seconds = maximum_history_seconds,
                          .force_silence = force_silence == JNI_TRUE,
                          .input_gain_db = input_gain_db,
                          .output_gain_db = output_gain_db,
                          .bypass = bypass == JNI_TRUE});
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeResetHistory(JNIEnv*, jobject, const jlong handle) {
  if (auto* const engine = autogain_engine_from(handle); engine != nullptr) {
    engine->reset_history();
  }
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeLevels(JNIEnv* env, jobject, const jlong handle) {
  constexpr jsize level_count = 11;
  jfloatArray result = env->NewFloatArray(level_count);
  const auto* const engine = autogain_engine_from(handle);
  if (engine == nullptr || result == nullptr) {
    return result;
  }
  const auto levels = engine->levels();
  const jfloat values[level_count] = {levels.momentary_lufs, levels.shortterm_lufs, levels.integrated_lufs,
                                      levels.relative_lufs,  levels.range_lu,     levels.loudness_lufs,
                                      levels.output_gain_db, levels.input_left_db, levels.input_right_db,
                                      levels.output_left_db, levels.output_right_db};
  env->SetFloatArrayRegion(result, 0, level_count, values);
  return result;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeRenderedFrames(JNIEnv*, jobject, const jlong handle) {
  const auto* const engine = autogain_engine_from(handle);
  return engine == nullptr ? 0 : static_cast<jlong>(engine->rendered_frames());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_github_wwmm_easyeffects_NativeAutogainPreview_nativeRunAutogainPreviewTest(JNIEnv* env, jobject) {
  return ee::mobile::run_autogain_preview_test(env);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeCreate(JNIEnv*, jobject) {
  auto engine = std::make_unique<ee::mobile::BassEnhancerPreviewEngine>();
  return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(engine.release()));
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeDestroy(JNIEnv*, jobject, const jlong handle) {
  auto* const engine = bass_enhancer_engine_from(handle);
  if (engine != nullptr) {
    engine->stop();
    delete engine;
  }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeStart(JNIEnv*,
                                                                       jobject,
                                                                       const jlong handle,
                                                                       const jfloat desired_sample_rate) {
  auto* const engine = bass_enhancer_engine_from(handle);
  if (engine == nullptr) {
    return -1000;
  }
  return static_cast<jint>(engine->start(desired_sample_rate));
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeStop(JNIEnv*, jobject, const jlong handle) {
  if (auto* const engine = bass_enhancer_engine_from(handle); engine != nullptr) {
    engine->stop();
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeIsRunning(JNIEnv*, jobject, const jlong handle) {
  const auto* const engine = bass_enhancer_engine_from(handle);
  return engine != nullptr && engine->is_running() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeSetParameters(JNIEnv*,
                                                                               jobject,
                                                                               const jlong handle,
                                                                               const jfloat amount_db,
                                                                               const jfloat harmonics,
                                                                               const jfloat scope_hz,
                                                                               const jfloat floor_hz,
                                                                               const jfloat blend,
                                                                               const jboolean floor_active,
                                                                               const jboolean listen,
                                                                               const jfloat input_gain_db,
                                                                               const jfloat output_gain_db,
                                                                               const jboolean bypass) {
  auto* const engine = bass_enhancer_engine_from(handle);
  if (engine == nullptr) {
    return;
  }
  engine->set_parameters({.amount_db = amount_db,
                          .harmonics = harmonics,
                          .scope_hz = scope_hz,
                          .floor_hz = floor_hz,
                          .blend = blend,
                          .floor_active = floor_active == JNI_TRUE,
                          .listen = listen == JNI_TRUE,
                          .input_gain_db = input_gain_db,
                          .output_gain_db = output_gain_db,
                          .bypass = bypass == JNI_TRUE});
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeLevels(JNIEnv* env,
                                                                        jobject,
                                                                        const jlong handle) {
  constexpr jsize level_count = 5;
  jfloatArray result = env->NewFloatArray(level_count);
  const auto* const engine = bass_enhancer_engine_from(handle);
  if (engine == nullptr || result == nullptr) {
    return result;
  }
  const auto levels = engine->levels();
  const jfloat values[level_count] = {levels.input_left_db, levels.input_right_db, levels.output_left_db,
                                      levels.output_right_db, levels.harmonics_level_db};
  env->SetFloatArrayRegion(result, 0, level_count, values);
  return result;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeRenderedFrames(JNIEnv*,
                                                                                jobject,
                                                                                const jlong handle) {
  const auto* const engine = bass_enhancer_engine_from(handle);
  return engine == nullptr ? 0 : static_cast<jlong>(engine->rendered_frames());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_github_wwmm_easyeffects_NativeBassEnhancerPreview_nativeRunBassEnhancerPreviewTest(JNIEnv* env, jobject) {
  return ee::mobile::run_bass_enhancer_preview_test(env);
}

