package com.darkydaff.pobs.ui

import android.Manifest
import android.app.Dialog
import android.app.PictureInPictureParams
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.util.Rational
import android.view.*
import android.widget.*
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.darkydaff.pobs.R
import com.darkydaff.pobs.data.BatteryRepository
import com.darkydaff.pobs.data.PreferencesRepository
import com.darkydaff.pobs.service.CameraStreamingService
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.switchmaterial.SwitchMaterial
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private lateinit var prefsRepo: PreferencesRepository
    private lateinit var batteryRepo: BatteryRepository

    private lateinit var previewSurfaceView: AutoFitSurfaceView
    private lateinit var overlayControls: View
    private lateinit var tvStreamBadge: TextView
    private lateinit var tvBattery: TextView
    private lateinit var ivBattery: ImageView
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
    private lateinit var btnTorch: MaterialButton
    private lateinit var btnDimScreen: MaterialButton
    private lateinit var btnScanQr: MaterialButton
    private lateinit var tvZoomBadge: TextView
    private lateinit var btnPauseStream: ImageButton
    private lateinit var pauseOverlay: View
    private lateinit var btnResumeFromOverlay: MaterialButton
    private var isUpdatingTogglesProgrammatically = false

    private var isScreenDimmed = false

    private val barcodeLauncher = registerForActivityResult(ScanContract()) { result ->
        if (result.contents != null) {
            val scanned = result.contents.trim()
            val host = scanned.removePrefix("pobs://")
            prefsRepo.saveLastHost(host)
            Toast.makeText(this, "Connecting to OBS ($host)...", Toast.LENGTH_SHORT).show()
            startStreamingService(host)
        }
    }

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

        previewSurfaceView = findViewById(R.id.previewSurfaceView)
        overlayControls = findViewById(R.id.overlayControls)
        tvStreamBadge = findViewById(R.id.tvStreamBadge)
        tvBattery = findViewById(R.id.tvBattery)
        ivBattery = findViewById(R.id.ivBattery)
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
        btnTorch = findViewById(R.id.btnTorch)
        btnDimScreen = findViewById(R.id.btnDimScreen)
        btnScanQr = findViewById(R.id.btnScanQr)
        tvZoomBadge = findViewById(R.id.tvZoomBadge)
        btnPauseStream = findViewById(R.id.btnPauseStream)
        pauseOverlay = findViewById(R.id.pauseOverlay)
        btnResumeFromOverlay = findViewById(R.id.btnResumeFromOverlay)

        // Pinch-to-zoom on camera preview
        val scaleDetector = ScaleGestureDetector(this, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                val current = CameraStreamingService.currentZoomRatio
                val targetZoom = current * detector.scaleFactor
                CameraStreamingService.setZoom(targetZoom)
                return true
            }
        })
        previewSurfaceView.setOnTouchListener { _, event ->
            scaleDetector.onTouchEvent(event)
            true
        }

        btnPauseStream.setOnClickListener {
            val nextState = !CameraStreamingService.isStreamingActive
            CameraStreamingService.setStreamActive(nextState)
            updateUi()
        }

        btnResumeFromOverlay.setOnClickListener {
            CameraStreamingService.setStreamActive(true)
            updateUi()
        }

        // Set proper aspect ratio to avoid squishing
        updateSurfaceAspectRatio()
        previewSurfaceView.holder.addCallback(this)

        // Setup format toggle groups
        initFormatToggles()

        btnFlipCamera.setOnClickListener {
            CameraStreamingService.switchCamera()
        }

        btnTorch.setOnClickListener {
            CameraStreamingService.toggleTorch()
        }

        btnDimScreen.setOnClickListener {
            toggleScreenDim()
        }

        btnEnterPip.setOnClickListener {
            enterPipMode()
        }

        btnOpenSettings.setOnClickListener {
            showSettingsDialog()
        }

        btnScanQr.setOnClickListener {
            val options = ScanOptions().apply {
                setPrompt("Scan OBS Studio QR Code")
                setBeepEnabled(true)
                setOrientationLocked(false)
                setBarcodeImageEnabled(false)
            }
            barcodeLauncher.launch(options)
        }

        checkPermissionsAndStart()
    }

    private fun updateSurfaceAspectRatio() {
        val orientation = resources.configuration.orientation
        if (orientation == Configuration.ORIENTATION_LANDSCAPE) {
            previewSurfaceView.setAspectRatio(16, 9)
        } else {
            previewSurfaceView.setAspectRatio(9, 16)
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        updateSurfaceAspectRatio()
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
        CameraStreamingService.onFormatChangedListener = null
        CameraStreamingService.onZoomChangedListener = null
        CameraStreamingService.onStreamStateListener = null
        CameraStreamingService.onStateChangedListener = null
    }

    override fun onResume() {
        super.onResume()
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
        CameraStreamingService.onStateChangedListener = {
            runOnUiThread {
                updateUi()
            }
        }
        updateUi()
        if (previewSurfaceView.holder.surface?.isValid == true) {
            CameraStreamingService.updatePreviewSurface(previewSurfaceView.holder.surface)
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        CameraStreamingService.updatePreviewSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        CameraStreamingService.updatePreviewSurface(holder.surface)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        CameraStreamingService.updatePreviewSurface(null)
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

        val isStreamingActive = CameraStreamingService.isStreamingActive
        if (isStreamingActive) {
            btnPauseStream.setImageResource(R.drawable.ic_pause)
            pauseOverlay.visibility = View.GONE
            if (CameraStreamingService.isStreaming) {
                tvStreamBadge.text = "LIVE"
                tvStreamBadge.setTextColor(0xFF22C55E.toInt())
                statusDot.setBackgroundColor(0xFF22C55E.toInt())
            } else {
                tvStreamBadge.text = "STANDBY"
                tvStreamBadge.setTextColor(0xFF71717A.toInt())
                statusDot.setBackgroundColor(0xFF71717A.toInt())
            }
        } else {
            btnPauseStream.setImageResource(R.drawable.ic_play)
            pauseOverlay.visibility = View.VISIBLE
            tvStreamBadge.text = "PAUSED"
            tvStreamBadge.setTextColor(0xFFF59E0B.toInt())
            statusDot.setBackgroundColor(0xFFF59E0B.toInt())
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
            btnTorch.iconTint = android.content.res.ColorStateList.valueOf(0xFF09090B.toInt())
            btnTorch.setBackgroundColor(0xFFEAB308.toInt())
        } else {
            btnTorch.iconTint = android.content.res.ColorStateList.valueOf(0xFFD4D4D8.toInt())
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
                // Non-squished aspect ratio matching orientation
                val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
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
        val btnSave = dialog.findViewById<MaterialButton>(R.id.btnSaveSettings)

        switchStab.isChecked = prefsRepo.isStabilizationEnabled()
        switchLowLatency.isChecked = prefsRepo.isUltraLowLatency()

        btnSave.setOnClickListener {
            prefsRepo.setStabilizationEnabled(switchStab.isChecked)
            prefsRepo.setUltraLowLatency(switchLowLatency.isChecked)
            triggerReconfiguration()
            dialog.dismiss()
        }

        dialog.show()
    }
}
