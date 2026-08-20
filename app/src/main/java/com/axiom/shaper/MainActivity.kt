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
        
        // Programmatic UI: Eliminates need for res/layout/activity_main.xml
        val button = Button(this).apply {
            text = "Engage Shaper Matrix (1.5 Mbps)"
            setOnClickListener {
                val intent = VpnService.prepare(this@MainActivity)
                if (intent != null) {
                    startActivityForResult(intent, VPN_REQUEST_CODE)
                } else {
                    startShaperService()
                }
            }
        }
        setContentView(button)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == VPN_REQUEST_CODE && resultCode == RESULT_OK) {
            startShaperService()
        } else {
            Toast.makeText(this, "VPN permission denied.", Toast.LENGTH_SHORT).show()
        }
    }

    private fun startShaperService() {
        val intent = Intent(this, ShaperVpnService::class.java)
        intent.putExtra("kbps_limit", 1500L)
        startForegroundService(intent)
        Toast.makeText(this, "Matrix engaged.", Toast.LENGTH_SHORT).show()
    }
}
