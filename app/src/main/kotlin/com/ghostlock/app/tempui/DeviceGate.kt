package com.ghostlock.app.tempui

import android.util.Log
import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.AccessSessionFactory
import com.ghostlock.app.core.AccessSessionRequest
import com.ghostlock.app.data.nativeaccess.JniAccessSession
import com.ghostlock.app.data.nativeaccess.JniAccessSessionFactory
import com.ghostlock.app.data.nativeaccess.NativeProtocol
import com.ghostlock.app.data.nativeaccess.NativeReply

/**
 * 设备门禁：把 JNI 边界在这台真机上走一遍，并把每一步的**原始**结果写进 logcat（TAG=GhostlockGate）。
 *
 * 它验的是边界本身，不是漏洞利用：会话能建起来、Snapshot 能答、没有设备 seam 的 op 必须**具名**
 * 拒绝（op-not-wired:<名字>）、关闭与销毁幂等、销毁后的句柄必须被拒 —— 这几条正是 J1/J2 与
 * J3a/J4a 在当前阶段能给出的全部承诺。
 *
 * 之所以把日志写进 logcat 而不只显示在屏幕上：判据要能被 adb 抓走（见 docs/analysis/device-gates/），
 * 屏幕上看到「好像成功了」不算证据。
 */
object DeviceGate {
    /** logcat 里的标签；抓证据时按它过滤。 */
    const val TAG = "GhostlockGate"

    /** 跑一遍门禁。@return 每一行（屏幕显示与 logcat 的内容完全相同）。 */
    fun run(factory: AccessSessionFactory = JniAccessSessionFactory()): List<String> {
        val lines = mutableListOf<String>()
        fun step(line: String) {
            lines += line
            Log.i(TAG, line)
        }

        step("gate: begin (boundary only; no exploit step is attempted)")

        for (backend in listOf("cve_2026_43499", "cve_2026_43284")) {
            when (val opened = factory.open(AccessSessionRequest(backend, ByteArray(0)))) {
                is AccessResult.Failure -> {
                    step("gate: " + backend + " open FAILED (" + opened.reason + ") " + opened.detail)
                }

                is AccessResult.Success -> {
                    step("gate: " + backend + " open ok handle=positive")
                    val session = opened.value
                    step("gate: " + backend + " phase=" + session.phase + " cleanup=" + session.cleanup)
                    if (session is JniAccessSession) {
                        runOps(session, backend, ::step)
                    } else {
                        step("gate: " + backend + " unexpected session implementation")
                    }
                    session.close()
                    step("gate: " + backend + " closed")
                }
            }
        }
        step("gate: end")
        return lines
    }

    private fun runOps(session: JniAccessSession, backend: String, step: (String) -> Unit) {
        when (val snapshot = session.invoke(NativeProtocol.Op.Snapshot)) {
            is AccessResult.Success -> {
                val reply = snapshot.value
                step(
                    "gate: " + backend + " snapshot -> " + reply +
                        " | phase=" + reply.u64(NativeProtocol.Key.Phase) +
                        " backend=" + reply.u64(NativeProtocol.Key.Backend) +
                        " flags=0x" + (reply.u64(NativeProtocol.Key.Flags) ?: 0uL).toString(16) +
                        " width=" + reply.u64(NativeProtocol.Key.Width),
                )
            }

            is AccessResult.Failure -> step("gate: " + backend + " snapshot transport-failure " + snapshot.detail)
        }

        /* 没有设备 seam 的 op：必须是具名拒绝，而不是沉默。 */
        val op = if (backend == "cve_2026_43499") NativeProtocol.Op.WriteWord else NativeProtocol.Op.WriteBlock
        val keys = if (backend == "cve_2026_43499") {
            mapOf(
                NativeProtocol.Key.Address to 0xffffff8042a00000uL,
                NativeProtocol.Key.Form to 1uL,
            )
        } else {
            mapOf(
                NativeProtocol.Key.Token to 1uL,
                NativeProtocol.Key.Offset to 0uL,
            )
        }
        val blobs = if (backend == "cve_2026_43499") {
            emptyMap()
        } else {
            mapOf(NativeProtocol.Key.Bytes to ByteArray(16))
        }
        step("gate: " + backend + " " + op.token + " -> " + describe(session.invoke(op, keys, blobs)))

        /* initialize 的真实答案：真后端的参数集还没接线（J3b/J4b），必须具名拒绝。 */
        step("gate: " + backend + " initialize -> " + describe(session.invoke(NativeProtocol.Op.Initialize)))
    }

    private fun describe(result: AccessResult<NativeReply>): String = when (result) {
        is AccessResult.Success -> result.value.toString()
        is AccessResult.Failure -> "transport-failure(" + result.reason + ") " + result.detail
    }
}
