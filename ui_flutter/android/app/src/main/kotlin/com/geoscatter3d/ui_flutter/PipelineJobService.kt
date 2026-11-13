package com.geoscatter3d.ui_flutter

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager

internal object PipelineNative {
    external fun create(): Long
    external fun start(handle: Long, source: ByteArray, bundle: ByteArray, points: Int, resume: Boolean): Long
    external fun poll(handle: Long, values: LongArray, stage: ByteArray, error: ByteArray): Int
    external fun cancel(handle: Long, task: Long): Int
}

internal object PipelineJobs {
    private var handle = 0L

    private inline fun <T> native(block: () -> T): T = try {
        block()
    } catch (error: LinkageError) {
        throw IllegalStateException("无法加载预处理库", error)
    }

    // Retain the handle for the process lifetime so a recreated Activity can read the last status.
    private fun pipeline(): Long {
        if (handle == 0L) {
            handle = native {
                System.loadLibrary("gs3d_pipeline_ffi")
                PipelineNative.create()
            }
            check(handle != 0L) { "无法创建预处理任务" }
        }
        return handle
    }

    @Synchronized
    fun start(source: String, bundle: String, points: Int, resume: Boolean): Long =
        native { PipelineNative.start(pipeline(), source.toByteArray(Charsets.UTF_8),
            bundle.toByteArray(Charsets.UTF_8), points, resume) }

    @Synchronized
    fun snapshot(): Map<String, Any> {
        val values = LongArray(4)
        val stage = ByteArray(32)
        val error = ByteArray(256)
        check(native { PipelineNative.poll(pipeline(), values, stage, error) } == 0) { "无法读取任务状态" }
        fun text(bytes: ByteArray): String {
            val end = bytes.indexOf(0).let { if (it < 0) bytes.size else it }
            return bytes.copyOfRange(0, end).toString(Charsets.UTF_8)
        }
        return mapOf("id" to values[0], "state" to values[1], "stage" to text(stage),
            "error" to text(error), "points" to values[2], "chunks" to values[3])
    }

    @Synchronized
    fun cancel(task: Long): Int = native { PipelineNative.cancel(pipeline(), task) }

    @Synchronized
    fun cancelRunning() {
        val status = snapshot()
        if (status["state"] == 1L) cancel(status["id"] as Long)
    }
}

class PipelineJobService : Service() {
    inner class LocalBinder : Binder() {
        val service: PipelineJobService get() = this@PipelineJobService
    }

    private val binder = LocalBinder()
    private val handler = Handler(Looper.getMainLooper())
    private var startupError: String? = null
    private var watching = false
    private var wakeLock: PowerManager.WakeLock? = null
    private val watch = object : Runnable {
        override fun run() {
            try {
                val status = PipelineJobs.snapshot()
                if (status["state"] == 1L) {
                    val stage = when (status["stage"]) {
                        "fingerprint" -> "校验输入"
                        "stats" -> "统计数据"
                        "points" -> "生成点块"
                        else -> "准备中"
                    }
                    getSystemService(NotificationManager::class.java).notify(1, notification(stage))
                    handler.postDelayed(this, 1000)
                } else {
                    watching = false
                    stopSelf()
                }
            } catch (_: Exception) {
                handler.postDelayed(this, 1000)
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                getSystemService(NotificationManager::class.java).createNotificationChannel(
                    NotificationChannel("pipeline", "点云预处理", NotificationManager.IMPORTANCE_LOW),
                )
            }
            val notice = notification("准备中")
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(1, notice, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
            } else {
                startForeground(1, notice)
            }
        } catch (error: Exception) {
            startupError = error.message ?: "无法启动后台服务"
            stopSelf()
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_NOT_STICKY

    override fun onBind(intent: Intent?): IBinder = binder

    @Suppress("WakelockTimeout")
    fun startJob(source: String, bundle: String, points: Int, resume: Boolean): Long {
        startupError?.let { throw IllegalStateException(it) }
        val task = PipelineJobs.start(source, bundle, points, resume)
        if (task > 0 && !watching) {
            try {
                // The service releases this in onDestroy, including Android's timeout callback.
                wakeLock = getSystemService(PowerManager::class.java)
                    .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "GeoScatter3D:Pipeline")
                    .apply { acquire() }
            } catch (error: Exception) {
                PipelineJobs.cancel(task)
                stopSelf()
                throw error
            }
            watching = true
            handler.post(watch)
        } else if (task <= 0 && PipelineJobs.snapshot()["state"] != 1L) {
            stopSelf()
        }
        return task
    }

    override fun onTimeout(startId: Int, fgsType: Int) {
        try { PipelineJobs.cancelRunning() } catch (_: Exception) { }
        stopSelf()
    }

    override fun onDestroy() {
        handler.removeCallbacks(watch)
        try { PipelineJobs.cancelRunning() } catch (_: Exception) { }
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
        super.onDestroy()
    }

    private fun notification(stage: String): Notification {
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, "pipeline")
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        return builder.setSmallIcon(android.R.drawable.stat_sys_upload)
            .setContentTitle("点云预处理")
            .setContentText(stage)
            .setContentIntent(open)
            .setOngoing(true)
            .build()
    }
}
