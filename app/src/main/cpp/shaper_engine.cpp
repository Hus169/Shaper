// ==============================================================================
// File: ShaperVpnService.kt (Android Application Layer)
// ==============================================================================
package com.axiom.shaper

import android.net.VpnService
import android.os.ParcelFileDescriptor
import android.content.Intent

class ShaperVpnService : VpnService() {

    companion object {
        init {
            // Load the compiled C++ native library (libshaper.so)
            System.loadLibrary("shaper")
        }
        // Declare the native function mapped from the C++ JNI bridge
        external fun startShaper(fd: Int, kbpsLimit: Long)
    }

    private var vpnInterface: ParcelFileDescriptor? = null
    private var shaperThread: Thread? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Extract the desired bandwidth limit from the intent (Default to 1500 kbps)
        val kbpsLimit = intent?.getLongExtra("kbps_limit", 1500L) ?: 1500L

        // Build the VpnService TUN interface
        // This intercepts all device traffic and routes it to the file descriptor
        vpnInterface = Builder()
            .addAddress("10.0.0.2", 32)
            .addRoute("0.0.0.0", 0)
            .addDnsServer("1.1.1.1")
            .setSession("Axiom Shaper Matrix")
            .establish()

        vpnInterface?.let { pfd ->
            val fd = pfd.fd
            
            // Hand off the raw file descriptor to the C++ TUN Matrix
            shaperThread = Thread {
                try {
                    startShaper(fd, kbpsLimit)
                } catch (e: Exception) {
                    e.printStackTrace()
                }
            }
            shaperThread?.start()
        }

        return START_STICKY
    }

    override fun onRevoke() {
        // Clean teardown sequence
        vpnInterface?.close()
        shaperThread?.interrupt()
    }
}
