package com.axiom.shaper

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.net.VpnService
import android.os.Build
import android.os.ParcelFileDescriptor
import androidx.core.app.NotificationCompat

class ShaperVpnService : VpnService() {
    companion object {
        init { System.loadLibrary("shaper") }
        external fun startShaper(fd: Int, kbpsLimit: Long)
    }

    private var vpnInterface: ParcelFileDescriptor? = null
    private var shaperThread: Thread? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        createNotificationChannel()
        
        val pendingIntent = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        val notification = NotificationCompat.Builder(this, "axiom_shaper_channel")
            .setContentTitle("Axiom Shaper Active")
            .setContentText("Intercepting and shaping traffic at 1500 kbps")
            .setSmallIcon(R.drawable.ic_axiom_logo)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
            
        startForeground(1, notification)

        val kbpsLimit = intent?.getLongExtra("kbps_limit", 1500L) ?: 1500L

        vpnInterface = Builder()
            .addAddress("10.0.0.2", 32)
            .addRoute("0.0.0.0", 0)
            .addDnsServer("1.1.1.1")
            .setSession("Axiom Shaper")
            .establish()

        vpnInterface?.let { pfd ->
            shaperThread = Thread { startShaper(pfd.fd, kbpsLimit) }
            shaperThread?.start()
        }
        return START_STICKY
    }

    override fun onRevoke() {
        vpnInterface?.close()
        shaperThread?.interrupt()
        stopForeground(STOP_FOREGROUND_REMOVE)
        
        // Reset UI state in MainActivity
        getSharedPreferences("AxiomPrefs", Context.MODE_PRIVATE).edit().putBoolean("is_active", false).apply()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                "axiom_shaper_channel", 
                "Axiom Shaper Service", 
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Shows when network traffic shaping is active"
            }
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
    }
}
