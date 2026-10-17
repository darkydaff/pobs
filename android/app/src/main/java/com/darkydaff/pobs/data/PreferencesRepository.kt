package com.darkydaff.pobs.data

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit

class PreferencesRepository(context: Context) {

    private val prefs: SharedPreferences = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun getSelectedConfig(capabilities: List<CameraCapability> = emptyList()): SelectedConfig {
        val savedFacing = prefs.getString(KEY_FACING, DEFAULT_FACING) ?: DEFAULT_FACING
        val savedSize = prefs.getString(KEY_SIZE, DEFAULT_SIZE) ?: DEFAULT_SIZE
        val savedFps = prefs.getInt(KEY_FPS, DEFAULT_FPS)

        val matchingCamera = capabilities.firstOrNull { it.facing.lowercase() == savedFacing.lowercase() }
            ?: capabilities.firstOrNull()

        val cameraId = prefs.getString(KEY_CAMERA_ID, null)
            ?: matchingCamera?.id
            ?: "1"

        return SelectedConfig(
            cameraId = cameraId,
            facing = savedFacing,
            size = savedSize,
            fps = savedFps
        )
    }

    fun saveSelectedConfig(config: SelectedConfig) {
        prefs.edit {
            putString(KEY_CAMERA_ID, config.cameraId)
            putString(KEY_FACING, config.facing)
            putString(KEY_SIZE, config.size)
            putInt(KEY_FPS, config.fps)
        }
    }

    fun getFacing(): String = prefs.getString(KEY_FACING, DEFAULT_FACING) ?: DEFAULT_FACING
    fun setFacing(facing: String) = prefs.edit { putString(KEY_FACING, facing) }

    fun getResolution(): String = prefs.getString(KEY_SIZE, DEFAULT_SIZE) ?: DEFAULT_SIZE
    fun setResolution(res: String) = prefs.edit { putString(KEY_SIZE, res) }

    fun getFps(): Int = prefs.getInt(KEY_FPS, DEFAULT_FPS)
    fun setFps(fps: Int) = prefs.edit { putInt(KEY_FPS, fps) }

    fun getBitrate(): Int = prefs.getInt(KEY_BITRATE, DEFAULT_BITRATE)
    fun setBitrate(bitrate: Int) = prefs.edit { putInt(KEY_BITRATE, bitrate) }

    fun getKeyframeInterval(): Int = prefs.getInt(KEY_KEYFRAME_INTERVAL, DEFAULT_KEYFRAME_INTERVAL)
    fun setKeyframeInterval(interval: Int) = prefs.edit { putInt(KEY_KEYFRAME_INTERVAL, interval) }

    fun getMaxBFrames(): Int = prefs.getInt(KEY_MAX_B_FRAMES, DEFAULT_MAX_B_FRAMES)
    fun setMaxBFrames(bFrames: Int) = prefs.edit { putInt(KEY_MAX_B_FRAMES, bFrames) }

    fun isUltraLowLatency(): Boolean = prefs.getBoolean(KEY_ULTRA_LOW_LATENCY, true)
    fun setUltraLowLatency(enabled: Boolean) = prefs.edit { putBoolean(KEY_ULTRA_LOW_LATENCY, enabled) }

    fun isStabilizationEnabled(): Boolean = prefs.getBoolean(KEY_STABILIZATION, true)
    fun setStabilizationEnabled(enabled: Boolean) = prefs.edit { putBoolean(KEY_STABILIZATION, enabled) }

    fun getLastHost(): String? = prefs.getString(KEY_LAST_HOST, null)
    fun saveLastHost(host: String) = prefs.edit { putString(KEY_LAST_HOST, host) }

    fun getOrientationMode(): String = prefs.getString(KEY_ORIENTATION_MODE, "auto") ?: "auto"
    fun setOrientationMode(mode: String) = prefs.edit { putString(KEY_ORIENTATION_MODE, mode) }

    companion object {
        private const val PREFS_NAME = "pobs_preferences"
        private const val KEY_CAMERA_ID = "camera_id"
        private const val KEY_FACING = "facing"
        private const val KEY_SIZE = "size"
        private const val KEY_FPS = "fps"
        private const val KEY_BITRATE = "bitrate"
        private const val KEY_KEYFRAME_INTERVAL = "keyframe_interval"
        private const val KEY_MAX_B_FRAMES = "max_b_frames"
        private const val KEY_ULTRA_LOW_LATENCY = "ultra_low_latency"
        private const val KEY_STABILIZATION = "stabilization"
        private const val KEY_LAST_HOST = "last_host"
        private const val KEY_ORIENTATION_MODE = "orientation_mode"

        const val DEFAULT_FACING = "front"
        const val DEFAULT_SIZE = "1920x1080"
        const val DEFAULT_FPS = 60
        const val DEFAULT_BITRATE = 12_000_000 // 12 Mbps
        const val DEFAULT_KEYFRAME_INTERVAL = 2 // 2 seconds
        const val DEFAULT_MAX_B_FRAMES = 1      // 1 B-frame
    }
}
