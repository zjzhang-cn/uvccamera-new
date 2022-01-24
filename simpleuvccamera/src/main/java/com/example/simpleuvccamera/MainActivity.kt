package com.example.simpleuvccamera

import android.Manifest
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.fragment.app.FragmentResultListener
import androidx.lifecycle.Lifecycle
import com.example.simpleuvccamera.fragment.CameraInfoFragment
import com.example.simpleuvccamera.widget.AspectRatioFrameLayout
import com.example.simpleuvccamera.widget.MyGLSurfaceView
import com.serenegiant.usb.DeviceFilter
import com.serenegiant.usb.IFrameCallback
import com.serenegiant.usb.USBMonitor
import com.serenegiant.usb.UVCCamera
import com.serenegiant.utils.FrameRateStat
import org.json.JSONArray
import org.json.JSONObject
import timber.log.Timber
import java.nio.ByteBuffer

class MainActivity : AppCompatActivity(), IFrameCallback {

    private var mUSBMonitor: USBMonitor? = null
    private var mUVCCamera: UVCCamera? = null
    private lateinit var cameraInfo:JSONObject
    private lateinit var glSurfaceView: MyGLSurfaceView
    lateinit var container: AspectRatioFrameLayout
    private lateinit var frameNumbView: TextView
    private lateinit var handler: Handler
    var frameNB = 0
    private var mStart = false
    private var width = 1280
    private var height = 720
    private var formatIndex: Int = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        var t = HandlerThread("uvc-camera")
        t.start()
        handler = Handler(t.looper)

        container = findViewById(R.id.texture_container)
        glSurfaceView = findViewById(R.id.my_gl_surface_view)
        frameNumbView = findViewById(R.id.tv_video_frames_nb)

        container.setAspectSize(width, height)
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
                Timber.i("onAttach:" + device.deviceName)
                val devices = mUSBMonitor?.deviceList?:ArrayList()
                if (devices.contains(device)) {
                    Timber.i("device list contains device $device. requestPermission.")
                    mUSBMonitor?.requestPermission(device)
                } else {
                    Timber.i("device list not contains device :$device")
                }
            }

            override fun onDettach(device: UsbDevice) {
                Log.i(TAG,"onDettach")
                handler.post {
                    mUVCCamera?.stopPreview()
                    mUVCCamera?.destroy()
                }
                runOnUiThread {
                    findViewById<View>(R.id.camera_group).visibility = View.GONE
                    findViewById<View>(R.id.no_camera_group).visibility = View.VISIBLE
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
            }
        })
        mUSBMonitor?.setDeviceFilter(DeviceFilter.getDeviceFilters(this, R.xml.device_filter))
        mUSBMonitor?.register()
    }

    private fun openDevice(device: UsbDevice, ctrlBlock: USBMonitor.UsbControlBlock, createNew: Boolean) {
        val devices = mUSBMonitor?.deviceList?:ArrayList()
        if (devices.contains(device)) {
        } else {
            Timber.i("device list not contains device $device")
            return
        }
        if (!createNew){
            val camera = mUVCCamera
            if (camera != null){
                runOnUiThread {
                    glSurfaceView.setYuvDataSize(width, height)
                    glSurfaceView.setDisplayOrientation(0)
                }
                Log.w(TAG,"resuse a device...");
                Timber.i("setPreviewSize ...")
                camera.setPreviewSize(width, height,1)
                Timber.i("setFrameCallback ...")
                camera.setFrameCallback(this@MainActivity)
                Timber.i("startPreview ...")
                camera.startPreview()
                Timber.i("startPreview done...")
                return
            }else{
                throw IllegalStateException("resuse a device with null camera...")
            }
        }
        try {
            val camera = UVCCamera()
            Timber.i("open camera...")
            camera.open(ctrlBlock)
            mUVCCamera = camera
            var jsonStr = camera.cameraSpec
            if (jsonStr == "") throw IllegalStateException("cameraSpec not invalid.")
            Timber.i(jsonStr)
            cameraInfo = JSONObject(jsonStr)
            getDefaultWithHeightAndFormatFromCameraInfo(cameraInfo)
            runOnUiThread {
                glSurfaceView.setYuvDataSize(width, height)
                glSurfaceView.setDisplayOrientation(0)
                findViewById<View>(R.id.camera_group).visibility = View.VISIBLE
                findViewById<View>(R.id.no_camera_group).visibility = View.GONE
            }
            Timber.i("setPreviewSize ...")
            camera.setPreviewSize(width, height,formatIndex)
            Timber.i("setFrameCallback ...")
            camera.setFrameCallback(this@MainActivity)
            Timber.i("startPreview ...")
            camera.startPreview()
            Timber.i("startPreview done...")

        } catch (e: Throwable) {
            runOnUiThread { frameNumbView.setText("err:${e.message}") }
        }
    }

    private fun getDefaultWithHeightAndFormatFromCameraInfo(obj:JSONObject) {
        val formats = obj.getJSONArray("formats")
        if (formats.length() > 0){
            val fmt = formats[0] as JSONObject
            val index = fmt.getInt("index")
            val size = fmt.getJSONArray("size")
            val default = fmt.getInt("default")
            val resolution = size[default] as String
            val splite = resolution.split("x")
            var w = splite[0].toInt()
            var h = splite[1].toInt()
            width = w
            height = h
            formatIndex = index
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
        if (frame.capacity() != width*height*3/2){
            runOnUiThread { frameNumbView.text =
                "frame size ${frame.capacity()} not equal to ${width*height*3/2}. ignore"
            }
            return
        }
        frameNB++
        glSurfaceView!!.feedData(frame)
        if (mStart) runOnUiThread { frameNumbView.text =
            "recv $frameNB frames size:${frame.capacity()} FPS:(${FrameRateStat.stat("FrameCB")})"
        }
    }

    companion object {
        const val TAG = "MainActivity"
    }

    fun onEjectCamera(v: View) {
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

    fun onCameraInfo(view: View) {
        handler.post {
            val camera = mUVCCamera?:return@post
            try {
                var jsonStr = camera.cameraSpec
                if (jsonStr == "") throw IllegalStateException("cameraSpec not invalid.")
                Timber.i(jsonStr)
                cameraInfo = JSONObject(jsonStr)
                val formats = cameraInfo.getJSONArray("formats").toString()
                runOnUiThread {
                    supportFragmentManager.setFragmentResultListener("CameraInfo",this
                    ) { requestKey, result ->
                        var fmtIndex = result.getInt("formatIndex")
                        var resolution = result.getString("resolution")!!
                        val splite = resolution.split("x")
                        var w = splite[0].toInt()
                        var h = splite[1].toInt()
                        width = w
                        height = h
                        formatIndex = fmtIndex

                        handler.post {  // change camera's resolution..
                            camera.stopPreview()
                            camera.setPreviewSize(width, height,fmtIndex)
                            camera.startPreview()
                        }
                        glSurfaceView.setYuvDataSize(width, height)
                        glSurfaceView.setDisplayOrientation(0)
                    }
                    CameraInfoFragment.newInstance(formats,width,height,formatIndex)
                        .show(supportFragmentManager,null)
                }
            }catch (e:Throwable){

            }
        }
    }
}