package com.vmgo.app.util

import android.content.Context
import com.vmgo.app.model.VmConfig
import java.io.File
import java.io.FileOutputStream

object OverlayManager {

    fun applyOverlays(context: Context, config: VmConfig) {
        val slotBase = File(config.systemPath).parentFile ?: return
        val overlayDir = File(slotBase, "overlays").apply { mkdirs() }

        // 1. Magisk / Root Overlay
        val xbinDir = File(config.systemPath, "xbin").apply { mkdirs() }
        val suFile = File(xbinDir, "su")

        if (config.enableRoot) {
            if (!suFile.exists()) {
                // Deploy lightweight standalone su wrapper
                suFile.writeText(
                    """
                    #!/system/bin/sh
                    # Standalone Root Daemon Shim for VM Go
                    export PATH=/system/bin:/system/xbin:${'$'}PATH
                    if [ "${'$'}1" = "-c" ]; then
                        shift
                        exec /system/bin/sh -c "${'$'}@"
                    else
                        exec /system/bin/sh
                    fi
                    """.trimIndent()
                )
                suFile.setExecutable(true, false)
                suFile.setReadable(true, false)
            }
        } else {
            if (suFile.exists()) {
                suFile.delete()
            }
        }

        // 2. Virtual Vendor Overlay Deployment
        deployVirtualVendor(context, config)
    }

    private fun deployVirtualVendor(context: Context, config: VmConfig) {
        val vendorDir = File(config.vendorPath).apply { mkdirs() }
        val etcDir = File(vendorDir, "etc/init/hw").apply { mkdirs() }

        copyAssetFile(context, "vendor_base/init.vphw.rc", File(etcDir, "init.vphw.rc"))
        copyAssetFile(context, "vendor_base/vendor.prop", File(vendorDir, "build.prop"))
        copyAssetFile(context, "vendor_base/ueventd.vphw.rc", File(vendorDir, "ueventd.rc"))
    }

    private fun copyAssetFile(context: Context, assetPath: String, targetFile: File) {
        try {
            if (!targetFile.exists()) {
                context.assets.open(assetPath).use { input ->
                    FileOutputStream(targetFile).use { output ->
                        input.copyTo(output)
                    }
                }
            }
        } catch (e: Exception) {
            // If asset not present, fallback
        }
    }
}
