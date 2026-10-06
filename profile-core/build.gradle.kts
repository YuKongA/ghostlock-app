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
    val outputDir = rootProject.layout.buildDirectory.dir("profiles")
    inputs.dir(profilesDir)
    outputs.dir(outputDir)
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("com.ghostlock.app.data.profile.ProfileExporter")
    args(
        profilesDir.asFile.absolutePath,
        outputDir.get().asFile.absolutePath,
        outputDir.get().asFile.absolutePath,
    )
}
