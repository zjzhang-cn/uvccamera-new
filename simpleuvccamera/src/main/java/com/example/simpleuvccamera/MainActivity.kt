package com.example.simpleuvccamera

import android.Manifest
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.lifecycle.Lifecycle
import com.example.simpleuvccamera.widget.AspectRatioFrameLayout
import com.example.simpleuvccamera.widget.MyGLSurfaceView
import com.serenegiant.usb.DeviceFilter
import com.serenegiant.usb.IFrameCallback
import com.serenegiant.usb.USBMonitor
import com.serenegiant.usb.UVCCamera
import java.nio.ByteBuffer

class MainActivity : AppCompatActivity(), IFrameCallback {
    private var mUSBMonitor: USBMonitor? = null
    private var mUVCCamera: UVCCamera? = null
    private lateinit var glSurfaceView: MyGLSurfaceView
    lateinit var container: AspectRatioFrameLayout
    private lateinit var frameNumbView: TextView
    private lateinit var handler: Handler
    var frameNB = 0
    private var mStart = false
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        var t = HandlerThread("uvc-camera")
        t.start()
        handler = Handler(t.looper)

        container = findViewById(R.id.texture_container)
        glSurfaceView = findViewById(R.id.my_gl_surface_view)
        frameNumbView = findViewById(R.id.tv_video_frames_nb)

        container.setAspectSize(1280, 720)
        var hasPermission = true
        if (Build.VERSION.SDK_INT>=28) {
            if (ActivityCompat.checkSelfPermission(this,
                    Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED ||
                ActivityCompat.checkSelfPermission(this,
                    Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED)
            {
                hasPermission = false
                ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.CAMERA,Manifest.permission.RECORD_AUDIO),111)
            }
        }
        if (hasPermission){
            initUSBMonitor()
        }else{
            Toast.makeText(this,"No camera permission.",Toast.LENGTH_SHORT).show()
        }
    }

    private fun initUSBMonitor() {
        mUSBMonitor = USBMonitor(this, object : SimpleUSBMonitorListener() {
            override fun onAttach(device: UsbDevice) {
                Log.i(TAG, "onAttach:" + device.deviceName)
                val devices = mUSBMonitor?.deviceList?:ArrayList()
                if (devices.contains(device)) {
                    Log.i(TAG, "device list contains device $device. requestPermission.")
                    mUSBMonitor?.requestPermission(device)
                } else {
                    Log.i(TAG, "device list not contains device :$device")
                }
            }

            override fun onDettach(device: UsbDevice) {
                Log.i(TAG,"onDettach")
                handler.post {
                    mUVCCamera?.stopPreview()
                    mUVCCamera?.destroy()
                }
            }

            override fun onConnect(
                device: UsbDevice,
                ctrlBlock: USBMonitor.UsbControlBlock,
                createNew: Boolean
            ) {
                Log.i(TAG,"onConnect")
                mStart = true
                runOnUiThread {
                    if (lifecycle.currentState.isAtLeast(Lifecycle.State.CREATED)) {
                        frameNumbView.setText("preview start...")
                        handler.post {
                            openDevice(device,ctrlBlock,createNew)
                        }
                    }
                }
                glSurfaceView.setYuvDataSize(1280, 720)
                glSurfaceView.setDisplayOrientation(90)
            }
        })
        mUSBMonitor?.setDeviceFilter(DeviceFilter.getDeviceFilters(this, R.xml.device_filter))
        mUSBMonitor?.register()
    }

    private fun openDevice(device: UsbDevice, ctrlBlock: USBMonitor.UsbControlBlock, createNew: Boolean) {
        val devices = mUSBMonitor?.deviceList?:ArrayList()
        if (devices.contains(device)) {
        } else {
            Log.i(TAG, "device list not contains device $device")
            return
        }
        if (!createNew){
            val camera = mUVCCamera
            if (camera != null){
                Log.w(TAG,"resuse a device...");
                Log.i(TAG, "setPreviewSize ...")
                camera.setPreviewSize(1280, 720, UVCCamera.PIXEL_FORMAT_YUV420SP)
                Log.i(TAG, "setFrameCallback ...")
                camera.setFrameCallback(this@MainActivity, UVCCamera.PIXEL_FORMAT_YUV420SP)
                Log.i(TAG, "startPreview ...")
                camera.startPreview()
                Log.i(TAG, "startPreview done...")
                return
            }else{
                throw IllegalStateException("resuse a device with null camera...")
            }
        }
        try {
            val camera = UVCCamera()
            Log.i(TAG, "open camera...")
            camera.open(ctrlBlock)
            mUVCCamera = camera
            Log.i(TAG, "setPreviewSize ...")
            camera.setPreviewSize(1280, 720, UVCCamera.PIXEL_FORMAT_YUV420SP)
            Log.i(TAG, "setFrameCallback ...")
            camera.setFrameCallback(this@MainActivity, UVCCamera.PIXEL_FORMAT_YUV420SP)
            Log.i(TAG, "startPreview ...")
            camera.startPreview()
            Log.i(TAG, "startPreview done...")
        } catch (e: Throwable) {
            runOnUiThread { frameNumbView.setText("err:${e.message}") }
        }
    }


    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 111){
            if (ActivityCompat.checkSelfPermission(this,
                    Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED ||
                ActivityCompat.checkSelfPermission(this,
                    Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED){
//                Toast.makeText(this,"Please restart app",Toast.LENGTH_LONG).show()
                recreate()
            }
        }
    }

    override fun onDestroy() {
        mUSBMonitor?.unregister()
        mUSBMonitor?.destroy()
        handler.post {
            mUVCCamera?.stopPreview()
            mUVCCamera?.destroy()
        }
        super.onDestroy()
    }

    override fun onFrame(frame: ByteBuffer) {
        frameNB++
        glSurfaceView!!.feedData(frame)
        if (mStart) runOnUiThread { frameNumbView.text = "recv $frameNB frames" }
    }

    companion object {
        const val TAG = "MainActivity"
    }

    fun onEjectCamera(view: android.view.View) {
        handler.post {
            if (mStart) {
                mUVCCamera?.stopPreview()
                mStart = false

                runOnUiThread {
                    Toast.makeText(this@MainActivity,getString(R.string.camera_safely_ejected_now),Toast.LENGTH_SHORT).
                    show()
                }
            }
            else {
                mStart = true
                mUVCCamera?.startPreview()
            }
        }
    }
}