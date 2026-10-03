@file:Suppress("UnstableApiUsage")

pluginManagement {
    repositories {
        /* 国内镜像优先（阿里云代理 google / central / gradle-plugin），
         * 官方源保留在后面兜底：镜像不可达时构建仍可继续。 */
        maven("https://maven.aliyun.com/repository/gradle-plugin")
        maven("https://maven.aliyun.com/repository/google")
        maven("https://maven.aliyun.com/repository/public")
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
        /* 同 pluginManagement：镜像优先，官方兜底。 */
        maven("https://maven.aliyun.com/repository/google")
        maven("https://maven.aliyun.com/repository/public")
        google {
            mavenContent {
                includeGroupAndSubgroups("androidx")
                includeGroupAndSubgroups("com.android")
                includeGroupAndSubgroups("com.google")
            }
        }
        mavenCentral()
    }
}

rootProject.name = "GhostLock"
include(":app")
include(":profile-core")
