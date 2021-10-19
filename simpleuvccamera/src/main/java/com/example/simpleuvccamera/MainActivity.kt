package com.example.simpleuvccamera

import android.hardware.usb.UsbDevice
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.Lifecycle
import com.example.simpleuvccamera.widget.AspectRatioFrameLayout
import com.example.simpleuvccamera.widget.MyGLSurfaceView
import com.serenegiant.usb.DeviceFilter
import com.serenegiant.usb.IFrameCallback
import com.serenegiant.usb.USBMonitor
import com.serenegiant.usb.UVCCamera
import java.nio.ByteBuffer

class MainActivity : AppCompatActivity(), IFrameCallback {
    private lateinit var mUSBMonitor: USBMonitor
    private var mUVCCamera: UVCCamera? = null
    private lateinit var glSurfaceView: MyGLSurfaceView
    lateinit var container: AspectRatioFrameLayout
    private lateinit var frameNumbView: TextView
    var frameNB = 0
    private var mStart = false
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        container = findViewById(R.id.texture_container)
        glSurfaceView = findViewById(R.id.my_gl_surface_view)
        frameNumbView = findViewById(R.id.tv_video_frames_nb)
        mUSBMonitor = USBMonitor(this, object : SimpleUSBMonitorListener() {
            override fun onAttach(device: UsbDevice) {
                Log.i(TAG, "onAttach:" + device.deviceName)
                val devices = mUSBMonitor.deviceList
                if (devices.contains(device)) {
                    Log.i(TAG, "device list contains device $device. requestPermission.")
                    mUSBMonitor.requestPermission(device)
                } else {
                    Log.i(TAG, "device list not contains device :$device")
                }
            }

            override fun onDettach(device: UsbDevice) {
                runOnUiThread{
                    mUVCCamera?.destroy()
                }
            }

            override fun onConnect(
                device: UsbDevice,
                ctrlBlock: USBMonitor.UsbControlBlock,
                createNew: Boolean
            ) {
                mStart = true
                runOnUiThread {
                    if (lifecycle.currentState.isAtLeast(Lifecycle.State.CREATED)) {
                        val devices = mUSBMonitor.deviceList
                        if (devices.contains(device)) {
                        } else {
                            Log.i(TAG, "device list not contains device $device")
                            return@runOnUiThread
                        }
                        try {
                            frameNumbView.setText("preview start...")
                            val camera = UVCCamera()
                            camera.open(ctrlBlock)
                            mUVCCamera = camera
                            camera.setPreviewSize(1280, 720, UVCCamera.PIXEL_FORMAT_YUV420SP)
                            camera.setFrameCallback(
                                this@MainActivity,
                                UVCCamera.PIXEL_FORMAT_YUV420SP
                            )
                            camera.startPreview()
                        }catch (e:Throwable){
                            frameNumbView.setText("err:${e.message}")
                        }
                    }
                }
                glSurfaceView.setYuvDataSize(1280, 720)
                glSurfaceView.setDisplayOrientation(90)
            }

            override fun onDisconnect(device: UsbDevice, ctrlBlock: USBMonitor.UsbControlBlock) {
                super.onDisconnect(device, ctrlBlock)
                mStart = false
                runOnUiThread { frameNumbView.setText("preview stop") }
            }
        })
        mUSBMonitor.setDeviceFilter(DeviceFilter.getDeviceFilters(this, R.xml.device_filter))
        mUSBMonitor.register()
        container.setAspectSize(1280, 720)
    }

    override fun onDestroy() {
        mUSBMonitor.unregister()
        mUSBMonitor.destroy()
        mUVCCamera?.stopPreview()
        mUVCCamera?.destroy()
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
}