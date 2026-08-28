package com.vmgo.app.core

import android.view.Surface
import com.vmgo.app.VmGoApp
import com.vmgo.app.model.VmConfig
import com.vmgo.app.util.AppLogger

object NativeVmEngine {

    private var isLibraryLoaded = false

    init {
        try {
            System.loadLibrary("vmgo_engine")
            isLibraryLoaded = true
            AppLogger.i("NativeVmEngine", "Native library libvmgo_engine.so loaded successfully.")
        } catch (e: UnsatisfiedLinkError) {
            isLibraryLoaded = false
            AppLogger.fatal("NativeVmEngine", "CRITICAL ERROR: Failed to load libvmgo_engine.so native library!", e)
        } catch (e: Throwable) {
            isLibraryLoaded = false
            AppLogger.fatal("NativeVmEngine", "Unexpected error during native library loading.", e)
        }
    }

    fun syncLogFilePath() {
        if (isLibraryLoaded) {
            try {
                val logPath = AppLogger.getLogFilePath()
                if (logPath.isNotEmpty()) {
                    nativeSetLogFilePath(logPath)
                }
            } catch (e: Throwable) {
                AppLogger.e("NativeVmEngine", "Failed to sync log file path with native engine", e)
            }
        }
    }

    // JNI Native Declarations
    external fun nativeSetLogFilePath(logPath: String)

    external fun nativeInit(
        slotId: String,
        rootFsPath: String,
        systemPath: String,
        vendorPath: String,
        dataPath: String,
        apexPath: String,
        socketDir: String,
        nativeLibDir: String,
        width: Int,
        height: Int,
        dpi: Int,
        fps: Int,
        enableRoot: Boolean,
        enableGapps: Boolean
    ): Boolean

    external fun nativeSetSurface(surface: Surface?, width: Int, height: Int): Boolean
    external fun nativeDestroySurface()
    external fun nativeRenderFrame(pixels: ByteArray, width: Int, height: Int, format: Int)

    external fun nativeSendTouchEvent(
        action: Int,
        pIds: IntArray,
        pXs: FloatArray,
        pYs: FloatArray,
        pPressures: FloatArray,
        pSizes: FloatArray,
        pointerCount: Int
    )

    external fun nativeSendKeyEvent(keyCode: Int, isDown: Boolean)

    external fun nativeSendSensorUpdate(sensorName: String, v0: Float, v1: Float, v2: Float)
    external fun nativeSendGpsLocation(lat: Double, lon: Double, alt: Float, speed: Float, bearing: Float, timestamp: Long)

    external fun nativeUnsparseImage(srcSparsePath: String, dstRawPath: String): Boolean
    external fun nativeIsSparseImage(filePath: String): Boolean
    external fun nativeExtractExt4Image(imagePath: String, targetDir: String): Boolean
    external fun nativeIsExt4Image(imagePath: String): Boolean
    external fun nativeGetEngineVersion(): String
    external fun nativeStopVm()

    // Guest Process Launcher
    external fun nativeLaunchGuest(slotId: String): Boolean
    external fun nativeIsGuestAlive(): Boolean
    external fun nativeKillGuest()

    fun initializeVm(config: VmConfig): Boolean {
        if (!isLibraryLoaded) {
            AppLogger.fatal("NativeVmEngine", "Cannot initialize VM: Native library is not loaded!")
            return false
        }

        syncLogFilePath()

        val libDir = try {
            VmGoApp.instance.applicationInfo.nativeLibraryDir
        } catch (e: Throwable) {
            "/data/data/com.vmgo.app/lib"
        }

        AppLogger.i("NativeVmEngine", "Calling nativeInit for slot ${config.id} (${config.displayWidth}x${config.displayHeight})")
        val success = nativeInit(
            slotId = config.id,
            rootFsPath = config.rootFsPath,
            systemPath = config.systemPath,
            vendorPath = config.vendorPath,
            dataPath = config.dataPath,
            apexPath = config.apexPath,
            socketDir = config.socketDir,
            nativeLibDir = libDir,
            width = config.displayWidth,
            height = config.displayHeight,
            dpi = config.displayDpi,
            fps = config.targetFps,
            enableRoot = config.enableRoot,
            enableGapps = config.enableGapps
        )

        if (success) {
            AppLogger.i("NativeVmEngine", "VM initialized successfully.")
        } else {
            AppLogger.e("NativeVmEngine", "nativeInit returned false!")
        }

        return success
    }
}
