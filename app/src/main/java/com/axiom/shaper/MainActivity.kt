package com.axiom.shaper
import android.Manifest; import android.content.Intent; import android.content.pm.PackageManager; import android.net.VpnService
import android.os.Build; import android.os.Bundle; import android.widget.Button; import android.widget.SeekBar
import android.widget.TextView; import android.widget.Toast; import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity; import androidx.core.app.ActivityCompat; import androidx.core.content.ContextCompat
class MainActivity : AppCompatActivity() {
    private val NOTIFICATION_REQUEST_CODE = 101
    private lateinit var btnToggleTun: Button; private lateinit var btnToggleGateway: Button
    private lateinit var sbBandwidth: SeekBar; private lateinit var tvBandwidthValue: TextView
    private var isTunActive = false; private var isGatewayActive = false; private var currentMbps = 10
    private val vpnLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result -> if (result.resultCode == RESULT_OK) startTunService() }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState); setContentView(R.layout.activity_main)
        btnToggleTun = findViewById(R.id.btn_toggle_tun); btnToggleGateway = findViewById(R.id.btn_toggle_gateway)
        sbBandwidth = findViewById(R.id.sb_bandwidth); tvBandwidthValue = findViewById(R.id.tv_bandwidth_value)
        
        btnToggleTun.setOnClickListener { if (isTunActive) { stopService(Intent(this, ShaperVpnService::class.java)); isTunActive = false; btnToggleTun.text = "ENGAGE LOCAL TUN" } else { requestPermissionsAndStartTun() } }
        
        sbBandwidth.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) { currentMbps = progress; tvBandwidthValue.text = "$currentMbps Mbps" }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}; override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })
        
        btnToggleGateway.setOnClickListener {
            if (isGatewayActive) { LocalProxy.stop(); isGatewayActive = false; btnToggleGateway.text = "ENGAGE HOTSPOT GATEWAY"; btnToggleGateway.backgroundTintList = android.content.res.ColorStateList.valueOf(getColor(android.R.color.holo_red_light)); Toast.makeText(this, "Gateway Disengaged.", Toast.LENGTH_SHORT).show() } 
            else { 
                val bps = currentMbps.toLong() * 1_000_000L
                LocalProxy.start(1080, bps, 50, 20, 1.0) // 50ms delay, 20ms jitter, 1% loss
                isGatewayActive = true; btnToggleGateway.text = "DISENGAGE GATEWAY"; btnToggleGateway.backgroundTintList = android.content.res.ColorStateList.valueOf(getColor(android.R.color.holo_green_dark))
                Toast.makeText(this, "Gateway Active. Set client proxy to Phone IP:1080", Toast.LENGTH_LONG).show() 
            }
        }
    }
    private fun requestPermissionsAndStartTun() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) { if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) { ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.POST_NOTIFICATIONS), NOTIFICATION_REQUEST_CODE); return } }
        val intent = VpnService.prepare(this); if (intent != null) vpnLauncher.launch(intent) else startTunService()
    }
    private fun startTunService() { val intent = Intent(this, ShaperVpnService::class.java).putExtra("kbps_limit", 1500L); if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent) else startService(intent); isTunActive = true; btnToggleTun.text = "DISENGAGE LOCAL TUN" }
}
