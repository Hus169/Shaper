package com.axiom.shaper

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
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
        val notification = NotificationCompat.Builder(this, "shaper_channel")
            .setContentTitle("Axiom Shaper Active")
            .setContentText("Enforcing bandwidth limits...")
            .setSmallIcon(android.R.drawable.ic_dialog_info)
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
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel("shaper_channel", "Shaper Service", NotificationManager.IMPORTANCE_LOW)
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
    }
}
