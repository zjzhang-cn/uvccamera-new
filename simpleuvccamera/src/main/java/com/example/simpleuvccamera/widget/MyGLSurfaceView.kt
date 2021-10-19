package com.example.simpleuvccamera.widget

import android.content.Context
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import android.util.Log
import java.nio.ByteBuffer

class MyGLSurfaceView(context: Context, attributeSet: AttributeSet?) : GLSurfaceView(context, attributeSet) {
    companion object {
        private const val TAG = "MyGLSurfaceView"
    }

    constructor(context: Context) : this(context, null)

    private val renderer: MyGLRenderer

    init {

        // Create an OpenGL ES 2.0 context
        setEGLContextClientVersion(2)

        renderer = MyGLRenderer()

        // Set the Renderer for drawing on the GLSurfaceView
        setRenderer(renderer)

        // Render the view only when there is a change in the drawing data
        renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
    }

    /**
     * 设置显示方向
     * @param degrees 显示旋转角度（逆时针），有效值是（0, 90, 180, and 270.）
     */
    fun setDisplayOrientation(degrees: Int) {
        renderer.setDisplayOrientation(degrees)
    }

    /**
     * 设置渲染的YUV数据的宽高
     * @param width 宽度
     * @param height 高度
     */
    fun setYuvDataSize(width: Int, height: Int) {
        Log.d(TAG, "setYuvDataSize $width * $height")
        renderer.setYuvDataSize(width, height)
    }

    /**
     * 填充预览YUV格式数据
     * @param yuvData yuv格式的数据
     * @param type YUV数据的格式 0 -> I420  1 -> NV12  2 -> NV21
     */
    fun feedData(buf: ByteBuffer) {
        renderer.feedData(buf)
        // 请求渲染新的YUV数据
        requestRender()
    }
}
