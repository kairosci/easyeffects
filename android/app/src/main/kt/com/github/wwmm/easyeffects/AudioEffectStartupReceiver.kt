/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Restores only an output effect the user explicitly enabled before reboot/update. */
class AudioEffectStartupReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED) {
            context.getSharedPreferences("global_equalizer", Context.MODE_PRIVATE).edit()
                .remove("player_sessions").apply()
        }
        if (intent.action != Intent.ACTION_BOOT_COMPLETED && intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return

        val outputSelected = !context.getSharedPreferences("audio_target", Context.MODE_PRIVATE)
            .getBoolean("input", false)
        val effectRequested = context.getSharedPreferences("global_equalizer", Context.MODE_PRIVATE)
            .getBoolean("enabled", false) || hasEnabledPlatformEffect(context) || hasEnabledDynamicsStage(context)
        if (outputSelected && effectRequested) {
            runCatching { GlobalEqualizerService.start(context.applicationContext) }
                .onFailure { android.util.Log.e("EasyEffectsGlobalEQ", "Could not restore the enabled output effect", it) }
        }
    }

    private fun hasEnabledPlatformEffect(context: Context): Boolean {
        val preferences = context.getSharedPreferences("android_platform_effects", Context.MODE_PRIVATE)
        return listOf("bass_boost", "virtualizer", "loudness_enhancer")
            .any { preferences.getBoolean("${it}_enabled", false) }
    }

    private fun hasEnabledDynamicsStage(context: Context): Boolean {
        val preferences = context.getSharedPreferences("android_dynamics_processing", Context.MODE_PRIVATE)
        return listOf("compressor", "expander", "gate", "limiter")
            .any { preferences.getBoolean("${it}_enabled", false) }
    }
}
