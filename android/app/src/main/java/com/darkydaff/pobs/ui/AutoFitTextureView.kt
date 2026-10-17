package com.darkydaff.pobs.ui

import android.content.Context
import android.util.AttributeSet
import android.view.TextureView

class AutoFitTextureView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyle: Int = 0
) : TextureView(context, attrs, defStyle) {

    private var aspectRatioWidth = 9
    private var aspectRatioHeight = 16

    fun setAspectRatio(width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        if (aspectRatioWidth == width && aspectRatioHeight == height) return
        aspectRatioWidth = width
        aspectRatioHeight = height
        post { requestLayout() }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
        val width = MeasureSpec.getSize(widthMeasureSpec)
        val height = MeasureSpec.getSize(heightMeasureSpec)

        if (aspectRatioWidth == 0 || aspectRatioHeight == 0) {
            setMeasuredDimension(width, height)
        } else {
            val viewAspectRatio = width.toDouble() / height.toDouble()
            val targetAspectRatio = aspectRatioWidth.toDouble() / aspectRatioHeight.toDouble()

            if (viewAspectRatio > targetAspectRatio) {
                // Width is larger, fit to available height
                val newWidth = (height * targetAspectRatio).toInt()
                setMeasuredDimension(newWidth, height)
            } else {
                // Height is larger, fit to available width
                val newHeight = (width / targetAspectRatio).toInt()
                setMeasuredDimension(width, newHeight)
            }
        }
    }
}
