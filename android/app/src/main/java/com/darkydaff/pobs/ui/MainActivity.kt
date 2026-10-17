package com.darkydaff.pobs.ui

import android.Manifest
import android.app.Dialog
import android.app.PictureInPictureParams
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.content.res.Configuration
import android.graphics.Matrix
import android.graphics.RectF
import android.graphics.SurfaceTexture
import android.hardware.SensorManager
import android.os.Build
import android.os.Bundle
import android.util.Rational
import android.view.*
import android.widget.*
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.cardview.widget.CardView
import androidx.core.content.ContextCompat
import com.darkydaff.pobs.R
import com.darkydaff.pobs.data.BatteryRepository
import com.darkydaff.pobs.data.PreferencesRepository
import com.darkydaff.pobs.service.CameraStreamingService
import com.darkydaff.pobs.service.CameraStreamingService.OrientationMode
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.switchmaterial.SwitchMaterial

class MainActivity : AppCompatActivity() {

    private lateinit var prefsRepo: PreferencesRepository
    private lateinit var batteryRepo: BatteryRepository

    private lateinit var viewfinderCard: CardView
    private lateinit var previewTextureView: AutoFitTextureView
    private lateinit var framingOverlay: StudioFramingOverlayView
    private lateinit var overlayControls: View
    private lateinit var tvStreamBadge: TextView
    private lateinit var tvBattery: TextView
    private lateinit var ivBattery: ImageView
    private lateinit var tvOrientationBadge: TextView
    private lateinit var statusDot: View
    private lateinit var btnEnterPip: ImageButton
    private lateinit var btnOpenSettings: ImageButton

    private lateinit var toggleResolution: MaterialButtonToggleGroup
    private lateinit var btnRes720: MaterialButton
    private lateinit var btnRes1080: MaterialButton
    private lateinit var btnRes4K: MaterialButton

    private lateinit var toggleFps: MaterialButtonToggleGroup
    private lateinit var btnFps30: MaterialButton
    private lateinit var btnFps60: MaterialButton

    private lateinit var btnFlipCamera: MaterialButton
    private lateinit var btnOrientation: MaterialButton
    private lateinit var btnGrid: MaterialButton
    private lateinit var btnTorch: MaterialButton
    private lateinit var btnDimScreen: MaterialButton
    private lateinit var tvZoomBadge: TextView
    private lateinit var btnPauseStream: ImageButton
    private lateinit var pauseOverlay: View
    private lateinit var btnResumeFromOverlay: MaterialButton

    private var orientationEventListener: OrientationEventListener? = null
    private var isUpdatingTogglesProgrammatically = false
    private var isScreenDimmed = false

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        if (permissions[Manifest.permission.CAMERA] == true) {
            val host = prefsRepo.getLastHost() ?: "127.0.0.1"
            startStreamingService(host)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        prefsRepo = PreferencesRepository(this)
        batteryRepo = BatteryRepository(this)

        viewfinderCard = findViewById(R.id.viewfinderCard)
        viewfinderCard.clipToOutline = true

        previewTextureView = findViewById(R.id.previewTextureView)
        framingOverlay = findViewById(R.id.framingOverlay)
        overlayControls = findViewById(R.id.overlayControls)
        tvStreamBadge = findViewById(R.id.tvStreamBadge)
        tvBattery = findViewById(R.id.tvBattery)
        ivBattery = findViewById(R.id.ivBattery)
        tvOrientationBadge = findViewById(R.id.tvOrientationBadge)
        statusDot = findViewById(R.id.statusDot)
        btnEnterPip = findViewById(R.id.btnEnterPip)
        btnOpenSettings = findViewById(R.id.btnOpenSettings)

        toggleResolution = findViewById(R.id.toggleResolution)
        btnRes720 = findViewById(R.id.btnRes720)
        btnRes1080 = findViewById(R.id.btnRes1080)
        btnRes4K = findViewById(R.id.btnRes4K)

        toggleFps = findViewById(R.id.toggleFps)
        btnFps30 = findViewById(R.id.btnFps30)
        btnFps60 = findViewById(R.id.btnFps60)

        btnFlipCamera = findViewById(R.id.btnFlipCamera)
        btnOrientation = findViewById(R.id.btnOrientation)
        btnGrid = findViewById(R.id.btnGrid)
        btnTorch = findViewById(R.id.btnTorch)
        btnDimScreen = findViewById(R.id.btnDimScreen)
        tvZoomBadge = findViewById(R.id.tvZoomBadge)
        btnPauseStream = findViewById(R.id.btnPauseStream)
        pauseOverlay = findViewById(R.id.pauseOverlay)
        btnResumeFromOverlay = findViewById(R.id.btnResumeFromOverlay)

        // TextureView surface listener
        previewTextureView.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
                surfaceTexture.setDefaultBufferSize(
                    CameraStreamingService.activeResolutionWidth,
                    CameraStreamingService.activeResolutionHeight
                )
                CameraStreamingService.updatePreviewSurface(Surface(surfaceTexture))
                configureTransform(width, height)
            }

            override fun onSurfaceTextureSizeChanged(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
                configureTransform(width, height)
            }

            override fun onSurfaceTextureDestroyed(surfaceTexture: SurfaceTexture): Boolean {
                CameraStreamingService.updatePreviewSurface(null)
                return true
            }

            override fun onSurfaceTextureUpdated(surfaceTexture: SurfaceTexture) {}
        }

        // Gestures: Pinch to zoom + Tap to focus ring + Double tap to flip camera
        val scaleDetector = ScaleGestureDetector(this, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                val current = CameraStreamingService.currentZoomRatio
                val targetZoom = current * detector.scaleFactor
                CameraStreamingService.setZoom(targetZoom)
                return true
            }
        })

        val gestureDetector = GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
                framingOverlay.triggerFocusAnimation(e.x, e.y)
                previewTextureView.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                return true
            }

            override fun onDoubleTap(e: MotionEvent): Boolean {
                previewTextureView.performHapticFeedback(HapticFeedbackConstants.CONFIRM)
                CameraStreamingService.switchCamera()
                return true
            }
        })

        val touchListener = View.OnTouchListener { _, event ->
            val scaleHandled = scaleDetector.onTouchEvent(event)
            val gestureHandled = gestureDetector.onTouchEvent(event)
            scaleHandled || gestureHandled || true
        }
        previewTextureView.setOnTouchListener(touchListener)
        framingOverlay.setOnTouchListener(touchListener)

        btnPauseStream.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            val nextState = !CameraStreamingService.isStreamingActive
            CameraStreamingService.setStreamActive(nextState)
            updateUi()
        }

        btnResumeFromOverlay.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            CameraStreamingService.setStreamActive(true)
            updateUi()
        }

        btnOrientation.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            val nextMode = when (CameraStreamingService.currentOrientationMode) {
                OrientationMode.AUTO -> OrientationMode.VERTICAL
                OrientationMode.VERTICAL -> OrientationMode.HORIZONTAL
                OrientationMode.HORIZONTAL -> OrientationMode.AUTO
            }
            CameraStreamingService.setOrientationMode(nextMode)
            updateUi()
        }

        btnGrid.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            framingOverlay.isGridEnabled = !framingOverlay.isGridEnabled
            val color = if (framingOverlay.isGridEnabled) 0xFF60A5FA.toInt() else 0xFF71717A.toInt()
            btnGrid.iconTint = ColorStateList.valueOf(color)
        }

        orientationEventListener = object : OrientationEventListener(this, SensorManager.SENSOR_DELAY_UI) {
            override fun onOrientationChanged(orientation: Int) {
                if (orientation != ORIENTATION_UNKNOWN) {
                    CameraStreamingService.updatePhysicalOrientation(orientation)
                }
            }
        }

        initFormatToggles()

        btnFlipCamera.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            CameraStreamingService.switchCamera()
        }

        btnTorch.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            CameraStreamingService.toggleTorch()
        }

        btnDimScreen.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            toggleScreenDim()
        }

        btnEnterPip.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            enterPipMode()
        }

        btnOpenSettings.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            showSettingsDialog()
        }

        checkPermissionsAndStart()
    }

    private fun configureTransform(viewWidth: Int, viewHeight: Int) {
        if (viewWidth == 0 || viewHeight == 0) return
        val matrix = Matrix()
        val centerX = viewWidth / 2f
        val centerY = viewHeight / 2f

        val facing = CameraStreamingService.activeFacing.lowercase()
        val isFront = facing == "front"
        val activeOrient = CameraStreamingService.activeOrientation

        if (isFront) {
            matrix.postScale(-1f, 1f, centerX, centerY)
        }
        previewTextureView.setTransform(matrix)
    }

    private fun initFormatToggles() {
        when (prefsRepo.getResolution()) {
            "3840x2160" -> toggleResolution.check(R.id.btnRes4K)
            "1280x720" -> toggleResolution.check(R.id.btnRes720)
            else -> toggleResolution.check(R.id.btnRes1080)
        }

        if (prefsRepo.getFps() == 30) {
            toggleFps.check(R.id.btnFps30)
        } else {
            toggleFps.check(R.id.btnFps60)
        }

        toggleResolution.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (isChecked && !isUpdatingTogglesProgrammatically) {
                val newRes = when (checkedId) {
                    R.id.btnRes4K -> "3840x2160"
                    R.id.btnRes720 -> "1280x720"
                    else -> "1920x1080"
                }
                prefsRepo.setResolution(newRes)
                triggerReconfiguration()
            }
        }

        toggleFps.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (isChecked && !isUpdatingTogglesProgrammatically) {
                val newFps = if (checkedId == R.id.btnFps30) 30 else 60
                prefsRepo.setFps(newFps)
                triggerReconfiguration()
            }
        }
    }

    private fun triggerReconfiguration() {
        val intent = Intent(this, CameraStreamingService::class.java).apply {
            action = CameraStreamingService.ACTION_RECONFIGURE
        }
        startService(intent)
        updateUi()
    }

    override fun onPause() {
        super.onPause()
        orientationEventListener?.disable()
        CameraStreamingService.onFormatChangedListener = null
        CameraStreamingService.onZoomChangedListener = null
        CameraStreamingService.onStreamStateListener = null
        CameraStreamingService.onOrientationChangedListener = null
        CameraStreamingService.onStateChangedListener = null
    }

    override fun onResume() {
        super.onResume()
        orientationEventListener?.enable()

        CameraStreamingService.onFormatChangedListener = { res, fps ->
            runOnUiThread {
                isUpdatingTogglesProgrammatically = true
                when (res) {
                    "3840x2160" -> toggleResolution.check(R.id.btnRes4K)
                    "1280x720" -> toggleResolution.check(R.id.btnRes720)
                    else -> toggleResolution.check(R.id.btnRes1080)
                }
                if (fps == 30) {
                    toggleFps.check(R.id.btnFps30)
                } else {
                    toggleFps.check(R.id.btnFps60)
                }
                isUpdatingTogglesProgrammatically = false
            }
        }
        CameraStreamingService.onZoomChangedListener = { ratio ->
            runOnUiThread {
                tvZoomBadge.text = String.format("%.1fx", ratio)
            }
        }
        CameraStreamingService.onStreamStateListener = {
            runOnUiThread {
                updateUi()
            }
        }
        CameraStreamingService.onOrientationChangedListener = { _, _, _ ->
            runOnUiThread {
                updateUi()
                configureTransform(previewTextureView.width, previewTextureView.height)
            }
        }
        CameraStreamingService.onStateChangedListener = {
            runOnUiThread {
                updateUi()
            }
        }
        updateUi()

        if (previewTextureView.isAvailable) {
            val surface = Surface(previewTextureView.surfaceTexture)
            CameraStreamingService.updatePreviewSurface(surface)
            configureTransform(previewTextureView.width, previewTextureView.height)
        }
    }

    private fun checkPermissionsAndStart() {
        val permissions = mutableListOf(Manifest.permission.CAMERA)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.POST_NOTIFICATIONS)
        }

        val missing = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (missing.isNotEmpty()) {
            permissionLauncher.launch(missing.toTypedArray())
        } else {
            val host = prefsRepo.getLastHost() ?: "127.0.0.1"
            startStreamingService(host)
        }
    }

    private fun startStreamingService(host: String) {
        val intent = Intent(this, CameraStreamingService::class.java).apply {
            action = CameraStreamingService.ACTION_START
            putExtra(CameraStreamingService.EXTRA_HOST, host)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent)
        } else {
            startService(intent)
        }
        updateUi()
    }

    private fun updateUi() {
        val pct = CameraStreamingService.batteryPercentage
        val isCharging = CameraStreamingService.isBatteryCharging
        ivBattery.setImageResource(if (isCharging) R.drawable.ic_battery_charging else R.drawable.ic_battery)
        tvBattery.text = if (isCharging) "$pct% (Charging)" else "$pct%"

        val isFront = CameraStreamingService.activeFacing.lowercase() == "front"
        if (isFront) {
            btnFlipCamera.text = "Front"
            btnFlipCamera.setBackgroundColor(0xFF2563EB.toInt())
            btnTorch.visibility = View.GONE
        } else {
            btnFlipCamera.text = "Back"
            btnFlipCamera.setBackgroundColor(0xFF1D4ED8.toInt())
            btnTorch.visibility = View.VISIBLE
        }

        val orientMode = CameraStreamingService.currentOrientationMode
        val activeOrient = CameraStreamingService.activeOrientation
        val orientText = if (activeOrient == "vertical") "📱 Vert" else "📺 Horiz"
        tvOrientationBadge.text = when (orientMode) {
            OrientationMode.AUTO -> "Auto • $orientText"
            OrientationMode.VERTICAL -> "Vert • $orientText"
            OrientationMode.HORIZONTAL -> "Horiz • $orientText"
        }

        when (orientMode) {
            OrientationMode.AUTO -> {
                btnOrientation.text = "Auto"
                btnOrientation.setIconResource(R.drawable.ic_orientation_auto)
                btnOrientation.iconTint = ColorStateList.valueOf(0xFF60A5FA.toInt())
            }
            OrientationMode.VERTICAL -> {
                btnOrientation.text = "Vert"
                btnOrientation.setIconResource(R.drawable.ic_orientation_vert)
                btnOrientation.iconTint = ColorStateList.valueOf(0xFF34D399.toInt())
            }
            OrientationMode.HORIZONTAL -> {
                btnOrientation.text = "Horiz"
                btnOrientation.setIconResource(R.drawable.ic_orientation_horiz)
                btnOrientation.iconTint = ColorStateList.valueOf(0xFFFBBF24.toInt())
            }
        }

        // Viewport aspect ratio stays constant (the physical phone rotates in hand)
        previewTextureView.setAspectRatio(9, 16)
        if (previewTextureView.width > 0 && previewTextureView.height > 0) {
            configureTransform(previewTextureView.width, previewTextureView.height)
        }

        val isStreamingActive = CameraStreamingService.isStreamingActive
        val isStreaming = CameraStreamingService.isStreaming
        val isConnected = CameraStreamingService.isConnectedToHost

        if (isStreamingActive) {
            btnPauseStream.setImageResource(R.drawable.ic_pause)
            pauseOverlay.visibility = View.GONE
            if (isStreaming && isConnected) {
                tvStreamBadge.text = "LIVE"
                tvStreamBadge.setTextColor(0xFF22C55E.toInt())
                statusDot.setBackgroundColor(0xFF22C55E.toInt())
                framingOverlay.setBackgroundResource(R.drawable.bg_tally_live)
            } else {
                tvStreamBadge.text = "STANDBY"
                tvStreamBadge.setTextColor(0xFF71717A.toInt())
                statusDot.setBackgroundColor(0xFF71717A.toInt())
                framingOverlay.setBackgroundResource(R.drawable.bg_tally_idle)
            }
        } else {
            btnPauseStream.setImageResource(R.drawable.ic_play)
            pauseOverlay.visibility = View.VISIBLE
            tvStreamBadge.text = "PAUSED"
            tvStreamBadge.setTextColor(0xFFF59E0B.toInt())
            statusDot.setBackgroundColor(0xFFF59E0B.toInt())
            framingOverlay.setBackgroundResource(R.drawable.bg_tally_paused)
        }

        tvZoomBadge.text = String.format("%.1fx", CameraStreamingService.currentZoomRatio)

        isUpdatingTogglesProgrammatically = true
        when (CameraStreamingService.activeResolution) {
            "3840x2160" -> toggleResolution.check(R.id.btnRes4K)
            "1280x720" -> toggleResolution.check(R.id.btnRes720)
            else -> toggleResolution.check(R.id.btnRes1080)
        }
        if (CameraStreamingService.activeFps == 30) {
            toggleFps.check(R.id.btnFps30)
        } else {
            toggleFps.check(R.id.btnFps60)
        }
        isUpdatingTogglesProgrammatically = false

        updateTorchButtonStyle(CameraStreamingService.isTorchOn)
    }

    private fun updateTorchButtonStyle(enabled: Boolean) {
        if (enabled) {
            btnTorch.iconTint = ColorStateList.valueOf(0xFF09090B.toInt())
            btnTorch.setBackgroundColor(0xFFEAB308.toInt())
        } else {
            btnTorch.iconTint = ColorStateList.valueOf(0xFFD4D4D8.toInt())
            btnTorch.setBackgroundColor(0xFF27272A.toInt())
        }
    }

    private fun toggleScreenDim() {
        isScreenDimmed = !isScreenDimmed
        val layoutParams = window.attributes
        if (isScreenDimmed) {
            layoutParams.screenBrightness = 0.01f
            btnDimScreen.setBackgroundColor(0xFF3F3F46.toInt())
        } else {
            layoutParams.screenBrightness = WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE
            btnDimScreen.setBackgroundColor(0xFF27272A.toInt())
        }
        window.attributes = layoutParams
    }

    private fun enterPipMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            try {
                val isLandscape = CameraStreamingService.activeOrientation == "horizontal"
                val ratio = if (isLandscape) Rational(16, 9) else Rational(9, 16)
                val params = PictureInPictureParams.Builder()
                    .setAspectRatio(ratio)
                    .build()
                enterPictureInPictureMode(params)
            } catch (_: Exception) {
                moveTaskToBack(true)
            }
        } else {
            moveTaskToBack(true)
        }
    }

    override fun onPictureInPictureModeChanged(
        isInPictureInPictureMode: Boolean,
        newConfig: Configuration
    ) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        if (isInPictureInPictureMode) {
            overlayControls.visibility = View.GONE
        } else {
            overlayControls.visibility = View.VISIBLE
            updateUi()
        }
    }

    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        if (CameraStreamingService.isStreaming) {
            enterPipMode()
        }
    }

    private fun showSettingsDialog() {
        val dialog = Dialog(this)
        dialog.setContentView(R.layout.dialog_settings)
        dialog.window?.setLayout(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        )

        val switchStab = dialog.findViewById<SwitchMaterial>(R.id.switchStabilization)
        val switchLowLatency = dialog.findViewById<SwitchMaterial>(R.id.switchUltraLowLatency)
        val etCustomHost = dialog.findViewById<com.google.android.material.textfield.TextInputEditText>(R.id.etCustomHost)
        val btnSave = dialog.findViewById<MaterialButton>(R.id.btnSaveSettings)

        switchStab.isChecked = prefsRepo.isStabilizationEnabled()
        switchLowLatency.isChecked = prefsRepo.isUltraLowLatency()
        val currentHost = prefsRepo.getLastHost() ?: "127.0.0.1"
        etCustomHost?.setText(currentHost)

        btnSave.setOnClickListener {
            prefsRepo.setStabilizationEnabled(switchStab.isChecked)
            prefsRepo.setUltraLowLatency(switchLowLatency.isChecked)
            val newHost = etCustomHost?.text?.toString()?.trim()
            val hostToSave = if (newHost.isNullOrEmpty()) "127.0.0.1" else newHost
            val hostChanged = (hostToSave != prefsRepo.getLastHost())
            prefsRepo.saveLastHost(hostToSave)

            triggerReconfiguration()
            if (hostChanged) {
                startStreamingService(hostToSave)
            }
            dialog.dismiss()
        }

        dialog.show()
    }
}
