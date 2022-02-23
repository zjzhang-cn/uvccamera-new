package com.example.simpleuvccamera.log

import android.content.Context
import android.os.Environment
import android.os.Process.myPid
import android.widget.Toast
import java.io.File
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.*

class MyLogcatSaver {
    var process:Process? = null
    fun startup(context: Context){
        try {
            val logDirectory = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),"simpleuvccamera/logs")
            val logFile = File(logDirectory, "log_${SimpleDateFormat("yyyy-MM-dd_HH-mm-ss").format(
                Date()
            )}.txt")

            // create app folder

            // create app folder
            if (!logDirectory.exists()) {
                val mkdirs = logDirectory.mkdirs()
                if (!mkdirs){
                    Toast.makeText(context,"create logs folder failed",Toast.LENGTH_LONG).show()
                    return
                }
            }

            // clear the previous logcat and then write the new one to the file

            // clear the previous logcat and then write the new one to the file
            Runtime.getRuntime().exec("logcat -c")
            process = Runtime.getRuntime().exec("logcat --pid ${myPid()} -f $logFile")
        }catch (e:Throwable){
            Toast.makeText(context,"save log failed",Toast.LENGTH_LONG).show()
        }
    }

    fun teardown(){
        process?.destroy()
    }
}