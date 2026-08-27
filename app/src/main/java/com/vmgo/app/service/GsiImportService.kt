package com.vmgo.app.service

import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Binder
import android.os.IBinder
import com.vmgo.app.core.NativeVmEngine
import com.vmgo.app.model.VmConfig
import com.vmgo.app.util.ApexExtractor
import com.vmgo.app.util.AppLogger
import com.vmgo.app.util.OverlayManager
import com.vmgo.app.util.StorageUtil
import kotlinx.coroutines.*
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream

class GsiImportService : Service() {

    private val binder = LocalBinder()
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    inner class LocalBinder : Binder() {
        fun getService(): GsiImportService = this@GsiImportService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    fun importGsi(
        context: Context,
        sourceUri: Uri,
        slotId: String,
        onProgress: (Int, String) -> Unit,
        onComplete: (Boolean, VmConfig?) -> Unit
    ) {
        serviceScope.launch {
            try {
                withContext(Dispatchers.Main) { onProgress(5, "Preparing slot directories...") }
                val config = StorageUtil.prepareSlotDirectories(context, slotId)
                val tempDir = File(context.cacheDir, "gsi_import").apply { mkdirs() }
                val tempFile = File(tempDir, "temp_imported_gsi.img")

                withContext(Dispatchers.Main) { onProgress(20, "Reading GSI image...") }
                val inputStream: InputStream? = context.contentResolver.openInputStream(sourceUri)
                if (inputStream == null) {
                    AppLogger.e("GsiImportService", "Cannot open input stream for URI: $sourceUri")
                    withContext(Dispatchers.Main) { onComplete(false, null) }
                    return@launch
                }

                val outputStream = FileOutputStream(tempFile)
                val buffer = ByteArray(64 * 1024)
                var bytesRead: Int
                while (inputStream.read(buffer).also { bytesRead = it } != -1) {
                    outputStream.write(buffer, 0, bytesRead)
                }
                outputStream.flush()
                outputStream.close()
                inputStream.close()

                withContext(Dispatchers.Main) { onProgress(45, "Checking image format (Sparse vs Raw)...") }
                val rawImageFile = File(tempDir, "system_raw_temp.img")

                val isSparse = NativeVmEngine.nativeIsSparseImage(tempFile.absolutePath)
                if (isSparse) {
                    withContext(Dispatchers.Main) { onProgress(55, "Converting Sparse image to raw ext4...") }
                    val unsparseSuccess = NativeVmEngine.nativeUnsparseImage(
                        tempFile.absolutePath,
                        rawImageFile.absolutePath
                    )
                    tempFile.delete()
                    if (!unsparseSuccess) {
                        AppLogger.e("GsiImportService", "Failed to unsparse image")
                        withContext(Dispatchers.Main) { onComplete(false, null) }
                        return@launch
                    }
                } else {
                    tempFile.renameTo(rawImageFile)
                }

                // 3. Extract ext4 filesystem partitions and system binaries
                withContext(Dispatchers.Main) { onProgress(70, "Extracting ext4 filesystem (bin, lib64, framework)...") }
                val extractSuccess = NativeVmEngine.nativeExtractExt4Image(
                    rawImageFile.absolutePath,
                    config.rootFsPath
                )

                // Also populate system directory if it was a system-as-root image
                val rootfsSystem = File(config.rootFsPath, "system")
                val finalSystemDir = if (rootfsSystem.exists() && rootfsSystem.isDirectory) {
                    rootfsSystem
                } else {
                    File(config.systemPath)
                }

                // Also keep raw image file for block reference if needed
                val finalRawFile = File(config.rootFsPath, "system.raw.img")
                rawImageFile.renameTo(finalRawFile)

                if (!extractSuccess) {
                    AppLogger.w("GsiImportService", "Ext4 directory extraction had notices, continuing with raw image")
                }

                // 4. Extract APEX Modules for Android 10/11/12+ Runtimes
                withContext(Dispatchers.Main) { onProgress(85, "Extracting APEX runtime modules...") }
                ApexExtractor.extractApexModules(
                    systemDir = finalSystemDir,
                    apexTargetDir = File(config.apexPath),
                    onProgress = { status ->
                        // Optional APEX progress
                    }
                )

                // 5. Deploy Virtual Vendor & Root Overlays
                withContext(Dispatchers.Main) { onProgress(95, "Configuring Virtual Vendor & Overlays...") }
                OverlayManager.applyOverlays(context, config)

                withContext(Dispatchers.Main) {
                    onProgress(100, "GSI ROM Ready & Installed!")
                    onComplete(true, config)
                }
            } catch (e: Exception) {
                AppLogger.fatal("GsiImportService", "Error during GSI import", e)
                withContext(Dispatchers.Main) { onComplete(false, null) }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        serviceScope.cancel()
    }
}
