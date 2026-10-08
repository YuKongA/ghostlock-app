package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * M3 (token list -> step queue): the generator skeleton, one sample asset and the
 * acceptance guard.
 *
 * ACCEPTANCE (Lead ruling 2026-10-06): the primary criterion is E1 = NORMALISED
 * PLAN EQUIVALENCE, not canonical/byte identity. The two forms deliberately carry
 * different canonical keys (available.<id>.<i>=token vs backend.<id>.queue plus
 * backend.<id>.route/queue_route), and D5' changed the wire shape on purpose, so
 * literal flatten/hex equality is unattainable by design.
 *
 * E2 (native normalisation of both forms) is exercised in the batch turn; E3 (the
 * per-asset byte-delta inventory) is a change record for the M5 golden update.
 */
class StepQueueAssetMigrationTest {

    private val assetsDir = File("src/main/assets/profile")

    /* 旧 template 已按用户 2026-10-06 ② 删除（general 取代）⇒ 样例改用真实迁移后的
     * 资产；token 不手写，按该资产声明的 route + 步骤序列在组合目录里反查。 */
    private val sample = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541.conf"

    /**
     * TODO(stepset-steps.tsv): the authoritative stepset -> step sequence export
     * (`make -C src stepset-steps-manifest`) is being produced by native-core.
     * Until it lands this skeleton uses an in-test stub so the generator is
     * compilable and testable; the stub MUST be deleted when the real resource
     * appears (the batch turn reads the file through [loadStepsetSteps]).
     */
    /** The AUTHORITY (native export): expected plans come from here, never from a stub. */
    private val realStepsets: Map<String, List<String>> by lazy {
        loadStepsetSteps(File("src/test/resources/stepset-steps.tsv"))
    }

    /** Which combination token each migrated asset was rendered from (test fixture). */
    /* 沿革: 旧夹具 5.15-template.conf 只有单条声明 ⇒ .single() 成立。用户 ② 删模板后改用
     * 真实资产（2 条声明：43284 无 route 轴 + 43499 multicast_waiter）⇒ 逐条取声明的计划，
     * 反查【限定该声明自己的 backend】+ route/stepset 双匹配；计划逐值比较，不比 id，
     * 也不依赖单条假设（M5：两条读取路径逐值等价）。
     * 注：queuePlan 的 backend 必须是【测试体比较的那条】（cve_2026_43499），否则派生出的
     * token 属于别的 backend，resolve 会返回 null（这正是修复前的失败）。 */
    private val sampleTokens: Map<String, String> by lazy {
        val text = File(assetsDir, sample).readText()
        /* 先算后取：把 queuePlan 移出 lambda（在 lambda 里调用它会触发编译器内部错误
         * DELEGATE_SPECIAL_FUNCTION_RETURN_TYPE_MISMATCH），lambda 里只读字段。 */
        val declared = StepQueueEquivalence.backendsOf(text)
        val planByBackend = declared.map { backend -> backend to StepQueueEquivalence.queuePlan(text, backend) }
        val routeBearing = requireNotNull(planByBackend.firstOrNull { entry -> entry.second.route != null }) {
            sample + ": no declaration with a route axis"
        }
        val backend = routeBearing.first
        val plan = routeBearing.second
        /* 目录是权威：在【本声明的 backend】内取 route 与步骤序列都一致的那个 token。 */
        val token = requireNotNull(
            CombinationCatalog.specs.firstOrNull { spec ->
                spec.backend.token == backend &&
                    spec.route?.token == plan.route &&
                    realStepsets[spec.steps.token] == plan.steps
            },
        ) { sample + ": no catalogue spec for " + backend + " with " + plan.route }.token
        mapOf(sample to token)
    }

    /** The real authority: stepset<TAB>step_index<TAB>step_name. Fail-closed. */
    private fun loadStepsetSteps(file: File): Map<String, List<String>> {
        check(file.isFile) { "missing stepset-steps table: " + file }
        val rows = file.readLines()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .map { line -> line.split('\t') }
        rows.forEach { parts ->
            check(parts.size == 3) { "stepset-steps.tsv needs 3 columns: " + parts.joinToString("|") }
        }
        return rows.groupBy({ it[0] }, { it[2] })
    }

    /** One `available { <backend> = [ "tok", ... ] }` declaration in an asset. */
    private data class TokenList(val backend: String, val tokens: List<String>)

    private val tokenListLine = Regex("^\\s*([A-Za-z0-9_.]+)\\s*=\\s*\\[\\s*(\"[^\"]+\"(?:\\s*,\\s*\"[^\"]+\")*)\\s*]\\s*$")

    /** Parses the token lists inside every `available { ... }` block. */
    private fun parseTokenLists(text: String): List<TokenList> {
        val out = mutableListOf<TokenList>()
        var insideAvailable = false
        for (line in text.lines()) {
            val trimmed = line.trim()
            if (trimmed == "available {") {
                insideAvailable = true
                continue
            }
            if (insideAvailable && trimmed == "}") {
                insideAvailable = false
                continue
            }
            if (!insideAvailable) continue
            val match = tokenListLine.find(line) ?: continue
            val tokens = Regex("\"([^\"]+)\"").findAll(match.groupValues[2])
                .map { it.groupValues[1] }
                .toList()
            out += TokenList(match.groupValues[1], tokens)
        }
        return out
    }

    private fun backendKindOf(token: String): BackendKind = requireNotNull(
        BackendKind.entries.firstOrNull { token == it.token },
    ) { "unknown backend token " + token }

    /** The generator: folds one token list into the object (queue) form. */
    private fun renderSelection(
        list: TokenList,
        stepsets: Map<String, List<String>>,
        indent: String,
    ): String {
        val token = list.tokens.singleOrNull()
        require(token != null) { "available." + list.backend + " needs exactly one token to migrate" }
        val spec = requireNotNull(CombinationCatalog.resolve(backendKindOf(list.backend), token)) {
            "token " + token + " is not in the combination catalogue"
        }
        val route = spec.route?.token
        val steps = stepsets[spec.steps.token]
        require(steps != null) { "no step sequence for stepset " + spec.steps.token }
        val inner = indent + "  "
        val stepsText = steps.joinToString(", ") { step -> "{ step = \"" + step + "\" }" }
        val lines = mutableListOf(indent + list.backend + " {")
        if (route != null) lines += inner + "route = \"" + route + "\""
        lines += inner + "queue = [ " + stepsText + " ]"
        lines += indent + "}"
        return lines.joinToString("\n")
    }

    /** Transactional: every anchor must match, otherwise nothing is produced. */
    private fun migrate(text: String, stepsets: Map<String, List<String>>): String {
        val lists = parseTokenLists(text)
        check(lists.isNotEmpty()) { "no available token list found" }
        var out = text
        for (list in lists) {
            val line = out.lines().firstOrNull { line ->
                val match = tokenListLine.find(line)
                match != null && match.groupValues[1] == list.backend
            } ?: throw IllegalArgumentException("anchor vanished for " + list.backend)
            val indent = line.takeWhile { it == ' ' }
            out = out.replace(line, renderSelection(list, stepsets, indent))
        }
        return out
    }

    /** The normalised plan both forms must agree on (E1). */
    private data class Plan(val backend: String, val route: String?, val steps: List<String>)

    private fun tokenPlan(list: TokenList, stepsets: Map<String, List<String>>): Plan {
        val spec = requireNotNull(CombinationCatalog.resolve(backendKindOf(list.backend), list.tokens.single()))
        return Plan(list.backend, spec.route?.token, requireNotNull(stepsets[spec.steps.token]))
    }

    private fun queuePlan(migrated: String, backend: String): Plan {
        val canonical = ProfileLayout.canonicalize(
            requireNotNull(HoconSupport.parseValue(migrated).asValueMap()),
        )
        /* Navigate the nested canonical map: flatten() only yields leaf keys. */
        val owner = canonical["backend"].asValueMap()?.get(backend).asValueMap()
        assertNotNull("migrated document has no backend owner", owner)
        val route = (owner!!["route"] as? String) ?: (owner["queue_route"] as? String)
        val queue = owner["queue"] as? List<*>
        assertNotNull("migrated document has no queue", queue)
        val steps = queue!!.map { element ->
            val map = element as? Map<*, *>
            (map?.get("step") as? String) ?: error("queue element without step: " + element)
        }
        return Plan(backend, route, steps)
    }

    /**
     * Batch write-back for the M3 migration (explicit switch only). Per file the
     * whole text is rebuilt first and every anchor must resolve, so a file is
     * either fully migrated or untouched.
     */
    @Test
    fun batchWriteBackEveryTokenFormAsset() {
        if (System.getProperty("glk.m3.writeback") != "true") return
        val assets = assetsDir.listFiles { file -> file.name.endsWith(".conf") }.orEmpty().sorted()
        var changed = 0
        for (asset in assets) {
            val text = asset.readText()
            if (StepQueueEquivalence.isMigrated(text)) continue
            val lists = parseTokenLists(text)
            if (lists.isEmpty()) continue
            var migrated = text
            var allResolved = true
            for (list in lists) {
                val anchorLine = migrated.lines().firstOrNull { line ->
                    val match = tokenListLine.find(line)
                    match != null && match.groupValues[1] == list.backend
                }
                if (anchorLine == null) {
                    allResolved = false
                    break
                }
                val indent = anchorLine.takeWhile { it == ' ' }
                migrated = migrated.replace(anchorLine, renderSelection(list, realStepsets, indent))
            }
            if (!allResolved || migrated == text) continue
            asset.writeText(migrated)
            changed++
        }
        println("m3-batch: migrated " + changed + " asset(s)")
    }

    /** M3: a migrated asset must carry ONLY the queue - native rejects a backend
     *  section holding both queue and steps (src/core/profile/glkv3_parse.cpp:407-419). */
    @Test
    fun aMigratedAssetCarriesNoTokenBesideItsQueue() {
        val assets = assetsDir.listFiles { file -> file.name.endsWith(".conf") }.orEmpty()
        var checked = 0
        for (asset in assets) {
            val text = asset.readText()
            if (!StepQueueEquivalence.isMigrated(text)) continue
            checked++
            assertTrue(
                asset.name + ": a migrated asset must not keep a token beside its queue",
                !StepQueueEquivalence.hasTokenBesideQueue(text),
            )
        }
        assertTrue("migrated assets checked", checked >= 60)
    }

    @Test
    fun everyAssetTokenResolvesThroughTheCatalogue() {
        val assets = assetsDir.listFiles { file -> file.name.endsWith(".conf") }.orEmpty()
        assertTrue("no assets found under " + assetsDir.absolutePath, assets.isNotEmpty())
        var withTokens = 0
        for (asset in assets) {
            for (list in parseTokenLists(asset.readText())) {
                withTokens++
                for (token in list.tokens) {
                    val spec = CombinationCatalog.resolve(backendKindOf(list.backend), token)
                    assertNotNull(asset.name + ": token " + token + " is not in the catalogue", spec)
                    assertNotNull(asset.name + ": token " + token + " has no stepset", spec!!.steps)
                }
            }
        }
        /* Inventory: 62 before the M3 migration, 61 once 5.15-template.conf was
         * written back, 0 when the batch turn finishes. */
        /* After the M3 batch no bundled asset declares a token list any more. */
        assertEquals("token-list assets", 0, withTokens)
    }

    /**
     * The checker: a PURE function of two independently sourced plans, so that a
     * defect on either side is visible. Falsifiable without touching production
     * code (see theCheckerRejectsADivergentPlan).
     *
     * INDEPENDENCE (fix 1, effective once the sample is written back): the
     * "expected" plan comes from the authoritative stepset table, the "actual"
     * plan is re-parsed from the ASSET FILE ON DISK - never from the same table
     * instance the renderer used. Until the write-back happens the actual side is
     * the rendered text in memory, which is why the batch turn must re-point it.
     */
    private fun assertPlanEquals(expected: Plan, actual: Plan) {
        assertEquals("backend", expected.backend, actual.backend)
        assertEquals("route", expected.route, actual.route)
        assertEquals("step sequence", expected.steps, actual.steps)
    }

    @Test
    fun theCheckerRejectsADivergentPlan() {
        val good = Plan("cve_2026_43499", "tcp_zerocopy", listOf("w1", "w2"))
        val wrongStep = Plan("cve_2026_43499", "tcp_zerocopy", listOf("w1", "w9"))
        val wrongRoute = Plan("cve_2026_43499", "select_stack", listOf("w1", "w2"))
        val thrown = runCatching { assertPlanEquals(good, wrongStep) }.exceptionOrNull()
        assertTrue("a wrong step must be rejected", thrown is AssertionError)
        val thrownRoute = runCatching { assertPlanEquals(good, wrongRoute) }.exceptionOrNull()
        assertTrue("a wrong route must be rejected", thrownRoute is AssertionError)
        /* And the control: identical plans must pass. */
        assertPlanEquals(good, good.copy())
    }

    @Test
    fun theSampleAssetDescribesTheSamePlanAsTheCatalogue() {
        val file = File(assetsDir, sample)
        val token = requireNotNull(sampleTokens[sample]) { "no token fixture for " + sample }
        val list = TokenList("cve_2026_43499", listOf(token))

        /* Expected: catalogue (route) + the AUTHORITY table (step sequence). */
        val expected = tokenPlan(list, realStepsets)

        /* The generator output for the same token, rendered transactionally. */
        val rendered = renderSelection(list, realStepsets, "    ")

        /* Explicit one-off write-back: -Dglk.m3.writeback=true (never implicit). */
        if (System.getProperty("glk.m3.writeback") == "true") {
            val current = file.readText()
            val anchor = current.lines().firstOrNull { line ->
                val match = tokenListLine.find(line)
                match != null && match.groupValues[1] == list.backend
            } ?: error("anchor vanished in " + sample)
            val indent = anchor.takeWhile { it == ' ' }
            val migrated = current.replace(anchor, renderSelection(list, realStepsets, indent))
            require(migrated != current) { "write-back produced no change" }
            file.writeText(migrated)
        }

        /* Actual: re-parsed from the ASSET ON DISK - an independent source. */
        val onDisk = file.readText()
        assertPlanEquals(expected, queuePlan(onDisk, list.backend))
        assertTrue("the migrated asset must not still carry a token list", parseTokenLists(onDisk).isEmpty())
        assertTrue("the asset must equal the generator output", onDisk.contains("queue = [ { step = "))
        assertEquals("generator output is deterministic", rendered, renderSelection(list, realStepsets, "    "))
    }
}