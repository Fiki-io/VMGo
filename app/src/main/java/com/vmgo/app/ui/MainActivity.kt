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
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.vmgo.app.R
import com.vmgo.app.core.NativeVmEngine
import com.vmgo.app.databinding.ActivityMainBinding
import com.vmgo.app.databinding.ItemVmSlotBinding
import com.vmgo.app.model.VmInstance
import com.vmgo.app.service.GsiImportService
import com.vmgo.app.util.AppLogger
import com.vmgo.app.util.StorageUtil

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private val vmSlots = mutableListOf<VmInstance>()
    private lateinit var adapter: VmSlotAdapter

    private var gsiImportService: GsiImportService? = null
    private var isServiceBound = false

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

        // Show engine version
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

    private fun setupRecyclerView() {
        adapter = VmSlotAdapter(
            items = vmSlots,
            onStartClick = { instance -> startVm(instance) },
            onConfigClick = { instance -> openConfig(instance) }
        )
        binding.rvSlots.layoutManager = LinearLayoutManager(this)
        binding.rvSlots.adapter = adapter
    }

    private fun setupListeners() {
        binding.btnSelectGsi.setOnClickListener {
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

    private fun loadSlots() {
        vmSlots.clear()
        val baseDir = StorageUtil.getSlotsBaseDir(this)
        val slots = baseDir.listFiles()?.filter { it.isDirectory && it.name.startsWith("slot_") }

        if (!slots.isNullOrEmpty()) {
            for (slot in slots) {
                val config = StorageUtil.prepareSlotDirectories(this, slot.name)
                vmSlots.add(VmInstance(config))
            }
        } else {
            // Default initial slot 1
            val defaultSlot = StorageUtil.prepareSlotDirectories(this, "slot_1")
            vmSlots.add(VmInstance(defaultSlot))
        }
        adapter.notifyDataSetChanged()
    }

    private fun handleGsiSelected(uri: Uri) {
        val targetSlot = vmSlots.firstOrNull() ?: return
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
                    Toast.makeText(this, "GSI Imported successfully to Slot 1!", Toast.LENGTH_LONG).show()
                    loadSlots()
                } else {
                    AppLogger.e("MainActivity", "Failed to import GSI image")
                    Toast.makeText(this, "Failed to import GSI image", Toast.LENGTH_LONG).show()
                }
            }
        )
    }

    private fun startVm(instance: VmInstance) {
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
            holder.binding.tvVmName.text = item.config.name
            holder.binding.tvVmDetails.text = "${item.config.osVersion} • ${item.config.displayWidth}x${item.config.displayHeight} • ${item.config.targetFps} FPS"
            holder.binding.tvVmStatus.text = item.status.name

            holder.binding.btnStartStop.setOnClickListener { onStartClick(item) }
            holder.binding.btnConfig.setOnClickListener { onConfigClick(item) }
        }

        override fun getItemCount(): Int = items.size
    }
}
