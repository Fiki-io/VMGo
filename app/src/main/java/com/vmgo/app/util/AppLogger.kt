package com.vmgo.app.util

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileWriter
import java.io.PrintWriter
import java.text.SimpleDateFormat
import java.util.*
import java.util.concurrent.Executors

object AppLogger {

    private const val TAG = "VMGo-Log"
    private const val MAX_LOG_FILE_SIZE = 5 * 1024 * 1024 // 5 MB max
    private val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)
    private val executor = Executors.newSingleThreadExecutor()

    private var logFile: File? = null
    private var isInitialized = false

    fun init(context: Context) {
        if (isInitialized) return
        val logDir = File(context.filesDir, "logs").apply { mkdirs() }
        logFile = File(logDir, "vmgo_system_logs.txt")
        isInitialized = true

        i("AppLogger", "=== VM Go System Log Session Started ===")
    }

    fun d(tag: String, message: String) {
        Log.d(tag, message)
        writeLogEntry("DEBUG", tag, message)
    }

    fun i(tag: String, message: String) {
        Log.i(tag, message)
        writeLogEntry("INFO", tag, message)
    }

    fun w(tag: String, message: String) {
        Log.w(tag, message)
        writeLogEntry("WARN", tag, message)
    }

    fun e(tag: String, message: String, throwable: Throwable? = null) {
        Log.e(tag, message, throwable)
        val fullMsg = if (throwable != null) {
            "$message\n${Log.getStackTraceString(throwable)}"
        } else {
            message
        }
        writeLogEntry("ERROR", tag, fullMsg)
    }

    fun fatal(tag: String, message: String, throwable: Throwable? = null) {
        Log.e(tag, "FATAL CRASH: $message", throwable)
        val fullMsg = if (throwable != null) {
            "FATAL CRASH: $message\n${Log.getStackTraceString(throwable)}"
        } else {
            "FATAL CRASH: $message"
        }
        writeLogEntry("FATAL", tag, fullMsg)
    }

    private fun writeLogEntry(level: String, tag: String, message: String) {
        val target = logFile ?: return
        val timeStr = dateFormat.format(Date())
        val logLine = "[$timeStr] [$level] [$tag]: $message\n"

        executor.execute {
            try {
                // Check file rotation if too large
                if (target.exists() && target.length() > MAX_LOG_FILE_SIZE) {
                    val backup = File(target.parentFile, "vmgo_system_logs_old.txt")
                    backup.delete()
                    target.renameTo(backup)
                }

                FileWriter(target, true).use { fw ->
                    fw.write(logLine)
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    fun readLogs(): String {
        val target = logFile ?: return "No log file initialized."
        return try {
            if (target.exists()) {
                target.readText()
            } else {
                "No logs recorded yet."
            }
        } catch (e: Exception) {
            "Error reading logs: ${e.message}"
        }
    }

    fun clearLogs(): Boolean {
        val target = logFile ?: return false
        return try {
            if (target.exists()) {
                target.delete()
            }
            target.createNewFile()
            i("AppLogger", "Logs cleared by user.")
            true
        } catch (e: Exception) {
            e.printStackTrace()
            false
        }
    }

    fun getLogFilePath(): String {
        return logFile?.absolutePath ?: ""
    }
}
