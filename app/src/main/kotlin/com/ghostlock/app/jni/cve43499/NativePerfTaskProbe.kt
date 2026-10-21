package com.ghostlock.app.jni.cve43499

/** Samples this process and returns the most-voted direct-map task candidate (0 = none). */
object NativePerfTaskProbe {
    init { System.loadLibrary("gl43499native") }
    external fun probe(): Long
}
