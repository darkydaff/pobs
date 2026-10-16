package com.darkydaff.pobs.net

import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.darkydaff.pobs.data.BatteryRepository
import com.darkydaff.pobs.data.CameraRepository
import com.darkydaff.pobs.data.PreferencesRepository
import okhttp3.*
import org.json.JSONArray
import org.json.JSONObject
import java.net.Inet4Address
import java.net.NetworkInterface
import java.util.concurrent.TimeUnit

class PobsWebSocketClient(
    private val context: Context,
    private val cameraRepository: CameraRepository,
    private val batteryRepository: BatteryRepository,
    private val preferencesRepository: PreferencesRepository
) {
    companion object {
        private const val TAG = "POBS_WS"
        private const val RECONNECT_DELAY_MS = 3000L
    }

    interface Listener {
        fun onConnectionStateChanged(connected: Boolean)
        fun onCameraSwitchedByHost(facing: String)
        fun onTorchToggledByHost(enabled: Boolean)
        fun onFormatChangedByHost(resolution: String, fps: Int)
        fun onZoomChangedByHost(ratio: Float)
        fun onStreamStateChanged(isStreaming: Boolean)
    }

    var listener: Listener? = null

    private val client = OkHttpClient.Builder()
        .readTimeout(0, TimeUnit.MILLISECONDS)
        .pingInterval(10, TimeUnit.SECONDS)
        .build()

    private var webSocket: WebSocket? = null
    var isConnected: Boolean = false
        private set
    var currentHost: String? = null
        private set

    private val mainHandler = Handler(Looper.getMainLooper())
    private var isReconnecting = false

    private val reconnectRunnable = Runnable {
        if (!isConnected && currentHost != null) {
            Log.d(TAG, "Attempting auto-reconnect to $currentHost...")
            connectInternal()
        }
    }

    fun connect(host: String, port: Int = 8765) {
        currentHost = host
        connectInternal(port)
    }

    private fun connectInternal(port: Int = 8765) {
        val host = currentHost ?: return
        try {
            webSocket?.close(1000, "Reconnecting")
        } catch (_: Exception) {}
        webSocket = null
        isConnected = false

        val cleanHost = host.removePrefix("pobs://").removePrefix("ws://").removePrefix("http://")
        val ip = if (cleanHost.contains(":")) cleanHost.substringBefore(":") else cleanHost
        val p = if (cleanHost.contains(":")) cleanHost.substringAfter(":").toIntOrNull() ?: port else port

        val url = "ws://$ip:$p"
        Log.i(TAG, "Connecting to OBS WebSocket server at $url...")

        val request = Request.Builder().url(url).build()
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(ws: WebSocket, response: Response) {
                Log.i(TAG, "Connected to OBS WebSocket at $url!")
                isConnected = true
                mainHandler.removeCallbacks(reconnectRunnable)
                sendHandshake()
                listener?.onConnectionStateChanged(true)
            }

            override fun onMessage(ws: WebSocket, text: String) {
                Log.d(TAG, "Received message: $text")
                handleMessage(text)
            }

            override fun onClosed(ws: WebSocket, code: Int, reason: String) {
                Log.i(TAG, "WebSocket closed: $reason ($code)")
                isConnected = false
                listener?.onConnectionStateChanged(false)
                scheduleReconnect()
            }

            override fun onFailure(ws: WebSocket, t: Throwable, response: Response?) {
                Log.w(TAG, "WebSocket connection failed to $url: ${t.message}")
                isConnected = false
                listener?.onConnectionStateChanged(false)

                if (currentHost != null && !currentHost!!.contains("127.0.0.1")) {
                    Log.i(TAG, "Falling back to ADB reverse tunnel (127.0.0.1:8765)...")
                    currentHost = "127.0.0.1:8765"
                    connectInternal()
                    return
                }
                scheduleReconnect()
            }
        })
    }

    private fun scheduleReconnect() {
        mainHandler.removeCallbacks(reconnectRunnable)
        if (currentHost != null) {
            mainHandler.postDelayed(reconnectRunnable, RECONNECT_DELAY_MS)
        }
    }

    fun disconnect() {
        mainHandler.removeCallbacks(reconnectRunnable)
        currentHost = null
        try {
            webSocket?.close(1000, "App closing")
        } catch (_: Exception) {}
        webSocket = null
        isConnected = false
        listener?.onConnectionStateChanged(false)
    }

    fun notifyCameraSwitched(newFacing: String) {
        val json = JSONObject().apply {
            put("type", "camera_switched")
            put("facing", newFacing)
        }
        sendJson(json)
    }

    fun notifyTorchSwitched(enabled: Boolean) {
        val json = JSONObject().apply {
            put("type", "torch_switched")
            put("enabled", enabled)
        }
        sendJson(json)
    }

    fun notifyFormatUpdated(resolution: String, fps: Int) {
        val json = JSONObject().apply {
            put("type", "format_updated")
            put("resolution", resolution)
            put("fps", fps)
        }
        sendJson(json)
    }

    fun sendBatteryUpdate(level: Int, isCharging: Boolean) {
        val json = JSONObject().apply {
            put("type", "battery_update")
            put("level", level)
            put("is_charging", isCharging)
        }
        sendJson(json)
    }

    fun notifyZoomUpdated(ratio: Float) {
        val json = JSONObject().apply {
            put("type", "zoom_updated")
            put("ratio", ratio.toDouble())
        }
        sendJson(json)
    }

    fun notifyStreamStateUpdated(enabled: Boolean) {
        val json = JSONObject().apply {
            put("type", "stream_state_updated")
            put("enabled", enabled)
        }
        sendJson(json)
    }

    private fun sendHandshake() {
        try {
            val bat = batteryRepository.getBatteryInfo()
            val caps = cameraRepository.getCameraCapabilities()
            val selected = preferencesRepository.getSelectedConfig()

            val json = JSONObject().apply {
                put("type", "hello")
                put("device_model", Build.MODEL)
                put("device_ip", getLocalIpAddress())
                put("battery", JSONObject().apply {
                    put("level", bat.level)
                    put("is_charging", bat.isCharging)
                })
                val camsArr = JSONArray()
                for (cap in caps) {
                    val cObj = JSONObject().apply {
                        put("id", cap.id)
                        put("facing", cap.facing)
                        val sizesArr = JSONArray()
                        cap.supportedSizes.forEach { sizesArr.put(it) }
                        put("sizes", sizesArr)
                        val fpsArr = JSONArray()
                        cap.supportedFps.forEach { fpsArr.put(it) }
                        put("fps", fpsArr)
                    }
                    camsArr.put(cObj)
                }
                put("cameras", camsArr)
                put("active_camera", selected.facing)
                put("active_resolution", preferencesRepository.getResolution())
                put("active_fps", preferencesRepository.getFps())
            }

            sendJson(json)
        } catch (e: Exception) {
            Log.e(TAG, "Error sending handshake", e)
        }
    }

    private fun handleMessage(text: String) {
        try {
            val json = JSONObject(text)
            when (json.optString("type")) {
                "set_camera" -> {
                    val facing = json.optString("facing")
                    if (facing.isNotEmpty()) {
                        listener?.onCameraSwitchedByHost(facing)
                    }
                }
                "set_torch" -> {
                    val enabled = json.optBoolean("enabled", false)
                    listener?.onTorchToggledByHost(enabled)
                }
                "set_format" -> {
                    val res = json.optString("resolution", "1920x1080")
                    val fps = json.optInt("fps", 60)
                    listener?.onFormatChangedByHost(res, fps)
                }
                "set_zoom" -> {
                    val ratio = json.optDouble("ratio", 1.0).toFloat()
                    listener?.onZoomChangedByHost(ratio)
                }
                "set_streaming" -> {
                    val enabled = json.optBoolean("enabled", true)
                    listener?.onStreamStateChanged(enabled)
                }
                "stream_state" -> {
                    val state = json.optString("state")
                    listener?.onStreamStateChanged(state == "streaming")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing message: $text", e)
        }
    }

    private fun sendJson(json: JSONObject) {
        val ws = webSocket
        if (ws != null && isConnected) {
            ws.send(json.toString())
        }
    }

    private fun getLocalIpAddress(): String {
        try {
            val en = NetworkInterface.getNetworkInterfaces()
            while (en.hasMoreElements()) {
                val intf = en.nextElement()
                val enumIpAddr = intf.inetAddresses
                while (enumIpAddr.hasMoreElements()) {
                    val inetAddress = enumIpAddr.nextElement()
                    if (!inetAddress.isLoopbackAddress && inetAddress is Inet4Address) {
                        return inetAddress.hostAddress ?: ""
                    }
                }
            }
        } catch (_: Exception) {}
        return ""
    }
}
