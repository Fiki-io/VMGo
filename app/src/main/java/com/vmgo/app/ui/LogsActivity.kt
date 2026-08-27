package com.vmgo.app.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.FileProvider
import com.vmgo.app.databinding.ActivityLogsBinding
import com.vmgo.app.util.AppLogger
import java.io.File

class LogsActivity : AppCompatActivity() {

    private lateinit var binding: ActivityLogsBinding
    private var rawLogs: String = ""
    private var currentFilterLevel: String = "ALL"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityLogsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupListeners()
        loadLogs()
    }

    private fun setupListeners() {
        binding.btnRefreshLogs.setOnClickListener {
            loadLogs()
            Toast.makeText(this, "Logs refreshed", Toast.LENGTH_SHORT).show()
        }

        binding.btnCopyLogs.setOnClickListener {
            val textToCopy = binding.tvLogContent.text.toString()
            if (textToCopy.isNotEmpty()) {
                val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                val clip = ClipData.newPlainText("VM Go Logs", textToCopy)
                clipboard.setPrimaryClip(clip)
                Toast.makeText(this, "Logs copied to clipboard!", Toast.LENGTH_SHORT).show()
            }
        }

        binding.btnClearLogs.setOnClickListener {
            AlertDialog.Builder(this)
                .setTitle("Clear Logs")
                .setMessage("Are you sure you want to clear all recorded system logs?")
                .setPositiveButton("Clear") { _, _ ->
                    AppLogger.clearLogs()
                    loadLogs()
                    Toast.makeText(this, "Logs cleared", Toast.LENGTH_SHORT).show()
                }
                .setNegativeButton("Cancel", null)
                .show()
        }

        binding.btnShareLogs.setOnClickListener {
            shareLogFile()
        }

        // Filter buttons
        binding.btnFilterAll.setOnClickListener {
            currentFilterLevel = "ALL"
            applyFilters()
        }
        binding.btnFilterError.setOnClickListener {
            currentFilterLevel = "ERROR"
            applyFilters()
        }
        binding.btnFilterWarn.setOnClickListener {
            currentFilterLevel = "WARN"
            applyFilters()
        }
        binding.btnFilterInfo.setOnClickListener {
            currentFilterLevel = "INFO"
            applyFilters()
        }

        // Search text watcher
        binding.etSearchLog.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                applyFilters()
            }
            override fun afterTextChanged(s: Editable?) {}
        })
    }

    private fun loadLogs() {
        rawLogs = AppLogger.readLogs()
        applyFilters()

        binding.logScrollView.post {
            binding.logScrollView.fullScroll(android.view.View.FOCUS_DOWN)
        }
    }

    private fun applyFilters() {
        val query = binding.etSearchLog.text.toString().trim()
        val lines = rawLogs.lines()

        val filteredLines = lines.filter { line ->
            // Level filter
            val levelMatches = when (currentFilterLevel) {
                "ERROR" -> line.contains("[ERROR]") || line.contains("[FATAL]") || line.contains("FATAL CRASH")
                "WARN" -> line.contains("[WARN]") || line.contains("[ERROR]") || line.contains("[FATAL]")
                "INFO" -> line.contains("[INFO]")
                else -> true
            }

            // Search query filter
            val queryMatches = if (query.isEmpty()) {
                true
            } else {
                line.contains(query, ignoreCase = true)
            }

            levelMatches && queryMatches
        }

        val resultText = if (filteredLines.isEmpty()) {
            "No logs match the current filter."
        } else {
            filteredLines.joinToString("\n")
        }

        binding.tvLogContent.text = resultText
        binding.tvLogStats.text = "Showing ${filteredLines.size} lines (${currentFilterLevel})"
    }

    private fun shareLogFile() {
        val path = AppLogger.getLogFilePath()
        if (path.isEmpty()) return

        val file = File(path)
        if (!file.exists()) {
            Toast.makeText(this, "Log file is empty", Toast.LENGTH_SHORT).show()
            return
        }

        try {
            val shareIntent = Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_SUBJECT, "VM Go Diagnostic Logs")
                putExtra(Intent.EXTRA_TEXT, binding.tvLogContent.text.toString())
            }
            startActivity(Intent.createChooser(shareIntent, "Share VM Go Logs"))
        } catch (e: Exception) {
            e.printStackTrace()
            Toast.makeText(this, "Failed to share log file", Toast.LENGTH_SHORT).show()
        }
    }
}
