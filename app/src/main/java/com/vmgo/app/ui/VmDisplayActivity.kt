package com.vmgo.app.ui

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

class VmDisplayActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private lateinit var binding: ActivityVmDisplayBinding
    private var vmConfig: VmConfig? = null
    private var isVmInitialized = false

    @SuppressLint("ClickableViewAccessibility")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityVmDisplayBinding.inflate(layoutInflater)
        setContentView(binding.root)

        vmConfig = intent.getSerializableExtra("VM_CONFIG") as? VmConfig
        if (vmConfig == null) {
            Toast.makeText(this, "Invalid VM Configuration", Toast.LENGTH_SHORT).show()
            finish()
            return
        }

        binding.vmSurfaceView.holder.addCallback(this)
        setupTouchForwarding()
        setupNavBar()
        setupFloatingBall()

        // Start Hardware Sensor & Location Bridge
        startService(Intent(this, HalBridgeService::class.java))
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val config = vmConfig ?: return
        if (!isVmInitialized) {
            isVmInitialized = NativeVmEngine.initializeVm(config)
        }
        NativeVmEngine.nativeSetSurface(
            holder.surface,
            config.displayWidth,
            config.displayHeight
        )
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        NativeVmEngine.nativeSetSurface(holder.surface, width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        NativeVmEngine.nativeDestroySurface()
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

            NativeVmEngine.nativeSendTouchEvent(
                action = action,
                pIds = ids,
                pXs = xs,
                pYs = ys,
                pPressures = pressures,
                pSizes = sizes,
                pointerCount = pointerCount
            )
            true
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupNavBar() {
        // Back Button (Android KeyCode 4)
        binding.btnNavBack.setOnClickListener {
            NativeVmEngine.nativeSendKeyEvent(4, true)
            binding.btnNavBack.postDelayed({ NativeVmEngine.nativeSendKeyEvent(4, false) }, 50)
        }

        // Home Button (Android KeyCode 3)
        binding.btnNavHome.setOnClickListener {
            NativeVmEngine.nativeSendKeyEvent(3, true)
            binding.btnNavHome.postDelayed({ NativeVmEngine.nativeSendKeyEvent(3, false) }, 50)
        }

        // Recents / App Switch Button (Android KeyCode 187)
        binding.btnNavRecents.setOnClickListener {
            NativeVmEngine.nativeSendKeyEvent(187, true)
            binding.btnNavRecents.postDelayed({ NativeVmEngine.nativeSendKeyEvent(187, false) }, 50)
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
                    // Show Quick Menu or Settings
                    Toast.makeText(this, "VM Quick Controls", Toast.LENGTH_SHORT).show()
                    true
                }
                else -> false
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopService(Intent(this, HalBridgeService::class.java))
        NativeVmEngine.nativeStopVm()
    }
}
