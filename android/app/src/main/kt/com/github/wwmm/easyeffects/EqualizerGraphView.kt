/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.github.wwmm.easyeffects

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import java.text.DecimalFormat
import java.text.DecimalFormatSymbols
import java.util.Locale
import kotlin.math.abs
import kotlin.math.log10

internal class EqualizerGraphView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {
    private val density = resources.displayMetrics.density
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.ee_grid)
        strokeWidth = 0.75f * density
        style = Paint.Style.STROKE
    }
    private val curvePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.ee_curve_left)
        strokeWidth = 3f * density
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        clearShadowLayer()
    }
    private val curveFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.ee_curve_left)
        alpha = 12
        style = Paint.Style.FILL
    }
    private val rightCurvePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.ee_curve_right)
        strokeWidth = 2f * density
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val pointPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.ee_curve_point)
        style = Paint.Style.FILL
    }
    private val selectedPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.ee_curve_selected)
        style = Paint.Style.FILL
    }
    private val selectedRingPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.ee_accent)
        style = Paint.Style.STROKE
        strokeWidth = 2f * density
    }
    private val responsePaths = Array(2) { Path() }
    private val responseCurves = Array(2) { FloatArray(128) }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = context.getColor(R.color.ee_text_muted)
        textSize = 11f * density
    }
    private var model: EqualizerModel? = null
    private var nativeEqualizer: NativeEqualizer? = null
    var selectedBand: Int = 0
        private set
    var onBandSelected: ((Int) -> Unit)? = null

    init {
        setBackgroundColor(context.getColor(R.color.ee_surface))
        minimumHeight = (250f * density).toInt()
        isLongClickable = true
    }

    fun connect(model: EqualizerModel, nativeEqualizer: NativeEqualizer) {
        this.model = model
        this.nativeEqualizer = nativeEqualizer
        updateResponseCurves()
        model.addChangeListener {
            updateResponseCurves()
            postInvalidateOnAnimation()
        }
        invalidate()
    }

    fun selectBand(index: Int) {
        val count = model?.bandCount ?: return
        selectedBand = index.coerceIn(0, count - 1)
        onBandSelected?.invoke(selectedBand)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val model = model ?: return
        labelPaint.textSize = if (width < 420f * density) 9f * density else 11f * density
        val left = 42f * density
        val right = width - 14f * density
        val top = 18f * density
        val bottom = height - 30f * density
        val plotWidth = right - left
        val plotHeight = bottom - top
        if (plotWidth <= 0f || plotHeight <= 0f) return

        for (db in -24..24 step 12) {
            val y = mapGain(db.toFloat(), top, bottom)
            if (db == 0) {
                gridPaint.alpha = 210
                gridPaint.strokeWidth = 1.25f * density
            } else {
                gridPaint.alpha = 105
                gridPaint.strokeWidth = 0.75f * density
            }
            canvas.drawLine(left, y, right, y, gridPaint)
            canvas.drawText("${db} dB", 2f * density, y + 4f * density, labelPaint)
        }
        // Use the same 1/3-octave frequency landmarks as the desktop spectrum.
        val ticks = listOf(20f, 37f, 70f, 132f, 247f, 462f, 866f, 1_600f, 3_000f, 5_700f, 10_700f)
        val locale = resources.configuration.locales[0] ?: Locale.getDefault()
        val kiloFormat = DecimalFormat("0.0", DecimalFormatSymbols.getInstance(locale))
        for (frequency in ticks) {
            val x = mapFrequency(frequency, left, right)
            canvas.drawLine(x, top, x, bottom, gridPaint)
            val label = if (frequency >= 1_000f) {
                "${kiloFormat.format(frequency / 1_000f)}k"
            } else {
                frequency.toInt().toString()
            }
            val labelWidth = labelPaint.measureText(label)
            val labelX = (x - labelWidth / 2f).coerceIn(0f, width - labelWidth)
            canvas.drawText(label, labelX, height - 7f * density, labelPaint)
        }
        val unit = context.getString(R.string.unit_hz)
        canvas.drawText(unit, width - labelPaint.measureText(unit) - 2f * density, height - 7f * density, labelPaint)

        val sampleRate = 48_000f
        for (channel in 0..1) {
            val curve = responseCurves[channel]
            val points = curve.lastIndex
            val path = responsePaths[channel]
            path.reset()
            for (index in curve.indices) {
                val ratio = index.toFloat() / points
                val response = curve[index].coerceIn(-24f, 24f)
                val x = left + ratio * plotWidth
                val y = mapGain(response, top, bottom)
                if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
            }
            if (channel == 0) {
                val fillPath = Path(path).apply {
                    lineTo(right, mapGain(0f, top, bottom))
                    lineTo(left, mapGain(0f, top, bottom))
                    close()
                }
                canvas.drawPath(fillPath, curveFillPaint)
            }
            canvas.drawPath(path, if (channel == 0) curvePaint else rightCurvePaint)
        }

        model.bands.take(model.bandCount).forEachIndexed { index, band ->
            val x = mapFrequency(band.frequencyHz, left, right)
            val channels = when (band.channelMode) { 1 -> intArrayOf(0); 2 -> intArrayOf(1); else -> intArrayOf(0, 1) }
            channels.forEach { channel ->
                val curve = responseCurves[channel]
                val position = (log10(band.frequencyHz / 20f) / log10(1000f)) * curve.lastIndex
                val low = position.toInt().coerceIn(0, curve.lastIndex)
                val high = (low + 1).coerceAtMost(curve.lastIndex)
                val response = curve[low] + (curve[high] - curve[low]) * (position - low)
                val y = mapGain(response, top, bottom)
                if (index == selectedBand) canvas.drawCircle(x, y, 8f * density, selectedRingPaint)
                canvas.drawCircle(x, y, (if (index == selectedBand) 5f else 3.5f) * density,
                    if (index == selectedBand) selectedPaint else pointPaint)
            }
        }
    }

    private fun updateResponseCurves() {
        val native = nativeEqualizer ?: return
        for (channel in 0..1) {
            responseCurves[channel] = native.responseCurve(responseCurves[channel].size, 48_000f, channel)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val model = model ?: return false
        val left = 42f * density
        val right = width - 14f * density
        val top = 18f * density
        val bottom = height - 30f * density
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                selectedBand = nearestBand(event.x, left, right)
                onBandSelected?.invoke(selectedBand)
                parent?.requestDisallowInterceptTouchEvent(true)
                invalidate()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                val band = model.bands[selectedBand]
                val frequencyRatio = ((event.x - left) / (right - left)).coerceIn(0f, 1f)
                val frequency = nativeEqualizer?.graphFrequencyFromX(frequencyRatio) ?: band.frequencyHz
                val targetResponse = (24f - ((event.y - top) / (bottom - top)) * 48f).coerceIn(-24f, 24f)
                val channel = if (band.channelMode == 2) 1 else 0
                val currentResponse = nativeEqualizer?.responseDb(frequency, 48_000f, channel) ?: band.gainDb
                val gain = nativeEqualizer?.graphGainForResponse(band.gainDb, currentResponse, targetResponse) ?: band.gainDb
                model.setBand(selectedBand, band.copy(frequencyHz = frequency, gainDb = gain))
                return true
            }
            MotionEvent.ACTION_UP -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                performClick()
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        onBandSelected?.invoke(selectedBand)
        return true
    }

    private fun nearestBand(x: Float, left: Float, right: Float): Int {
        val model = model ?: return 0
        val ratio = ((x - left) / (right - left)).coerceIn(0f, 1f)
        val target = nativeEqualizer?.graphFrequencyFromX(ratio) ?: 20f
        return model.bands.take(model.bandCount).indices.minByOrNull { index ->
            abs(log10(model.bands[index].frequencyHz) - log10(target))
        } ?: 0
    }

    private fun mapFrequency(frequency: Float, left: Float, right: Float): Float =
        left + (log10(frequency / 20f) / log10(1000f)) * (right - left)

    private fun mapGain(gain: Float, top: Float, bottom: Float): Float =
        top + ((24f - gain.coerceIn(-24f, 24f)) / 48f) * (bottom - top)
}
