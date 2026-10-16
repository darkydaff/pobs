package com.darkydaff.pobs.provider

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import com.darkydaff.pobs.data.BatteryRepository
import com.darkydaff.pobs.data.CameraRepository
import com.darkydaff.pobs.data.PreferencesRepository
import com.darkydaff.pobs.data.SelectedConfig
import org.json.JSONArray
import org.json.JSONObject

class PobsConfigProvider : ContentProvider() {

    override fun onCreate(): Boolean {
        return true
    }

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?
    ): Cursor {
        val ctx = context ?: return MatrixCursor(arrayOf(COLUMN_CONFIG))

        try {
            val cameraRepo = CameraRepository(ctx)
            val prefsRepo = PreferencesRepository(ctx)
            val batteryRepo = BatteryRepository(ctx)

            val capabilities = cameraRepo.getCameraCapabilities()
            val selected = prefsRepo.getSelectedConfig(capabilities)
            val battery = batteryRepo.getBatteryInfo()

            val scrcpyCommand = "scrcpy --video-source=camera --camera-facing=${selected.facing} " +
                    "--camera-size=${selected.size} --max-fps=${selected.fps} " +
                    "--v4l2-sink=/dev/video0 --no-playback --v4l2-buffer=0"

            val rootJson = JSONObject().apply {
                put("version", PROTOCOL_VERSION)
                put("battery", JSONObject().apply {
                    put("level", battery.level)
                    put("isCharging", battery.isCharging)
                })
                put("selectedConfig", JSONObject().apply {
                    put("cameraId", selected.cameraId)
                    put("facing", selected.facing)
                    put("size", selected.size)
                    put("fps", selected.fps)
                })
                put("scrcpyCommandPreview", scrcpyCommand)

                val capsArray = JSONArray()
                for (cap in capabilities) {
                    val capObj = JSONObject().apply {
                        put("id", cap.id)
                        put("facing", cap.facing)
                        put("supportedSizes", JSONArray(cap.supportedSizes))
                        put("supportedFps", JSONArray(cap.supportedFps))
                    }
                    capsArray.put(capObj)
                }
                put("cameraCapabilities", capsArray)
            }

            val cursor = MatrixCursor(arrayOf(COLUMN_CONFIG, COLUMN_JSON))
            val jsonStr = rootJson.toString()
            cursor.addRow(arrayOf(jsonStr, jsonStr))
            return cursor
        } catch (e: Exception) {
            val errJson = JSONObject().apply {
                put("version", PROTOCOL_VERSION)
                put("error", e.message ?: "Failed to query camera capabilities")
            }
            val cursor = MatrixCursor(arrayOf(COLUMN_CONFIG, COLUMN_JSON))
            val jsonStr = errJson.toString()
            cursor.addRow(arrayOf(jsonStr, jsonStr))
            return cursor
        }
    }

    override fun getType(uri: Uri): String {
        return "vnd.android.cursor.item/vnd.com.darkydaff.pobs.config"
    }

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0

    override fun update(
        uri: Uri,
        values: ContentValues?,
        selection: String?,
        selectionArgs: Array<out String>?
    ): Int {
        val ctx = context ?: return 0
        if (values == null) return 0
        try {
            val prefsRepo = PreferencesRepository(ctx)
            val cameraRepo = CameraRepository(ctx)
            val caps = cameraRepo.getCameraCapabilities()
            val current = prefsRepo.getSelectedConfig(caps)

            val newFacing = values.getAsString("facing") ?: current.facing
            val newSize = values.getAsString("size") ?: current.size
            val newFps = values.getAsInteger("fps") ?: current.fps
            val newCap = caps.firstOrNull { it.facing.lowercase() == newFacing.lowercase() }
            val newCameraId = newCap?.id ?: current.cameraId

            prefsRepo.saveSelectedConfig(
                SelectedConfig(
                    cameraId = newCameraId,
                    facing = newFacing,
                    size = newSize,
                    fps = newFps
                )
            )
            return 1
        } catch (e: Exception) {
            return 0
        }
    }

    companion object {
        const val AUTHORITY = "com.darkydaff.pobs.provider"
        const val PROTOCOL_VERSION = 1
        const val COLUMN_CONFIG = "config"
        const val COLUMN_JSON = "json"
    }
}
