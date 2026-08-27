package com.vmgo.app.util

import android.content.Context
import com.vmgo.app.model.VmConfig
import java.io.File

object StorageUtil {

    fun getSlotsBaseDir(context: Context): File {
        val dir = File(context.filesDir, "vm_slots")
        if (!dir.exists()) dir.mkdirs()
        return dir
    }

    fun prepareSlotDirectories(context: Context, slotId: String): VmConfig {
        val baseSlot = File(getSlotsBaseDir(context), slotId)
        val rootFsDir = File(baseSlot, "rootfs").apply { mkdirs() }
        val systemDir = File(baseSlot, "system").apply { mkdirs() }
        val vendorDir = File(baseSlot, "vendor").apply { mkdirs() }
        val dataDir = File(baseSlot, "data").apply { mkdirs() }
        val apexDir = File(baseSlot, "apex").apply { mkdirs() }
        val socketDir = File(baseSlot, "dev").apply { mkdirs() }

        return VmConfig(
            id = slotId,
            name = "VM Slot ${slotId.replace("slot_", "")}",
            rootFsPath = rootFsDir.absolutePath,
            systemPath = systemDir.absolutePath,
            vendorPath = vendorDir.absolutePath,
            dataPath = dataDir.absolutePath,
            apexPath = apexDir.absolutePath,
            socketDir = socketDir.absolutePath
        )
    }

    fun deleteSlot(context: Context, slotId: String): Boolean {
        val slotDir = File(getSlotsBaseDir(context), slotId)
        return slotDir.deleteRecursively()
    }
}
