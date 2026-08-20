package com.axiom.shaper

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.VpnService
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : AppCompatActivity() {
    private val VPN_REQUEST_CODE = 100
    private val NOTIFICATION_REQUEST_CODE = 101
    private lateinit var tvStatus: TextView
    private lateinit var btnToggle: Button
    private val PREFS_NAME = "AxiomPrefs"
    private val KEY_IS_ACTIVE = "is_active"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        tvStatus = findViewById(R.id.tv_status)
        btnToggle = findViewById(R.id.btn_toggle)

        updateUI()

        btnToggle.setOnClickListener {
            if (isActive()) {
                stopShaperService()
            } else {
                requestPermissionsAndStart()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        updateUI()
    }

    private fun updateUI() {
        val active = isActive()
        if (active) {
            tvStatus.text = "Status: INTERCEPTING"
            tvStatus.setTextColor(getColor(android.R.color.holo_green_light))
            btnToggle.text = "DISENGAGE"
            btnToggle.backgroundTintList = android.content.res.ColorStateList.valueOf(getColor(android.R.color.holo_red_light))
        } else {
            tvStatus.text = "Status: STANDBY"
            tvStatus.setTextColor(getColor(android.R.color.darker_gray))
            btnToggle.text = "ENGAGE MATRIX"
            btnToggle.backgroundTintList = android.content.res.ColorStateList.valueOf(getColor(android.R.color.holo_green_light))
        }
    }

    private fun requestPermissionsAndStart() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.POST_NOTIFICATIONS), NOTIFICATION_REQUEST_CODE)
                return
            }
        }
        prepareVpn()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == NOTIFICATION_REQUEST_CODE) {
            if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                prepareVpn()
            } else {
                Toast.makeText(this, "Notification permission required for background status.", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun prepareVpn() {
        val intent = VpnService.prepare(this)
        if (intent != null) {
            startActivityForResult(intent, VPN_REQUEST_CODE)
        } else {
            startShaperService()
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == VPN_REQUEST_CODE && resultCode == RESULT_OK) {
            startShaperService()
        } else {
            Toast.makeText(this, "VPN permission denied.", Toast.LENGTH_SHORT).show()
        }
        updateUI()
    }

    private fun startShaperService() {
        val intent = Intent(this, ShaperVpnService::class.java)
        intent.putExtra("kbps_limit", 1500L)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent)
        } else {
            startService(intent)
        }
        setActive(true)
        updateUI()
        Toast.makeText(this, "Matrix engaged. Traffic is being shaped.", Toast.LENGTH_SHORT).show()
    }

    private fun stopShaperService() {
        val intent = Intent(this, ShaperVpnService::class.java)
        stopService(intent)
        setActive(false)
        updateUI()
        Toast.makeText(this, "Matrix disengaged. Network restored.", Toast.LENGTH_SHORT).show()
    }

    private fun setActive(active: Boolean) {
        getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit().putBoolean(KEY_IS_ACTIVE, active).apply()
    }

    private fun isActive(): Boolean {
        return getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).getBoolean(KEY_IS_ACTIVE, false)
    }
}
