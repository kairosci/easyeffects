/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.Context
import androidx.core.content.edit

internal enum class AndroidDynamicsStage(val nativeId: Int, val label: Int, val defaultThreshold: Float, val defaultRatio: Float) {
    COMPRESSOR(0, R.string.android_dynamics_compressor, -18f, 3f),
    GATE(1, R.string.android_dynamics_gate, -60f, 1f),
    LIMITER(2, R.string.android_dynamics_limiter, -1f, 10f),
    EXPANDER(3, R.string.android_dynamics_expander, -60f, 2f),
}

internal data class AndroidDynamicsState(val enabled: Boolean, val thresholdDb: Float, val ratio: Float)

/** A small, explicit adapter to the MBC and limiter stages in Android DynamicsProcessing. */
internal class AndroidDynamicsProcessing(context: Context, private val native: NativeEqualizer) {
    private val preferences = context.applicationContext.getSharedPreferences("android_dynamics_processing", Context.MODE_PRIVATE)
    private val states = AndroidDynamicsStage.entries.associateWith { stage ->
        AndroidDynamicsState(
            enabled = preferences.getBoolean(key(stage, "enabled"), false),
            thresholdDb = preferences.getFloat(key(stage, "threshold"), stage.defaultThreshold).coerceIn(-60f, 0f),
            ratio = preferences.getFloat(key(stage, "ratio"), stage.defaultRatio).coerceIn(1f, 20f),
        )
    }.toMutableMap()

    fun state(stage: AndroidDynamicsStage) = states.getValue(stage)

    fun set(
        stage: AndroidDynamicsStage,
        enabled: Boolean = state(stage).enabled,
        thresholdDb: Float = state(stage).thresholdDb,
        ratio: Float = state(stage).ratio,
    ) {
        val next = AndroidDynamicsState(enabled, thresholdDb.coerceIn(-60f, 0f), ratio.coerceIn(1f, 20f))
        if (next.enabled || state(stage).enabled || AudioRuntime.globalOutputEqualizer.isEnabled) {
            if (!AudioRuntime.globalOutputEqualizer.isEnabled) prepareBackend()
            native.configureDynamicsStage(stage.nativeId, next.enabled, next.thresholdDb, next.ratio)
        }
        states[stage] = next
        preferences.edit {
            putBoolean(key(stage, "enabled"), next.enabled)
            putFloat(key(stage, "threshold"), next.thresholdDb)
            putFloat(key(stage, "ratio"), next.ratio)
        }
    }

    /** Ensures this plugin can run independently from the global EQ plugin. */
    fun prepareBackend() {
        check(native.prepareAndroidDynamicsProcessing() > 0) {
            "Android did not provide an audio processing backend"
        }
    }

    fun restoreEnabled() {
        AndroidDynamicsStage.entries.forEach { stage ->
            if (preferences.getBoolean(key(stage, "enabled"), false)) {
                runCatching { set(stage, enabled = true) }.onFailure {
                    states[stage] = state(stage).copy(enabled = false)
                    preferences.edit { putBoolean(key(stage, "enabled"), false) }
                    android.util.Log.e("EasyEffectsDynamics", "Could not restore ${stage.name}", it)
                }
            }
        }
    }

    fun disableAll() {
        AndroidDynamicsStage.entries.forEach { stage ->
            if (state(stage).enabled) runCatching { set(stage, enabled = false) }
        }
    }

    fun hasEnabledStages() = states.values.any { it.enabled }

    private fun key(stage: AndroidDynamicsStage, field: String) = "${stage.name.lowercase()}_$field"
}
