package com.example.simpleuvccamera

import android.Manifest
import android.content.DialogInterface
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageFormat.NV21
import android.graphics.Rect
import android.graphics.YuvImage
import android.hardware.usb.UsbDevice
import android.net.Uri
import android.os.*
import android.util.DisplayMetrics
import android.util.Log
import android.view.View
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.fragment.app.FragmentResultListener
import androidx.lifecycle.Lifecycle
import com.example.simpleuvccamera.fragment.CameraInfoFragment
import com.example.simpleuvccamera.log.MyLogcatSaver
import com.example.simpleuvccamera.widget.AspectRatioFrameLayout
import com.example.simpleuvccamera.widget.MyGLSurfaceView
import com.serenegiant.usb.DeviceFilter
import com.serenegiant.usb.IFrameCallback
import com.serenegiant.usb.USBMonitor
import com.serenegiant.usb.UVCCamera
import com.serenegiant.utils.FrameRateStat
import com.tsinglink.android.library.YuvLib
import com.tsinglink.android.library.freetype.TextDraw
import org.json.JSONArray
import org.json.JSONObject
import timber.log.Timber
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.text.SimpleDateFormat
import java.util.*
import kotlin.collections.ArrayList

class MainActivity : AppCompatActivity(), IFrameCallback {

    private var requestTakingPicture: Boolean = false
    private var requestTakingPictureMillis:Long = 0
    private var mUSBMonitor: USBMonitor? = null
    private var mUVCCamera: UVCCamera? = null
    private var logSaver:MyLogcatSaver? = null
    private var textDraw = TextDraw()
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
        setSupportActionBar(findViewById(R.id.toolbar))

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

        if (ActivityCompat.checkSelfPermission(this,Manifest.permission.WRITE_EXTERNAL_STORAGE)
            == PackageManager.PERMISSION_GRANTED){
            logSaver = MyLogcatSaver().apply {
                startup(applicationContext)
            }
            Toast.makeText(this,getString(R.string.toast_log_path),Toast.LENGTH_SHORT).show()
        }

        findViewById<View>(R.id.take_picture).setOnClickListener {
            onTakePicture(it)
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
                    mUVCCamera?.apply {
                        stopPreview(this)
                    }
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
                startPreview(camera)
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
            startPreview(camera)

        } catch (e: Throwable) {
            runOnUiThread { frameNumbView.setText("err:${e.message}") }
        }
    }

    private fun startPreview(camera: UVCCamera) {
        Timber.i("setPreviewSize with width:%d,height:%d,format:%d...",width,height,formatIndex)
        camera.setPreviewSize(width, height, formatIndex)
        Timber.i("setFrameCallback ...")
        camera.setFrameCallback(this@MainActivity)
        Timber.i("startPreview ...")
        camera.startPreview()
        Timber.i("startPreview done...")
        if (File("/system/fonts/NotoSansCJK-Regular.ttc").exists()) {
            textDraw.init("/system/fonts/NotoSansCJK-Regular.ttc", 40, 0.0f)
        }
    }

    private fun stopPreview(camera: UVCCamera){
        camera.stopPreview()
        textDraw.release()
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
        }else if (requestCode == 112){
            if (grantResults.size >= 2 && grantResults[0] == PackageManager.PERMISSION_GRANTED &&
                grantResults[1] == PackageManager.PERMISSION_GRANTED){
                logSaver = MyLogcatSaver().apply {
                    startup(applicationContext)
                }
                Toast.makeText(this,getString(R.string.toast_log_path),Toast.LENGTH_SHORT).show()
            }
        }
    }

    override fun onDestroy() {
        mUSBMonitor?.unregister()
        mUSBMonitor?.destroy()
        handler.post {
            mUVCCamera?.apply {
                stopPreview(this)
            }
            mUVCCamera?.destroy()
        }
        logSaver?.teardown()
        logSaver = null
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
        textDraw.drawBf("Hello,UVC摄像头.${SimpleDateFormat("MM:ss").format(Date())}",20,60,
            frame,width,height)
        if (requestTakingPicture){
            try {
                onTakePictureInterval(frame)
            }catch (e:Throwable){
                e.printStackTrace()
            }
            requestTakingPicture = false
        }
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
                mUVCCamera?.apply {
                    stopPreview(this)
                }
                mStart = false

                runOnUiThread {
                    Toast.makeText(this@MainActivity,getString(R.string.camera_safely_ejected_now),Toast.LENGTH_SHORT).
                    show()
                }
            }
            else {
                mStart = true
                mUVCCamera?.apply {
                    startPreview(this)
                }
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
                            stopPreview(camera)
                            startPreview(camera)
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

    fun onSaveLog(view: View) {
        if (logSaver != null){
            AlertDialog.Builder(this).setMessage(getString(R.string.log_describe))
                .setPositiveButton(android.R.string.ok,null).show()
            return
        }
        AlertDialog.Builder(this).setMessage(getString(R.string.save_log_request))
        .setPositiveButton(android.R.string.ok
        ) { _, _ ->
            if (ActivityCompat.checkSelfPermission(
                    this,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                ) != PackageManager.PERMISSION_GRANTED
                || ActivityCompat.checkSelfPermission(
                    this,
                    Manifest.permission.READ_EXTERNAL_STORAGE
                ) != PackageManager.PERMISSION_GRANTED
            ) {
                ActivityCompat.requestPermissions(
                    this,
                    arrayOf(Manifest.permission.WRITE_EXTERNAL_STORAGE,
                        Manifest.permission.READ_EXTERNAL_STORAGE),
                    112
                )
            } else {

            }
        }.setNegativeButton(android.R.string.cancel,null).show()
    }

    fun onTakePicture(view: View) {
        view.isEnabled = false
        requestTakingPicture = true
        requestTakingPictureMillis = SystemClock.elapsedRealtime()
        view.postDelayed({
             view.isEnabled = true
        },3000)
    }

    private fun onTakePictureInterval(frame: ByteBuffer) {
        val width = width
        val height = height
        if (width == 0 || height == 0
            || SystemClock.currentThreadTimeMillis() - requestTakingPictureMillis >= 2000){ //
                runOnUiThread {
                    findViewById<View>(R.id.take_picture).isEnabled = true
                }
            return
        }
        val buf = ByteArray(frame.capacity())
        frame.get(buf)
        val nv21 = ByteArray(frame.capacity())
        YuvLib.ConvertFromI420(buf, nv21, width, height, 2)
        val yuvImage = YuvImage(nv21, NV21, width, height, null)
        val privateJpeg = File(getExternalFilesDir(Environment.DIRECTORY_PICTURES), "tmp.jpg")
        FileOutputStream(privateJpeg).use {
            yuvImage.compressToJpeg(Rect(0, 0, width, height), 80, it)
        }
        runOnUiThread {
            findViewById<View>(R.id.take_picture).isEnabled = true
            val imageView = ImageView(this)
            val lpwidth = (resources.displayMetrics.widthPixels*0.75).toInt()
            val lpheight = (lpwidth * height * 1.0/width).toInt()
            val lp = FrameLayout.LayoutParams(lpwidth,lpheight)
            imageView.layoutParams = lp
            imageView.setImageURI(Uri.fromFile(privateJpeg))
            AlertDialog.Builder(this).setView(imageView).show()
        }
    }
}