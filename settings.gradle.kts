@file:Suppress("UnstableApiUsage")

pluginManagement {
    repositories {
        google {
            mavenContent {
                includeGroupAndSubgroups("androidx")
                includeGroupAndSubgroups("com.android")
                includeGroupAndSubgroups("com.google")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositories {
        google {
            mavenContent {
                includeGroupAndSubgroups("androidx")
                includeGroupAndSubgroups("com.android")
                includeGroupAndSubgroups("com.google")
            }
        }
        mavenCentral()
        /* libsu（KernelSU 的 shell 层）只发在 JitPack。 */
        maven("https://jitpack.io")
    }
}

rootProject.name = "GhostLock"
include(":app")
// //TODO(kotlin-disable-20261010): profile-core 已禁用（用户裁定删除，检查归 Kotlin）；恢复见台账
// include(":profile-core")
