/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.Context
import androidx.core.content.edit
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.pow

internal enum class FilterType(val nativeValue: Int, val labelResource: Int) {
    PEAKING(0, R.string.filter_peaking),
    LOW_SHELF(1, R.string.filter_low_shelf),
    HIGH_SHELF(2, R.string.filter_high_shelf),
    LOW_PASS(3, R.string.filter_low_pass),
    HIGH_PASS(4, R.string.filter_high_pass),
    BAND_PASS(5, R.string.filter_band_pass),
    NOTCH(6, R.string.filter_notch),
    ALL_PASS(7, R.string.filter_all_pass),
    OFF(8, R.string.filter_off),
}

internal data class EqualizerBand(
    val type: FilterType = FilterType.PEAKING,
    val frequencyHz: Float,
    val q: Float = 1.4f,
    val gainDb: Float = 0f,
    val enabled: Boolean = true,
    val channelMode: Int = 0,
    val solo: Boolean = false,
)

internal class EqualizerModel(context: Context, private val native: NativeEqualizer) {
    private val preferences = context.getSharedPreferences("equalizer", Context.MODE_PRIVATE)
    private val changeListeners = mutableListOf<() -> Unit>()
    private val mutableBands = MutableList(32) { index ->
        val frequency = 20f * 1000f.pow(index / 31f)
        EqualizerBand(frequencyHz = frequency)
    }
    val bands: List<EqualizerBand> get() = mutableBands

    var bandCount: Int = 32
        private set
    var preampDb: Float = 0f
        private set
    @Volatile var bypass: Boolean = false
        private set

    init {
        configureBandLayout(bandCount)
        restore()
        migrateLegacyDefaultLayout()
        syncNative()
    }

    fun addChangeListener(listener: () -> Unit) {
        changeListeners += listener
    }

    fun setBand(index: Int, band: EqualizerBand) {
        if (index !in mutableBands.indices) return
        val soloStateChanged = band.solo || mutableBands[index].solo
        if (band.solo) {
            for (other in mutableBands.indices) {
                if (other != index) mutableBands[other] = mutableBands[other].copy(solo = false)
            }
        }
        mutableBands[index] = band.copy(
            frequencyHz = band.frequencyHz.coerceIn(20f, 20_000f),
            q = band.q.coerceIn(0.1f, 18f),
            gainDb = band.gainDb.coerceIn(-24f, 24f),
            channelMode = band.channelMode.coerceIn(0, 2),
        )
        if (soloStateChanged) {
            for (bandIndex in mutableBands.indices) syncNativeBand(bandIndex)
        } else {
            syncNativeBand(index)
        }
        save()
        notifyChanged()
    }

    fun setBandCount(count: Int) {
        val newCount = count.coerceIn(1, 32)
        if (newCount == bandCount) return
        bandCount = newCount
        configureBandLayout(bandCount)
        for (index in mutableBands.indices) syncNativeBand(index)
        save()
        notifyChanged()
    }

    fun setPreamp(gainDb: Float) {
        preampDb = gainDb.coerceIn(-24f, 24f)
        native.setPreamp(preampDb)
        save()
        notifyChanged()
    }

    fun setBypass(value: Boolean) {
        bypass = value
        native.setBypass(value)
        save()
        notifyChanged()
    }

    fun flatten() {
        for (index in mutableBands.indices) {
            mutableBands[index] = mutableBands[index].copy(gainDb = 0f)
            syncNativeBand(index)
        }
        preampDb = 0f
        native.setPreamp(0f)
        save()
        notifyChanged()
    }

    fun reset() {
        bandCount = 32
        preampDb = 0f
        bypass = false
        mutableBands.indices.forEach { index ->
            mutableBands[index] = EqualizerBand(frequencyHz = 20f * 1000f.pow(index / 31f))
        }
        configureBandLayout(bandCount)
        syncNative()
        save()
        notifyChanged()
    }

    fun sortBandsByFrequency() {
        val sortedActiveBands = mutableBands.take(bandCount).sortedBy { it.frequencyHz }
        sortedActiveBands.forEachIndexed { index, band -> mutableBands[index] = band }
        syncNative()
        save()
        notifyChanged()
    }

    fun distributeFrequenciesLogarithmically() {
        configureBandLayout(bandCount)
        syncNative()
        save()
        notifyChanged()
    }

    fun toJson(): JSONObject = JSONObject().apply {
        put("format", "com.github.wwmm.easyeffects.equalizer")
        put("version", 1)
        put("bandCount", bandCount)
        put("preampDb", preampDb.toDouble())
        put("bypass", bypass)
        put("bands", JSONArray().apply {
            mutableBands.forEach { band ->
                put(JSONObject().apply {
                    put("type", band.type.name)
                    put("frequencyHz", band.frequencyHz.toDouble())
                    put("q", band.q.toDouble())
                    put("gainDb", band.gainDb.toDouble())
                    put("enabled", band.enabled)
                    put("channelMode", band.channelMode)
                    put("solo", band.solo)
                })
            }
        })
    }

    fun toEqualizerApo(): String = buildString {
        appendLine("Preamp: ${"%.2f".format(java.util.Locale.ROOT, preampDb)} dB")
        mutableBands.take(bandCount).forEachIndexed { index, band ->
            if (!band.enabled || band.type == FilterType.OFF) return@forEachIndexed
            val type = when (band.type) {
                FilterType.PEAKING -> "PK"
                FilterType.LOW_SHELF -> "LS"
                FilterType.HIGH_SHELF -> "HS"
                FilterType.LOW_PASS -> "LP"
                FilterType.HIGH_PASS -> "HP"
                FilterType.BAND_PASS -> "BP"
                FilterType.NOTCH -> "NO"
                FilterType.ALL_PASS -> "AP"
                FilterType.OFF -> return@forEachIndexed
            }
            appendLine(
                "Filter ${index + 1}: ON $type Fc ${"%.2f".format(java.util.Locale.ROOT, band.frequencyHz)} Hz " +
                    "Gain ${"%.2f".format(java.util.Locale.ROOT, band.gainDb)} dB " +
                    "Q ${"%.3f".format(java.util.Locale.ROOT, band.q)}",
            )
        }
    }

    fun loadJson(json: JSONObject) {
        require(json.optString("format") == "com.github.wwmm.easyeffects.equalizer") {
            "Unsupported preset format"
        }
        require(json.optInt("version") == 1) { "Unsupported preset version" }
        val incoming = json.optJSONArray("bands") ?: error("Preset has no bands")
        require(incoming.length() in 1..32) { "A preset must contain 1 to 32 bands" }

        bandCount = json.optInt("bandCount", incoming.length()).coerceIn(1, 32)
        preampDb = json.optDouble("preampDb", 0.0).toFloat().coerceIn(-24f, 24f)
        bypass = json.optBoolean("bypass", false)
        for (index in 0 until incoming.length()) {
            val item = incoming.getJSONObject(index)
            val type = runCatching { FilterType.valueOf(item.optString("type", "PEAKING")) }
                .getOrDefault(FilterType.PEAKING)
            mutableBands[index] = EqualizerBand(
                type = type,
                frequencyHz = item.optDouble("frequencyHz", 1000.0).toFloat(),
                q = item.optDouble("q", 1.4).toFloat(),
                gainDb = item.optDouble("gainDb", 0.0).toFloat(),
                enabled = item.optBoolean("enabled", true),
                channelMode = item.optInt("channelMode", 0),
                solo = item.optBoolean("solo", false),
            )
        }
        for (index in incoming.length() until mutableBands.size) {
            mutableBands[index] = mutableBands[index].copy(gainDb = 0f)
        }
        syncNative()
        save()
        notifyChanged()
    }

    fun loadEqualizerApo(text: String) {
        val filterLine = Regex("""(?i)Filter\s*\d*\s*:\s*(OFF|ON\s+([A-Z]+(?:\s+\d+DB)?)(.*))""")
        val frequency = Regex("""(?i)\bFc\s+([0-9.,]+)\s*Hz""")
        val gain = Regex("""(?i)\bGain\s+([+-]?[0-9.]+)\s*dB""")
        val qValue = Regex("""(?i)\bQ\s+([0-9.]+)""")
        val types = mapOf(
            "PK" to FilterType.PEAKING, "PEQ" to FilterType.PEAKING, "LS" to FilterType.LOW_SHELF,
            "LS 6DB" to FilterType.LOW_SHELF, "LS 12DB" to FilterType.LOW_SHELF,
            "HS" to FilterType.HIGH_SHELF, "HS 6DB" to FilterType.HIGH_SHELF,
            "HS 12DB" to FilterType.HIGH_SHELF, "LP" to FilterType.LOW_PASS,
            "HP" to FilterType.HIGH_PASS, "BP" to FilterType.BAND_PASS,
            "NO" to FilterType.NOTCH, "AP" to FilterType.ALL_PASS,
        )
        val parsed = text.lineSequence().mapNotNull { line ->
            val match = filterLine.find(line) ?: return@mapNotNull null
            if (match.groupValues[1].equals("OFF", ignoreCase = true)) return@mapNotNull null
            val filterType = types[match.groupValues[2].uppercase()] ?: return@mapNotNull null
            val rest = match.groupValues[3]
            val fc = frequency.find(rest)?.groupValues?.get(1)?.replace(",", "")?.toFloatOrNull()
                ?: return@mapNotNull null
            val db = gain.find(rest)?.groupValues?.get(1)?.toFloatOrNull() ?: 0f
            val q = qValue.find(rest)?.groupValues?.get(1)?.toFloatOrNull() ?: 1.4f
            EqualizerBand(type = filterType, frequencyHz = fc, q = q, gainDb = db)
        }.take(32).toList()
        if (parsed.isEmpty()) {
            val graphicEq = Regex("""(?im)^\s*GraphicEQ\s*:\s*(.+)$""").find(text)
            val points = graphicEq?.groupValues?.get(1)?.let { line ->
                Regex("""([0-9]+(?:\.[0-9]+)?)\s+([+-]?[0-9]+(?:\.[0-9]+)?)""")
                    .findAll(line)
                    .mapNotNull { match ->
                        val fc = match.groupValues[1].toFloatOrNull() ?: return@mapNotNull null
                        val db = match.groupValues[2].toFloatOrNull() ?: return@mapNotNull null
                        if (fc > 0f) fc to db else null
                    }
                    .sortedBy { it.first }
                    .toList()
            }.orEmpty()
            require(points.size >= 2) { "No supported Equalizer APO filters or GraphicEQ data were found" }

            for (index in mutableBands.indices) {
                val frequencyHz = 20f * 1000f.pow(index / 31f)
                val rightPoint = points.indexOfFirst { it.first >= frequencyHz }.let { found ->
                    if (found < 0) points.lastIndex else found
                }
                val leftPoint = (rightPoint - 1).coerceAtLeast(0)
                val (leftFrequency, leftDb) = points[leftPoint]
                val (rightFrequency, rightDb) = points[rightPoint]
                val ratio = if (rightFrequency == leftFrequency) 0f else
                    ((kotlin.math.ln(frequencyHz) - kotlin.math.ln(leftFrequency)) /
                        (kotlin.math.ln(rightFrequency) - kotlin.math.ln(leftFrequency))).coerceIn(0f, 1f)
                mutableBands[index] = EqualizerBand(
                    frequencyHz = frequencyHz,
                    q = 2.2f,
                    gainDb = (leftDb + (rightDb - leftDb) * ratio).coerceIn(-24f, 24f),
                )
            }
            bandCount = 32
            val graphicPreamp = Regex("""(?i)Preamp\s*:\s*([+-]?[0-9.]+)\s*dB""").find(text)
            preampDb = graphicPreamp?.groupValues?.get(1)?.toFloatOrNull()?.coerceIn(-24f, 24f) ?: 0f
            syncNative()
            save()
            notifyChanged()
            return
        }

        for (index in mutableBands.indices) {
            mutableBands[index] = parsed.getOrNull(index)?.copy(enabled = true)
                ?: mutableBands[index].copy(gainDb = 0f, enabled = false)
        }
        bandCount = parsed.size
        val preamp = Regex("""(?i)Preamp\s*:\s*([+-]?[0-9.]+)\s*dB""").find(text)
        preampDb = preamp?.groupValues?.get(1)?.toFloatOrNull()?.coerceIn(-24f, 24f) ?: 0f
        syncNative()
        save()
        notifyChanged()
    }

    private fun restore() {
        val saved = preferences.getString("state", null) ?: return
        runCatching { loadJson(JSONObject(saved)) }
    }

    private fun configureBandLayout(count: Int) {
        val step = (20_000f / 20f).pow(1f / count)
        var lowerFrequency = 20f
        for (index in 0 until count) {
            val upperFrequency = lowerFrequency * step
            val centerFrequency = (lowerFrequency + upperFrequency) * 0.5f
            val bandwidth = upperFrequency - lowerFrequency
            mutableBands[index] = mutableBands[index].copy(
                frequencyHz = centerFrequency,
                q = centerFrequency / bandwidth,
            )
            lowerFrequency = upperFrequency
        }
    }

    private fun migrateLegacyDefaultLayout() {
        if (bandCount != 10) return
        val stillUsingOldGrid = mutableBands.take(bandCount).withIndex().all { (index, band) ->
            val oldFrequency = 20f * 1000f.pow(index / 31f)
            kotlin.math.abs(band.frequencyHz - oldFrequency) < 0.1f && kotlin.math.abs(band.q - 1.4f) < 0.001f
        }
        if (stillUsingOldGrid) {
            configureBandLayout(bandCount)
            save()
        }
    }

    private fun syncNative() {
        for (index in mutableBands.indices) syncNativeBand(index)
        native.setPreamp(preampDb)
        native.setBypass(bypass)
    }

    private fun syncNativeBand(index: Int) {
        val band = mutableBands[index]
        val soloBand = mutableBands.indices.firstOrNull { index ->
            index < bandCount && mutableBands[index].solo && mutableBands[index].enabled
        } ?: -1
        val soloAllowsBand = soloBand < 0 || soloBand == index
        native.setBand(index, band.copy(enabled = index < bandCount && band.enabled && soloAllowsBand))
    }

    private fun save() {
        preferences.edit {
            putString("state", toJson().toString())
        }
    }

    private fun notifyChanged() = changeListeners.toList().forEach { it() }
}
