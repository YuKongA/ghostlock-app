package com.ghostlock.app.ui

import com.ghostlock.app.data.component.FrontendKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The root-manager step after a run: open its UI, say it cannot be opened, or do
 * nothing. The rule is pure ([rootManagerAction]); the UI only performs it.
 *
 * [rootManagerAction] takes the App's EXISTING success criterion (exploit
 * process exit code 0, GhostlockViewModel.runExploit) plus one separate fact:
 * whether the run actually produced root state. `force_attack_test` succeeds
 * while discarding the root child, so it must not open a manager UI that has
 * nothing to show.
 */
class RootManagerLaunchTest {

    private val ksu = "me.weishu.kernelsu"

    @Test
    fun `a successful run opens the manager once, and every run gets its own action`() {
        assertEquals(
            RootManagerAction.Launch(ksu),
            rootManagerAction(succeeded = true, rootProduced = true, packageName = ksu, launchable = true),
        )
        /* Two consecutive successes: the planner keeps no state, so each run
         * decides on its own (the VM sends exactly one effect per run). */
        assertEquals(
            listOf(RootManagerAction.Launch(ksu), RootManagerAction.Launch(ksu)),
            listOf(true, true).map {
                rootManagerAction(succeeded = it, rootProduced = true, packageName = ksu, launchable = true)
            },
        )
    }

    @Test
    fun `a manager without a launchable UI is announced, never silent`() {
        assertEquals(
            RootManagerAction.Hint,
            rootManagerAction(succeeded = true, rootProduced = true, packageName = ksu, launchable = false),
        )
    }

    @Test
    fun `a failed run opens nothing`() {
        assertEquals(
            RootManagerAction.Skip,
            rootManagerAction(succeeded = false, rootProduced = true, packageName = ksu, launchable = true),
        )
        assertEquals(
            RootManagerAction.Skip,
            rootManagerAction(succeeded = false, rootProduced = true, packageName = null, launchable = false),
        )
    }

    /**
     * force_attack_test: the run succeeds, but it ignored an already loaded
     * KernelSU and the rooted child is discarded — there is no root state to
     * look at, so the manager UI must NOT open (a jump there would mislead).
     */
    @Test
    fun `a force-attack-test run leaves no root state and opens nothing`() {
        assertEquals(
            RootManagerAction.Skip,
            rootManagerAction(succeeded = true, rootProduced = false, packageName = ksu, launchable = true),
        )
        /* Even the hint is wrong there: nothing is expected to be opened. */
        assertEquals(
            RootManagerAction.Skip,
            rootManagerAction(succeeded = true, rootProduced = false, packageName = ksu, launchable = false),
        )
    }

    /** A run that leaves no manager behind must not open a guessed package. */
    @Test
    fun `a run without a root manager does nothing`() {
        assertEquals(
            RootManagerAction.Skip,
            rootManagerAction(succeeded = true, rootProduced = true, packageName = null, launchable = false),
        )
    }

    /**
     * The App-side package table mirrors native `lkm_image.cpp:346-348`
     * (`default_root_package` -> schema `root_package_convention`, schema.hpp:66-77):
     * KernelSU -> `me.weishu.kernelsu`, anything unknown -> empty (never guessed).
     */
    @Test
    fun `the package table mirrors the native authority`() {
        assertEquals(ksu, RootManager.KernelSU.packageName)
        assertEquals("kernelsu", RootManager.KernelSU.token)
        /* Every other entry is traceable to a repository fact, never guessed:
         * root_script.cpp:40/46/49 (ksud discovery globs) and the repository's
         * own "KernelSU/ReSukiSU/KowSU" wording. FolkPatch is deliberately NOT
         * here: native maps it to an EMPTY package
         * (cve_2026_43284_lkm_test.cpp:752), i.e. it must not be guessed. */
        assertEquals("me.weishu.kernelsu.pr", RootManager.KernelSuPr.packageName)
        assertEquals("com.resukisu.resukisu", RootManager.ReSukiSU.packageName)
        assertEquals("com.kowx712.supermanager", RootManager.KowSU.packageName)
        assertEquals(RootManager.KowSU, RootManager.resolve("kowxu"))
        assertNull(RootManager.resolve("folkpatch"))
        /* Both wired terminals end in the KernelSU root child (ADR-0006). */
        assertEquals(RootManager.KernelSU, RootManager.of(FrontendKind.RootChild))
        assertEquals(RootManager.KernelSU, RootManager.of(FrontendKind.UmhForward))
        assertNull(RootManager.of(null))
    }
}
