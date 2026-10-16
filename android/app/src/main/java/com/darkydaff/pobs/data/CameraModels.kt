package com.darkydaff.pobs.data

data class CameraCapability(
    val id: String,
    val facing: String, // "front", "back", or "external"
    val supportedSizes: List<String>, // e.g. ["3840x2160", "1920x1080", "1280x720"]
    val supportedFps: List<Int> // e.g. [30, 60]
)

data class SelectedConfig(
    val cameraId: String,
    val facing: String,
    val size: String,
    val fps: Int
)

data class BatteryInfo(
    val level: Int,
    val isCharging: Boolean
)
