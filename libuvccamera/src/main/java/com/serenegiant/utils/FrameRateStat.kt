package com.serenegiant.utils

import android.os.SystemClock
import android.util.Log
import java.util.*
import java.util.concurrent.ArrayBlockingQueue

/**
 * Created by John on 2017/5/12.
 */
object FrameRateStat {
    private val sMap: MutableMap<String, Queue<Long>> = HashMap()
    private val sLogMap: MutableMap<String, Long> = HashMap()

    fun stat(tag: String): Int {
        var numb = sMap[tag]
        if (numb == null) numb = ArrayBlockingQueue(100)
        sMap[tag] = numb
        while (true) {
            if (!numb.offer(SystemClock.elapsedRealtime())) {
                numb.poll()
            } else {
                break
            }
        }
        val interval = SystemClock.elapsedRealtime() - numb.peek()
        if (interval >= 500) {
            val v = numb.size * 1000 / interval
            var lastLogMillis = sLogMap[tag]
            if (lastLogMillis == null) lastLogMillis = 0L
            if (SystemClock.elapsedRealtime() - lastLogMillis >= 30000) {
                Log.i(tag,"%s frameRate:${numb.size}/${interval}=${v}")
                lastLogMillis = SystemClock.elapsedRealtime()
                sLogMap[tag] = lastLogMillis
            }
            return v.toInt()
        }
        return 1
    }

    fun clear(tag: String) {
        sMap.remove(tag)
        sLogMap.remove(tag)
    }
}