/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.Context
import androidx.core.content.edit

/** UI and persistence adapter for the C++ Android output-mix backend. */
internal class GlobalOutputEqualizer(
    context: Context,
    private val nativeEqualizer: NativeEqualizer,
) {
    private val preferences = context.applicationContext.getSharedPreferences("global_equalizer", Context.MODE_PRIVATE)

    val isEnabled: Boolean get() = nativeEqualizer.isGlobalOutputEqualizerEnabled
    val isRequested: Boolean get() = preferences.getBoolean(KEY_ENABLED, false)
    val activeBandCount: Int get() = nativeEqualizer.globalOutputEqualizerBandCount
    val lastError: String? get() = preferences.getString(KEY_LAST_ERROR, null)

    fun enable(): Int {
        preferences.edit {
            putBoolean(KEY_ENABLED, true)
            remove(KEY_LAST_ERROR)
        }
        return try {
            val count = nativeEqualizer.enableGlobalOutputEqualizer(true)
            check(count > 0) { "Android reports no bands for the global output equalizer" }
            preferences.edit { remove(KEY_LAST_ERROR) }
            count
        } catch (failure: Throwable) {
            recordFailure(failure.localizedMessage ?: failure.javaClass.simpleName)
            throw failure
        }
    }

    fun update() {
        if (isEnabled) runCatching { nativeEqualizer.updateGlobalOutputEqualizer(true) }
            .onFailure {
                disable()
                recordFailure(it.localizedMessage ?: it.javaClass.simpleName)
            }
        runCatching { nativeEqualizer.updateSessionEqualizers() }
    }

    fun disable() {
        nativeEqualizer.disableGlobalOutputEqualizer()
        preferences.edit {
            putBoolean(KEY_ENABLED, false)
            remove(KEY_LAST_ERROR)
        }
    }

    fun recordFailure(message: String) {
        preferences.edit {
            putBoolean(KEY_ENABLED, true)
            putString(KEY_LAST_ERROR, message)
        }
    }

    val activeSessionCount: Int get() = nativeEqualizer.activeSessionEqualizerCount

    fun nativeEnableSession(sessionId: Int): Int = nativeEqualizer.enableSessionEqualizer(sessionId)
    fun nativeDisableSession(sessionId: Int) = nativeEqualizer.disableSessionEqualizer(sessionId)
    fun nativeDisableSessions() = nativeEqualizer.disableSessionEqualizers()
    fun clearFailure() = preferences.edit { remove(KEY_LAST_ERROR) }

    companion object {
        private const val KEY_ENABLED = "enabled"
        private const val KEY_LAST_ERROR = "last_error"
    }
}
