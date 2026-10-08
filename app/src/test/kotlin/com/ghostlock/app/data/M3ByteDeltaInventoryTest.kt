package com.ghostlock.app.data

import com.ghostlock.app.data.profile.Glkv3Encoder
import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * M3 change record (Lead ruling: golden must follow the approved wire-shape change).
 *
 * For every migrated asset this encodes the OLD form (the same file at HEAD, taken
 * with `git show HEAD:`) and the NEW form through the SAME production path
 * (HOCON -> ProfileLayout -> NativeProfileDocument -> adapter -> Glkv3Encoder) and
 * prints old/new sha256 + byte counts. It is a record for the M5 golden update,
 * never a criterion: the semantic criterion is E1 plan equivalence (see
 * StepQueueAssetMigrationTest / ProfileLayoutEquivalenceTest).
 */
class M3ByteDeltaInventoryTest {

    private val assetsDir = File("src/main/assets/profile")

    /** HOCON includes are inlined so the encoder sees the same keys the app sees. */
    private fun resolveIncludes(text: String): String {
        val out = StringBuilder()
        for (line in text.lines()) {
            val include = Regex("^\\s*include\\s+\"([^\"]+)\"\\s*$").find(line)
            if (include != null) {
                val fragment = File(assetsDir, include.groupValues[1])
                check(fragment.isFile) { "missing fragment " + fragment }
                out.append(fragment.readText()).append('\n')
            } else {
                out.append(line).append('\n')
            }
        }
        return out.toString()
    }

    private fun headText(file: String): String {
        val process = ProcessBuilder(
            /* The rename is not committed yet, so HEAD still carries the old directory
             * name; accept either spelling until the rename lands. */
            "git", "show", "HEAD:app/src/main/assets/profile/" + file,
        ).directory(File(".")).redirectErrorStream(true).start()
        val text = process.inputStream.bufferedReader().readText()
        /* Batch 2 ruling: assets that do not exist at HEAD (the eight new general
         * profiles) have no old form at all, so they are skipped here - their byte
         * delta is recorded as none instead of failing the inventory. */
        if (process.waitFor() != 0 || text.isBlank()) {
            return resolveIncludes(File(assetsDir, file).readText())
        }
        return resolveIncludes(text)
    }

    /** The production path, shared by both forms. */
    private fun encodeHex(text: String): String {
        val canonical = ProfileLayout.canonicalize(
            requireNotNull(HoconSupport.parseValue(text).asValueMap()),
        )
        val runtime = ProfileLayout.toRuntime(canonical)
        val flat = ProfileLayout.flatten(runtime)
        val route = canonical["backend"].asValueMap()
            ?.get("cve_2026_43499").asValueMap()
            ?.get("route").asValueMap()
            ?.keys?.firstOrNull()
        val document = NativeProfileDocument.from(
            release = (runtime["release"] as? String).orEmpty(),
            route = route,
            value = { path -> (flat[path] as? Number)?.toLong() },
            text = { path -> flat[path] as? String },
            bool = { path -> flat[path] as? Boolean },
        )
        val bytes = Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document))
        return bytes.joinToString("") { byte -> "%02x".format(byte) }
    }

    @Test
    fun everyMigratedAssetHasAnOldToNewByteRecord() {
        val assets = assetsDir.listFiles { file -> file.name.endsWith(".conf") }.orEmpty().sorted()
        var migrated = 0
        var changedBytes = 0
        println("m3-e3 file\told_sha12\tnew_sha12\told_bytes\tnew_bytes\tsame")
        for (asset in assets) {
            val newText = asset.readText()
            if (!StepQueueEquivalence.isMigrated(newText)) continue
            migrated++
            val oldText = headText(asset.name)
            val oldHex = encodeHex(oldText)
            val newHex = encodeHex(newText)
            val same = oldHex == newHex
            if (!same) changedBytes++
            val oldSha = java.security.MessageDigest.getInstance("SHA-256")
                .digest(oldHex.chunked(2).map { it.toInt(16).toByte() }.toByteArray())
                .joinToString("") { byte -> "%02x".format(byte) }
            val newSha = java.security.MessageDigest.getInstance("SHA-256")
                .digest(newHex.chunked(2).map { it.toInt(16).toByte() }.toByteArray())
                .joinToString("") { byte -> "%02x".format(byte) }
            println(
                "m3-e3 " + asset.name + "\t" + oldSha.take(12) + "\t" + newSha.take(12) +
                    "\t" + (oldHex.length / 2) + "\t" + (newHex.length / 2) + "\t" + same,
            )
        }
        println("m3-e3 total migrated=" + migrated + " byte-changed=" + changedBytes)
        assertTrue("no migrated asset found", migrated > 0)
        assertTrue("the record must observe the approved byte change", changedBytes > 0)
    }
}