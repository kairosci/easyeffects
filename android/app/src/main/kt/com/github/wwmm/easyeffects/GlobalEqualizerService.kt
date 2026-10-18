/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.media.audiofx.AudioEffect
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat

class GlobalEqualizerService : Service() {
    private val sessions = loadSavedSessions()
    private val audioSessionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val sessionId = intent.getIntExtra(AudioEffect.EXTRA_AUDIO_SESSION, -1)
            if (sessionId <= 0) return
            val packageName = intent.getStringExtra(AudioEffect.EXTRA_PACKAGE_NAME).orEmpty()
            if (packageName == context.packageName) return
            when (intent.action) {
                AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION -> {
                    if (AudioRuntime.globalOutputEqualizer.isEnabled) return
                    runCatching {
                        AudioRuntime.globalOutputEqualizer.nativeEnableSession(sessionId)
                        sessions[sessionId] = packageName
                        saveSessions()
                    }.onFailure {
                        android.util.Log.w("EasyEffectsGlobalEQ", "Could not attach to audio session $sessionId ($packageName)", it)
                    }
                }
                AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION -> {
                    sessions.remove(sessionId)
                    AudioRuntime.globalOutputEqualizer.nativeDisableSession(sessionId)
                    saveSessions()
                }
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, getString(R.string.global_eq_notification_channel), NotificationManager.IMPORTANCE_LOW),
        )
        val filter = android.content.IntentFilter().apply {
            addAction(AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION)
            addAction(AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION)
        }
        ContextCompat.registerReceiver(this, audioSessionReceiver, filter, ContextCompat.RECEIVER_EXPORTED)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val requested = intent?.action != ACTION_DISABLE
        if (!requested) {
            AudioRuntime.dynamicsProcessing.disableAll()
            AudioRuntime.platformEffects.disableAll()
            AudioRuntime.globalOutputEqualizer.disable()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf(startId)
            return START_NOT_STICKY
        }

        if (!AudioRuntime.hasEnabledOutputPlugins()) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf(startId)
            return START_NOT_STICKY
        }

        startAsForeground()
        if (AudioRuntime.globalOutputEqualizer.isRequested && !AudioRuntime.globalOutputEqualizer.isEnabled) {
            runCatching {
                AudioRuntime.globalOutputEqualizer.enable()
            }.onFailure {
                android.util.Log.e("EasyEffectsGlobalEQ", "Could not start C++ global audio backend", it)
                AudioRuntime.globalOutputEqualizer.recordFailure(it.localizedMessage ?: it.javaClass.simpleName)
            }
        }
        if (AudioRuntime.globalOutputEqualizer.isEnabled && sessions.isNotEmpty()) {
            AudioRuntime.globalOutputEqualizer.nativeDisableSessions()
            sessions.clear()
            saveSessions()
        } else if (!AudioRuntime.globalOutputEqualizer.isEnabled) {
            sessions.forEach { (sessionId, _) ->
                runCatching { AudioRuntime.globalOutputEqualizer.nativeEnableSession(sessionId) }
                    .onFailure { android.util.Log.w("EasyEffectsGlobalEQ", "Could not restore audio session $sessionId", it) }
            }
        }
        AudioRuntime.platformEffects.restoreEnabled()
        if (AudioRuntime.dynamicsProcessing.hasEnabledStages() && !AudioRuntime.globalOutputEqualizer.isEnabled) {
            runCatching { AudioRuntime.dynamicsProcessing.prepareBackend() }
                .onFailure { android.util.Log.e("EasyEffectsDynamics", "Could not prepare Android DynamicsProcessing", it) }
        }
        AudioRuntime.dynamicsProcessing.restoreEnabled()
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        runCatching { AudioRuntime.globalOutputEqualizer.nativeDisableSessions() }
        if (!AudioRuntime.globalOutputEqualizer.isRequested) {
            sessions.clear()
            saveSessions()
        }
        runCatching { unregisterReceiver(audioSessionReceiver) }
        super.onDestroy()
    }

    private fun loadSavedSessions(): LinkedHashMap<Int, String> = runCatching {
        val saved = getSharedPreferences(PREFERENCES, MODE_PRIVATE).getString(KEY_SESSIONS, "{}") ?: "{}"
        val json = org.json.JSONObject(saved)
        LinkedHashMap<Int, String>().apply {
            json.keys().forEach { key -> key.toIntOrNull()?.let { put(it, json.optString(key)) } }
        }
    }.getOrDefault(linkedMapOf())

    private fun saveSessions() {
        val json = org.json.JSONObject()
        sessions.forEach { (sessionId, packageName) -> json.put(sessionId.toString(), packageName) }
        getSharedPreferences(PREFERENCES, MODE_PRIVATE).edit().putString(KEY_SESSIONS, json.toString()).apply()
    }

    private fun startAsForeground() {
        val openApp = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val stopEffect = PendingIntent.getService(
            this,
            1,
            Intent(this, GlobalEqualizerService::class.java).setAction(ACTION_DISABLE),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val notification: Notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_monochrome)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(getString(R.string.enabled))
            .setContentIntent(openApp)
            .setOngoing(true)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .addAction(0, getString(R.string.global_eq_turn_off), stopEffect)
            .build()

        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    companion object {
        private const val CHANNEL_ID = "global_equalizer"
        private const val NOTIFICATION_ID = 3201
        private const val PREFERENCES = "global_equalizer"
        private const val KEY_SESSIONS = "player_sessions"
        private const val ACTION_DISABLE = "com.github.wwmm.easyeffects.action.DISABLE_GLOBAL_EQ"

        fun start(context: android.content.Context) {
            ContextCompat.startForegroundService(context, Intent(context, GlobalEqualizerService::class.java))
        }

        fun stop(context: android.content.Context) {
            context.startService(Intent(context, GlobalEqualizerService::class.java).setAction(ACTION_DISABLE))
        }
    }
}
