package com.darkydaff.pobs.data

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager

class BatteryRepository(private val context: Context) {

    fun getBatteryInfo(): BatteryInfo {
        val filter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        val batteryStatus: Intent? = context.registerReceiver(null, filter)

        val level: Int = batteryStatus?.let { intent ->
            val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
            val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
            if (level >= 0 && scale > 0) (level * 100) / scale else -1
        } ?: -1

        val status: Int = batteryStatus?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) ?: -1
        val isCharging = status == BatteryManager.BATTERY_STATUS_CHARGING ||
                status == BatteryManager.BATTERY_STATUS_FULL

        return BatteryInfo(
            level = level,
            isCharging = isCharging
        )
    }
}
