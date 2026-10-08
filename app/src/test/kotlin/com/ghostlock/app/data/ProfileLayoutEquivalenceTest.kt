package com.ghostlock.app.data

import android.app.Application
import java.io.File
import com.ghostlock.app.data.component.CombinationCatalog
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/**
 * S4 R3 per-file flat-equivalence safety gate.
 *
 * For every bundled profile and shared fragment, flatten(canonicalize(new))
 * must equal flatten(canonicalize(legacy fixture)) key by key and value by
 * value. The legacy fixtures in profile-legacy/ are the pre-R3 files
 * (includes expanded), so any alias/value drift in the migration fails here.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ProfileLayoutEquivalenceTest {
    private val context: Application = RuntimeEnvironment.getApplication()

    /* Which canonical branch each legacy fixture took (flat vs available-token);
     * the coverage assertion below requires BOTH, so a branch silently vanishing
     * turns this test red instead of quietly reducing coverage. */
    private val branchesSeen = mutableSetOf<String>()

    /* 用户 ① 只恢复了【支持列表】设备的 43499 声明；未恢复者（8 份 general +
     * 6.1.145-android14-11-maybe-dirty）仍只声明 43284 ⇒ 其 fixture 的 43499 支
     * 不可比，显式跳过并把集合【钉住】——多一个/少一个都会变红。 */
    private val skippedFixtures = mutableSetOf<String>()

    /* 8 份 *-general.conf 是用户 ② 之后【新增】的兜底资产（此前是 4 份 template），
     * 它们没有历史 legacy fixture ⇒ 无从做 flat 等价对拍 ⇒ 显式跳过并把集合【钉住】
     * （多一个/少一个都会变红；名字以 ls 实测为准，不手抄）。 */
    private val missingFixtures = mutableSetOf<String>()

    private val fragmentFiles = listOf(
        "credential-6x.conf",
        "kernelsnitch-6x.conf",
        "execution-tuning.conf",
        "execution-select-stack.conf",
        "execution-tcp-zerocopy.conf",
    )

    private fun fixtureText(name: String): String = requireNotNull(
        javaClass.classLoader?.getResourceAsStream("profile-legacy/$name"),
    ) { "missing legacy fixture $name" }.bufferedReader().use { it.readText() }

    private fun flatten(text: String): Map<String, Any?> {
        val parsed = requireNotNull(HoconSupport.parseValue(text).asValueMap()) {
            "profile did not parse"
        }
        return ProfileLayout.flatten(ProfileLayout.canonicalize(parsed))
    }

    @Test
    fun everyBundledProfileAndFragmentIsFlatEquivalentToItsLegacyFixture() {
        val loader = AssetConfigLoader(context)
        val index = requireNotNull(
            HoconSupport.parseValue(loader.load("profile/index.conf")).asValueMap(),
        )
        val files = index["profiles"].asValueList().orEmpty()
            .mapNotNull { it.asValueMap()?.get("file") as? String } + fragmentFiles
        assertTrue("index lists no profiles", files.size >= 60)

        /* Explicit THREE buckets (migrated / token form / no selection at all):
         * the shared fragments and index.conf declare no selection, so a two-way
         * split would silently absorb them. Every bucket is listed by name. */
        val migratedFiles = mutableListOf<String>()
        val tokenFormFiles = mutableListOf<String>()
        val noSelectionFiles = mutableListOf<String>()
        for (file in files) {
            /* AssetConfigLoader, not a raw asset read: it resolves HOCON includes
             * (profiles include credential-6x / kernelsnitch-6x). */
            val newText = loader.load("profile/" + file)
            assertTrue("$file: empty asset", newText.isNotBlank())
            val declaresTokenList = StepQueueEquivalence.tokenLists(newText).isNotEmpty()
            /* A file that mentions "available" must use exactly one of the two
             * forms; a file that never declares a selection (the shared fragments
             * and index.conf) legitimately has neither. Falsify by deleting the
             * token list of a token-form asset while keeping the word available. */
            assertTrue(
                "$file: a file with an available block must use one of the two selection forms",
                StepQueueEquivalence.isMigrated(newText) || declaresTokenList ||
                    !newText.contains("available"),
            )
            if (!StepQueueEquivalence.isMigrated(newText) && !declaresTokenList) {
                noSelectionFiles += file
                continue
            }
            if (StepQueueEquivalence.isMigrated(newText)) {
                migratedFiles += file
                /* M3 (Lead ruling 2026-10-06): a migrated asset is judged by E1 =
                 * normalised plan equivalence - expected from the LEGACY fixture
                 * (token form) through the catalogue plus the native stepset table,
                 * actual from this asset's own object form. Two independent sources. */
                /* 8 份 *-general.conf 是用户 ② 之后新增的兜底资产，没有历史 legacy
                 * fixture ⇒ 无从做 flat 等价对拍 ⇒ 显式跳过并钉住集合（不静默缩小覆盖）。 */
                if (!File("src/test/resources/profile-legacy", file).isFile) {
                    missingFixtures += file
                    continue
                }
                val fixture = fixtureText(file)
                /* fixture 保持原样：它是【无 backend id 的 v2 flat 形态】（被测对象，不改）。
                 * 资产是 available.<id> 形态。两侧按【语义】对齐 —— 比较计划
                 * （route + 步骤序列，逐值），不比较承载它的标识 id
                 * （M5：两条读取路径必须逐值等价）。 */
                val flatLeaves = ProfileLayout.flatten(
                    ProfileLayout.canonicalize(
                        requireNotNull(HoconSupport.parseValue(fixture).asValueMap()),
                    ),
                )
                /* 两条读取路径 = 【源文本（raw / legacy）】 vs 【迁移后（canonical）】：
                 *   raw-flat 面：fixture 源文本的 backend.steps + route.*
                 *   migrated-token 面：canonicalize → flatten 的 available.<backend>.<index>
                 * 有哪面就断言哪面；两面都在就【两面都断言】——这正是 M5「同一份数据的两条
                 * 读取路径必须逐值等价」的实证；判据统一在计划层：
                 *   tokenPlan(backend, token)  vs  queuePlan(newText, backend)
                 * 0 命中 / 多命中 / 目录里没有该 token ⇒ 都红。
                 * B 的结论（2026-10-06）：并非两种迁移形态，而是【读取对象】不同 —— 早先误读
                 * flatLeaves（它已是迁移后结果）当作 flat 面。 */
                /* 该 backend 是否【真的被声明】（有 queue）——判据只写这一份：
                 * 资产可能有 backend.<id> 的几何段却没有 queue（用户 ① 只恢复了支持列表
                 * 设备的 43499 声明）⇒ 无 queue 即不可比 ⇒ 显式跳过并钉住集合。 */
                fun assetDeclaresQueue(backend: String): Boolean {
                    val owner = ProfileLayout.canonicalize(
                        requireNotNull(HoconSupport.parseValue(newText).asValueMap()),
                    )["backend"].asValueMap()?.get(backend).asValueMap()
                    return !(owner?.get("queue") as? List<*>).isNullOrEmpty()
                }
                val rawFlat = requireNotNull(HoconSupport.parseValue(fixture).asValueMap())
                val rawBackend = rawFlat["backend"].asValueMap()
                val flatSteps = rawBackend?.get("steps") as? String
                val rawRoute = rawFlat["route"].asValueMap()?.keys?.firstOrNull()
                val availEntries = flatLeaves.filterKeys { it.startsWith("available.") }
                require(flatSteps != null || availEntries.isNotEmpty()) {
                    file + ": neither canonical face present | allKeys=" +
                        flatLeaves.keys.sorted().joinToString(",")
                }
                val faces = mutableListOf<Triple<String, String, String>>()
                if (flatSteps != null) {
                    val flatRoute = rawRoute
                    /* 0 命中与【多命中】都红：多命中说明 fixture 的计划在目录里不唯一
                     * （如 select_stack + w1_w3 同时匹配 pselect_rootchild 与 pselect_umh）——
                     * 那本身就是歧义。失败消息带上两个输入与全部候选，便于定位形态差异。 */
                    val candidates = CombinationCatalog.specs.filter { candidate ->
                        candidate.steps.token == flatSteps && candidate.route?.token == flatRoute
                    }
                    /* flat 面【不携带 path】⇒ 天生欠定 ⇒ 只给候选集合约束：
                     * asset 计划必须等于【某个】候选的计划（信息少的一面用包含式，M5）。
                     * 候选为空 ⇒ 红；asset 计划不在候选里 ⇒ 红。 */
                    require(candidates.isNotEmpty()) {
                        file + " [raw-flat]: no catalogue token for steps=" + flatSteps +
                            " route=" + flatRoute +
                            " | allKeys=" + flatLeaves.keys.sorted().joinToString(",") +
                            " | avail=" + availEntries
                    }
                    if (!assetDeclaresQueue(candidates.first().backend.token)) {
                        skippedFixtures += file
                        continue
                    }
                    val flatAssetPlan = StepQueueEquivalence.queuePlan(newText, candidates.first().backend.token)
                    val matchedCandidate = candidates.any { candidate ->
                        val plan = StepQueueEquivalence.tokenPlan(candidate.backend.token, candidate.token)
                        plan.route == flatAssetPlan.route && plan.steps == flatAssetPlan.steps
                    }
                    assertTrue(
                        file + " [raw-flat]: asset plan (" + flatAssetPlan.route + "/" + flatAssetPlan.steps +
                            ") is not among candidates " +
                            candidates.joinToString(" | ") { candidate ->
                                val plan = StepQueueEquivalence.tokenPlan(candidate.backend.token, candidate.token)
                                candidate.token + "=>" + plan.route + "/" + plan.steps
                            },
                        matchedCandidate,
                    )
                    branchesSeen += "raw-flat"
                }
                if (availEntries.isNotEmpty()) {
                    val backend = requireNotNull(availEntries.keys.first().split('.').getOrNull(1)) {
                        file + " [token]: cannot read the backend from " + availEntries
                    }
                    val token = availEntries.values.first().toString()
                    requireNotNull(StepQueueEquivalence.backendKindOf(backend).let { kind ->
                        CombinationCatalog.resolve(kind, token)
                    }) {
                        file + " [token]: " + token + " is not in the catalogue for " + backend +
                            " | avail=" + availEntries
                    }
                    faces += Triple("migrated-token", backend, token)
                }
                for ((branch, backend, token) in faces) {
                    if (!assetDeclaresQueue(backend)) {
                        skippedFixtures += file
                        continue
                    }
                    branchesSeen += branch
                    /* 永久自诊断：queuePlan 的失败消息不带文件名，这里补上 file/branch/backend，
                     * 否则无法定位是哪一份 fixture 的哪一支（判据不变，只是补上下文）。 */
                    val assetPlan = try {
                        StepQueueEquivalence.queuePlan(newText, backend)
                    } catch (error: IllegalArgumentException) {
                        throw IllegalArgumentException(
                            file + " [" + branch + "]: backend=" + backend + ": " + error.message,
                            error,
                        )
                    }
                    StepQueueEquivalence.assertPlanEquals(
                        StepQueueEquivalence.tokenPlan(backend, token),
                        assetPlan,
                    )
                }
            } else {
                tokenFormFiles += file
                val expected = flatten(fixtureText(file))
                val actual = flatten(newText)
                assertEquals("$file: owner-qualified flattening drifted", expected, actual)
            }
        }
        /* 覆盖守卫：两种 canonical 分支都必须被这个用例走到 —— 任一支消失 ⇒ 明确变红，
         * 而不是静默缩小覆盖（守卫要能失败）。 */
        assertEquals(
            "both reading paths must be exercised (raw-flat + migrated-token)",
            setOf("raw-flat", "migrated-token"),
            branchesSeen,
        )
        /* 跳过的 fixture 集合【钉住】：多一个/少一个都变红（防静默缩小覆盖）。 */
        assertEquals(
            "skipped fixtures changed (update deliberately, not silently)",
            setOf("6.1.145-android14-11-maybe-dirty.conf"),
            skippedFixtures,
        )
        /* 没有 legacy fixture 的资产集合【钉住】（名字来自 ls 实测）。 */
        assertEquals(
            "fixtures without a legacy counterpart changed (update deliberately, not silently)",
            setOf(
                "5.10-android12-general.conf", "5.10-android13-general.conf",
                "5.15-android13-general.conf", "5.15-android14-general.conf",
                "6.1-android14-general.conf", "6.12-android16-general.conf",
                "6.18-android17-general.conf", "6.6-android15-general.conf",
            ),
            missingFixtures,
        )

        /* Content-derived, never hand-copied: after the batch every asset that
         * declares a selection is migrated and none is left in token form. */
        assertEquals("token form must be empty after the M3 batch", 0, tokenFormFiles.size)
        assertEquals(
            "every asset with a selection must be migrated",
            files.size - noSelectionFiles.size,
            migratedFiles.size,
        )
        assertEquals(
            "the three buckets must cover every iterated asset",
            files.size,
            migratedFiles.size + tokenFormFiles.size + noSelectionFiles.size,
)
        println("m3-buckets migrated=" + migratedFiles + " tokenForm=" + tokenFormFiles)
    }

    @Test
    fun aliasExceptionsAreRegisteredInTheFlatEquivalence() {
        val flat = flatten(fixtureText("6.1.115-android14-11-ga2521ca27699-ab13294383.conf"))
        /* S4 R6b: selection.steps is cancelled; the legacy backend.steps step
         * id migrates to the combination token at backend.<id>.steps. */
        assertNull(flat["selection.steps"])
        assertEquals("tcp_rootchild", flat["backend.cve_2026_43499.steps"])
        /* kernelsnitch.collisions -> backend.<id>.kernel.kernelsnitch_collisions */
        assertEquals(4L, flat["backend.cve_2026_43499.kernel.kernelsnitch_collisions"])
        /* route.<kind>.compact_waiter -> the shared kernel flag */
        assertEquals(true, flat["backend.cve_2026_43499.kernel.compact_waiter"])
        /* R6a: fallback.to / fallback.route.* are recognized and ignored, so
         * the legacy fallback geometry never reaches the canonical map and the
         * migrated asset no longer carries that route branch. */
        assertNull(flat["common.fallback_route"])
        assertNull(flat["backend.cve_2026_43499.route.select_stack.waiter_shift"])
    }
}
