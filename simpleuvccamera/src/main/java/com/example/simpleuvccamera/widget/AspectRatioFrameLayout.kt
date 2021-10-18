package com.example.simpleuvccamera.widget

import android.content.Context
import android.util.AttributeSet
import android.widget.FrameLayout

class AspectRatioFrameLayout : FrameLayout {
    private var aspectHeight: Int = 0
    private var aspectWidth: Int = 0
    private var videoAspectRatio = 0f

    constructor(context: Context?) : super(context!!) {}
    constructor(context: Context?, attrs: AttributeSet?) : super(context!!, attrs) {}

    /**
     * Set the aspect ratio that this view should satisfy.
     *
     * @param widthHeightRatio The width to height ratio.
     */
    fun setAspectSize(width: Int, height: Int) {
        aspectWidth = width
        aspectHeight = height
        if (width == 0 || height == 0) {
            if (videoAspectRatio != 0f) {
                videoAspectRatio = 0f
                requestLayout()
            }
        } else {
            val aspectRatio = width * 1f / height
            if (videoAspectRatio != aspectRatio) {
                videoAspectRatio = aspectRatio
                requestLayout()
            }
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
        if (videoAspectRatio == 0f) {
            // Aspect ratio not set.
            return
        }

        var width = measuredWidth
        var height = measuredHeight
        var widthSpec = MeasureSpec.getMode(widthMeasureSpec)
        var heightSpec = MeasureSpec.getMode(heightMeasureSpec)

        if (width != 0 && height != 0) {
            val viewAspectRatio = width.toFloat() / height
            val aspectDeformation = videoAspectRatio / viewAspectRatio - 1
            if (Math.abs(aspectDeformation) <= MAX_ASPECT_RATIO_DEFORMATION_FRACTION) {
                // We're within the allowed tolerance.
                return
            }
        }
        if (widthSpec == heightSpec) {
            when (widthSpec) {
                MeasureSpec.UNSPECIFIED -> {
                    width = aspectWidth
                    height = aspectHeight
                }
                MeasureSpec.AT_MOST -> {
                    if (height != 0) {
                        var ratio = width * 1f / height
                        if (ratio > videoAspectRatio) {
//                            height = height
                            width = (height * videoAspectRatio).toInt()
                        } else {
                            height = (width / videoAspectRatio).toInt()
                        }
                    }
                }
                MeasureSpec.EXACTLY -> {
                    // 保持原比例不变.
                }
            }
        } else when (widthSpec) {
            MeasureSpec.EXACTLY -> {
                height = when (heightSpec) {
                    MeasureSpec.UNSPECIFIED -> {
                        //
                        (width / videoAspectRatio).toInt()
                    }
                    else -> {
                        kotlin.math.min((width / videoAspectRatio).toInt(), height)
                    }
                }
            }
            MeasureSpec.UNSPECIFIED -> {
                width = when (height) {
                    MeasureSpec.EXACTLY -> {
                        (height * videoAspectRatio).toInt()
                    }
                    else -> {
                        (height * videoAspectRatio).toInt()
                    }
                }
            }
            else -> {
                when (heightSpec) {
                    MeasureSpec.UNSPECIFIED -> {
                        //
                        height = (width / videoAspectRatio).toInt()
                    }
                    else -> {
                        width = kotlin.math.min((height * videoAspectRatio).toInt(), width)
                    }
                }
            }
        }
        super.onMeasure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY))
    }

    companion object {
        /**
         * The [FrameLayout] will not resize itself if the fractional difference between its natural
         * aspect ratio and the requested aspect ratio falls below this threshold.
         *
         *
         * This tolerance allows the view to occupy the whole of the screen when the requested aspect
         * ratio is very close, but not exactly equal to, the aspect ratio of the screen. This may reduce
         * the number of view layers that need to be composited by the underlying system, which can help
         * to reduce power consumption.
         */
        private const val MAX_ASPECT_RATIO_DEFORMATION_FRACTION = 0.01f
    }
}