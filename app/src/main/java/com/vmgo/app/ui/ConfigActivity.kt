package com.vmgo.app.ui

import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.vmgo.app.databinding.ActivityConfigBinding
import com.vmgo.app.model.VmConfig

class ConfigActivity : AppCompatActivity() {

    private lateinit var binding: ActivityConfigBinding
    private var vmConfig: VmConfig? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityConfigBinding.inflate(layoutInflater)
        setContentView(binding.root)

        vmConfig = intent.getSerializableExtra("VM_CONFIG") as? VmConfig
        if (vmConfig == null) {
            finish()
            return
        }

        populateFields()
        setupListeners()
    }

    private fun populateFields() {
        val config = vmConfig ?: return
        binding.etResolution.setText("${config.displayWidth}x${config.displayHeight}")
        binding.etDpi.setText(config.displayDpi.toString())
        binding.etFps.setText(config.targetFps.toString())

        binding.switchMagisk.isChecked = config.enableRoot
        binding.switchGapps.isChecked = config.enableGapps

        binding.etBrand.setText(config.brand)
        binding.etModel.setText(config.model)
        binding.etSerial.setText(config.serial)
        binding.etImei.setText(config.imei)
    }

    private fun setupListeners() {
        binding.btnSaveConfig.setOnClickListener {
            val config = vmConfig ?: return@setOnClickListener

            // Parse Resolution
            val resStr = binding.etResolution.text.toString().trim()
            if (resStr.contains("x")) {
                val parts = resStr.split("x")
                val w = parts.getOrNull(0)?.toIntOrNull() ?: 1080
                val h = parts.getOrNull(1)?.toIntOrNull() ?: 1920
                config.displayWidth = w
                config.displayHeight = h
            }

            config.displayDpi = binding.etDpi.text.toString().toIntOrNull() ?: 480
            config.targetFps = binding.etFps.text.toString().toIntOrNull() ?: 60

            config.enableRoot = binding.switchMagisk.isChecked
            config.enableGapps = binding.switchGapps.isChecked

            config.brand = binding.etBrand.text.toString().trim()
            config.model = binding.etModel.text.toString().trim()
            config.serial = binding.etSerial.text.toString().trim()
            config.imei = binding.etImei.text.toString().trim()

            Toast.makeText(this, "Configuration Saved!", Toast.LENGTH_SHORT).show()
            finish()
        }
    }
}
