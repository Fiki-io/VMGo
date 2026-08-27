package com.vmgo.app.model

enum class VmStatus {
    STOPPED,
    STARTING,
    RUNNING,
    PAUSED,
    ERROR
}

data class VmInstance(
    val config: VmConfig,
    var status: VmStatus = VmStatus.STOPPED,
    var lastBootTime: Long = 0L
)
