package com.ghostlock.app.jni.cve43284

/**
 *  计划闭包校验的结论（一次调用一份）。
 *
 *  @property ok      true = 镜像区 / 载荷区 / 站点块都落在载体边界内、互不重叠、站点块整块可写。
 *  @property refusal 失败时的具名拒绝（形如 `plan:<原因>...`）；通过时为 null。
 *  @property summary 计划形状的一行摘要（`plan: image=[0,12304) payload=[0xfe628,0xfe7f8) site=0xb30ac carrier=32MB`）：
 *                    通过时它就是调用方要打印的那一行；失败时用来对照被拒的输入。
 */
data class PlanCheck(
    val ok: Boolean,
    val refusal: String?,
    val summary: String,
)

/**
 *  写入前的计划闭包 / 目标边界校验（纯 Kotlin：只读入参、不碰 native、不写日志、无全局状态）。
 *
 *  # 参照
 *  ghostlock-app-ref@5f184760 的 `src/core/backend/cve_2026_43284/steps/chain.cpp:390-435`
 *  （validate_plan_closure：每个区域的 `[offset, offset+len)` 必须在边界内，且两两不重叠）；
 *  调用点在写**第一个字节之前**（同文件 `:184-199` 的 apply_plan，与 `:496` 的 ComputePlan 阶段：
 *  闭包不通过就一个块都不写）；目标边界那一条见 `:408-416`（`end > target_size` ⇒ 拒绝）。
 *
 *  # 区域（把 ref 的两份 plan 合成一份三区域计划，全部落在同一个载体上）
 *  - 镜像区：`[imageOffset, imageOffset + imageSize)`；
 *  - 载荷区：`[payloadOffset, payloadOffset + payloadSize)`；
 *  - 站点块：`[alignDown(siteOffset, width), alignDown(siteOffset, width) + width)`。
 *    站点**本身**不要求 width 对齐（真机计划 site=0xb30ac 落在块 0xb30a0），要整块可写的是它所在的块。
 *
 *  # 校验顺序（先命中的那条就是 refusal）
 *  1. `carrierPath` 为空 ⇒ `plan:carrier-path-empty`（ref execution_binding.cpp:128-131 的 ModulePathEmpty 同义）；
 *  2. `width` 非正 / 非 2 的幂 ⇒ `plan:width-invalid:<w>`；
 *  3. `carrierSize` 为负 ⇒ `plan:carrier-size-invalid:<n>`；
 *  4. 负数或相加溢出 ⇒ `plan:region-out-of-bounds:<name>:off=<o>:len=<l>:carrier=<c>`；
 *  5. `width` 必须整除**长度**，且镜像区的 offset 也要对齐 ⇒ `plan:width-misaligned:<name>:off=0x..:len=<l>:width=<w>`
 *     （对应 ref `:398-400` 的 `region.len % kChainBlockBytes != 0`）。载荷区的 offset **不要求**对齐：
 *     原语按 `offset - offset % width` 落块（core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp:88、:236），
 *     真机计划的 payload=0xfe628 与 site=0xb30ac 都不是 16 的倍数，却都写成功 —— 要求原始 offset 对齐会把真计划判死；
 *  6. 区域**起点**必须落在 `[0, carrierSize)` 内 ⇒ `plan:region-out-of-bounds:<name>:...`
 *     （"越界"＝起点已在载体外；区域**末端**越过载体归第 7 条）；
 *  7. 镜像区 / 载荷区的末端越过载体 ⇒ `plan:carrier-too-small:need=<n>:have=<m>`（十进制，n=末端，m=carrierSize）；
 *  8. 站点块末端越过载体 ⇒ `plan:site-block-unwritable:block=0x..:end=0x..:carrier=0x..`
 *     （站点是一次整块读改写：末块不完整就一个字节都不写）；
 *  9. 三个区域两两不重叠 ⇒ `plan:regions-overlap:<a>:<b>`（名字顺序固定 image -> payload -> site）。
 *
 *  # 前提
 *  `carrierSize` 由调用方给：ref 用 `stat()` 的 `st_size`（execution_binding.cpp:184-188），0 表示"未知"。
 *  本端口要求一个**声明上界**而不是 0（见 [CarrierCandidates.DECLARED_SIZE_BYTES]）：
 *  0 会让每个区域都判成容量不足，闭包就退化成"永远不写"。
 */
object PlanClosure {
    /** 页缓存写的宽度：一块十六字节（ref steps/chain.hpp:51 的 `kChainBlockBytes = 16`）。 */
    const val BLOCK_BYTES: Long = 16L

    /**
     *  校验一份计划能否写进载体。
     *
     *  @param carrierPath  载体路径（只用于判断计划是否指定了载体；空路径直接拒绝）。
     *  @param carrierSize  载体大小（字节，调用方给的边界）：ref 取自 `stat()`，读不到时用声明上界。
     *  @param imageSize    镜像区长度（字节）。
     *  @param imageOffset  镜像区起点（字节）。
     *  @param payloadOffset 载荷区起点（字节）。
     *  @param payloadSize  载荷区长度（字节）。
     *  @param siteOffset   站点字偏移（字节；不要求 width 对齐）。
     *  @param width        块宽（字节）：必须是正整数且为 2 的幂；镜像区 offset/len 与载荷区 len 必须是它的倍数。
     *  @return 通过时 `ok=true`、`refusal=null`；失败时 `refusal` 为具名原因，`summary` 两边都有。
     */
    fun check(
        carrierPath: String,
        carrierSize: Long,
        imageSize: Long,
        imageOffset: Long,
        payloadOffset: Long,
        payloadSize: Long,
        siteOffset: Long,
        width: Long = BLOCK_BYTES,
    ): PlanCheck {
        val summary = shape(carrierSize, imageOffset, imageSize, payloadOffset, payloadSize, siteOffset)
        fun refuse(reason: String) = PlanCheck(false, reason, summary)

        if (carrierPath.isBlank()) {
            return refuse("plan:carrier-path-empty")
        }
        if (width <= 0L || (width and (width - 1L)) != 0L) {
            return refuse("plan:width-invalid:" + width)
        }
        if (carrierSize < 0L) {
            return refuse("plan:carrier-size-invalid:" + carrierSize)
        }

        val imageEnd = endOrNull(imageOffset, imageSize)
        if (imageEnd == null) {
            return refuse(outOfBounds("image", imageOffset, imageSize, carrierSize))
        }
        val payloadEnd = endOrNull(payloadOffset, payloadSize)
        if (payloadEnd == null) {
            return refuse(outOfBounds("payload", payloadOffset, payloadSize, carrierSize))
        }

        /* 镜像区从 imageOffset 起整块铺开 ⇒ offset 与 len 都要 width 对齐。
         * 载荷区只查 len：原语按 offset - offset % width 落块（pagecache_steps.cpp:88、:236），
         * 真机计划 payload=0xfe628 不是 16 的倍数也照样写成功。 */
        if (imageOffset % width != 0L || imageSize % width != 0L) {
            return refuse(misaligned("image", imageOffset, imageSize, width))
        }
        if (payloadSize % width != 0L) {
            return refuse(misaligned("payload", payloadOffset, payloadSize, width))
        }

        /* 起点必须已在载体里（负值 / 越过末端都算越界）。 */
        if (imageSize > 0L && imageOffset >= carrierSize) {
            return refuse(outOfBounds("image", imageOffset, imageSize, carrierSize))
        }
        if (payloadSize > 0L && payloadOffset >= carrierSize) {
            return refuse(outOfBounds("payload", payloadOffset, payloadSize, carrierSize))
        }

        /* 容量：镜像区 / 载荷区的末端就是载体必须提供的跨度（站点块另有整块规则）。 */
        val need = maxOf(if (imageSize > 0L) imageEnd else 0L, if (payloadSize > 0L) payloadEnd else 0L)
        if (need > carrierSize) {
            return refuse("plan:carrier-too-small:need=" + need + ":have=" + carrierSize)
        }

        /* 站点所在块必须整块落在载体里：整块读改写不能只写一半。 */
        val siteStart = siteOffset - Math.floorMod(siteOffset, width)
        val siteEnd = if (siteStart > Long.MAX_VALUE - width) -1L else siteStart + width
        if (siteStart < 0L || siteEnd < 0L || siteEnd > carrierSize) {
            return refuse(
                "plan:site-block-unwritable:block=0x" + siteStart.toString(16) +
                    ":end=0x" + siteEnd.toString(16) +
                    ":carrier=0x" + carrierSize.toString(16),
            )
        }

        /* 两两不重叠（ref :418-433 的 O(n^2) 走法；顺序固定 image -> payload -> site）。 */
        val spans = arrayOf(
            longArrayOf(imageOffset, imageEnd),
            longArrayOf(payloadOffset, payloadEnd),
            longArrayOf(siteStart, siteEnd),
        )
        val names = arrayOf("image", "payload", "site")
        for (i in spans.indices) {
            for (j in i + 1 until spans.size) {
                val first = spans[i]
                val second = spans[j]
                val nonEmpty = first[1] > first[0] && second[1] > second[0]
                if (nonEmpty && first[0] < second[1] && second[0] < first[1]) {
                    return refuse("plan:regions-overlap:" + names[i] + ":" + names[j])
                }
            }
        }

        return PlanCheck(true, null, summary)
    }

    /**
     *  区域末端；负数或相加溢出时返回 null（调用方据此按越界拒绝）。
     *
     *  @param offset 区域起点（字节）。
     *  @param len    区域长度（字节）。
     *  @return `offset + len`；非法输入为 null。
     */
    private fun endOrNull(offset: Long, len: Long): Long? {
        if (offset < 0L || len < 0L) {
            return null
        }
        if (offset > Long.MAX_VALUE - len) {
            return null
        }
        return offset + len
    }

    /** 越界拒绝的固定形状。 */
    private fun outOfBounds(name: String, offset: Long, len: Long, carrierSize: Long): String =
        "plan:region-out-of-bounds:" + name + ":off=" + offset + ":len=" + len + ":carrier=" + carrierSize

    /** width 不整除拒绝的固定形状。 */
    private fun misaligned(name: String, offset: Long, len: Long, width: Long): String =
        "plan:width-misaligned:" + name + ":off=0x" + offset.toString(16) + ":len=" + len + ":width=" + width

    /**
     *  计划形状的一行：镜像区用十进制（与设备日志里的 `bytes=<n>` 同口径），载荷区 / 站点用十六进制。
     *
     *  @return 形如 `plan: image=[0,12304) payload=[0xfe628,0xfe7f8) site=0xb30ac carrier=32MB`。
     */
    private fun shape(
        carrierSize: Long,
        imageOffset: Long,
        imageSize: Long,
        payloadOffset: Long,
        payloadSize: Long,
        siteOffset: Long,
    ): String =
        "plan: image=[" + imageOffset + "," + (imageOffset + imageSize) + ")" +
            " payload=[0x" + payloadOffset.toString(16) + ",0x" + (payloadOffset + payloadSize).toString(16) + ")" +
            " site=0x" + siteOffset.toString(16) +
            " carrier=" + humanSize(carrierSize)

    /** 载体尺寸的人读形态：整 MB / 整 KB 才换算，其余按字节（32MiB -> "32MB"）。 */
    private fun humanSize(bytes: Long): String = when {
        bytes > 0L && bytes % (1024L * 1024L) == 0L -> (bytes / (1024L * 1024L)).toString() + "MB"
        bytes > 0L && bytes % 1024L == 0L -> (bytes / 1024L).toString() + "KB"
        else -> bytes.toString() + "B"
    }
}
