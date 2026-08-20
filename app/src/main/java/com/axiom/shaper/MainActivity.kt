// ==============================================================================
// File: app/src/main/java/com/axiom/shaper/MainActivity.kt
// ==============================================================================
package com.axiom.shaper

import android.content.Intent
import android.net.VpnService
import android.os.Bundle
import android.widget.Button
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private val VPN_REQUEST_CODE = 100

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        val startButton = findViewById<Button>(R.id.btn_start)
        startButton.setOnClickListener {
            val intent = VpnService.prepare(this)
            if (intent != null) {
                // User has not granted VPN permission yet
                startActivityForResult(intent, VPN_REQUEST_CODE)
            } else {
                // Permission already granted, start the service directly
                startShaperService()
            }
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == VPN_REQUEST_CODE && resultCode == RESULT_OK) {
            startShaperService()
        } else {
            Toast.makeText(this, "VPN permission denied. Shaper inactive.", Toast.LENGTH_SHORT).show()
        }
    }

    private fun startShaperService() {
        val intent = Intent(this, ShaperVpnService::class.java)
        intent.putExtra("kbps_limit", 1500L) // 1.5 Mbps limit
        startForegroundService(intent)
        Toast.makeText(this, "Axiom Shaper Matrix engaged.", Toast.LENGTH_SHORT).show()
    }
}
