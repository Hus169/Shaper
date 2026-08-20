package com.axiom.shaper

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.VpnService
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : AppCompatActivity() {
    private val NOTIFICATION_REQUEST_CODE = 101
    private lateinit var btnToggleTun: Button
    private lateinit var btnToggleWan: Button
    private lateinit var etTargetIp: EditText
    private lateinit var sbBandwidth: SeekBar
    private lateinit var tvSliderValue: TextView
    private var isTunActive = false
    private var isWanActive = false
    private var currentMbps = 0

    private val vpnLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == RESULT_OK) startTunService()
        else Toast.makeText(this, "VPN permission denied.", Toast.LENGTH_SHORT).show()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        btnToggleTun = findViewById(R.id.btn_toggle_tun)
        btnToggleWan = findViewById(R.id.btn_toggle_wan)
        etTargetIp = findViewById(R.id.et_target_ip)
        sbBandwidth = findViewById(R.id.sb_bandwidth)
        tvSliderValue = findViewById(R.id.tv_slider_value)

        btnToggleTun.setOnClickListener {
            if (isTunActive) {
                stopService(Intent(this, ShaperVpnService::class.java))
                isTunActive = false
                btnToggleTun.text = "ENGAGE LOCAL MATRIX"
            } else { requestPermissionsAndStartTun() }
        }

        sbBandwidth.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                currentMbps = progress
                tvSliderValue.text = "$currentMbps Mbps"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        btnToggleWan.setOnClickListener {
            if (isWanActive) {
                WanSaturator.stop()
                isWanActive = false
                btnToggleWan.text = "START WAN SATURATION"
                Toast.makeText(this, "WAN Saturator Disengaged.", Toast.LENGTH_SHORT).show()
            } else {
                val ip = etTargetIp.text.toString().trim()
                if (ip.isEmpty() || !ip.contains(".")) {
                    Toast.makeText(this, "Enter a valid Target IP", Toast.LENGTH_SHORT).show(); return@setOnClickListener
                }
                if (currentMbps == 0) {
                    Toast.makeText(this, "Set bandwidth > 0 Mbps", Toast.LENGTH_SHORT).show(); return@setOnClickListener
                }
                WanSaturator.start(ip, 5201, currentMbps)
                isWanActive = true
                btnToggleWan.text = "STOP WAN SATURATION"
                Toast.makeText(this, "Saturating WAN at $currentMbps Mbps...", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun requestPermissionsAndStartTun() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.POST_NOTIFICATIONS), NOTIFICATION_REQUEST_CODE); return
            }
        }
        val intent = VpnService.prepare(this)
        if (intent != null) vpnLauncher.launch(intent) else startTunService()
    }

    private fun startTunService() {
        val intent = Intent(this, ShaperVpnService::class.java).putExtra("kbps_limit", 1500L)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent) else startService(intent)
        isTunActive = true
        btnToggleTun.text = "DISENGAGE LOCAL MATRIX"
    }
}
