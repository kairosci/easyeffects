/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "android_global_equalizer.hpp"
#include "android_platform_effects.hpp"
#include "android_platform_effects_backend.hpp"

#include <android/log.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ee::mobile::android_global_equalizer {
namespace {
constexpr int global_session_id = 0;
constexpr int effect_priority = 100;
constexpr int dynamics_band_count = 32;
constexpr float minimum_frequency_hz = 20.0F;
constexpr float maximum_frequency_hz = 20000.0F;

enum class BackendKind { equalizer, dynamics_processing };

struct DynamicsStageState {
  bool compressor_enabled = false;
  bool gate_enabled = false;
  bool expander_enabled = false;
  bool limiter_enabled = false;
  float compressor_threshold_db = -18.0F;
  float compressor_ratio = 3.0F;
  float gate_threshold_db = -60.0F;
  float expander_threshold_db = -60.0F;
  float expander_ratio = 2.0F;
  float limiter_threshold_db = -1.0F;
};

struct Backend {
  jobject effect = nullptr;
  BackendKind kind = BackendKind::equalizer;
  int band_count = 0;
  bool equalizer_enabled = false;
};

std::mutex backends_mutex;
std::unordered_map<const ParametricEqualizer*, Backend> backends;
std::unordered_map<const ParametricEqualizer*, std::unordered_map<int, Backend>> session_backends;
std::unordered_map<const ParametricEqualizer*, DynamicsStageState> dynamics_stage_states;

auto java_string(JNIEnv* env, const jstring value) -> std::string {
  if (value == nullptr) {
    return {};
  }
  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (chars == nullptr) {
    return {};
  }
  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  env->DeleteLocalRef(value);
  return result;
}

auto consume_exception(JNIEnv* env) -> std::string {
  jthrowable exception = env->ExceptionOccurred();
  if (exception == nullptr) {
    return {};
  }
  env->ExceptionClear();
  jclass throwable_class = env->GetObjectClass(exception);
  const jmethodID to_string = env->GetMethodID(throwable_class, "toString", "()Ljava/lang/String;");
  jstring description = to_string == nullptr
                            ? nullptr
                            : static_cast<jstring>(env->CallObjectMethod(exception, to_string));
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  const std::string result = java_string(env, description);
  env->DeleteLocalRef(throwable_class);
  env->DeleteLocalRef(exception);
  return result.empty() ? "Android audio effect API failed" : result;
}

void check_exception(JNIEnv* env, const char* operation) {
  if (env->ExceptionCheck()) {
    throw std::runtime_error(std::string(operation) + ": " + consume_exception(env));
  }
}

auto find_class(JNIEnv* env, const char* name) -> jclass {
  jclass result = env->FindClass(name);
  check_exception(env, "Could not load Android audio effect class");
  if (result == nullptr) {
    throw std::runtime_error(std::string("Could not load Android class ") + name);
  }
  return result;
}

auto method(JNIEnv* env, jclass type, const char* name, const char* signature) -> jmethodID {
  jmethodID result = env->GetMethodID(type, name, signature);
  check_exception(env, "Could not resolve Android audio effect method");
  if (result == nullptr) {
    throw std::runtime_error(std::string("Could not resolve Android method ") + name);
  }
  return result;
}

void release_backend(JNIEnv* env, Backend& backend) noexcept {
  if (backend.effect == nullptr) {
    return;
  }
  jclass type = env->GetObjectClass(backend.effect);
  jmethodID release = env->GetMethodID(type, "release", "()V");
  if (release != nullptr) {
    env->CallVoidMethod(backend.effect, release);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  env->DeleteLocalRef(type);
  env->DeleteGlobalRef(backend.effect);
  backend.effect = nullptr;
}

auto has_control(JNIEnv* env, const jobject effect) -> bool {
  jclass type = env->GetObjectClass(effect);
  const jmethodID has_control_method = method(env, type, "hasControl", "()Z");
  const bool result = env->CallBooleanMethod(effect, has_control_method) == JNI_TRUE;
  check_exception(env, "Could not check Android global effect control");
  env->DeleteLocalRef(type);
  return result;
}

void set_enabled(JNIEnv* env, const jobject effect, const bool enabled) {
  jclass type = env->GetObjectClass(effect);
  const jmethodID set_enabled_method = method(env, type, "setEnabled", "(Z)I");
  const jint result = env->CallIntMethod(effect, set_enabled_method, enabled ? JNI_TRUE : JNI_FALSE);
  check_exception(env, "Could not change Android global effect state");
  if (result != 0) {
    throw std::runtime_error("Android refused the global effect enable state");
  }
  const jmethodID get_enabled_method = method(env, type, "getEnabled", "()Z");
  const bool actual = env->CallBooleanMethod(effect, get_enabled_method) == JNI_TRUE;
  check_exception(env, "Could not read Android global effect state");
  if (actual != enabled) {
    throw std::runtime_error("Android global effect did not enter the requested state");
  }
  env->DeleteLocalRef(type);
}

auto any_dynamics_stage_enabled(const DynamicsStageState& state) -> bool {
  return state.compressor_enabled || state.gate_enabled || state.expander_enabled || state.limiter_enabled;
}

void apply_equalizer(JNIEnv* env, const jobject effect, const ParametricEqualizer& equalizer) {
  jclass type = env->GetObjectClass(effect);
  const jmethodID get_band_count = method(env, type, "getNumberOfBands", "()S");
  const jshort band_count = env->CallShortMethod(effect, get_band_count);
  check_exception(env, "Could not query Android global equalizer bands");
  if (band_count <= 0) {
    throw std::runtime_error("Android reports no global equalizer bands");
  }
  const jmethodID get_range = method(env, type, "getBandLevelRange", "()[S");
  auto range = static_cast<jshortArray>(env->CallObjectMethod(effect, get_range));
  check_exception(env, "Could not query Android global equalizer range");
  if (range == nullptr || env->GetArrayLength(range) < 2) {
    throw std::runtime_error("Android reports an invalid global equalizer range");
  }
  jshort level_range[2]{};
  env->GetShortArrayRegion(range, 0, 2, level_range);
  check_exception(env, "Could not read Android global equalizer range");
  env->DeleteLocalRef(range);
  const jmethodID get_center = method(env, type, "getCenterFreq", "(S)I");
  const jmethodID set_level = method(env, type, "setBandLevel", "(SS)V");
  for (jshort band = 0; band < band_count; ++band) {
    const jint center_millihertz = env->CallIntMethod(effect, get_center, band);
    check_exception(env, "Could not query Android global equalizer frequency");
    const float frequency_hz = static_cast<float>(center_millihertz) / 1000.0F;
    const float response_left = equalizer.response_db(frequency_hz, 48000.0F, 0);
    const float response_right = equalizer.response_db(frequency_hz, 48000.0F, 1);
    const float gain_db = std::clamp((response_left + response_right) * 0.5F,
                                     static_cast<float>(level_range[0]) / 100.0F,
                                     static_cast<float>(level_range[1]) / 100.0F);
    const auto level_millibel = static_cast<jshort>(std::lround(gain_db * 100.0F));
    env->CallVoidMethod(effect, set_level, band, level_millibel);
    check_exception(env, "Could not configure Android global equalizer band");
  }
  env->DeleteLocalRef(type);
}

auto create_equalizer(JNIEnv* env, const ParametricEqualizer& equalizer, const bool enabled, const int session_id) -> Backend {
  jclass type = find_class(env, "android/media/audiofx/Equalizer");
  const jmethodID constructor = method(env, type, "<init>", "(II)V");
  jobject local_effect = env->NewObject(type, constructor, effect_priority, session_id);
  check_exception(env, "Could not create Android global Equalizer");
  env->DeleteLocalRef(type);
  if (local_effect == nullptr) {
    throw std::runtime_error("Android did not create a global Equalizer instance");
  }

  Backend backend{env->NewGlobalRef(local_effect), BackendKind::equalizer, 0, enabled};
  env->DeleteLocalRef(local_effect);
  try {
    if (!has_control(env, backend.effect)) {
      throw std::runtime_error("Another audio effect controls the global Equalizer");
    }
    apply_equalizer(env, backend.effect, equalizer);
    jclass backend_type = env->GetObjectClass(backend.effect);
    const jmethodID get_band_count = method(env, backend_type, "getNumberOfBands", "()S");
    backend.band_count = env->CallShortMethod(backend.effect, get_band_count);
    check_exception(env, "Could not query Android global equalizer bands");
    env->DeleteLocalRef(backend_type);
    set_enabled(env, backend.effect, enabled);
  } catch (...) {
    release_backend(env, backend);
    throw;
  }
  return backend;
}

void apply_dynamics_processing(JNIEnv* env,
                               const jobject effect,
                               const ParametricEqualizer& equalizer,
                               const bool equalizer_enabled) {
  jclass type = env->GetObjectClass(effect);
  const jmethodID get_channel_count = method(env, type, "getChannelCount", "()I");
  const jint channel_count = env->CallIntMethod(effect, get_channel_count);
  check_exception(env, "Could not query global DynamicsProcessing channels");
  if (channel_count < 1 || channel_count > 8) {
    throw std::runtime_error("Android reports an unsupported global DynamicsProcessing channel count");
  }
  const jmethodID set_band = method(
      env, type, "setPreEqBandByChannelIndex", "(IILandroid/media/audiofx/DynamicsProcessing$EqBand;)V");
  const jclass band_type = find_class(env, "android/media/audiofx/DynamicsProcessing$EqBand");
  const jmethodID band_constructor = method(env, band_type, "<init>", "(ZFF)V");
  const float ratio = std::pow(maximum_frequency_hz / minimum_frequency_hz, 1.0F / dynamics_band_count);
  for (jint channel = 0; channel < channel_count; ++channel) {
    for (jint band = 0; band < dynamics_band_count; ++band) {
      const float upper_hz = minimum_frequency_hz * std::pow(ratio, static_cast<float>(band + 1));
      const float lower_hz = band == 0 ? minimum_frequency_hz
                                       : minimum_frequency_hz * std::pow(ratio, static_cast<float>(band));
      const float center_hz = std::sqrt(lower_hz * upper_hz);
      const float gain_db = std::clamp(
          equalizer.response_db(center_hz, 48000.0F, std::min(channel, 1)), -24.0F, 24.0F);
      jobject eq_band = env->NewObject(band_type, band_constructor,
                                       equalizer_enabled ? JNI_TRUE : JNI_FALSE, upper_hz, gain_db);
      check_exception(env, "Could not create a global DynamicsProcessing EQ band");
      env->CallVoidMethod(effect, set_band, channel, band, eq_band);
      check_exception(env, "Could not configure global DynamicsProcessing EQ band");
      env->DeleteLocalRef(eq_band);
    }
  }
  env->DeleteLocalRef(band_type);
  env->DeleteLocalRef(type);
}

auto create_dynamics_processing(JNIEnv* env, const ParametricEqualizer& equalizer, const bool enabled) -> Backend {
  jclass builder_type = find_class(env, "android/media/audiofx/DynamicsProcessing$Config$Builder");
  const jmethodID builder_constructor = method(env, builder_type, "<init>", "(IIZIZIZIZ)V");
  jobject builder = env->NewObject(builder_type,
                                  builder_constructor,
                                  0,
                                  2,
                                  JNI_TRUE,
                                  dynamics_band_count,
                                  JNI_TRUE,
                                  3,
                                  JNI_FALSE,
                                  0,
                                  JNI_TRUE);
  check_exception(env, "Could not create Android DynamicsProcessing configuration");
  const jmethodID build = method(env, builder_type, "build", "()Landroid/media/audiofx/DynamicsProcessing$Config;");
  jobject config = env->CallObjectMethod(builder, build);
  check_exception(env, "Could not build Android DynamicsProcessing configuration");
  env->DeleteLocalRef(builder);
  env->DeleteLocalRef(builder_type);

  jclass type = find_class(env, "android/media/audiofx/DynamicsProcessing");
  const jmethodID constructor =
      method(env, type, "<init>", "(IILandroid/media/audiofx/DynamicsProcessing$Config;)V");
  jobject local_effect = env->NewObject(type, constructor, effect_priority, global_session_id, config);
  check_exception(env, "Could not create Android global DynamicsProcessing");
  env->DeleteLocalRef(config);
  env->DeleteLocalRef(type);
  if (local_effect == nullptr) {
    throw std::runtime_error("Android did not create global DynamicsProcessing");
  }

  Backend backend{env->NewGlobalRef(local_effect), BackendKind::dynamics_processing,
                  dynamics_band_count, enabled};
  env->DeleteLocalRef(local_effect);
  try {
    if (!has_control(env, backend.effect)) {
      throw std::runtime_error("Another audio effect controls the global output mix");
    }
    apply_dynamics_processing(env, backend.effect, equalizer, enabled);
    set_enabled(env, backend.effect, enabled);
  } catch (...) {
    release_backend(env, backend);
    throw;
  }
  return backend;
}

void apply_curve(JNIEnv* env, Backend& backend, const ParametricEqualizer& equalizer) {
  if (backend.kind == BackendKind::equalizer) {
    apply_equalizer(env, backend.effect, equalizer);
  } else {
    apply_dynamics_processing(env, backend.effect, equalizer, backend.equalizer_enabled);
  }
}

}  // namespace

auto enable(JNIEnv* env, const ParametricEqualizer& equalizer, const bool enabled) -> int {
  std::lock_guard lock(backends_mutex);
  if (auto current = backends.find(&equalizer); current != backends.end()) {
    current->second.equalizer_enabled = enabled;
    apply_curve(env, current->second, equalizer);
    const auto stage_state = dynamics_stage_states.find(&equalizer);
    set_enabled(env, current->second.effect,
                enabled || (stage_state != dynamics_stage_states.end() &&
                            any_dynamics_stage_enabled(stage_state->second)));
    return current->second.band_count;
  }

  std::string dynamics_failure;
  try {
    Backend backend = create_dynamics_processing(env, equalizer, enabled);
    backends.emplace(&equalizer, backend);
    return backend.band_count;
  } catch (const std::exception& failure) {
    dynamics_failure = failure.what();
    __android_log_print(ANDROID_LOG_WARN, "EasyEffectsAudio", "DynamicsProcessing unavailable: %s",
                        dynamics_failure.c_str());
  }

  try {
    Backend backend = create_equalizer(env, equalizer, enabled, global_session_id);
    backends.emplace(&equalizer, backend);
    return backend.band_count;
  } catch (const std::exception& failure) {
    __android_log_print(ANDROID_LOG_ERROR, "EasyEffectsAudio", "Android Equalizer unavailable: %s",
                        failure.what());
    throw std::runtime_error("Android global DynamicsProcessing failed: " + dynamics_failure +
                             "; Equalizer failed: " + failure.what());
  }
}

void update(JNIEnv* env, const ParametricEqualizer& equalizer, const bool enabled) {
  std::lock_guard lock(backends_mutex);
  const auto current = backends.find(&equalizer);
  if (current == backends.end()) {
    return;
  }
  current->second.equalizer_enabled = enabled;
  apply_curve(env, current->second, equalizer);
  const auto stage_state = dynamics_stage_states.find(&equalizer);
  set_enabled(env, current->second.effect,
              enabled || (stage_state != dynamics_stage_states.end() &&
                          any_dynamics_stage_enabled(stage_state->second)));
}

auto enable_session(JNIEnv* env, const ParametricEqualizer& equalizer, const int session_id) -> int {
  if (session_id <= 0) throw std::invalid_argument("Audio session id must be positive");
  std::lock_guard lock(backends_mutex);
  if (const auto owner = session_backends.find(&equalizer); owner != session_backends.end()) {
    if (const auto current = owner->second.find(session_id); current != owner->second.end()) {
    apply_equalizer(env, current->second.effect, equalizer);
    set_enabled(env, current->second.effect, true);
    return current->second.band_count;
    }
  }
  Backend backend = create_equalizer(env, equalizer, true, session_id);
  const int bands = backend.band_count;
  session_backends[&equalizer].emplace(session_id, backend);
  return bands;
}

void update_sessions(JNIEnv* env, const ParametricEqualizer& equalizer) {
  std::lock_guard lock(backends_mutex);
  const auto found = session_backends.find(&equalizer);
  if (found == session_backends.end()) return;
  for (auto& [session_id, backend] : found->second) {
    (void)session_id;
    apply_equalizer(env, backend.effect, equalizer);
    set_enabled(env, backend.effect, true);
  }
}

void disable_session(JNIEnv* env, const ParametricEqualizer* equalizer, const int session_id) noexcept {
  std::lock_guard lock(backends_mutex);
  const auto found = session_backends.find(equalizer);
  if (found == session_backends.end()) return;
  if (const auto session = found->second.find(session_id); session != found->second.end()) {
    release_backend(env, session->second);
    found->second.erase(session);
  }
  if (found->second.empty()) session_backends.erase(found);
}

void disable_sessions(JNIEnv* env, const ParametricEqualizer* equalizer) noexcept {
  std::lock_guard lock(backends_mutex);
  const auto found = session_backends.find(equalizer);
  if (found == session_backends.end()) return;
  for (auto& [session_id, backend] : found->second) {
    (void)session_id;
    release_backend(env, backend);
  }
  session_backends.erase(found);
}

auto active_session_count(const ParametricEqualizer* equalizer) noexcept -> int {
  std::lock_guard lock(backends_mutex);
  const auto found = session_backends.find(equalizer);
  return found == session_backends.end() ? 0 : static_cast<int>(found->second.size());
}

void disable(JNIEnv* env, const ParametricEqualizer* equalizer) noexcept {
  std::lock_guard lock(backends_mutex);
  const auto current = backends.find(equalizer);
  if (current != backends.end()) {
    release_backend(env, current->second);
    backends.erase(current);
  }
  if (const auto sessions = session_backends.find(equalizer); sessions != session_backends.end()) {
    for (auto& [session_id, backend] : sessions->second) {
      (void)session_id;
      release_backend(env, backend);
    }
    session_backends.erase(sessions);
  }
  dynamics_stage_states.erase(equalizer);
  ee::mobile::android_platform_effects_backend::release(env, equalizer);
}

void configure_dynamics_stage(JNIEnv* env,
                              const ParametricEqualizer* equalizer,
                              const int stage,
                              const bool enabled,
                              const float threshold_db,
                              const float ratio) {
  if (equalizer == nullptr || stage < 0 || stage > 3 || !std::isfinite(threshold_db) || !std::isfinite(ratio)) {
    throw std::invalid_argument("Invalid Android DynamicsProcessing stage parameters");
  }
  if (threshold_db < -60.0F || threshold_db > 0.0F || ratio < 1.0F || ratio > 20.0F) {
    throw std::out_of_range("Dynamics threshold must be -60..0 dB and ratio must be 1..20");
  }

  std::lock_guard lock(backends_mutex);
  const auto backend = backends.find(equalizer);
  if (backend == backends.end() || backend->second.kind != BackendKind::dynamics_processing) {
    throw std::runtime_error("This device did not provide DynamicsProcessing; this stage is unavailable");
  }
  auto& state = dynamics_stage_states[equalizer];
  switch (stage) {
    case 0:
      state.compressor_enabled = enabled;
      state.compressor_threshold_db = threshold_db;
      state.compressor_ratio = ratio;
      break;
    case 1:
      state.gate_enabled = enabled;
      state.gate_threshold_db = threshold_db;
      break;
    case 2:
      state.limiter_enabled = enabled;
      state.limiter_threshold_db = threshold_db;
      break;
    case 3:
      state.expander_enabled = enabled;
      state.expander_threshold_db = threshold_db;
      state.expander_ratio = ratio;
      break;
    default:
      throw std::invalid_argument("Unknown Android DynamicsProcessing stage");
  }

  jobject effect = backend->second.effect;
  jclass effect_type = env->GetObjectClass(effect);
  const jmethodID get_channel_count = method(env, effect_type, "getChannelCount", "()I");
  const jint channel_count = env->CallIntMethod(effect, get_channel_count);
  check_exception(env, "Could not query DynamicsProcessing channel count");
  const jmethodID set_mbc_band = method(
      env, effect_type, "setMbcBandByChannelIndex", "(IILandroid/media/audiofx/DynamicsProcessing$MbcBand;)V");
  const jmethodID set_limiter = method(
      env, effect_type, "setLimiterByChannelIndex", "(ILandroid/media/audiofx/DynamicsProcessing$Limiter;)V");
  jclass mbc_band_type = find_class(env, "android/media/audiofx/DynamicsProcessing$MbcBand");
  const jmethodID mbc_constructor = method(env, mbc_band_type, "<init>", "(ZFFFFFFFFFF)V");
  jclass limiter_type = find_class(env, "android/media/audiofx/DynamicsProcessing$Limiter");
  const jmethodID limiter_constructor = method(env, limiter_type, "<init>", "(ZZIFFFFF)V");

  constexpr std::array<float, 3U> crossover_hz{250.0F, 2500.0F, 20000.0F};
  for (jint channel = 0; channel < channel_count; ++channel) {
    for (jint band = 0; band < 3; ++band) {
      jobject mbc_band = env->NewObject(mbc_band_type,
                                       mbc_constructor,
                                       (state.compressor_enabled || state.gate_enabled || state.expander_enabled) ? JNI_TRUE : JNI_FALSE,
                                       crossover_hz[static_cast<std::size_t>(band)],
                                       10.0F,
                                       100.0F,
                                       state.compressor_enabled ? state.compressor_ratio : 1.0F,
                                       state.compressor_enabled ? state.compressor_threshold_db : 0.0F,
                                       0.0F,
                                       state.gate_enabled ? state.gate_threshold_db
                                                           : (state.expander_enabled ? state.expander_threshold_db : -90.0F),
                                       state.expander_enabled ? state.expander_ratio : 1.0F,
                                       0.0F,
                                       0.0F);
      check_exception(env, "Could not construct Android multiband compressor band");
      env->CallVoidMethod(effect, set_mbc_band, channel, band, mbc_band);
      check_exception(env, "Could not configure Android multiband compressor band");
      env->DeleteLocalRef(mbc_band);
    }
    jobject limiter = env->NewObject(limiter_type,
                                    limiter_constructor,
                                    JNI_TRUE,
                                    state.limiter_enabled ? JNI_TRUE : JNI_FALSE,
                                    0,
                                    5.0F,
                                    100.0F,
                                    std::max(10.0F, ratio),
                                    state.limiter_threshold_db,
                                    0.0F);
    check_exception(env, "Could not construct Android limiter stage");
    env->CallVoidMethod(effect, set_limiter, channel, limiter);
    check_exception(env, "Could not configure Android limiter stage");
    env->DeleteLocalRef(limiter);
  }
  env->DeleteLocalRef(limiter_type);
  env->DeleteLocalRef(mbc_band_type);
  env->DeleteLocalRef(effect_type);
  set_enabled(env, backend->second.effect,
              backend->second.equalizer_enabled || any_dynamics_stage_enabled(state));
}

auto is_enabled(JNIEnv* env, const ParametricEqualizer* equalizer) noexcept -> bool {
  std::lock_guard lock(backends_mutex);
  const auto current = backends.find(equalizer);
  if (current == backends.end() || current->second.effect == nullptr) {
    return false;
  }

  jclass type = nullptr;
  try {
    if (!has_control(env, current->second.effect)) {
      return false;
    }
    type = env->GetObjectClass(current->second.effect);
    const jmethodID get_enabled = method(env, type, "getEnabled", "()Z");
    const bool effect_enabled = env->CallBooleanMethod(current->second.effect, get_enabled) == JNI_TRUE;
    check_exception(env, "Could not query Android global effect state");
    env->DeleteLocalRef(type);
    type = nullptr;
    return current->second.equalizer_enabled && effect_enabled;
  } catch (const std::exception& failure) {
    if (type != nullptr) {
      env->DeleteLocalRef(type);
    }
    __android_log_print(ANDROID_LOG_WARN, "EasyEffectsAudio", "Could not query global effect state: %s",
                        failure.what());
    return false;
  }
}

auto is_effect_active(JNIEnv* env, const ParametricEqualizer* equalizer) noexcept -> bool {
  std::lock_guard lock(backends_mutex);
  const auto current = backends.find(equalizer);
  if (current == backends.end() || current->second.effect == nullptr) {
    return false;
  }

  jclass type = nullptr;
  try {
    if (!has_control(env, current->second.effect)) {
      return false;
    }
    type = env->GetObjectClass(current->second.effect);
    const jmethodID get_enabled = method(env, type, "getEnabled", "()Z");
    const bool enabled = env->CallBooleanMethod(current->second.effect, get_enabled) == JNI_TRUE;
    check_exception(env, "Could not query Android audio processor state");
    env->DeleteLocalRef(type);
    return enabled;
  } catch (const std::exception& failure) {
    if (type != nullptr) {
      env->DeleteLocalRef(type);
    }
    __android_log_print(ANDROID_LOG_WARN, "EasyEffectsAudio", "Could not query processor state: %s",
                        failure.what());
    return false;
  }
}

auto active_band_count(const ParametricEqualizer* equalizer) noexcept -> int {
  std::lock_guard lock(backends_mutex);
  const auto current = backends.find(equalizer);
  return current == backends.end() ? 0 : current->second.band_count;
}

auto configured_band_gain_db(JNIEnv* env, const ParametricEqualizer* equalizer, const int channel, const int band)
    -> float {
  std::lock_guard lock(backends_mutex);
  const auto current = backends.find(equalizer);
  if (current == backends.end() || channel < 0 || channel > 1 || band < 0 ||
      band >= current->second.band_count) {
    throw std::out_of_range("Android global equalizer band is unavailable");
  }

  if (current->second.kind == BackendKind::equalizer) {
    jclass type = env->GetObjectClass(current->second.effect);
    const jmethodID get_band_level = method(env, type, "getBandLevel", "(S)S");
    const jshort level = env->CallShortMethod(current->second.effect, get_band_level, static_cast<jshort>(band));
    check_exception(env, "Could not read Android global equalizer band");
    env->DeleteLocalRef(type);
    return static_cast<float>(level) / 100.0F;
  }

  jclass type = env->GetObjectClass(current->second.effect);
  const jmethodID get_band = method(
      env, type, "getPreEqBandByChannelIndex", "(II)Landroid/media/audiofx/DynamicsProcessing$EqBand;");
  jobject eq_band = env->CallObjectMethod(current->second.effect, get_band, channel, band);
  check_exception(env, "Could not read Android DynamicsProcessing band");
  if (eq_band == nullptr) {
    env->DeleteLocalRef(type);
    throw std::runtime_error("Android DynamicsProcessing returned an empty EQ band");
  }
  jclass band_type = env->GetObjectClass(eq_band);
  const jmethodID get_gain = method(env, band_type, "getGain", "()F");
  const jfloat gain = env->CallFloatMethod(eq_band, get_gain);
  check_exception(env, "Could not read Android DynamicsProcessing gain");
  env->DeleteLocalRef(band_type);
  env->DeleteLocalRef(eq_band);
  env->DeleteLocalRef(type);
  return gain;
}

}  // namespace ee::mobile::android_global_equalizer
