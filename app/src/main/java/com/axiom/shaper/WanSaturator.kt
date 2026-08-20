package com.axiom.shaper

object WanSaturator {
    init {
        System.loadLibrary("shaper")
    }

    external fun nativeStart(targetIp: String, targetPort: Int, targetBps: Long)
    external fun nativeStop()

    fun start(targetIp: String, targetPort: Int, targetMbps: Int) {
        // Convert Mbps to bps
        val bps = targetMbps.toLong() * 1000000L
        nativeStart(targetIp, targetPort, bps)
    }

    fun stop() {
        nativeStop()
    }
}
