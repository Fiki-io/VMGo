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
                withContext(Dispatchers.Main) { onProgress(10, "Preparing slot directories...") }
                val config = StorageUtil.prepareSlotDirectories(context, slotId)
                val tempDir = File(context.cacheDir, "gsi_import").apply { mkdirs() }
                val tempFile = File(tempDir, "temp_imported_gsi.img")

                withContext(Dispatchers.Main) { onProgress(25, "Reading GSI image...") }
                val inputStream: InputStream? = context.contentResolver.openInputStream(sourceUri)
                if (inputStream == null) {
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

                withContext(Dispatchers.Main) { onProgress(60, "Analyzing filesystem format...") }
                val targetRawFile = File(config.systemPath, "system.raw.img")

                val isSparse = NativeVmEngine.nativeIsSparseImage(tempFile.absolutePath)
                if (isSparse) {
                    withContext(Dispatchers.Main) { onProgress(75, "Converting Sparse image to raw ext4...") }
                    val success = NativeVmEngine.nativeUnsparseImage(
                        tempFile.absolutePath,
                        targetRawFile.absolutePath
                    )
                    if (!success) {
                        withContext(Dispatchers.Main) { onComplete(false, null) }
                        return@launch
                    }
                } else {
                    tempFile.copyTo(targetRawFile, overwrite = true)
                }
                tempFile.delete()

                // Extract APEX Modules for Android 10/11/12+ Runtimes
                withContext(Dispatchers.Main) { onProgress(85, "Extracting APEX runtime modules...") }
                ApexExtractor.extractApexModules(
                    systemDir = File(config.systemPath),
                    apexTargetDir = File(config.apexPath),
                    onProgress = { status ->
                        // Optional progress status
                    }
                )

                // Deploy Virtual Vendor & Root Overlays
                withContext(Dispatchers.Main) { onProgress(95, "Configuring Virtual Vendor & Overlays...") }
                OverlayManager.applyOverlays(context, config)

                withContext(Dispatchers.Main) {
                    onProgress(100, "GSI Imported Successfully!")
                    onComplete(true, config)
                }
            } catch (e: Exception) {
                e.printStackTrace()
                withContext(Dispatchers.Main) { onComplete(false, null) }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        serviceScope.cancel()
    }
}
