/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "android_platform_effects_backend.hpp"
#include "android_platform_effects.hpp"

#include <array>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ee::mobile::android_platform_effects_backend {
namespace {
enum class BackendKind { bass_boost, virtualizer, loudness_enhancer };
struct Backend { jobject effect = nullptr; BackendKind kind = BackendKind::bass_boost; int band_count = 0; };
constexpr int global_session_id = 0;
constexpr int effect_priority = 100;
std::mutex backends_mutex;
using PlatformEffectBackends = std::array<Backend, 3U>;
std::unordered_map<const ParametricEqualizer*, PlatformEffectBackends> backends;
constexpr std::array<const char*, 3U> effect_classes{
    "android/media/audiofx/BassBoost",
    "android/media/audiofx/Virtualizer",
    "android/media/audiofx/LoudnessEnhancer",
};
constexpr std::array<BackendKind, 3U> effect_kinds{
    BackendKind::bass_boost, BackendKind::virtualizer, BackendKind::loudness_enhancer};
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

void set_platform_parameter(JNIEnv* env, const Backend& backend, const int effect, const int value) {
  jclass type = env->GetObjectClass(backend.effect);
  if (type == nullptr) {
    check_exception(env, "Could not inspect Android platform effect");
    throw std::runtime_error("Could not inspect Android platform effect");
  }
  const char* method_name = nullptr;
  const char* signature = nullptr;
  if (effect == 2) {
    method_name = "setTargetGain";
    signature = "(I)V";
  } else {
    method_name = "setStrength";
    signature = "(S)V";
  }
  const jmethodID setter = method(env, type, method_name, signature);
  if (effect == 2) {
    env->CallVoidMethod(backend.effect, setter, static_cast<jint>(value));
    check_exception(env, "Could not configure Android LoudnessEnhancer");
  } else {
    env->CallVoidMethod(backend.effect, setter, static_cast<jshort>(value));
    check_exception(env, "Could not configure Android platform effect parameter");
  }
  env->DeleteLocalRef(type);
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


}  // namespace

void configure(JNIEnv* env,
                               const ParametricEqualizer* equalizer,
                               const int effect,
                               const bool enabled,
                               const int value) {
  if (equalizer == nullptr || effect < 0 || effect >= static_cast<int>(effect_classes.size())) {
    throw std::invalid_argument("Unknown Android platform effect");
  }
  if (!ee::mobile::valid_platform_effect_parameter(effect, value)) {
    throw std::out_of_range("Android platform effect parameter is out of range");
  }

  std::lock_guard lock(backends_mutex);
  auto& effects = backends[equalizer];
  Backend& backend = effects[static_cast<std::size_t>(effect)];
  if (backend.effect == nullptr) {
    jclass type = find_class(env, effect_classes[static_cast<std::size_t>(effect)]);
    const bool loudness_enhancer = effect == 2;
    const jmethodID constructor = method(env, type, "<init>", loudness_enhancer ? "(I)V" : "(II)V");
    jobject local_effect = loudness_enhancer
                               ? env->NewObject(type, constructor, global_session_id)
                               : env->NewObject(type, constructor, effect_priority, global_session_id);
    check_exception(env, "Could not create Android global platform effect");
    env->DeleteLocalRef(type);
    if (local_effect == nullptr) {
      throw std::runtime_error("Android did not create the requested global platform effect");
    }
    backend = {env->NewGlobalRef(local_effect), effect_kinds[static_cast<std::size_t>(effect)], 0};
    env->DeleteLocalRef(local_effect);
    if (!has_control(env, backend.effect)) {
      release_backend(env, backend);
      throw std::runtime_error("Another app or system component controls this Android platform effect");
    }
  }
  try {
    set_platform_parameter(env, backend, effect, value);
    set_enabled(env, backend.effect, enabled);
  } catch (...) {
    if (enabled) {
      try {
        set_enabled(env, backend.effect, false);
      } catch (...) {
      }
    }
    throw;
  }
}


void release(JNIEnv* env, const ParametricEqualizer* owner) noexcept {
  std::lock_guard lock(backends_mutex);
  const auto found = backends.find(owner);
  if (found == backends.end()) return;
  for (auto& backend : found->second) release_backend(env, backend);
  backends.erase(found);
}

}  // namespace ee::mobile::android_platform_effects_backend
