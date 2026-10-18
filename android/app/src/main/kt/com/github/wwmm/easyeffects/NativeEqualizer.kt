/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import java.nio.ByteBuffer

internal class NativeEqualizer : AutoCloseable {
    private var handle = nativeCreate()

    init {
        require(handle != 0L) { "Could not create native equalizer" }
    }

    fun setBand(index: Int, band: EqualizerBand) {
        if (handle != 0L) {
            nativeSetBand(
                handle,
                index,
                band.type.nativeValue,
                band.frequencyHz,
                band.q,
                band.gainDb,
                band.enabled,
                band.channelMode,
            )
        }
    }

    fun setBypass(bypass: Boolean) {
        if (handle != 0L) nativeSetBypass(handle, bypass)
    }

    fun reset() {
        if (handle != 0L) nativeReset(handle)
    }

    fun setPreamp(gainDb: Float) {
        if (handle != 0L) nativeSetPreamp(handle, gainDb)
    }

    fun process(buffer: ByteBuffer, frames: Int, channels: Int, sampleRate: Int, floatPcm: Boolean) {
        if (handle == 0L) return
        if (floatPcm) {
            nativeProcessFloat(handle, buffer, frames, channels, sampleRate)
        } else {
            nativeProcessPcm16(handle, buffer, frames, channels, sampleRate)
        }
    }

    fun responseDb(frequencyHz: Float, sampleRate: Float = 48_000f, channel: Int = 0): Float =
        if (handle == 0L) 0f else nativeResponseDb(handle, frequencyHz, sampleRate, channel)

    fun responseCurve(pointCount: Int, sampleRate: Float = 48_000f, channel: Int): FloatArray =
        if (handle == 0L) FloatArray(pointCount) else nativeResponseCurve(handle, pointCount, sampleRate, channel)

    fun graphFrequencyFromX(normalizedX: Float): Float = nativeGraphFrequencyFromX(normalizedX)

    fun graphGainForResponse(currentBandGainDb: Float, currentResponseDb: Float, targetResponseDb: Float): Float =
        nativeGraphGainForResponse(currentBandGainDb, currentResponseDb, targetResponseDb)

    fun enableGlobalOutputEqualizer(enabled: Boolean): Int =
        if (handle == 0L) 0 else nativeEnableGlobalOutputEqualizer(handle, enabled)

    /** Creates Android DynamicsProcessing even when the global EQ switch is off. */
    fun prepareAndroidDynamicsProcessing(): Int =
        if (handle == 0L) 0 else nativeEnableGlobalOutputEqualizer(handle, false)

    fun updateGlobalOutputEqualizer(enabled: Boolean) {
        if (handle != 0L) nativeUpdateGlobalOutputEqualizer(handle, enabled)
    }

    fun disableGlobalOutputEqualizer() {
        if (handle != 0L) nativeDisableGlobalOutputEqualizer(handle)
    }

    fun enableSessionEqualizer(sessionId: Int): Int =
        if (handle == 0L) 0 else nativeEnableSessionEqualizer(handle, sessionId)

    fun disableSessionEqualizer(sessionId: Int) {
        if (handle != 0L) nativeDisableSessionEqualizer(handle, sessionId)
    }

    fun disableSessionEqualizers() {
        if (handle != 0L) nativeDisableSessionEqualizers(handle)
    }

    fun updateSessionEqualizers() {
        if (handle != 0L) nativeUpdateSessionEqualizers(handle)
    }

    val activeSessionEqualizerCount: Int
        get() = if (handle == 0L) 0 else nativeActiveSessionEqualizerCount(handle)

    val isGlobalOutputEqualizerEnabled: Boolean
        get() = handle != 0L && nativeIsGlobalOutputEqualizerEnabled(handle)

    val globalOutputEqualizerBandCount: Int
        get() = if (handle == 0L) 0 else nativeGlobalOutputEqualizerBandCount(handle)

    fun globalOutputEqualizerBandGainDb(channel: Int, band: Int): Float =
        if (handle == 0L) 0f else nativeGlobalOutputEqualizerBandGainDb(handle, channel, band)

    fun configureAndroidPlatformEffect(effect: Int, enabled: Boolean, value: Int) {
        check(handle != 0L) { "Native equalizer is not initialized" }
        nativeConfigureAndroidPlatformEffect(handle, effect, enabled, value)
    }

    fun configureDynamicsStage(stage: Int, enabled: Boolean, thresholdDb: Float, ratio: Float) {
        check(handle != 0L) { "Native equalizer is not initialized" }
        nativeConfigureDynamicsStage(handle, stage, enabled, thresholdDb, ratio)
    }

    internal fun runAndroidBackendChecks(): String =
        if (handle == 0L) "FAIL: native equalizer is not initialized" else nativeRunAndroidBackendChecks(handle)

    internal fun runPcmPlaybackTest(): String = nativeRunPcmPlaybackTest()

    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeSetBand(
        handle: Long,
        index: Int,
        type: Int,
        frequencyHz: Float,
        q: Float,
        gainDb: Float,
        enabled: Boolean,
        channelMode: Int,
    )
    private external fun nativeSetBypass(handle: Long, bypass: Boolean)
    private external fun nativeSetPreamp(handle: Long, gainDb: Float)
    private external fun nativeReset(handle: Long)
    private external fun nativeProcessFloat(
        handle: Long,
        buffer: ByteBuffer,
        frames: Int,
        channels: Int,
        sampleRate: Int,
    )
    private external fun nativeProcessPcm16(
        handle: Long,
        buffer: ByteBuffer,
        frames: Int,
        channels: Int,
        sampleRate: Int,
    )
    private external fun nativeResponseDb(handle: Long, frequencyHz: Float, sampleRate: Float, channel: Int): Float
    private external fun nativeResponseCurve(handle: Long, pointCount: Int, sampleRate: Float, channel: Int): FloatArray
    private external fun nativeGraphFrequencyFromX(normalizedX: Float): Float
    private external fun nativeGraphGainForResponse(currentBandGainDb: Float, currentResponseDb: Float, targetResponseDb: Float): Float
    private external fun nativeEnableGlobalOutputEqualizer(handle: Long, enabled: Boolean): Int
    private external fun nativeUpdateGlobalOutputEqualizer(handle: Long, enabled: Boolean)
    private external fun nativeDisableGlobalOutputEqualizer(handle: Long)
    private external fun nativeEnableSessionEqualizer(handle: Long, sessionId: Int): Int
    private external fun nativeDisableSessionEqualizer(handle: Long, sessionId: Int)
    private external fun nativeDisableSessionEqualizers(handle: Long)
    private external fun nativeUpdateSessionEqualizers(handle: Long)
    private external fun nativeActiveSessionEqualizerCount(handle: Long): Int
    private external fun nativeIsGlobalOutputEqualizerEnabled(handle: Long): Boolean
    private external fun nativeGlobalOutputEqualizerBandCount(handle: Long): Int
    private external fun nativeGlobalOutputEqualizerBandGainDb(handle: Long, channel: Int, band: Int): Float
    private external fun nativeConfigureAndroidPlatformEffect(handle: Long, effect: Int, enabled: Boolean, value: Int)
    private external fun nativeConfigureDynamicsStage(handle: Long, stage: Int, enabled: Boolean, thresholdDb: Float, ratio: Float)
    private external fun nativeRunAndroidBackendChecks(handle: Long): String
    private external fun nativeRunPcmPlaybackTest(): String

    companion object {
        init {
            System.loadLibrary("easyeffects_audio")
        }
    }
}
