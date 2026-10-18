/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.Context

/** Desktop Bass Enhancer parameters and defaults, matching easyeffects_db_bass_enhancer.kcfg. */
internal data class BassEnhancerSettings(
    val amountDb: Float = 0f,
    val harmonics: Float = 8.5f,
    val scopeHz: Float = 100f,
    val floorHz: Float = 20f,
    val blend: Float = 0f,
    val floorActive: Boolean = false,
    val listen: Boolean = false,
    val inputGainDb: Float = 0f,
    val outputGainDb: Float = 0f,
    val bypass: Boolean = false,
)

internal data class BassEnhancerLevels(
    val inputLeftDb: Float,
    val inputRightDb: Float,
    val outputLeftDb: Float,
    val outputRightDb: Float,
    val harmonicsLevelDb: Float,
)

/**
 * App-owned AAudio preview around the portable C++ BassEnhancerProcessor. It never
 * touches another app's PCM; global playback still needs a system/OEM route.
 */
internal class NativeBassEnhancerPreview : AutoCloseable {
    private var handle = nativeCreate()

    init {
        require(handle != 0L) { "Could not create native bass enhancer preview" }
    }

    /** Opens and starts the AAudio preview stream. Returns 0 on success or a negative code. */
    fun start(sampleRate: Int): Int = if (handle == 0L) -1000 else nativeStart(handle, sampleRate.toFloat())

    fun stop() {
        if (handle != 0L) nativeStop(handle)
    }

    val isRunning: Boolean
        get() = handle != 0L && nativeIsRunning(handle)

    fun apply(settings: BassEnhancerSettings) {
        if (handle == 0L) return
        nativeSetParameters(
            handle,
            settings.amountDb,
            settings.harmonics,
            settings.scopeHz,
            settings.floorHz,
            settings.blend,
            settings.floorActive,
            settings.listen,
            settings.inputGainDb,
            settings.outputGainDb,
            settings.bypass,
        )
    }

    fun levels(): BassEnhancerLevels {
        if (handle == 0L) return silentLevels
        val values = nativeLevels(handle)
        if (values.size < 5) return silentLevels
        return BassEnhancerLevels(
            inputLeftDb = values[0],
            inputRightDb = values[1],
            outputLeftDb = values[2],
            outputRightDb = values[3],
            harmonicsLevelDb = values[4],
        )
    }

    val renderedFrames: Long
        get() = if (handle == 0L) 0L else nativeRenderedFrames(handle)

    internal fun runBassEnhancerPreviewTest(): String = nativeRunBassEnhancerPreviewTest()

    override fun close() {
        if (handle != 0L) {
            nativeStop(handle)
            nativeDestroy(handle)
            handle = 0L
        }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeStart(handle: Long, desiredSampleRate: Float): Int
    private external fun nativeStop(handle: Long)
    private external fun nativeIsRunning(handle: Long): Boolean
    private external fun nativeSetParameters(
        handle: Long,
        amountDb: Float,
        harmonics: Float,
        scopeHz: Float,
        floorHz: Float,
        blend: Float,
        floorActive: Boolean,
        listen: Boolean,
        inputGainDb: Float,
        outputGainDb: Float,
        bypass: Boolean,
    )
    private external fun nativeLevels(handle: Long): FloatArray
    private external fun nativeRenderedFrames(handle: Long): Long
    private external fun nativeRunBassEnhancerPreviewTest(): String

    companion object {
        private val silentLevels = BassEnhancerLevels(
            inputLeftDb = -100f,
            inputRightDb = -100f,
            outputLeftDb = -100f,
            outputRightDb = -100f,
            harmonicsLevelDb = -100f,
        )

        init {
            System.loadLibrary("easyeffects_audio")
        }
    }
}

/** Persists Bass Enhancer parameters between launches, mirroring the desktop kcfg defaults. */
internal object BassEnhancerSettingsStore {
    private const val PREFS = "bass_enhancer"

    fun load(context: Context): BassEnhancerSettings {
        val defaults = BassEnhancerSettings()
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return BassEnhancerSettings(
            amountDb = prefs.getFloat("amount", defaults.amountDb).coerceIn(-100f, 36f),
            harmonics = prefs.getFloat("harmonics", defaults.harmonics).coerceIn(0.1f, 10f),
            scopeHz = prefs.getFloat("scope", defaults.scopeHz).coerceIn(10f, 250f),
            floorHz = prefs.getFloat("floor", defaults.floorHz).coerceIn(10f, 120f),
            blend = prefs.getFloat("blend", defaults.blend).coerceIn(-10f, 10f),
            floorActive = prefs.getBoolean("floorActive", defaults.floorActive),
            listen = prefs.getBoolean("listen", defaults.listen),
            inputGainDb = prefs.getFloat("inputGain", defaults.inputGainDb).coerceIn(-36f, 36f),
            outputGainDb = prefs.getFloat("outputGain", defaults.outputGainDb).coerceIn(-36f, 36f),
            bypass = prefs.getBoolean("bypass", defaults.bypass),
        )
    }

    fun save(context: Context, settings: BassEnhancerSettings) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putFloat("amount", settings.amountDb)
            .putFloat("harmonics", settings.harmonics)
            .putFloat("scope", settings.scopeHz)
            .putFloat("floor", settings.floorHz)
            .putFloat("blend", settings.blend)
            .putBoolean("floorActive", settings.floorActive)
            .putBoolean("listen", settings.listen)
            .putFloat("inputGain", settings.inputGainDb)
            .putFloat("outputGain", settings.outputGainDb)
            .putBoolean("bypass", settings.bypass)
            .apply()
    }
}
