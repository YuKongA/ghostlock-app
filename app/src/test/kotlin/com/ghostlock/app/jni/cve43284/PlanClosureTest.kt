package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 *  写入前的计划闭包校验（六例：越界 / 重叠 / 末块不可写 / width 不整除 / 容量不足 / 全通过）。
 *
 *  参照 ghostlock-app-ref@5f184760 的
 *  `src/core/backend/cve_2026_43284/steps/chain.cpp:390-435`（区域闭包 + 两两不重叠）与
 *  `:184-199`（apply_plan 在写第一个字节之前校验，不通过就一个块都不写）。
 *
 *  样例值取自真机计划：site=0xb30ac、payload=0xfe628、镜像 12304 B、载体声明上界 32 MiB。
 */
class PlanClosureTest {
    /** 一份"出厂即通过"的计划；每例只改要考的那一项。 */
    private fun plan(
        carrierSize: Long = 32L * 1024L * 1024L,
        imageSize: Long = 12304L,
        imageOffset: Long = 0L,
        payloadOffset: Long = 0xfe628L,
        payloadSize: Long = 0x1d0L,
        siteOffset: Long = 0xb30acL,
        width: Long = 16L,
    ): PlanCheck = PlanClosure.check(
        carrierPath = "/vendor/lib64/libbinderdebug.so",
        carrierSize = carrierSize,
        imageSize = imageSize,
        imageOffset = imageOffset,
        payloadOffset = payloadOffset,
        payloadSize = payloadSize,
        siteOffset = siteOffset,
        width = width,
    )

    @Test
    fun aRegionThatStartsOutsideTheCarrierIsRefused() {
        val checked = plan(payloadOffset = 32L * 1024L * 1024L + 16L)
        assertFalse("起点已在载体外 ⇒ 拒绝", checked.ok)
        assertTrue(
            "具名拒绝要指向 payload",
            checked.refusal!!.startsWith("plan:region-out-of-bounds:payload"),
        )
        assertTrue("失败也带计划形状", checked.summary.startsWith("plan: image=["))
    }

    @Test
    fun twoRegionsThatOverlapAreRefusedByName() {
        /* 载荷区挪进镜像区里：镜像 [0,12304) 与载荷 [0x1000,0x11d0) 相交。 */
        val checked = plan(payloadOffset = 0x1000L)
        assertFalse("重叠 ⇒ 拒绝", checked.ok)
        assertEquals("plan:regions-overlap:image:payload", checked.refusal)
    }

    @Test
    fun aSiteBlockThatDoesNotFitWholeIsRefused() {
        /* 载体 0x40001（末块不完整）：站点字在 0x40000，它所在的整块 [0x40000,0x40010) 越界。 */
        val checked = plan(
            carrierSize = 0x40001L,
            imageSize = 4096L,
            payloadOffset = 0x10000L,
            payloadSize = 0x20L,
            siteOffset = 0x40000L,
        )
        assertFalse("末块不完整 ⇒ 拒绝", checked.ok)
        assertTrue(
            "具名拒绝要指向站点块",
            checked.refusal!!.startsWith("plan:site-block-unwritable:block=0x40000"),
        )
    }

    @Test
    fun aWidthThatDoesNotDivideOffsetOrLengthIsRefused() {
        /* 镜像区起点不整除：8 % 16 = 8。 */
        val badOffset = plan(imageOffset = 8L)
        assertFalse("起点不整除 ⇒ 拒绝", badOffset.ok)
        assertEquals(
            "plan:width-misaligned:image:off=0x8:len=12304:width=16",
            badOffset.refusal,
        )
        /* 长度不整除：12300 % 16 = 12。 */
        val badLength = plan(imageSize = 12300L)
        assertFalse("长度不整除 ⇒ 拒绝", badLength.ok)
        assertTrue(badLength.refusal!!.startsWith("plan:width-misaligned:image"))
        /* 载荷区长度不整除（0x1dd = 477）：即使真机计划的 offset 0xfe628 不对齐，长度也必须整块。 */
        val badPayloadLength = plan(payloadSize = 0x1ddL)
        assertFalse("载荷长度不整除 ⇒ 拒绝", badPayloadLength.ok)
        assertTrue(badPayloadLength.refusal!!.startsWith("plan:width-misaligned:payload"))
    }

    @Test
    fun aCarrierTooSmallForTheImageIsRefusedWithNeedAndHave() {
        /* 载体 8192 B 装不下 12304 B 的镜像区；载荷区放在载体里，好让容量那条先命中。 */
        val checked = plan(carrierSize = 8192L, payloadOffset = 0x1000L, payloadSize = 0x20L)
        assertFalse("容量不足 ⇒ 拒绝", checked.ok)
        assertEquals("plan:carrier-too-small:need=12304:have=8192", checked.refusal)
    }

    @Test
    fun separateHookTargetStillChecksImageAgainstCarrier() {
        val checked = PlanClosure.check(
            carrierPath = "/carrier",
            carrierSize = 8192L,
            imageSize = 12304L,
            imageOffset = 0L,
            payloadOffset = 0x1000L,
            payloadSize = 0x20L,
            siteOffset = 0x1000L,
            hookTargetSize = 1024L * 1024L,
        )
        assertFalse("image 超出 carrier ⇒ 拒绝", checked.ok)
        assertEquals("plan:carrier-too-small:need=12304:have=8192", checked.refusal)
    }

    @Test
    fun aClosedPlanPrintsTheShapeLine() {
        val checked = plan()
        assertTrue("三区域都在 32 MiB 载体里且互不重叠 ⇒ 通过", checked.ok)
        assertNull(checked.refusal)
        assertEquals(
            "plan: image=[0,12304) payload=[0xfe628,0xfe7f8) site=0xb30ac carrier=32MB",
            checked.summary,
        )
    }
}
