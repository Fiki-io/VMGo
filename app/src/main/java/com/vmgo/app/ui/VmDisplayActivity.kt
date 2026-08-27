package com.vmgo.app.ui

import android.annotation.SuppressLint
import android.content.Intent
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Bundle
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.vmgo.app.core.NativeVmEngine
import com.vmgo.app.databinding.ActivityVmDisplayBinding
import com.vmgo.app.model.VmConfig
import com.vmgo.app.service.HalBridgeService
import com.vmgo.app.util.AppLogger
import kotlinx.coroutines.*
import java.io.File

class VmDisplayActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private lateinit var binding: ActivityVmDisplayBinding
    private var vmConfig: VmConfig? = null
    private var isVmInitialized = false
    private var renderJob: Job? = null
    private var isRunning = true

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

        binding.vmSurfaceView.holder.addCallback(this)
        setupTouchForwarding()
        setupNavBar()
        setupFloatingBall()

        try {
            startService(Intent(this, HalBridgeService::class.java))
        } catch (e: Exception) {
            AppLogger.e("VmDisplayActivity", "Failed to start HalBridgeService", e)
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

            // Start animated boot display
            startBootDisplayLoop(holder, config)
        } catch (e: Throwable) {
            AppLogger.fatal("VmDisplayActivity", "Exception during VM surface initialization", e)
        }
    }

    private fun startBootDisplayLoop(holder: SurfaceHolder, config: VmConfig) {
        renderJob?.cancel()
        renderJob = CoroutineScope(Dispatchers.Default).launch {
            val bgPaint = Paint().apply { color = Color.parseColor("#0A0E17") }
            val logoPaint = Paint().apply {
                color = Color.parseColor("#00E5FF")
                textSize = 72f
                typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
                textAlign = Paint.Align.CENTER
                isAntiAlias = true
            }
            val statusPaint = Paint().apply {
                color = Color.parseColor("#94A3B8")
                textSize = 36f
                textAlign = Paint.Align.CENTER
                isAntiAlias = true
            }
            val glowPaint = Paint().apply {
                color = Color.parseColor("#1A00E5FF")
                isAntiAlias = true
            }

            var frameCount = 0
            val statusMessages = listOf(
                "Initializing VFS Sandbox...",
                "Mounting GSI System Partition...",
                "Starting Virtual Hardware HAL...",
                "Connecting QEMU Pipe Server...",
                "VM Container Active & Running"
            )

            while (isRunning && isActive) {
                try {
                    val canvas: Canvas? = holder.lockCanvas()
                    if (canvas != null) {
                        try {
                            val w = canvas.width.toFloat()
                            val h = canvas.height.toFloat()

                            // Background
                            canvas.drawRect(0f, 0f, w, h, bgPaint)

                            // Glowing circle animation
                            val pulse = (Math.sin(frameCount * 0.1) * 20).toFloat()
                            canvas.drawCircle(w / 2f, h / 2f - 100f, 120f + pulse, glowPaint)

                            // Logo
                            canvas.drawText("VM Go", w / 2f, h / 2f - 80f, logoPaint)

                            // Animated status
                            val msgIdx = (frameCount / 30).coerceAtMost(statusMessages.size - 1)
                            canvas.drawText(statusMessages[msgIdx], w / 2f, h / 2f + 40f, statusPaint)
                            canvas.drawText("${config.osVersion} • ${config.displayWidth}x${config.displayHeight}", w / 2f, h / 2f + 100f, statusPaint)

                        } finally {
                            holder.unlockCanvasAndPost(canvas)
                        }
                    }
                } catch (e: Exception) {
                    // Surface destroyed or locked
                    break
                }

                frameCount++
                delay(33) // ~30 FPS boot loop
            }
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
        renderJob?.cancel()
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
                    Toast.makeText(this, "VM Go Active • Slot: ${vmConfig?.id}", Toast.LENGTH_SHORT).show()
                    true
                }
                else -> false
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        isRunning = false
        renderJob?.cancel()
        try {
            stopService(Intent(this, HalBridgeService::class.java))
            NativeVmEngine.nativeStopVm()
        } catch (e: Throwable) {
            AppLogger.e("VmDisplayActivity", "Error in onDestroy", e)
        }
    }
}
