/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.Context
import androidx.core.content.edit

internal enum class AndroidPlatformEffect(
    val nativeId: Int,
    val label: Int,
    val minimum: Int,
    val maximum: Int,
    val defaultValue: Int,
) {
    BASS_BOOST(0, R.string.android_effect_bass_boost, 0, 1000, 500),
    VIRTUALIZER(1, R.string.android_effect_virtualizer, 0, 1000, 500),
    LOUDNESS_ENHANCER(2, R.string.android_effect_loudness, 0, 2400, 0),
}

internal data class AndroidPlatformEffectState(val enabled: Boolean, val value: Int)

/** Controls Android's own output-effect implementations through the C++ JNI backend. */
internal class AndroidPlatformEffects(context: Context, private val native: NativeEqualizer) {
    private val preferences = context.applicationContext.getSharedPreferences("android_platform_effects", Context.MODE_PRIVATE)
    private val states = AndroidPlatformEffect.entries.associateWith { effect ->
        AndroidPlatformEffectState(
            enabled = preferences.getBoolean(enabledKey(effect), false),
            value = preferences.getInt(valueKey(effect), effect.defaultValue).coerceIn(effect.minimum, effect.maximum),
        )
    }.toMutableMap()

    fun state(effect: AndroidPlatformEffect): AndroidPlatformEffectState = states.getValue(effect)

    fun set(effect: AndroidPlatformEffect, enabled: Boolean = state(effect).enabled, value: Int = state(effect).value) {
        val next = AndroidPlatformEffectState(enabled, value.coerceIn(effect.minimum, effect.maximum))
        runCatching { native.configureAndroidPlatformEffect(effect.nativeId, next.enabled, next.value) }
            .getOrElse { throw IllegalStateException(it.localizedMessage ?: it.javaClass.simpleName, it) }
        states[effect] = next
        preferences.edit {
            putBoolean(enabledKey(effect), next.enabled)
            putInt(valueKey(effect), next.value)
        }
    }

    fun restoreEnabled() {
        AndroidPlatformEffect.entries.forEach { effect ->
            if (preferences.getBoolean(enabledKey(effect), false)) {
                runCatching { set(effect, enabled = true) }.onFailure {
                    states[effect] = state(effect).copy(enabled = false)
                    preferences.edit { putBoolean(enabledKey(effect), false) }
                    android.util.Log.e("EasyEffectsPlatformFx", "Could not restore ${effect.name}", it)
                }
            }
        }
    }

    fun disableAll() {
        AndroidPlatformEffect.entries.forEach { effect ->
            if (state(effect).enabled) runCatching { set(effect, enabled = false) }
        }
    }

    fun hasEnabledEffects(): Boolean = states.values.any { it.enabled }

    private fun enabledKey(effect: AndroidPlatformEffect) = "${effect.name.lowercase()}_enabled"
    private fun valueKey(effect: AndroidPlatformEffect) = "${effect.name.lowercase()}_value"
}
