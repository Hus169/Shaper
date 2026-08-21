package com.axiom.shaper

object AirtimeFlooder {
    init { System.loadLibrary("shaper") }
    external fun nativeStart(intensity: Int)
    external fun nativeStop()
    fun start(intensity: Int) { nativeStart(intensity) }
    fun stop() { nativeStop() }
}
