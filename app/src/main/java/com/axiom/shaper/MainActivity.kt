package com.axiom.shaper

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.VpnService
import android.os.Build
import android.os.Bundle
import android.widget.Button
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
    private lateinit var btnToggleAirtime: Button
    private lateinit var sbIntensity: SeekBar
    private lateinit var tvIntensityValue: TextView
    private var isTunActive = false
    private var isAirtimeActive = false
    private var currentIntensity = 50

    private val vpnLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == RESULT_OK) startTunService()
        else Toast.makeText(this, "VPN permission denied.", Toast.LENGTH_SHORT).show()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        btnToggleTun = findViewById(R.id.btn_toggle_tun)
        btnToggleAirtime = findViewById(R.id.btn_toggle_airtime)
        sbIntensity = findViewById(R.id.sb_intensity)
        tvIntensityValue = findViewById(R.id.tv_intensity_value)

        btnToggleTun.setOnClickListener {
            if (isTunActive) {
                stopService(Intent(this, ShaperVpnService::class.java))
                isTunActive = false
                btnToggleTun.text = "ENGAGE LOCAL MATRIX"
            } else { requestPermissionsAndStartTun() }
        }

        sbIntensity.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                currentIntensity = progress
                tvIntensityValue.text = "$currentIntensity / 100"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        btnToggleAirtime.setOnClickListener {
            if (isAirtimeActive) {
                AirtimeFlooder.stop()
                isAirtimeActive = false
                btnToggleAirtime.text = "START AIRTIME EXHAUSTION"
                btnToggleAirtime.backgroundTintList = android.content.res.ColorStateList.valueOf(getColor(android.R.color.holo_red_light))
                Toast.makeText(this, "Airtime Exhaustion Disengaged.", Toast.LENGTH_SHORT).show()
            } else {
                AirtimeFlooder.start(currentIntensity)
                isAirtimeActive = true
                btnToggleAirtime.text = "STOP AIRTIME EXHAUSTION"
                btnToggleAirtime.backgroundTintList = android.content.res.ColorStateList.valueOf(getColor(android.R.color.holo_green_dark))
                Toast.makeText(this, "Choking Wi-Fi airtime at intensity $currentIntensity...", Toast.LENGTH_SHORT).show()
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
