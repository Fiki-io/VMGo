package com.vmgo.app.model

import java.io.Serializable

data class VmConfig(
    val id: String = "slot_1",
    var name: String = "VM Slot 1",
    var osVersion: String = "Android 12.1 (ARM64)",
    var rootFsPath: String = "",
    var systemPath: String = "",
    var vendorPath: String = "",
    var dataPath: String = "",
    var apexPath: String = "",
    var socketDir: String = "",

    var displayWidth: Int = 1080,
    var displayHeight: Int = 1920,
    var displayDpi: Int = 480,
    var targetFps: Int = 60,

    var enableRoot: Boolean = true,
    var enableGapps: Boolean = true,
    var enableAudio: Boolean = true,
    var enableCamera: Boolean = true,
    var enableSensors: Boolean = true,
    var enableGps: Boolean = true,

    var brand: String = "Google",
    var model: String = "Pixel 4",
    var device: String = "marlin",
    var serial: String = "G020I5087DFLXCAH",
    var imei: String = "352931543119555",
    var macAddress: String = "95:22:EE:A1:7B:02"
) : Serializable
