package com.vmgo.app.ui

import android.animation.ObjectAnimator
import android.annotation.SuppressLint
import android.content.Intent
import android.os.Bundle
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.vmgo.app.core.NativeVmEngine
import com.vmgo.app.databinding.ActivityVmDisplayBinding
import com.vmgo.app.model.VmConfig
import com.vmgo.app.service.HalBridgeService
import com.vmgo.app.util.AppLogger
import kotlinx.coroutines.*

class VmDisplayActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private lateinit var binding: ActivityVmDisplayBinding
    private var vmConfig: VmConfig? = null
    private var isVmInitialized = false
    private val activityScope = CoroutineScope(Dispatchers.Main + Job())

    @SuppressLint("ClickableViewAccessibility")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityVmDisplayBinding.inflate(layoutInflater)
        setContentView(binding.root)

        vmConfig = intent.getSerializableExtra("VM_CONFIG") as? VmConfig
        if (vmConfig == null) {
            AppLogger.e("VmDisplayActivity", "Invalid VM Configuration passed in intent")
            Toast.makeText(this, "Invalid VM Configuration", Toast.LENGTH_SHORT).show()
            finish()
            return
        }

        binding.tvBootSlotInfo.text = "${vmConfig?.name} • ${vmConfig?.displayWidth}x${vmConfig?.displayHeight} • ${vmConfig?.osVersion}"

        binding.vmSurfaceView.holder.addCallback(this)
        setupTouchForwarding()
        setupNavBar()
        setupFloatingBall()
        startBootSequence()

        try {
            startService(Intent(this, HalBridgeService::class.java))
        } catch (e: Exception) {
            AppLogger.e("VmDisplayActivity", "Failed to start HalBridgeService", e)
        }
    }

    private fun startBootSequence() {
        // Pulsing logo animation
        val pulseX = ObjectAnimator.ofFloat(binding.ivBootLogo, "scaleX", 0.9f, 1.1f).apply {
            duration = 1000
            repeatMode = ObjectAnimator.REVERSE
            repeatCount = ObjectAnimator.INFINITE
            start()
        }
        val pulseY = ObjectAnimator.ofFloat(binding.ivBootLogo, "scaleY", 0.9f, 1.1f).apply {
            duration = 1000
            repeatMode = ObjectAnimator.REVERSE
            repeatCount = ObjectAnimator.INFINITE
            start()
        }

        activityScope.launch {
            binding.tvBootStatus.text = "Initializing VFS Sandbox..."
            delay(400)
            binding.tvBootStatus.text = "Mounting GSI Partitions (/system, /apex, /vendor)..."
            delay(500)
            binding.tvBootStatus.text = "Starting QEMU Pipe HAL Server..."
            delay(400)
            binding.tvBootStatus.text = "Connecting Display Compositor..."
            delay(600)
            binding.tvBootStatus.text = "VM Container Active & Running"

            // Fade out overlay when ready
            delay(1200)
            binding.bootOverlayLayout.animate()
                .alpha(0f)
                .setDuration(500)
                .withEndAction {
                    binding.bootOverlayLayout.visibility = View.GONE
                    pulseX.cancel()
                    pulseY.cancel()
                }
                .start()
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val config = vmConfig ?: return
        AppLogger.i("VmDisplayActivity", "Surface created for ${config.id} (${config.displayWidth}x${config.displayHeight})")

        try {
            if (!isVmInitialized) {
                isVmInitialized = NativeVmEngine.initializeVm(config)
            }

            NativeVmEngine.nativeSetSurface(
                holder.surface,
                config.displayWidth,
                config.displayHeight
            )
        } catch (e: Throwable) {
            AppLogger.fatal("VmDisplayActivity", "Exception during VM surface initialization", e)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        try {
            NativeVmEngine.nativeSetSurface(holder.surface, width, height)
        } catch (e: Throwable) {
            AppLogger.e("VmDisplayActivity", "Error in surfaceChanged", e)
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        try {
            NativeVmEngine.nativeDestroySurface()
        } catch (e: Throwable) {
            AppLogger.e("VmDisplayActivity", "Error in surfaceDestroyed", e)
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupTouchForwarding() {
        binding.vmSurfaceView.setOnTouchListener { _, event ->
            val pointerCount = event.pointerCount
            val ids = IntArray(pointerCount)
            val xs = FloatArray(pointerCount)
            val ys = FloatArray(pointerCount)
            val pressures = FloatArray(pointerCount)
            val sizes = FloatArray(pointerCount)

            for (i in 0 until pointerCount) {
                ids[i] = event.getPointerId(i)
                xs[i] = event.getX(i)
                ys[i] = event.getY(i)
                pressures[i] = event.getPressure(i)
                sizes[i] = event.getSize(i)
            }

            val action = when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> 0
                MotionEvent.ACTION_UP -> 1
                MotionEvent.ACTION_MOVE -> 2
                MotionEvent.ACTION_CANCEL -> 3
                MotionEvent.ACTION_POINTER_DOWN -> 5
                MotionEvent.ACTION_POINTER_UP -> 6
                else -> event.actionMasked
            }

            try {
                NativeVmEngine.nativeSendTouchEvent(
                    action = action,
                    pIds = ids,
                    pXs = xs,
                    pYs = ys,
                    pPressures = pressures,
                    pSizes = sizes,
                    pointerCount = pointerCount
                )
            } catch (e: Throwable) {
                // Ignore touch drop
            }
            true
        }
    }

    private fun setupNavBar() {
        binding.btnNavBack.setOnClickListener {
            try {
                NativeVmEngine.nativeSendKeyEvent(4, true)
                binding.btnNavBack.postDelayed({ NativeVmEngine.nativeSendKeyEvent(4, false) }, 50)
            } catch (e: Throwable) { e.printStackTrace() }
        }

        binding.btnNavHome.setOnClickListener {
            try {
                NativeVmEngine.nativeSendKeyEvent(3, true)
                binding.btnNavHome.postDelayed({ NativeVmEngine.nativeSendKeyEvent(3, false) }, 50)
            } catch (e: Throwable) { e.printStackTrace() }
        }

        binding.btnNavRecents.setOnClickListener {
            try {
                NativeVmEngine.nativeSendKeyEvent(187, true)
                binding.btnNavRecents.postDelayed({ NativeVmEngine.nativeSendKeyEvent(187, false) }, 50)
            } catch (e: Throwable) { e.printStackTrace() }
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupFloatingBall() {
        var dX = 0f
        var dY = 0f

        binding.floatingBall.setOnTouchListener { view, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    dX = view.x - event.rawX
                    dY = view.y - event.rawY
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    view.animate()
                        .x(event.rawX + dX)
                        .y(event.rawY + dY)
                        .setDuration(0)
                        .start()
                    true
                }
                MotionEvent.ACTION_UP -> {
                    Toast.makeText(this, "VM Go Active • ${vmConfig?.name}", Toast.LENGTH_SHORT).show()
                    true
                }
                else -> false
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        activityScope.cancel()
        try {
            stopService(Intent(this, HalBridgeService::class.java))
            NativeVmEngine.nativeStopVm()
        } catch (e: Throwable) {
            AppLogger.e("VmDisplayActivity", "Error in onDestroy", e)
        }
    }
}
