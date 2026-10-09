@file:Suppress("UnstableApiUsage")

plugins {
    id("org.jetbrains.kotlin.jvm")
}

// Keep this module's output under the repository-root build/ directory.
layout.buildDirectory.set(rootProject.layout.buildDirectory.dir("profile-core"))

kotlin {
    jvmToolchain(21)
}

dependencies {
    implementation("com.typesafe:config:1.4.3")
    /* GLKv3 wire codec (MessagePack). The encoder wraps the packer to emit the
     * canonical form the native MPack writer produces: shortest integers and
     * UTF-8-byte-sorted keys. Pure Java, no runtime transitive dependencies. */
    implementation("org.msgpack:msgpack-core:0.9.12")
    /* @VisibleForTesting marks the retained v2 writer as golden/equivalence
     * test-only; production never writes v2 (GLKv3-5). Pure-JVM annotation,
     * no runtime transitive dependencies. */
    implementation("androidx.annotation:annotation:1.9.1")
    testImplementation("junit:junit:4.13.2")
}

/**
 * Serializes every bundled HOCON kernel profile into the GLKv3 MessagePack
 * layout the native side reads. The exporter runs the same [ProfileMerger] /
 * [ProfileResolver] / `NativeProfileDocument` code as the app and encodes it
 * with `Glkv3Encoder` + `NativeProfileGlkv3Adapter`.
 */
tasks.register<JavaExec>("exportProfiles") {
    description = "Serialize bundled HOCON kernel profiles into GLKv3 .bin documents"
    group = "build"
    val profilesDir = rootProject.layout.projectDirectory.dir("app/src/main/assets/profile")
    /* ONE source for every path handed to the exporter. Deriving the output dir
     * with absolutePath while the build dir came from canonicalPath made the
     * containment guard fail whenever the two forms differ (a relocated or
     * symlinked build tree, or two coexisting trees). All three now come from the
     * same canonical value, so the guard sees one form only. */
    val buildDirPath = rootProject.layout.buildDirectory.get().asFile.canonicalFile
    val outputDir = buildDirPath.resolve("profiles")
    inputs.dir(profilesDir)
    outputs.dir(outputDir)
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("com.ghostlock.app.data.profile.ProfileExporter")
    args(
        profilesDir.asFile.absolutePath,
        outputDir.absolutePath,
        outputDir.absolutePath,
        /* Same value and same form as the output dir above (was canonicalPath while
         * the output used absolutePath). */
        buildDirPath.absolutePath,
    )
    /* A6-4 document segment three: an OPTIONAL single KMI label. UNSET keeps the
     * historical four-argument invocation, which declares no payload at all, so
     * the exported bytes of every profile stay identical. Set (-PlkmLabel=<label>)
     * it hands the finishing script and that label's module to the document; the
     * label is validated against the native-exported manifest (never a handwritten
     * list) and a missing module fails loudly instead of exporting a payload with
     * no bytes behind it. */
    val lkmLabel = (findProperty("lkmLabel") as String?)?.takeIf { it.isNotBlank() }
    if (lkmLabel != null) {
        val manifest = rootProject.file("profile-core/src/main/resources/lkm-kmi-manifest.tsv")
        check(manifest.isFile) { "missing label manifest: " + manifest }
        val labels = manifest.readLines()
            .map { it.trimEnd() }
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .map { it.substringBefore('\t') }
        check(lkmLabel in labels) {
            "unknown KMI label '" + lkmLabel + "'; the manifest declares: " + labels.joinToString(", ")
        }
        val script = rootProject.file("tools/lkm/ghostlock/root_cmd.sh")
        check(script.isFile) { "missing finishing script: " + script }
        val module = rootProject.file("build/app/lkm/" + lkmLabel + "/ghostlock.ko")
        check(module.isFile) {
            "missing module for label " + lkmLabel + ": " + module +
                " (run ./gradlew buildLkmImages)"
        }
        inputs.file(script)
        inputs.file(module)
        args(script.absolutePath, module.absolutePath)
    }
}
