package com.vmgo.app.util

import android.content.Context
import android.os.Build
import android.os.Process
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.*
import kotlin.system.exitProcess

class AppCrashHandler private constructor(private val context: Context) : Thread.UncaughtExceptionHandler {

    private val defaultHandler = Thread.getDefaultUncaughtExceptionHandler()

    init {
        Thread.setDefaultUncaughtExceptionHandler(this)
    }

    companion object {
        fun install(context: Context) {
            AppCrashHandler(context)
        }
    }

    override fun uncaughtException(thread: Thread, throwable: Throwable) {
        try {
            saveCrashReport(thread, throwable)
        } catch (e: Exception) {
            e.printStackTrace()
        } finally {
            // Forward to default system handler or terminate cleanly
            defaultHandler?.uncaughtException(thread, throwable) ?: run {
                Process.killProcess(Process.myPid())
                exitProcess(10)
            }
        }
    }

    private fun saveCrashReport(thread: Thread, throwable: Throwable) {
        val sw = StringWriter()
        val pw = PrintWriter(sw)
        throwable.printStackTrace(pw)
        val stackTrace = sw.toString()

        val timeStr = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())

        val report = buildString {
            appendLine("==================== FATAL CRASH REPORT ====================")
            appendLine("Timestamp: $timeStr")
            appendLine("App Version: 1.0.0 (VM Go)")
            appendLine("Thread: ${thread.name} (ID: ${thread.id})")
            appendLine("Android OS: ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
            appendLine("Device: ${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE})")
            appendLine("Hardware ABI: ${Build.SUPPORTED_ABIS.joinToString(", ")}")
            appendLine("------------------------------------------------------------")
            appendLine("EXCEPTION: ${throwable.javaClass.name}: ${throwable.message}")
            appendLine("STACK TRACE:")
            appendLine(stackTrace)
            appendLine("============================================================")
        }

        // Write directly to AppLogger
        AppLogger.fatal("CrashHandler", report)

        // Also write standalone crash report file
        val crashDir = File(context.filesDir, "logs").apply { mkdirs() }
        val crashFile = File(crashDir, "last_fatal_crash.txt")
        crashFile.writeText(report)
    }
}
