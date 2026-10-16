package com.darkydaff.pobs.data

import android.content.Context
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.graphics.SurfaceTexture
import android.util.Range

class CameraRepository(private val context: Context) {

    fun getCameraCapabilities(): List<CameraCapability> {
        val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as? CameraManager
            ?: return emptyList()

        val result = mutableListOf<CameraCapability>()

        try {
            for (id in cameraManager.cameraIdList) {
                val characteristics = cameraManager.getCameraCharacteristics(id)
                
                val lensFacing = characteristics.get(CameraCharacteristics.LENS_FACING)
                val facingStr = when (lensFacing) {
                    CameraCharacteristics.LENS_FACING_FRONT -> "front"
                    CameraCharacteristics.LENS_FACING_BACK -> "back"
                    CameraCharacteristics.LENS_FACING_EXTERNAL -> "external"
                    else -> "unknown"
                }

                val map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                val sizes = map?.getOutputSizes(SurfaceTexture::class.java)
                    ?.map { "${it.width}x${it.height}" }
                    ?.distinct()
                    ?.sortedByDescending { 
                        val parts = it.split("x")
                        if (parts.size == 2) (parts[0].toIntOrNull() ?: 0) * (parts[1].toIntOrNull() ?: 0) else 0
                    }
                    ?: emptyList()

                val fpsRanges = characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                val fpsList = fpsRanges
                    ?.map { range: Range<Int> -> range.upper }
                    ?.distinct()
                    ?.sortedDescending()
                    ?: emptyList()

                result.add(
                    CameraCapability(
                        id = id,
                        facing = facingStr,
                        supportedSizes = sizes,
                        supportedFps = fpsList
                    )
                )
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }

        return result
    }
}
