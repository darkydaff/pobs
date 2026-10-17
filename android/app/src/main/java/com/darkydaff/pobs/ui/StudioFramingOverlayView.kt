package com.darkydaff.pobs.ui

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View
import android.view.animation.DecelerateInterpolator

class StudioFramingOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    var isGridEnabled: Boolean = false
        set(value) {
            field = value
            invalidate()
        }

    private val cornerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#8060A5FA") // Semi-transparent studio cyan
        style = Paint.Style.STROKE
        strokeWidth = 3f * resources.displayMetrics.density
        strokeCap = Paint.Cap.ROUND
    }

    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#26FFFFFF") // 15% opacity white
        style = Paint.Style.STROKE
        strokeWidth = 1f * resources.displayMetrics.density
    }

    private val centerCrossPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#4D38BDF8") // 30% opacity cyan
        style = Paint.Style.STROKE
        strokeWidth = 1.5f * resources.displayMetrics.density
        strokeCap = Paint.Cap.ROUND
    }

    private val focusPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#38BDF8") // High-visibility bright cyan
        style = Paint.Style.STROKE
        strokeWidth = 2f * resources.displayMetrics.density
    }

    private var focusX = -1f
    private var focusY = -1f
    private var focusRadius = 0f
    private var focusAlpha = 0
    private var focusAnimator: ValueAnimator? = null

    fun triggerFocusAnimation(x: Float, y: Float) {
        focusX = x
        focusY = y
        focusAnimator?.cancel()

        val startRadius = 48f * resources.displayMetrics.density
        val targetRadius = 24f * resources.displayMetrics.density

        focusAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 600
            interpolator = DecelerateInterpolator()
            addUpdateListener { anim ->
                val progress = anim.animatedValue as Float
                focusRadius = startRadius - (startRadius - targetRadius) * progress
                focusAlpha = ((1f - progress * 0.7f) * 255).toInt()
                invalidate()
            }
            start()
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0f || h <= 0f) return

        val dp = resources.displayMetrics.density
        val cornerLen = 16f * dp
        val margin = 8f * dp

        // 1. Draw Cinema Corner Brackets (Top-Left, Top-Right, Bottom-Left, Bottom-Right)
        // Top-Left
        canvas.drawLine(margin, margin + cornerLen, margin, margin, cornerPaint)
        canvas.drawLine(margin, margin, margin + cornerLen, margin, cornerPaint)

        // Top-Right
        canvas.drawLine(w - margin - cornerLen, margin, w - margin, margin, cornerPaint)
        canvas.drawLine(w - margin, margin, w - margin, margin + cornerLen, cornerPaint)

        // Bottom-Left
        canvas.drawLine(margin, h - margin - cornerLen, margin, h - margin, cornerPaint)
        canvas.drawLine(margin, h - margin, margin + cornerLen, h - margin, cornerPaint)

        // Bottom-Right
        canvas.drawLine(w - margin - cornerLen, h - margin, w - margin, h - margin, cornerPaint)
        canvas.drawLine(w - margin, h - margin - cornerLen, w - margin, h - margin, cornerPaint)

        // 2. Draw Rule-of-Thirds Grid & Center Crosshair if enabled
        if (isGridEnabled) {
            // Horizontal lines
            canvas.drawLine(0f, h / 3f, w, h / 3f, gridPaint)
            canvas.drawLine(0f, h * 2f / 3f, w, h * 2f / 3f, gridPaint)

            // Vertical lines
            canvas.drawLine(w / 3f, 0f, w / 3f, h, gridPaint)
            canvas.drawLine(w * 2f / 3f, 0f, w * 2f / 3f, h, gridPaint)

            // Center Crosshair
            val cx = w / 2f
            val cy = h / 2f
            val crossSize = 10f * dp
            canvas.drawLine(cx - crossSize, cy, cx + crossSize, cy, centerCrossPaint)
            canvas.drawLine(cx, cy - crossSize, cx, cy + crossSize, centerCrossPaint)
        }

        // 3. Draw Tap-to-Focus Ring
        if (focusX >= 0f && focusY >= 0f && focusAlpha > 0) {
            focusPaint.alpha = focusAlpha
            canvas.drawCircle(focusX, focusY, focusRadius, focusPaint)

            val pipLen = 6f * dp
            canvas.drawLine(focusX, focusY - focusRadius - pipLen, focusX, focusY - focusRadius, focusPaint)
            canvas.drawLine(focusX, focusY + focusRadius, focusX, focusY + focusRadius + pipLen, focusPaint)
            canvas.drawLine(focusX - focusRadius - pipLen, focusY, focusX - focusRadius, focusY, focusPaint)
            canvas.drawLine(focusX + focusRadius, focusY, focusX + focusRadius + pipLen, focusY, focusPaint)
        }
    }
}
