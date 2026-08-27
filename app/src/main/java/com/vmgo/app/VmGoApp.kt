package com.vmgo.app

import android.app.Application
import com.vmgo.app.core.NativeVmEngine
import com.vmgo.app.util.AppCrashHandler
import com.vmgo.app.util.AppLogger

class VmGoApp : Application() {

    override fun onCreate() {
        super.onCreate()

        // 1. Initialize persistent logger
        AppLogger.init(this)

        // 2. Install global crash and uncaught exception handler
        AppCrashHandler.install(this)

        // 3. Synchronize log file path with C++ native engine
        NativeVmEngine.syncLogFilePath()

        AppLogger.i("VmGoApp", "VM Go Application and Native Logger synchronized.")
    }
}
