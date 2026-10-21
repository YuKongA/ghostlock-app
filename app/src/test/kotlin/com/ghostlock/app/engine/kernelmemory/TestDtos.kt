package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.generated.CredentialLayout43499
import com.ghostlock.app.core.generated.Discover43499
import com.ghostlock.app.core.generated.Init43499
import com.ghostlock.app.core.generated.KernelLayout43499
import com.ghostlock.app.core.generated.Prepare43499
import com.ghostlock.app.core.generated.TargetOffsets43499

/** 测试用 DTO 工厂：字段值只需非零/自洽；引擎不读 DTO，真实映射由 native 计划验证。 */
internal object TestDtos {
    fun init(): Init43499 = Init43499(
        1, 0, 1, 0x18, 0x20, 0x28, 0x80, 15, 3, 4, 60, 100, 0, 0, 0, 0,
        1, 1, 4, 0, true, 3, 0, false, 0, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0x4000, 0x5000,
    )

    fun layout(): KernelLayout43499 = KernelLayout43499(
        0x18, 0x20, 0x28, 0x1080, 0xF80, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60,
        0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0,
        0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100, 0x108, 0x110, 0x118, 0x120, 8,
    )

    fun credential(): CredentialLayout43499 = CredentialLayout43499(
        176, 0, 256, 48, 3, 0x1ffffffffff, 128, 136, 144, 152, 0, 0, 0, 0, 4,
    )

    fun targets(): TargetOffsets43499 = TargetOffsets43499(
        0x100, 0, true, 8, 0x798, 0, true, 8, 0x790, 0, true, 8,
        0x7A8, 0, true, 8, 0x8, 0, true, 8,
    )

    fun prepare(): Prepare43499 = Prepare43499(
        4, 3, 256, 1000, 3, 8, 2, 15, 6000, 6000, 500, 1000, 100, 50, 16, 1000,
        4096, 65536, 4096, 2048, 3, 0, 4, 64, 32, 128, 64, 32, 64, 64, 64, 16, 8,
        true, 65536, 4096, 1, 4, 0,
    )

    fun discover(prepareGeneration: Long = 1L): Discover43499 = Discover43499(
        4, 3, 256, 1000, 3, 8, 2, 16, 1000, 4096, 65536, 4096, 2048, 3, 0, 4,
        64, 32, 128, 64, 32, 64, 64, 0xffff800000000000uL.toLong(), 0xffffc00000000000uL.toLong(),
        0, 0, 8, 64, 16, prepareGeneration,
    )

    fun session(): KernelSession = KernelSession(init(), layout(), credential(), targets())

    fun facts(
        prepared: Boolean = true,
        discovered: Boolean = true,
        victim: Boolean = true,
        pageBase: Long = 0x1000,
        found: Long = 0x2000,
        victimTask: Long = 0x3000,
        prepareGeneration: Long = 1,
        discoveryGeneration: Long = 1,
        victimGeneration: Long = 1,
    ): Facts43499 = Facts43499(
        phase = 1,
        flags = (if (prepared) Facts43499.PREPARED_BIT else 0L) or
            (if (discovered) Facts43499.DISCOVERED_BIT else 0L) or
            (if (victim) Facts43499.VICTIM_BIT else 0L),
        width = 8,
        pageBase = pageBase,
        word0 = 0x10,
        word1 = 0x20,
        payloadPage = 0x30,
        payloadBase = 0x40,
        aliasDelta = 0x50,
        found = found,
        mmStruct = 0x60,
        victimTask = victimTask,
        victimPid = 0x70,
        prepareGeneration = prepareGeneration,
        discoveryGeneration = discoveryGeneration,
        victimGeneration = victimGeneration,
        generationsAvailable = true,
    )
}
