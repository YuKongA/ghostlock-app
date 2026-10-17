package com.ghostlock.app.jni.cve43284

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 *  载体候选表的三例：顺序 / 可读标记 / 默认仍取第一项。
 *
 *  参照 ghostlock-app-ref@5f184760 的
 *  `src/core/backend/cve_2026_43284/execution_binding.cpp:141-148`（profile 的 carrier_path 优先）、
 *  `src/core/backend/cve_2026_43284/steps/chain.hpp:69-75`（kDefaultCarriers 顺序）、
 *  `src/core/backend/cve_2026_43284/backend_terminal.cpp:318-331`（只绑一个载体，不回退）。
 */
class CarrierCandidatesTest {
    @Test
    fun theOrderIsTheProfileCarrierFirstThenTheReferenceDefaults() {
        val ordered = CarrierCandidates.ordered("/data/local/tmp/one-shot.so")
        assertEquals("profile 一项 + 四个默认", 5, ordered.size)
        assertEquals("/data/local/tmp/one-shot.so", ordered[0].path)
        assertTrue(
            "第一项要写明它来自 profile",
            ordered[0].note.contains("execution_binding.cpp:141-148"),
        )
        assertEquals(
            listOf(
                "/vendor/lib64/libbinderdebug.so",
                "/vendor/lib64/libstagefrighthw.so",
                "/vendor/lib64/libstagefright_aidl_bufferpool2.so",
                "/vendor/lib64/libbsp_module.so",
            ),
            ordered.drop(1).map { candidate -> candidate.path },
        )
        /* 同路径只留第一次出现（ref steps/chain.cpp:51-66 的 CarrierList::add）。 */
        val deduped = CarrierCandidates.ordered("/vendor/lib64/libstagefrighthw.so")
        assertEquals(4, deduped.size)
        assertEquals(
            listOf(
                "/vendor/lib64/libstagefrighthw.so",
                "/vendor/lib64/libbinderdebug.so",
                "/vendor/lib64/libstagefright_aidl_bufferpool2.so",
                "/vendor/lib64/libbsp_module.so",
            ),
            deduped.map { candidate -> candidate.path },
        )
    }

    @Test
    fun readabilityIsMarkedForTheSelectedCandidate() {
        /* 判据注入：只有别的候选可读 ⇒ 选中的 binderdebug 仍应标 0。 */
        val unreadable = CarrierCandidates.select(
            readable = { path -> path != "/vendor/lib64/libbinderdebug.so" },
        )
        assertFalse(unreadable.readable)
        assertEquals(
            "carrier: /vendor/lib64/libbinderdebug.so (candidate 1/4, readable=0)",
            unreadable.line(),
        )
        val readable = CarrierCandidates.select(
            readable = { path -> path == "/vendor/lib64/libbinderdebug.so" },
        )
        assertTrue(readable.readable)
        assertEquals(
            "carrier: /vendor/lib64/libbinderdebug.so (candidate 1/4, readable=1)",
            readable.line(),
        )
    }

    @Test
    fun theDefaultChoiceStaysTheFirstCandidate() {
        val choice = CarrierCandidates.select()
        assertEquals("当前实现仍取第一项", 0, choice.index)
        assertEquals(4, choice.total)
        assertEquals("/vendor/lib64/libbinderdebug.so", choice.path)
        /* 有 profile 路径时，第一项就是它。 */
        val profiled = CarrierCandidates.select("/vendor/lib64/libbsp_module.so")
        assertEquals(0, profiled.index)
        assertEquals("/vendor/lib64/libbsp_module.so", profiled.path)
        /* 合并计划用声明上界 32 MiB，而不是载体 stat 真值（ref 的 target_size=0 不能直接拿来用）。 */
        assertEquals(32L * 1024L * 1024L, CarrierCandidates.DECLARED_SIZE_BYTES)
    }
}
