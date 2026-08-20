package com.axiom.shaper

import android.content.Intent
import android.net.VpnService
import android.os.Bundle
import android.widget.Button
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {
    private val vpnLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == RESULT_OK) {
            startShaperService()
        } else {
            Toast.makeText(this, "VPN permission denied.", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        val button = Button(this).apply {
            text = "Engage Shaper Matrix (1.5 Mbps)"
            setOnClickListener {
                val intent = VpnService.prepare(this@MainActivity)
                if (intent != null) {
                    vpnLauncher.launch(intent)
                } else {
                    startShaperService()
                }
            }
        }
        setContentView(button)
    }

    private fun startShaperService() {
        val intent = Intent(this, ShaperVpnService::class.java)
        intent.putExtra("kbps_limit", 1500L)
        startForegroundService(intent)
        Toast.makeText(this, "Matrix engaged.", Toast.LENGTH_SHORT).show()
    }
}
