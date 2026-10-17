package com.darkydaff.pobs.service

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.hardware.camera2.*
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.*
import android.util.Log
import android.util.Range
import android.view.Surface
import androidx.core.app.NotificationCompat
import com.darkydaff.pobs.R
import com.darkydaff.pobs.data.BatteryRepository
import com.darkydaff.pobs.data.CameraRepository
import com.darkydaff.pobs.data.PreferencesRepository
import com.darkydaff.pobs.net.PobsWebSocketClient
import com.darkydaff.pobs.ui.MainActivity
import java.io.DataOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean

class CameraStreamingService : Service() {

    enum class OrientationMode {
        AUTO,
        VERTICAL,
        HORIZONTAL
    }

    companion object {
        const val TAG = "POBS_SERVICE"
        const val NOTIFICATION_ID = 1001
        const val CHANNEL_ID = "pobs_camera_stream_channel"

        const val ACTION_START = "com.darkydaff.pobs.action.START"
        const val ACTION_STOP = "com.darkydaff.pobs.action.STOP"
        const val ACTION_SWITCH_CAMERA = "com.darkydaff.pobs.action.SWITCH_CAMERA"
        const val ACTION_TOGGLE_TORCH = "com.darkydaff.pobs.action.TOGGLE_TORCH"
        const val ACTION_RECONFIGURE = "com.darkydaff.pobs.action.RECONFIGURE"

        const val EXTRA_HOST = "extra_host"
        const val EXTRA_PORT = "extra_port"
        const val EXTRA_FACING = "extra_facing"
        const val EXTRA_TORCH = "extra_torch"

        @Volatile var isStreaming = false
        @Volatile var isStreamingActive = true
        @Volatile var currentZoomRatio = 1.0f
        @Volatile var minZoomRatio = 1.0f
        @Volatile var maxZoomRatio = 8.0f
        @Volatile var activeFacing = "back"
        @Volatile var activeResolution = "1920x1080"
        @Volatile var activeFps = 60
        @Volatile var isTorchOn = false
        @Volatile var isConnectedToHost = false
        @Volatile var currentOrientationMode: OrientationMode = OrientationMode.AUTO
        @Volatile var activeOrientation: String = "vertical"
        @Volatile var currentRotationDegrees: Int = 90
        @Volatile var batteryPercentage = 100
        @Volatile var isBatteryCharging = false

        @Volatile private var previewSurface: Surface? = null
        @Volatile private var activeService: CameraStreamingService? = null

        var onFormatChangedListener: ((String, Int) -> Unit)? = null
        var onZoomChangedListener: ((Float) -> Unit)? = null
        var onStreamStateListener: ((Boolean) -> Unit)? = null
        var onOrientationChangedListener: ((OrientationMode, String, Int) -> Unit)? = null
        var onStateChangedListener: (() -> Unit)? = null
        private val mainHandler = Handler(Looper.getMainLooper())

        fun notifyStateChanged() {
            mainHandler.post {
                onStateChangedListener?.invoke()
            }
        }

        fun updatePreviewSurface(surface: Surface?) {
            previewSurface = surface
            activeService?.notifyPreviewSurfaceChanged()
        }

        fun setZoom(ratio: Float) {
            activeService?.setZoomInternal(ratio)
        }

        fun setStreamActive(active: Boolean) {
            activeService?.setStreamingActiveInternal(active)
        }

        fun switchCamera(facing: String? = null) {
            val targetFacing = facing ?: if (activeFacing.lowercase() == "front") "back" else "front"
            activeService?.switchCameraInternal(targetFacing)
        }

        fun toggleTorch(enable: Boolean? = null) {
            val target = enable ?: !isTorchOn
            activeService?.toggleTorchInternal(target)
        }

        fun setOrientationMode(mode: OrientationMode) {
            activeService?.setOrientationModeInternal(mode)
        }

        fun updatePhysicalOrientation(angleDegrees: Int) {
            activeService?.updatePhysicalOrientationInternal(angleDegrees)
        }
    }

    private lateinit var prefsRepo: PreferencesRepository
    private lateinit var cameraRepo: CameraRepository
    private lateinit var batteryRepo: BatteryRepository
    private lateinit var wsClient: PobsWebSocketClient

    private var wakeLock: PowerManager.WakeLock? = null
    private var backgroundThread: HandlerThread? = null
    private var backgroundHandler: Handler? = null
    private var packetBuffer = ByteArray(2 * 1024 * 1024)

    // Video pipeline
    private var videoSocket: Socket? = null
    private var videoOutputStream: DataOutputStream? = null
    private var mediaCodec: MediaCodec? = null
    private var encoderSurface: Surface? = null

    // Camera2
    private var cameraManager: CameraManager? = null
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var captureRequestBuilder: CaptureRequest.Builder? = null

    private var currentHost = "127.0.0.1"
    private var videoPort = 8766
    private val isRunning = AtomicBoolean(false)
    private var streamingThread: Thread? = null

    // Real-time battery receiver
    private val batteryReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
            val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
            val status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1)
            val isCharging = status == BatteryManager.BATTERY_STATUS_CHARGING ||
                             status == BatteryManager.BATTERY_STATUS_FULL
            val pct = if (scale > 0) (level * 100) / scale else level

            if (pct >= 0) {
                batteryPercentage = pct
                isBatteryCharging = isCharging
                wsClient.sendBatteryUpdate(pct, isCharging)
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        activeService = this
        prefsRepo = PreferencesRepository(this)
        cameraRepo = CameraRepository(this)
        batteryRepo = BatteryRepository(this)

        activeFacing = prefsRepo.getSelectedConfig().facing
        activeResolution = prefsRepo.getResolution()
        activeFps = prefsRepo.getFps()

        currentOrientationMode = when (prefsRepo.getOrientationMode().lowercase()) {
            "vertical" -> OrientationMode.VERTICAL
            "horizontal" -> OrientationMode.HORIZONTAL
            else -> OrientationMode.AUTO
        }
        recalculateOrientation(forceNotify = false)

        createNotificationChannel()
        startBackgroundThread()
        acquireWakeLock()

        val filter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        registerReceiver(batteryReceiver, filter)

        wsClient = PobsWebSocketClient(this, cameraRepo, batteryRepo, prefsRepo).apply {
            listener = object : PobsWebSocketClient.Listener {
                override fun onConnectionStateChanged(connected: Boolean) {
                    isConnectedToHost = connected
                    Log.i(TAG, "Control WebSocket connected: $connected")
                    if (connected) {
                        notifyFormatUpdated(activeResolution, activeFps)
                        sendBatteryUpdate(batteryPercentage, isBatteryCharging)
                        notifyOrientationUpdated(activeOrientation, currentRotationDegrees, currentOrientationMode.name.lowercase())
                    }
                }

                override fun onCameraSwitchedByHost(facing: String) {
                    switchCameraInternal(facing)
                }

                override fun onTorchToggledByHost(enabled: Boolean) {
                    toggleTorchInternal(enabled)
                }

                override fun onFormatChangedByHost(resolution: String, fps: Int) {
                    Log.i(TAG, "Host requested format update: $resolution @ ${fps}fps")
                    activeResolution = resolution
                    activeFps = fps
                    prefsRepo.setResolution(resolution)
                    prefsRepo.setFps(fps)
                    reconfigurePipeline()
                    mainHandler.post {
                        onFormatChangedListener?.invoke(resolution, fps)
                    }
                }

                override fun onZoomChangedByHost(ratio: Float) {
                    Log.i(TAG, "Host requested zoom ratio: $ratio")
                    setZoomInternal(ratio)
                }

                override fun onStreamStateChanged(streaming: Boolean) {
                    Log.i(TAG, "Host requested stream state: $streaming")
                    setStreamingActiveInternal(streaming)
                }

                override fun onOrientationModeChangedByHost(mode: String) {
                    Log.i(TAG, "Host requested orientation mode: $mode")
                    val targetMode = when (mode.lowercase()) {
                        "vertical" -> OrientationMode.VERTICAL
                        "horizontal" -> OrientationMode.HORIZONTAL
                        else -> OrientationMode.AUTO
                    }
                    setOrientationModeInternal(targetMode)
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        activeService = null
        try { unregisterReceiver(batteryReceiver) } catch (_: Exception) {}
        stopStream()
        releaseWakeLock()
        stopBackgroundThread()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action ?: ACTION_START
        when (action) {
            ACTION_START -> {
                val host = intent?.getStringExtra(EXTRA_HOST) ?: prefsRepo.getLastHost() ?: "127.0.0.1"
                val port = intent?.getIntExtra(EXTRA_PORT, 8766) ?: 8766
                currentHost = host
                videoPort = port
                startForegroundStream()
            }
            ACTION_SWITCH_CAMERA -> {
                val facing = intent?.getStringExtra(EXTRA_FACING)
                    ?: if (activeFacing == "front") "back" else "front"
                switchCameraInternal(facing)
            }
            ACTION_TOGGLE_TORCH -> {
                val enable = intent?.getBooleanExtra(EXTRA_TORCH, !isTorchOn) ?: !isTorchOn
                toggleTorchInternal(enable)
            }
            ACTION_RECONFIGURE -> {
                reconfigurePipeline()
            }
            ACTION_STOP -> {
                stopStream()
                stopSelf()
            }
        }

        return START_STICKY
    }

    private fun startForegroundStream() {
        val notification = buildNotification("Running Pixel Camera...")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }

        wsClient.connect(currentHost, 8765)

        // Start camera and encoder immediately so preview is visible right away!
        backgroundHandler?.post {
            if (cameraDevice == null) {
                initMediaCodec()
                openCamera(activeFacing)
            }
        }

        if (!isRunning.getAndSet(true)) {
            streamingThread = Thread {
                runStreamingPipeline()
            }.apply { start() }
        }
    }

    private fun runStreamingPipeline() {
        var spsPpsBuffer: ByteArray? = null
        val bufferInfo = MediaCodec.BufferInfo()

        while (isRunning.get()) {
            val cleanHost = (wsClient.currentHost ?: currentHost).removePrefix("pobs://").removePrefix("ws://").removePrefix("http://")
            val targetIp = if (cleanHost.contains(":")) cleanHost.substringBefore(":") else cleanHost

            val socket = Socket().apply {
                tcpNoDelay = true
                sendBufferSize = 64 * 1024
            }

            try {
                try {
                    socket.connect(InetSocketAddress(targetIp, videoPort), 2000)
                } catch (e: Exception) {
                    if (targetIp != "127.0.0.1") {
                        socket.connect(InetSocketAddress("127.0.0.1", videoPort), 2000)
                    } else {
                        throw e
                    }
                }
                videoSocket = socket
                videoOutputStream = DataOutputStream(socket.getOutputStream())
                isStreaming = true
                notifyStateChanged()
                Log.i(TAG, "Connected to OBS video receiver at ${socket.remoteSocketAddress}!")

                // Request keyframe so OBS immediately starts rendering video
                try {
                    val bundle = Bundle().apply {
                        putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
                    }
                    mediaCodec?.setParameters(bundle)
                } catch (_: Exception) {}

                // Daemon watcher thread to immediately detect OBS disconnect / restart
                Thread {
                    try {
                        val inStream = socket.getInputStream()
                        val dummy = ByteArray(16)
                        while (inStream.read(dummy) != -1) {}
                    } catch (_: Exception) {}
                    Log.i(TAG, "OBS video receiver closed connection, resetting video socket...")
                    try { socket.close() } catch (_: Exception) {}
                }.apply { isDaemon = true; start() }

                while (isRunning.get() && videoSocket?.isConnected == true) {
                    val outputBufferId = mediaCodec?.dequeueOutputBuffer(bufferInfo, 10000) ?: -1
                    if (outputBufferId >= 0) {
                        if (!isStreamingActive) {
                            mediaCodec?.releaseOutputBuffer(outputBufferId, false)
                            continue
                        }
                        val outputBuffer = mediaCodec?.getOutputBuffer(outputBufferId)
                        if (outputBuffer != null) {
                            val payloadSize = bufferInfo.size
                            outputBuffer.position(bufferInfo.offset)
                            outputBuffer.limit(bufferInfo.offset + payloadSize)

                            if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                                if (spsPpsBuffer == null || spsPpsBuffer!!.size != payloadSize) {
                                    spsPpsBuffer = ByteArray(payloadSize)
                                }
                                outputBuffer.get(spsPpsBuffer!!, 0, payloadSize)
                            } else {
                                val isKeyFrame = (bufferInfo.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0
                                val spsSize = if (isKeyFrame && spsPpsBuffer != null) spsPpsBuffer!!.size else 0
                                val totalPayloadSize = spsSize + payloadSize
                                val totalPacketSize = totalPayloadSize + 4

                                if (packetBuffer.size < totalPacketSize) {
                                    packetBuffer = ByteArray(totalPacketSize + 131072)
                                }

                                packetBuffer[0] = ((totalPayloadSize ushr 24) and 0xFF).toByte()
                                packetBuffer[1] = ((totalPayloadSize ushr 16) and 0xFF).toByte()
                                packetBuffer[2] = ((totalPayloadSize ushr 8) and 0xFF).toByte()
                                packetBuffer[3] = (totalPayloadSize and 0xFF).toByte()

                                if (spsSize > 0) {
                                    System.arraycopy(spsPpsBuffer!!, 0, packetBuffer, 4, spsSize)
                                }

                                outputBuffer.get(packetBuffer, 4 + spsSize, payloadSize)

                                val outStream = videoOutputStream
                                if (outStream != null) {
                                    outStream.write(packetBuffer, 0, totalPacketSize)
                                    outStream.flush()
                                }
                            }
                        }
                        mediaCodec?.releaseOutputBuffer(outputBufferId, false)
                    }
                }
            } catch (e: Exception) {
                // If not connected to OBS, drain output buffers so encoder doesn't stall
                val dummyInfo = MediaCodec.BufferInfo()
                try {
                    val drainId = mediaCodec?.dequeueOutputBuffer(dummyInfo, 0) ?: -1
                    if (drainId >= 0) {
                        mediaCodec?.releaseOutputBuffer(drainId, false)
                    }
                } catch (_: Exception) {}

                if (isRunning.get()) {
                    try { Thread.sleep(1500) } catch (_: Exception) {}
                }
            } finally {
                isStreaming = false
                notifyStateChanged()
                try { videoOutputStream?.close() } catch (_: Exception) {}
                try { videoSocket?.close() } catch (_: Exception) {}
                videoOutputStream = null
                videoSocket = null
            }
        }
    }

    private fun initMediaCodec() {
        releaseMediaCodec()
        activeResolution = prefsRepo.getResolution()
        activeFps = prefsRepo.getFps()
        val bitrate = prefsRepo.getBitrate()

        val (w, h) = when (activeResolution) {
            "3840x2160" -> Pair(3840, 2160)
            "1280x720" -> Pair(1280, 720)
            else -> Pair(1920, 1080)
        }

        Log.i(TAG, "Configuring Ultra-Low-Latency MediaCodec: ${w}x${h} @ ${activeFps}fps")

        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, w, h).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
            setInteger(MediaFormat.KEY_FRAME_RATE, activeFps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR)
            setInteger(MediaFormat.KEY_COMPLEXITY, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR)
            setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline)
            setInteger(MediaFormat.KEY_LEVEL, MediaCodecInfo.CodecProfileLevel.AVCLevel52)

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                setInteger(MediaFormat.KEY_LATENCY, 0)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                try {
                    setInteger(MediaFormat.KEY_INTRA_REFRESH_PERIOD, activeFps)
                } catch (_: Exception) {}
            }
        }

        try {
            mediaCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC).apply {
                configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                encoderSurface = createInputSurface()
                start()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to initialize MediaCodec", e)
        }
    }

    private fun releaseMediaCodec() {
        try {
            mediaCodec?.stop()
            mediaCodec?.release()
        } catch (_: Exception) {}
        mediaCodec = null
        encoderSurface = null
    }

    @SuppressLint("MissingPermission")
    private fun openCamera(facing: String) {
        cameraManager = getSystemService(Context.CAMERA_SERVICE) as CameraManager
        val targetFacingConstant = if (facing.lowercase() == "front")
            CameraCharacteristics.LENS_FACING_FRONT else CameraCharacteristics.LENS_FACING_BACK

        var chosenId: String? = null
        for (id in cameraManager!!.cameraIdList) {
            val chars = cameraManager!!.getCameraCharacteristics(id)
            if (chars.get(CameraCharacteristics.LENS_FACING) == targetFacingConstant) {
                chosenId = id
                break
            }
        }
        val cameraId = chosenId ?: cameraManager!!.cameraIdList.firstOrNull() ?: return

        val characteristics = cameraManager!!.getCameraCharacteristics(cameraId)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val range = characteristics.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE)
            if (range != null) {
                minZoomRatio = range.lower
                maxZoomRatio = range.upper.coerceAtMost(8.0f)
            }
        }
        currentZoomRatio = currentZoomRatio.coerceIn(minZoomRatio, maxZoomRatio)

        try {
            cameraManager!!.openCamera(cameraId, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    Log.i(TAG, "Camera $cameraId ($facing) successfully opened!")
                    cameraDevice = camera
                    configureCaptureSession()
                    mainHandler.post { notifyStateChanged() }
                }

                override fun onDisconnected(camera: CameraDevice) {
                    Log.w(TAG, "Camera $cameraId ($facing) disconnected")
                    camera.close()
                    if (cameraDevice == camera) {
                        cameraDevice = null
                    }
                    mainHandler.post { notifyStateChanged() }
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    Log.e(TAG, "Camera $cameraId ($facing) error: $error")
                    camera.close()
                    if (cameraDevice == camera) {
                        cameraDevice = null
                    }
                    mainHandler.post { notifyStateChanged() }
                }
            }, backgroundHandler)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to open camera $cameraId", e)
        }
    }

    private fun configureCaptureSession() {
        val camera = cameraDevice ?: return
        val targets = mutableListOf<Surface>()
        encoderSurface?.let { if (it.isValid) targets.add(it) }
        val pSurface = previewSurface
        if (pSurface != null && pSurface.isValid) {
            targets.add(pSurface)
        }

        if (targets.isEmpty()) {
            Log.w(TAG, "No valid surfaces available for capture session")
            return
        }

        try {
            val builder = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                targets.forEach { addTarget(it) }
                set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO)
                set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
                set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(activeFps, activeFps))

                if (prefsRepo.isUltraLowLatency()) {
                    set(CaptureRequest.NOISE_REDUCTION_MODE, CaptureRequest.NOISE_REDUCTION_MODE_FAST)
                    set(CaptureRequest.EDGE_MODE, CaptureRequest.EDGE_MODE_FAST)
                    set(CaptureRequest.TONEMAP_MODE, CaptureRequest.TONEMAP_MODE_FAST)
                    set(CaptureRequest.HOT_PIXEL_MODE, CaptureRequest.HOT_PIXEL_MODE_FAST)
                } else {
                    set(CaptureRequest.NOISE_REDUCTION_MODE, CaptureRequest.NOISE_REDUCTION_MODE_HIGH_QUALITY)
                    set(CaptureRequest.EDGE_MODE, CaptureRequest.EDGE_MODE_HIGH_QUALITY)
                }

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    set(CaptureRequest.CONTROL_ZOOM_RATIO, currentZoomRatio)
                }

                if (prefsRepo.isStabilizationEnabled()) {
                    set(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_ON)
                    set(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE_ON)
                }

                if (isTorchOn && activeFacing.lowercase() == "back") {
                    set(CaptureRequest.FLASH_MODE, CaptureRequest.FLASH_MODE_TORCH)
                } else {
                    set(CaptureRequest.FLASH_MODE, CaptureRequest.FLASH_MODE_OFF)
                }
            }

            camera.createCaptureSession(targets, object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(session: CameraCaptureSession) {
                    captureSession = session
                    captureRequestBuilder = builder
                    try {
                        session.setRepeatingRequest(builder.build(), null, backgroundHandler)
                        Log.i(TAG, "Capture session configured with ${targets.size} surfaces for $activeFacing camera!")
                    } catch (e: Exception) {
                        Log.e(TAG, "Error setting repeating request", e)
                    }
                }

                override fun onConfigureFailed(session: CameraCaptureSession) {
                    Log.e(TAG, "Failed to configure camera capture session")
                }
            }, backgroundHandler)
        } catch (e: Exception) {
            Log.e(TAG, "Exception creating capture session", e)
        }
    }

    private fun closeCaptureSession() {
        try { captureSession?.stopRepeating() } catch (_: Exception) {}
        try { captureSession?.close() } catch (_: Exception) {}
        captureSession = null
        captureRequestBuilder = null
    }

    private fun closeCamera() {
        closeCaptureSession()
        try { cameraDevice?.close() } catch (_: Exception) {}
        cameraDevice = null
    }

    fun notifyPreviewSurfaceChanged() {
        backgroundHandler?.post {
            if (cameraDevice != null) {
                Log.i(TAG, "Preview surface updated, re-configuring capture session...")
                closeCaptureSession()
                configureCaptureSession()
            }
        }
    }

    fun setZoomInternal(ratio: Float) {
        val clamped = ratio.coerceIn(minZoomRatio, maxZoomRatio)
        currentZoomRatio = clamped
        backgroundHandler?.post {
            val session = captureSession ?: return@post
            val b = captureRequestBuilder ?: return@post
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                b.set(CaptureRequest.CONTROL_ZOOM_RATIO, clamped)
            }
            try {
                session.setRepeatingRequest(b.build(), null, backgroundHandler)
            } catch (e: Exception) {
                Log.e(TAG, "Error applying zoom ratio", e)
            }
        }
        mainHandler.post {
            onZoomChangedListener?.invoke(clamped)
        }
        wsClient.notifyZoomUpdated(clamped)
    }

    fun setStreamingActiveInternal(active: Boolean) {
        isStreamingActive = active
        mainHandler.post {
            onStreamStateListener?.invoke(active)
        }
        wsClient.notifyStreamStateUpdated(active)
    }

    fun switchCameraInternal(newFacing: String) {
        if (newFacing.lowercase() == activeFacing.lowercase()) return
        Log.i(TAG, "Switching camera to $newFacing...")
        activeFacing = newFacing
        prefsRepo.setFacing(newFacing)
        recalculateOrientation(forceNotify = true)
        if (newFacing.lowercase() == "front" && isTorchOn) {
            isTorchOn = false
            wsClient.notifyTorchSwitched(false)
        }
        notifyStateChanged()

        backgroundHandler?.post {
            closeCamera()
            // 150ms delay to let camera HAL release client
            backgroundHandler?.postDelayed({
                openCamera(newFacing)
                updateNotification("Streaming: ${activeFacing.replaceFirstChar { it.uppercase() }} Camera")
                wsClient.notifyCameraSwitched(newFacing)
                notifyStateChanged()
            }, 150)
        }
    }

    fun toggleTorchInternal(enabled: Boolean) {
        if (activeFacing.lowercase() != "back") {
            isTorchOn = false
            wsClient.notifyTorchSwitched(false)
            notifyStateChanged()
            return
        }
        isTorchOn = enabled
        wsClient.notifyTorchSwitched(enabled)
        notifyStateChanged()
        backgroundHandler?.post {
            val reqBuilder = captureRequestBuilder
            val session = captureSession
            if (reqBuilder != null && session != null) {
                try {
                    reqBuilder.set(
                        CaptureRequest.FLASH_MODE,
                        if (enabled) CaptureRequest.FLASH_MODE_TORCH else CaptureRequest.FLASH_MODE_OFF
                    )
                    session.setRepeatingRequest(reqBuilder.build(), null, backgroundHandler)
                } catch (e: Exception) {
                    Log.e(TAG, "Error updating torch repeating request", e)
                }
            }
        }
    }

    private var lastReportedRotation = -1
    private var lastReportedOrientation = ""
    private var lastPhysicalAngle = 0

    fun setOrientationModeInternal(mode: OrientationMode) {
        currentOrientationMode = mode
        prefsRepo.setOrientationMode(mode.name.lowercase())
        recalculateOrientation(forceNotify = true)
    }

    fun updatePhysicalOrientationInternal(angle: Int) {
        if (angle == -1) return
        val snappedAngle = when (angle) {
            in 45..134 -> 90
            in 135..224 -> 180
            in 225..314 -> 270
            else -> 0
        }
        if (snappedAngle != lastPhysicalAngle) {
            lastPhysicalAngle = snappedAngle
            if (currentOrientationMode == OrientationMode.AUTO) {
                recalculateOrientation()
            }
        }
    }

    fun recalculateOrientation(forceNotify: Boolean = false) {
        val targetOrientation: String
        val targetRotation: Int

        when (currentOrientationMode) {
            OrientationMode.VERTICAL -> {
                targetOrientation = "vertical"
                targetRotation = if (activeFacing.lowercase() == "front") 270 else 90
            }
            OrientationMode.HORIZONTAL -> {
                targetOrientation = "horizontal"
                targetRotation = 0
            }
            OrientationMode.AUTO -> {
                if (lastPhysicalAngle == 90) {
                    targetOrientation = "horizontal"
                    targetRotation = 180
                } else if (lastPhysicalAngle == 270) {
                    targetOrientation = "horizontal"
                    targetRotation = 0
                } else if (lastPhysicalAngle == 180) {
                    targetOrientation = "vertical"
                    targetRotation = if (activeFacing.lowercase() == "front") 90 else 270
                } else {
                    targetOrientation = "vertical"
                    targetRotation = if (activeFacing.lowercase() == "front") 270 else 90
                }
            }
        }

        activeOrientation = targetOrientation
        currentRotationDegrees = targetRotation

        if (forceNotify || targetRotation != lastReportedRotation || targetOrientation != lastReportedOrientation) {
            lastReportedRotation = targetRotation
            lastReportedOrientation = targetOrientation
            if (::wsClient.isInitialized) {
                wsClient.notifyOrientationUpdated(targetOrientation, targetRotation, currentOrientationMode.name.lowercase())
            }
            mainHandler.post {
                onOrientationChangedListener?.invoke(currentOrientationMode, targetOrientation, targetRotation)
                notifyStateChanged()
            }
        }
    }

    private fun reconfigurePipeline() {
        backgroundHandler?.post {
            closeCamera()
            releaseMediaCodec()
            initMediaCodec()
            openCamera(activeFacing)
            wsClient.notifyFormatUpdated(activeResolution, activeFps)
        }
    }

    private fun stopStream() {
        isStreaming = false
        isRunning.set(false)
        wsClient.disconnect()
        backgroundHandler?.post {
            closeCamera()
            releaseMediaCodec()
        }
        try { videoSocket?.close() } catch (_: Exception) {}
        videoSocket = null
    }

    private fun startBackgroundThread() {
        backgroundThread = HandlerThread("CameraBackground").apply {
            start()
            backgroundHandler = Handler(looper)
        }
    }

    private fun stopBackgroundThread() {
        backgroundThread?.quitSafely()
        try {
            backgroundThread?.join()
            backgroundThread = null
            backgroundHandler = null
        } catch (_: Exception) {}
    }

    private fun acquireWakeLock() {
        val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "pobs:streaming_wakelock").apply {
            acquire(24 * 60 * 60 * 1000L)
        }
    }

    private fun releaseWakeLock() {
        try {
            if (wakeLock?.isHeld == true) wakeLock?.release()
        } catch (_: Exception) {}
        wakeLock = null
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "POBS Camera Background Streaming",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Keeps camera hardware encoding active in background"
            }
            val nm = getSystemService(NotificationManager::class.java)
            nm?.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(text: String): Notification {
        val openIntent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this, 0, openIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("POBS Pixel Camera")
            .setContentText(text)
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val nm = getSystemService(NotificationManager::class.java)
        nm?.notify(NOTIFICATION_ID, buildNotification(text))
    }
}
