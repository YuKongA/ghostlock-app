plugins {
    id("org.jetbrains.kotlin.multiplatform")
    id("com.android.kotlin.multiplatform.library")
    id("org.jetbrains.kotlin.plugin.serialization")
}

val pluginV2SchemaSource = rootProject.file(
    "tools/engineering/openspec/changes/plugin-sdk-v2-exact-design/schemas/plugin-package-v2.schema.json",
)

val generatePluginV2Schema = tasks.register<Copy>("generatePluginV2Schema") {
    from(pluginV2SchemaSource)
    into(layout.buildDirectory.dir("generated/plugin-v2-schema/commonMain/com/ghostlock/plugin/core/v2"))
}

kotlin {
    @OptIn(org.jetbrains.kotlin.gradle.ExperimentalKotlinGradlePluginApi::class)
    applyDefaultHierarchyTemplate {
        common {
            group("jvmShared") {
                withJvm()
                withAndroidTarget()
            }
        }
    }

    android {
        namespace = "com.ghostlock.plugin.core"
        compileSdk {
            version = release(37) {
                minorApiLevel = 2
            }
        }
        minSdk = 31
    }
    jvm()
    jvmToolchain(21)

    sourceSets {
        commonMain {
            resources.srcDir(generatePluginV2Schema)
        }
        commonMain.dependencies {
            implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.12.0")
        }
        commonTest.dependencies {
            implementation(kotlin("test"))
        }
    }
}

tasks.named<Test>("jvmTest") {
    systemProperty("ghostlock.plugin.v2.schema.source", pluginV2SchemaSource.absolutePath)
}
