/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.Context

/** Desktop reference modes, in DbAutogain::reference order. */
internal enum class AutogainReference(val index: Int, val labelResource: Int) {
    MOMENTARY(0, R.string.autogain_reference_momentary),
    SHORTTERM(1, R.string.autogain_reference_shortterm),
    INTEGRATED(2, R.string.autogain_reference_integrated),
    GEOMETRIC_MSI(3, R.string.autogain_reference_msi),
    GEOMETRIC_MS(4, R.string.autogain_reference_ms),
    GEOMETRIC_MI(5, R.string.autogain_reference_mi),
    GEOMETRIC_SI(6, R.string.autogain_reference_si),
}

/** Desktop Autogain parameters and defaults, matching easyeffects_db_autogain.kcfg. */
internal data class AutogainSettings(
    val reference: Int = AutogainReference.GEOMETRIC_MSI.index,
    val targetDb: Float = -23f,
    val silenceThresholdDb: Float = -70f,
    val maximumHistorySeconds: Int = 15,
    val forceSilence: Boolean = false,
    val inputGainDb: Float = 0f,
    val outputGainDb: Float = 0f,
    val bypass: Boolean = false,
)

internal data class AutogainLevels(
    val momentaryLufs: Float,
    val shorttermLufs: Float,
    val integratedLufs: Float,
    val relativeLufs: Float,
    val rangeLu: Float,
    val loudnessLufs: Float,
    val outputGainDb: Float,
    val inputLeftDb: Float,
    val inputRightDb: Float,
    val outputLeftDb: Float,
    val outputRightDb: Float,
)

/**
 * App-owned AAudio preview around the portable C++ AutogainProcessor. It never
 * touches another app's PCM; global playback still needs a system/OEM route.
 */
internal class NativeAutogainPreview : AutoCloseable {
    private var handle = nativeCreate()

    init {
        require(handle != 0L) { "Could not create native autogain preview" }
    }

    /** Opens and starts the AAudio preview stream. Returns 0 on success or a negative code. */
    fun start(sampleRate: Int): Int = if (handle == 0L) -1000 else nativeStart(handle, sampleRate.toFloat())

    fun stop() {
        if (handle != 0L) nativeStop(handle)
    }

    val isRunning: Boolean
        get() = handle != 0L && nativeIsRunning(handle)

    fun apply(settings: AutogainSettings) {
        if (handle == 0L) return
        nativeSetParameters(
            handle,
            settings.reference,
            settings.targetDb,
            settings.silenceThresholdDb,
            settings.maximumHistorySeconds,
            settings.forceSilence,
            settings.inputGainDb,
            settings.outputGainDb,
            settings.bypass,
        )
    }

    fun resetHistory() {
        if (handle != 0L) nativeResetHistory(handle)
    }

    fun levels(): AutogainLevels {
        if (handle == 0L) return silentLevels
        val values = nativeLevels(handle)
        if (values.size < 11) return silentLevels
        return AutogainLevels(
            momentaryLufs = values[0],
            shorttermLufs = values[1],
            integratedLufs = values[2],
            relativeLufs = values[3],
            rangeLu = values[4],
            loudnessLufs = values[5],
            outputGainDb = values[6],
            inputLeftDb = values[7],
            inputRightDb = values[8],
            outputLeftDb = values[9],
            outputRightDb = values[10],
        )
    }

    val renderedFrames: Long
        get() = if (handle == 0L) 0L else nativeRenderedFrames(handle)

    internal fun runAutogainPreviewTest(): String = nativeRunAutogainPreviewTest()

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
        reference: Int,
        targetDb: Float,
        silenceThresholdDb: Float,
        maximumHistorySeconds: Int,
        forceSilence: Boolean,
        inputGainDb: Float,
        outputGainDb: Float,
        bypass: Boolean,
    )
    private external fun nativeResetHistory(handle: Long)
    private external fun nativeLevels(handle: Long): FloatArray
    private external fun nativeRenderedFrames(handle: Long): Long
    private external fun nativeRunAutogainPreviewTest(): String

    companion object {
        private val silentLevels = AutogainLevels(
            momentaryLufs = -100f,
            shorttermLufs = -100f,
            integratedLufs = -100f,
            relativeLufs = -100f,
            rangeLu = 0f,
            loudnessLufs = -100f,
            outputGainDb = 0f,
            inputLeftDb = -100f,
            inputRightDb = -100f,
            outputLeftDb = -100f,
            outputRightDb = -100f,
        )

        init {
            System.loadLibrary("easyeffects_audio")
        }
    }
}

/** Persists Autogain parameters between launches, mirroring the desktop kcfg defaults. */
internal object AutogainSettingsStore {
    private const val PREFS = "autogain"

    fun load(context: Context): AutogainSettings {
        val defaults = AutogainSettings()
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return AutogainSettings(
            reference = prefs.getInt("reference", defaults.reference).coerceIn(0, 6),
            targetDb = prefs.getFloat("target", defaults.targetDb).coerceIn(-100f, 0f),
            silenceThresholdDb = prefs.getFloat("silenceThreshold", defaults.silenceThresholdDb).coerceIn(-100f, 0f),
            maximumHistorySeconds = prefs.getInt("maximumHistory", defaults.maximumHistorySeconds).coerceIn(6, 3600),
            forceSilence = prefs.getBoolean("forceSilence", defaults.forceSilence),
            inputGainDb = prefs.getFloat("inputGain", defaults.inputGainDb).coerceIn(-36f, 36f),
            outputGainDb = prefs.getFloat("outputGain", defaults.outputGainDb).coerceIn(-36f, 36f),
            bypass = prefs.getBoolean("bypass", defaults.bypass),
        )
    }

    fun save(context: Context, settings: AutogainSettings) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putInt("reference", settings.reference)
            .putFloat("target", settings.targetDb)
            .putFloat("silenceThreshold", settings.silenceThresholdDb)
            .putInt("maximumHistory", settings.maximumHistorySeconds)
            .putBoolean("forceSilence", settings.forceSilence)
            .putFloat("inputGain", settings.inputGainDb)
            .putFloat("outputGain", settings.outputGainDb)
            .putBoolean("bypass", settings.bypass)
            .apply()
    }
}
