package com.ghostlock.app.tempui

import com.ghostlock.app.core.generated.CredentialLayout43499
import com.ghostlock.app.core.generated.FinishVictim43499
import com.ghostlock.app.core.generated.Init43499
import com.ghostlock.app.core.generated.KernelLayout43499
import com.ghostlock.app.core.generated.Prepare43499
import com.ghostlock.app.core.generated.TargetOffsets43499
import com.ghostlock.app.core.generated.WordWrite43499
import com.ghostlock.app.jni.cve43284.Native43284
import com.ghostlock.app.jni.cve43499.Native43499
import com.ghostlock.app.log.GhostLog

/**
 * 新函数面的真机探针（D2 装配的门禁）。
 *
 * 它证明的是"**新库能被 Kotlin 调用**"这件事，逐项如下：
 *  1. 两个库各自加载成功（一个 backend 一个库）；
 *  2. create 给出正句柄；initialize 接受生成的 DTO（43499 四组）并按名回答；
 *  3. snapshot 回来的是**具名**数组（名字由 native 的 snapshotFields 导出，不手抄）；
 *  4. 没有设备 seam 的调用一律**具名拒绝**（op-not-wired:<函数名>）；
 *  5. shutdown / destroy 走通，销毁后句柄作废（stale-handle）。
 *
 * 参数用哨兵值（占位）：真实 profile 仍在设计（本阶段目标到 J7 为止），所以这里只求"值能一路
 * 走到该到的地方"，不求它们是这一台机器的真值。
 */
object TypedBackendProbe {
    /** 探针日志的 logcat 标签。 */
    const val TAG = "GhostlockTyped"

    /** 跑两个 backend 的探针。@return 每一行（屏幕与 logcat 相同）。 */
    fun run(): List<String> {
        val lines = mutableListOf<String>()
        fun step(line: String) {
            lines += line
            GhostLog.i(TAG, line)
        }
        probe43499(::step)
        probe43284(::step)
        return lines
    }

    private fun probe43499(step: (String) -> Unit) {
        if (!Native43499.ensureLoaded()) {
            step("typed: 43499 library missing (lib" + Native43499.LIBRARY + ".so)")
            return
        }
        step("typed: 43499 library loaded")
        val refusal = arrayOf("")
        val handle = Native43499.create()
        if (handle <= 0) {
            step("typed: 43499 create failed")
            return
        }
        step("typed: 43499 create ok")

        val names = Native43499.snapshotFields()
        val snapshot = LongArray(names.size)
        var code = Native43499.initialize(
            handle, init43499(), layout43499(), credential43499(), targets43499(), refusal,
        )
        step("typed: 43499 initialize code=" + code + " refusal=" + refusal[0])

        code = Native43499.snapshot(handle, snapshot, refusal)
        step(
            "typed: 43499 snapshot code=" + code + " " +
                names.indices.joinToString(",") { names[it] + "=" + snapshot[it] },
        )

        val out = LongArray(names.size)
        code = Native43499.prepare(handle, prepare43499(), out, refusal)
        step("typed: 43499 prepare code=" + code + " refusal=" + refusal[0])

        code = Native43499.writeWord(handle, write43499(), refusal)
        step("typed: 43499 writeWord code=" + code + " refusal=" + refusal[0])

        code = Native43499.finishVictim(handle, FinishVictim43499(0, 0L), out, refusal)
        step("typed: 43499 finishVictim code=" + code + " refusal=" + refusal[0])

        code = Native43499.shutdown(handle, refusal)
        step("typed: 43499 shutdown code=" + code + " refusal=" + refusal[0])
        code = Native43499.destroy(handle, refusal)
        step("typed: 43499 destroy code=" + code + " refusal=" + refusal[0])
        code = Native43499.shutdown(handle, refusal)
        step("typed: 43499 shutdown-after-destroy code=" + code + " refusal=" + refusal[0])
    }

    private fun probe43284(step: (String) -> Unit) {
        if (!Native43284.ensureLoaded()) {
            step("typed: 43284 library missing (lib" + Native43284.LIBRARY + ".so)")
            return
        }
        step("typed: 43284 library loaded")
        val refusal = arrayOf("")
        val handle = Native43284.create()
        if (handle <= 0) {
            step("typed: 43284 create failed")
            return
        }
        step("typed: 43284 create ok")

        /* 参数顺序由 native 的 initializeFields() 导出：先记下来，再用同一顺序给值。 */
        val fields = Native43284.initializeFields()
        step("typed: 43284 initializeFields=" + fields.joinToString(","))
        val params = LongArray(fields.size)
        params[0] = 150L
        params[1] = 800L
        params[2] = 10L
        var code = Native43284.initialize(handle, params, refusal)
        step("typed: 43284 initialize code=" + code + " refusal=" + refusal[0])

        val snapshot = LongArray(Native43284.snapshotFields().size)
        code = Native43284.snapshot(handle, snapshot, refusal)
        step("typed: 43284 snapshot code=" + code + " values=" + snapshot.joinToString(","))

        val image = Native43284.embeddedImage(handle, 0, refusal)
        step(
            "typed: 43284 embeddedImage code=" + (if (image == null) "null" else "ok") +
                " size=" + (image?.size ?: -1) + " refusal=" + refusal[0],
        )
        val hook = LongArray(Native43284.hookFields().size)
        code = Native43284.hookWords(handle, hook, refusal)
        step("typed: 43284 hookWords code=" + code + " values=" + hook.joinToString(",") + " refusal=" + refusal[0])

        val ticket = LongArray(1)
        code = Native43284.resolveTarget(handle, 1, "/system/lib64/libc++.so".toByteArray(), ticket, refusal)
        step("typed: 43284 resolveTarget code=" + code + " refusal=" + refusal[0])

        code = Native43284.shutdown(handle, refusal)
        step("typed: 43284 shutdown code=" + code + " refusal=" + refusal[0])
        code = Native43284.destroy(handle, refusal)
        step("typed: 43284 destroy code=" + code + " refusal=" + refusal[0])
    }

    /* ---- 43499 的哨兵参数（顺序＝D1 单表的顺序；只求值能走到该到的地方） ---- */

    private fun init43499() = Init43499(
        1L, 2, 1, 96L, 48L, 56L, 264L, 4L, 7L, 0L, 150L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0, 0,
        false, 0L, 0L, false, 4096L, false, 0L, 12L,
    )

    private fun layout43499() = KernelLayout43499(
        0x1111L, 0x2222L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,
        0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0x3333L, 0L, 0L, 0L, 8,
    )

    private fun credential43499() = CredentialLayout43499(
        176L, 0L, 256L, 48L, 3L, 2199023255551L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 4,
    )

    private fun targets43499() = TargetOffsets43499(
        47885704L, 0L, true, 4, 0L, 0L, false, 0, 0L, 0L, false, 0, 0L, 0L, false, 0,
        2144L, 0L, false, 4,
    )

    private fun prepare43499() = Prepare43499(
        4L, 3L, 0L, 0L, 0L, 0L, 0L, 4L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 4096L, 0x200000L, 512L,
        1024L, 0L, 0, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 64L, 0L, false, 0L, 0L, 0L, 0L, 0L,
    )

    private fun write43499() = WordWrite43499(
        /* 直接量超出 Long 范围：按无符号写再转回来（内核直接映射里的高地址就是这样）。 */
        0xFFFF888000001000uL.toLong(), 0L, 1, 0L, false, 0L, 0L, 0L, false,
    )
}
