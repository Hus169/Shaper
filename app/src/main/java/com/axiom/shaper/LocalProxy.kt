package com.axiom.shaper
object LocalProxy {
    init { System.loadLibrary("shaper") }
    external fun nativeStart(port: Int, bps: Long, delay: Int, jitter: Int, loss: Double)
    external fun nativeStop()
    fun start(port: Int, bps: Long, delay: Int, jitter: Int, loss: Double) { nativeStart(port, bps, delay, jitter, loss) }
    fun stop() { nativeStop() }
}
