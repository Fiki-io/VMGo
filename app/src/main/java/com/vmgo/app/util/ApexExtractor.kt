package com.vmgo.app.util

import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.util.zip.ZipInputStream

object ApexExtractor {

    /**
     * Unpacks all .apex and .capex files from system/apex into the slot's apex/ directory
     * This prepares the ART runtime (libart.so, linker64) for Android 10/11/12+ GSIs.
     */
    fun extractApexModules(systemDir: File, apexTargetDir: File, onProgress: ((String) -> Unit)? = null) {
        val systemApexDir = File(systemDir, "apex")
        if (!systemApexDir.exists() || !systemApexDir.isDirectory) return

        val apexFiles = systemApexDir.listFiles()?.filter {
            it.name.endsWith(".apex") || it.name.endsWith(".capex")
        } ?: return

        for (apexFile in apexFiles) {
            val moduleName = apexFile.name
                .removeSuffix(".apex")
                .removeSuffix(".capex")

            val moduleTargetDir = File(apexTargetDir, moduleName).apply { mkdirs() }
            onProgress?.invoke("Extracting APEX module: $moduleName...")

            try {
                unzipApex(apexFile, moduleTargetDir)
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    private fun unzipApex(zipFile: File, targetDir: File) {
        ZipInputStream(FileInputStream(zipFile)).use { zis ->
            var entry = zis.nextEntry
            val buffer = ByteArray(32 * 1024)

            while (entry != null) {
                val outFile = File(targetDir, entry.name)

                // Prevent path traversal
                if (!outFile.canonicalPath.startsWith(targetDir.canonicalPath)) {
                    entry = zis.nextEntry
                    continue
                }

                if (entry.isDirectory) {
                    outFile.mkdirs()
                } else {
                    outFile.parentFile?.mkdirs()
                    FileOutputStream(outFile).use { fos ->
                        var len: Int
                        while (zis.read(buffer).also { len = it } > 0) {
                            fos.write(buffer, 0, len)
                        }
                    }
                    if (entry.name.contains("bin/") || entry.name.endsWith(".so")) {
                        outFile.setExecutable(true, false)
                    }
                }
                zis.closeEntry()
                entry = zis.nextEntry
            }
        }
    }
}
