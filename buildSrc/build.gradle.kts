plugins {
    `kotlin-dsl`
}

kotlin {
    jvmToolchain(21)
}

sourceSets {
    main {
        kotlin.exclude("**/GenerateSupportedKernelsTask 2.kt")
    }
}
