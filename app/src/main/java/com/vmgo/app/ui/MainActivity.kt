package com.vmgo.app.ui

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.net.Uri
import android.os.Bundle
import android.os.IBinder
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.vmgo.app.R
import com.vmgo.app.core.NativeVmEngine
import com.vmgo.app.databinding.ActivityMainBinding
import com.vmgo.app.databinding.ItemVmSlotBinding
import com.vmgo.app.model.VmInstance
import com.vmgo.app.model.VmStatus
import com.vmgo.app.service.GsiImportService
import com.vmgo.app.util.AppLogger
import com.vmgo.app.util.StorageUtil
import java.io.File

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private val vmSlots = mutableListOf<VmInstance>()
    private lateinit var adapter: VmSlotAdapter

    private var gsiImportService: GsiImportService? = null
    private var isServiceBound = false
    private var selectedTargetSlotId: String = "slot_1"

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as GsiImportService.LocalBinder
            gsiImportService = binder.getService()
            isServiceBound = true
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            gsiImportService = null
            isServiceBound = false
        }
    }

    private val pickGsiLauncher = registerForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri != null) {
            handleGsiSelected(uri)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        try {
            binding.tvEngineVersion.text = NativeVmEngine.nativeGetEngineVersion()
        } catch (e: Throwable) {
            binding.tvEngineVersion.text = getString(R.string.tagline)
        }

        setupRecyclerView()
        setupListeners()
        loadSlots()

        val intent = Intent(this, GsiImportService::class.java)
        bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    override fun onResume() {
        super.onResume()
        loadSlots()
    }

    private fun setupRecyclerView() {
        adapter = VmSlotAdapter(
            items = vmSlots,
            onStartClick = { instance -> handleStartClick(instance) },
            onConfigClick = { instance -> openConfig(instance) }
        )
        binding.rvSlots.layoutManager = LinearLayoutManager(this)
        binding.rvSlots.adapter = adapter
    }

    private fun setupListeners() {
        binding.btnSelectGsi.setOnClickListener {
            selectedTargetSlotId = vmSlots.firstOrNull()?.config?.id ?: "slot_1"
            pickGsiLauncher.launch("*/*")
        }

        binding.btnOpenLogs.setOnClickListener {
            startActivity(Intent(this, LogsActivity::class.java))
        }

        binding.fabCreateVm.setOnClickListener {
            val nextIndex = vmSlots.size + 1
            val newConfig = StorageUtil.prepareSlotDirectories(this, "slot_$nextIndex")
            vmSlots.add(VmInstance(newConfig))
            adapter.notifyItemInserted(vmSlots.size - 1)
            AppLogger.i("MainActivity", "Created new VM Slot: slot_$nextIndex")
            Toast.makeText(this, "Created VM Slot $nextIndex", Toast.LENGTH_SHORT).show()
        }
    }

    private fun isRomInstalled(instance: VmInstance): Boolean {
        val rawFile = File(instance.config.systemPath, "system.raw.img")
        return rawFile.exists() && rawFile.length() > 0
    }

    private fun loadSlots() {
        vmSlots.clear()
        val baseDir = StorageUtil.getSlotsBaseDir(this)
        val slots = baseDir.listFiles()?.filter { it.isDirectory && it.name.startsWith("slot_") }?.sortedBy { it.name }

        if (!slots.isNullOrEmpty()) {
            for (slot in slots) {
                val config = StorageUtil.prepareSlotDirectories(this, slot.name)
                val instance = VmInstance(config)
                instance.status = if (isRomInstalled(instance)) VmStatus.STOPPED else VmStatus.ERROR
                vmSlots.add(instance)
            }
        } else {
            val defaultSlot = StorageUtil.prepareSlotDirectories(this, "slot_1")
            val instance = VmInstance(defaultSlot)
            instance.status = if (isRomInstalled(instance)) VmStatus.STOPPED else VmStatus.ERROR
            vmSlots.add(instance)
        }
        adapter.notifyDataSetChanged()
    }

    private fun handleGsiSelected(uri: Uri) {
        val targetSlot = vmSlots.find { it.config.id == selectedTargetSlotId } ?: vmSlots.firstOrNull() ?: return
        binding.progressBarImport.visibility = View.VISIBLE
        binding.tvImportStatus.visibility = View.VISIBLE
        binding.btnSelectGsi.isEnabled = false

        AppLogger.i("MainActivity", "Starting GSI import from URI: $uri to slot ${targetSlot.config.id}")

        gsiImportService?.importGsi(
            context = this,
            sourceUri = uri,
            slotId = targetSlot.config.id,
            onProgress = { percent, status ->
                binding.progressBarImport.progress = percent
                binding.tvImportStatus.text = "$status ($percent%)"
            },
            onComplete = { success, updatedConfig ->
                binding.progressBarImport.visibility = View.GONE
                binding.tvImportStatus.visibility = View.GONE
                binding.btnSelectGsi.isEnabled = true

                if (success && updatedConfig != null) {
                    AppLogger.i("MainActivity", "GSI Imported successfully to slot ${targetSlot.config.id}")
                    Toast.makeText(this, "GSI ROM Imported successfully to ${targetSlot.config.name}!", Toast.LENGTH_LONG).show()
                    loadSlots()
                } else {
                    AppLogger.e("MainActivity", "Failed to import GSI image")
                    Toast.makeText(this, "Failed to import GSI image", Toast.LENGTH_LONG).show()
                }
            }
        )
    }

    private fun handleStartClick(instance: VmInstance) {
        if (!isRomInstalled(instance)) {
            AlertDialog.Builder(this)
                .setTitle("GSI ROM Belum Terpasang")
                .setMessage("Slot ini belum memiliki image GSI ROM. Silakan pilih file GSI ROM (.img / .xz / .zip) terlebih dahulu.")
                .setPositiveButton("Choose GSI") { _, _ ->
                    selectedTargetSlotId = instance.config.id
                    pickGsiLauncher.launch("*/*")
                }
                .setNegativeButton("Cancel", null)
                .show()
            return
        }

        AppLogger.i("MainActivity", "Launching VM Display Activity for ${instance.config.id}")
        val intent = Intent(this, VmDisplayActivity::class.java).apply {
            putExtra("VM_CONFIG", instance.config)
        }
        startActivity(intent)
    }

    private fun openConfig(instance: VmInstance) {
        val intent = Intent(this, ConfigActivity::class.java).apply {
            putExtra("VM_CONFIG", instance.config)
        }
        startActivity(intent)
    }

    override fun onDestroy() {
        super.onDestroy()
        if (isServiceBound) {
            unbindService(serviceConnection)
        }
    }

    inner class VmSlotAdapter(
        private val items: List<VmInstance>,
        private val onStartClick: (VmInstance) -> Unit,
        private val onConfigClick: (VmInstance) -> Unit
    ) : RecyclerView.Adapter<VmSlotAdapter.ViewHolder>() {

        inner class ViewHolder(val binding: ItemVmSlotBinding) : RecyclerView.ViewHolder(binding.root)

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
            val binding = ItemVmSlotBinding.inflate(LayoutInflater.from(parent.context), parent, false)
            return ViewHolder(binding)
        }

        override fun onBindViewHolder(holder: ViewHolder, position: Int) {
            val item = items[position]
            val installed = isRomInstalled(item)

            holder.binding.tvVmName.text = item.config.name
            holder.binding.tvVmDetails.text = "${item.config.osVersion} • ${item.config.displayWidth}x${item.config.displayHeight} • ${item.config.targetFps} FPS"

            if (installed) {
                holder.binding.tvVmStatus.text = "Ready (ROM Installed)"
                holder.binding.tvVmStatus.setTextColor(getColor(R.color.accent_cyan))
                holder.binding.btnStartStop.text = getString(R.string.start_vm)
            } else {
                holder.binding.tvVmStatus.text = "No ROM (Import GSI)"
                holder.binding.tvVmStatus.setTextColor(getColor(R.color.accent_amber))
                holder.binding.btnStartStop.text = "Import GSI"
            }

            holder.binding.btnStartStop.setOnClickListener { onStartClick(item) }
            holder.binding.btnConfig.setOnClickListener { onConfigClick(item) }
        }

        override fun getItemCount(): Int = items.size
    }
}
