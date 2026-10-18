/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.Context

internal object AudioRuntime {
    lateinit var nativeEqualizer: NativeEqualizer
        private set
    lateinit var model: EqualizerModel
        private set
    lateinit var globalOutputEqualizer: GlobalOutputEqualizer
        private set
    lateinit var platformEffects: AndroidPlatformEffects
        private set
    lateinit var dynamicsProcessing: AndroidDynamicsProcessing
        private set

    fun initialize(context: Context) {
        if (!::nativeEqualizer.isInitialized) {
            nativeEqualizer = NativeEqualizer()
            model = EqualizerModel(context.applicationContext, nativeEqualizer)
            globalOutputEqualizer = GlobalOutputEqualizer(context.applicationContext, nativeEqualizer)
            platformEffects = AndroidPlatformEffects(context.applicationContext, nativeEqualizer)
            dynamicsProcessing = AndroidDynamicsProcessing(context.applicationContext, nativeEqualizer)
        }
    }

    fun hasEnabledOutputPlugins(): Boolean =
        globalOutputEqualizer.isRequested ||
            platformEffects.hasEnabledEffects() ||
            dynamicsProcessing.hasEnabledStages()
}

class EasyEffectsApplication : android.app.Application() {
    override fun onCreate() {
        super.onCreate()
        AudioRuntime.initialize(this)
    }
}
