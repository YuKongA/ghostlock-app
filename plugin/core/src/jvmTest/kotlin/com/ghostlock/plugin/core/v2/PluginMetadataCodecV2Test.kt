package com.ghostlock.plugin.core.v2

import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs
import kotlin.test.assertNotNull

class PluginMetadataCodecV2Test {
    @Test
    fun `JCS and semantic root match approved vectors`() {
        val vectors = listOf(
            """{"a":"é\n","b":"/","n":9007199254740991}""" to
                "11086213d42d89629b66859ac995381c363581e5b44af917d33979e83176bc62",
            """{"A":"\u0000","a":"😀","z":0}""" to
                "241114d9ce18bea7e68b68b364d350276e84f243a0d402ae317711afb28f51d7",
            """{"files":[{"path":"x","sha256":"${ZERO_SHA}","size":0}],"format":{"major":2,"minor":0}}""" to
                "112bd0a46d587329e18fca4e29d1ba1ec583593464dae933ca53d8e2b61b0bc8",
        )

        vectors.forEach { (source, expectedRoot) ->
            assertEquals(source, PluginMetadataCodecV2.canonicalizeJson(source))
            assertEquals(expectedRoot, PluginMetadataCodecV2.semanticRootOfJson(source))
        }
    }

    @Test
    fun `strict JSON rejects duplicate unknown fractional and non NFC input`() {
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackend().replaceFirst("\"major\":2", "\"major\":2,\"major\":2"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackend().replaceFirst("\"minor\":0", "\"minor\":0,\"extra\":0"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackend().replaceFirst("\"versionCode\":1", "\"versionCode\":1.0"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackend().replace("GhostLock", "e\u0301"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackend().replaceFirst("ui/classes.dex", "ui/../classes.dex"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(
                validBackend().replace("\"code\":[\"ui/classes.dex\"]", "\"code\":[\"ui/../classes.dex\"]"),
            )
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(
                validProvider().replace(
                    "\"kind\":\"DOCUMENT\",\"path\":\"data/profile.json\"",
                    "\"kind\":\"DOCUMENT\",\"path\":\"data/./profile.json\"",
                ),
            )
        }
    }

    @Test
    fun `all four component roles decode into closed models`() {
        assertIs<BackendComponentModel>(PluginMetadataCodecV2.decodeManifest(validBackend()).components.single())
        assertIs<ProviderComponentModel>(PluginMetadataCodecV2.decodeManifest(validProvider()).components.single())
        assertIs<ProviderComponentModel>(PluginMetadataCodecV2.decodeManifest(validRuntimeOnlyProvider()).components.single())
        assertIs<ToolComponentModel>(PluginMetadataCodecV2.decodeManifest(validDataTool()).components.single())
        assertIs<CustomExecutorComponentModel>(PluginMetadataCodecV2.decodeManifest(validCustomExecutor()).components.single())
        listOf(
            validBackendWithNative("ARM64", "arm64-v8a", 64, 183),
            validBackendWithNative("ARMV7", "armeabi-v7a", 32, 40),
            validBackendWithNative("X86", "x86", 32, 3),
            validBackendWithNative("X86_64", "x86_64", 64, 62),
        ).forEach { manifest ->
            assertIs<BackendComponentModel>(PluginMetadataCodecV2.decodeManifest(manifest).components.single())
        }
    }

    @Test
    fun `semantic validation closes file provider and data only cross references`() {
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackend().replaceFirst("ui/classes.dex", "ui/missing.dex"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validProvider().replace("\"contractId\":\"ghostlock.physical\",\"contractVersion\":{\"major\":1,\"minor\":0},\"outputs\"", "\"contractId\":\"ghostlock.other\",\"contractVersion\":{\"major\":1,\"minor\":0},\"outputs\""))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validDataTool().replace("\"payloads\":{}", "\"payloads\":{\"ui\":{\"code\":[\"data/profile.json\"]}}"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackend().replace("\"architecture\":\"ARM64\",\"abi\":\"arm64-v8a\"", "\"architecture\":\"ARM64\",\"abi\":\"x86\""))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validProvider().replace("\"resources\":[\"profile\"]", "\"resources\":[]"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackend().replaceFirst("\"builtAgainstMinor\":0,\"minimumHostMinor\":0", "\"builtAgainstMinor\":0,\"minimumHostMinor\":1"))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(
                validProvider().replace(",\"runtimeEntryClass\":\"dev.ghostlock.ProviderRuntime\"", ""),
            )
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(validBackendWithNative("ARM64", "arm64-v8a", 64, 3))
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(desktopManifestWithUiNativeReference())
        }
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodecV2.decodeManifest(
                desktopManifestWithUiNativeReference()
                    .replace("\"ui/plugin.jar\",\"lib/x86_64/libbackend.so\"", "\"ui/plugin.jar\"")
                    .replace("\"code\":[\"runtime/plugin.jar\"]", "\"code\":[\"runtime/plugin.jar\",\"lib/x86_64/libbackend.so\"]"),
            )
        }
    }

    @Test
    fun `semantic root ignores JSON property order but binds manifest content`() {
        val manifest = PluginMetadataCodecV2.decodeManifest(validBackend())
        val reordered = PluginMetadataCodecV2.decodeManifest(
            validBackend().replace(
                "\"publisherId\":\"dev.ghostlock\",\"packageId\":\"backend\"",
                "\"packageId\":\"backend\",\"publisherId\":\"dev.ghostlock\"",
            ),
        )
        val changed = PluginMetadataCodecV2.decodeManifest(validBackend().replace("\"versionCode\":1", "\"versionCode\":2"))

        assertEquals(PluginMetadataCodecV2.semanticContentRoot(manifest), PluginMetadataCodecV2.semanticContentRoot(reordered))
        kotlin.test.assertNotEquals(
            PluginMetadataCodecV2.semanticContentRoot(manifest),
            PluginMetadataCodecV2.semanticContentRoot(changed),
        )
        val exactComponents = PluginMetadataCodecV2.exactComponentObjects(manifest)
        assertEquals(ComponentKindModel.BACKEND, exactComponents.single().kind)
        assertEquals(PluginMetadataCodecV2.semanticContentRoot(manifest), exactComponents.single().exactPackage.semanticContentRoot)
    }

    @Test
    fun `generated schema resource is byte identical to approved authority`() {
        val resource = javaClass.classLoader.getResourceAsStream("plugin-package-v2.schema.json")
        assertNotNull(resource)
        val generatedBytes = resource.use { it.readBytes() }
        val sourcePath = assertNotNull(System.getProperty("ghostlock.plugin.v2.schema.source"))
        assertEquals(File(sourcePath).readBytes().toList(), generatedBytes.toList())
    }

    private fun validBackend(): String = executableManifest(
        component = """
          {"kind":"BACKEND","id":"backend","uiEntryClass":"dev.ghostlock.BackendUi","runtimeEntryClass":"dev.ghostlock.BackendRuntime","resources":[],"dependencies":[],"exports":[],"hostOperations":[],"backendKey":"ghostlock.backend","selectionPolicy":"SELECTABLE","executionDomains":["APP"],"abortPolicy":"COOPERATIVE","providerSlots":[]}
        """.trimIndent(),
    )

    private fun validProvider(): String = executableManifest(
        component = """
          {"kind":"BACKEND_PROVIDER","id":"provider","uiEntryClass":"dev.ghostlock.ProviderUi","runtimeEntryClass":"dev.ghostlock.ProviderRuntime","resources":["profile"],"dependencies":[],"exports":[],"staticExports":[{"id":"physical","contractId":"ghostlock.physical","contractVersion":{"major":1,"minor":0},"outputs":[{"outputId":"profile","resourceId":"profile"}]}],"hostOperations":[],"providerBinding":{"parent":{"publisherId":"dev.ghostlock","packageId":"backend","componentId":"backend"},"slotId":"physical","contractId":"ghostlock.physical","contractVersion":{"major":1,"minor":0},"physicalStaticExportId":"physical"},"jobs":[]}
        """.trimIndent(),
        resources = """[{"id":"profile","kind":"DOCUMENT","path":"data/profile.json","schemaId":"ghostlock.profile","schemaVersion":{"major":1,"minor":0},"executable":false}]""",
        extraFiles = ",{" + "\"path\":\"data/profile.json\",\"sha256\":\"$ZERO_SHA\",\"size\":0}",
    )

    private fun validCustomExecutor(): String = executableManifest(
        component = """
          {"kind":"CUSTOM_EXECUTOR","id":"custom","uiEntryClass":"dev.ghostlock.CustomUi","runtimeEntryClass":"dev.ghostlock.CustomRuntime","resources":[],"dependencies":[],"exports":[],"hostOperations":[],"languages":["lua"],"webUiSupport":false}
        """.trimIndent(),
    )

    private fun validRuntimeOnlyProvider(): String = runtimeOnlyManifest(
        component = """
          {"kind":"BACKEND_PROVIDER","id":"provider","runtimeEntryClass":"dev.ghostlock.ProviderRuntime","resources":[],"dependencies":[],"exports":[],"staticExports":[],"hostOperations":[],"providerBinding":{"parent":{"publisherId":"dev.ghostlock","packageId":"backend","componentId":"backend"},"slotId":"physical","contractId":"ghostlock.physical","contractVersion":{"major":1,"minor":0}},"jobs":[]}
        """.trimIndent(),
    )

    private fun validBackendWithNative(
        architecture: String,
        abi: String,
        elfClass: Int,
        elfMachine: Int,
    ): String = executableManifest(
        component = """
          {"kind":"BACKEND","id":"backend","uiEntryClass":"dev.ghostlock.BackendUi","runtimeEntryClass":"dev.ghostlock.BackendRuntime","resources":[],"dependencies":[],"exports":[],"hostOperations":[],"backendKey":"ghostlock.backend","selectionPolicy":"SELECTABLE","executionDomains":["APP"],"abortPolicy":"COOPERATIVE","providerSlots":[]}
        """.trimIndent(),
        extraFiles = ",{" + "\"path\":\"lib/$abi/libbackend.so\",\"sha256\":\"$ZERO_SHA\",\"size\":0}",
        nativeLibraries = """[{"path":"lib/$abi/libbackend.so","abi":"$abi","elfClass":$elfClass,"elfMachine":$elfMachine,"soname":"libbackend.so","neededLibraries":[]}]""",
    ).replace(
        "\"architecture\":\"ARM64\",\"abi\":\"arm64-v8a\"",
        "\"architecture\":\"$architecture\",\"abi\":\"$abi\"",
    )

    private fun desktopManifestWithUiNativeReference(): String = """
        {"format":{"major":2,"minor":0},"package":{"publisherId":"dev.ghostlock","packageId":"desktop","versionCode":1,"versionName":"GhostLock","variant":"linux-x86-64"},"target":{"platform":"DESKTOP_LINUX","architecture":"X86_64","abi":"x86_64","minimumPlatformVersion":0,"dataOnly":false},"api":{"runtime":{"major":2,"builtAgainstMinor":0,"minimumHostMinor":0},"ui":{"major":2,"builtAgainstMinor":0,"minimumHostMinor":0}},"files":[{"path":"ui/plugin.jar","sha256":"$ZERO_SHA","size":0},{"path":"runtime/plugin.jar","sha256":"$ZERO_SHA","size":0},{"path":"lib/x86_64/libbackend.so","sha256":"$ZERO_SHA","size":0},{"path":"i18n/en.json","sha256":"$ZERO_SHA","size":0}],"payloads":{"ui":{"code":["ui/plugin.jar","lib/x86_64/libbackend.so"]},"runtime":{"code":["runtime/plugin.jar"],"nativeLibraries":[{"path":"lib/x86_64/libbackend.so","abi":"x86_64","elfClass":64,"elfMachine":62,"soname":"libbackend.so","neededLibraries":[]}]}},"resources":[],"i18n":{"namespace":"dev.ghostlock.desktop","defaultLocale":"en","bundles":[{"locale":"en","path":"i18n/en.json"}]},"components":[{"kind":"BACKEND","id":"backend","uiEntryClass":"dev.ghostlock.BackendUi","runtimeEntryClass":"dev.ghostlock.BackendRuntime","resources":[],"dependencies":[],"exports":[],"hostOperations":[],"backendKey":"ghostlock.backend","selectionPolicy":"SELECTABLE","executionDomains":["APP"],"abortPolicy":"COOPERATIVE","providerSlots":[]}]}
    """.trimIndent()

    private fun validDataTool(): String = """
        {"format":{"major":2,"minor":0},"package":{"publisherId":"dev.ghostlock","packageId":"profiles","versionCode":1,"versionName":"GhostLock","variant":"universal"},"target":{"platform":"UNIVERSAL","architecture":"UNIVERSAL","abi":"universal","minimumPlatformVersion":0,"dataOnly":true},"api":{"runtime":{"major":2,"builtAgainstMinor":0,"minimumHostMinor":0},"ui":{"major":2,"builtAgainstMinor":0,"minimumHostMinor":0}},"files":[{"path":"data/profile.json","sha256":"$ZERO_SHA","size":0},{"path":"i18n/en.json","sha256":"$ZERO_SHA","size":0}],"payloads":{},"resources":[{"id":"profile","kind":"DOCUMENT","path":"data/profile.json","schemaId":"ghostlock.profile","schemaVersion":{"major":1,"minor":0},"executable":false}],"i18n":{"namespace":"dev.ghostlock.profiles","defaultLocale":"en","bundles":[{"locale":"en","path":"i18n/en.json"}]},"components":[{"kind":"TOOL","id":"profiles","resources":["profile"],"dependencies":[],"exports":[],"staticExports":[{"id":"profiles","contractId":"ghostlock.profiles","contractVersion":{"major":1,"minor":0},"outputs":[{"outputId":"profile","resourceId":"profile"}]}],"hostOperations":[],"jobs":[]}]}
    """.trimIndent()

    private fun executableManifest(
        component: String,
        resources: String = "[]",
        extraFiles: String = "",
        nativeLibraries: String = "[]",
    ): String = """
        {"format":{"major":2,"minor":0},"package":{"publisherId":"dev.ghostlock","packageId":"backend","versionCode":1,"versionName":"GhostLock","variant":"android-arm64"},"target":{"platform":"ANDROID","architecture":"ARM64","abi":"arm64-v8a","minimumPlatformVersion":31,"dataOnly":false},"api":{"runtime":{"major":2,"builtAgainstMinor":0,"minimumHostMinor":0},"ui":{"major":2,"builtAgainstMinor":0,"minimumHostMinor":0}},"files":[{"path":"ui/classes.dex","sha256":"$ZERO_SHA","size":0},{"path":"runtime/classes.dex","sha256":"$ZERO_SHA","size":0},{"path":"i18n/en.json","sha256":"$ZERO_SHA","size":0}$extraFiles],"payloads":{"ui":{"code":["ui/classes.dex"]},"runtime":{"code":["runtime/classes.dex"],"nativeLibraries":$nativeLibraries}},"resources":$resources,"i18n":{"namespace":"dev.ghostlock.backend","defaultLocale":"en","bundles":[{"locale":"en","path":"i18n/en.json"}]},"components":[$component]}
    """.trimIndent()

    private fun runtimeOnlyManifest(component: String): String = """
        {"format":{"major":2,"minor":0},"package":{"publisherId":"dev.ghostlock","packageId":"backend","versionCode":1,"versionName":"GhostLock","variant":"android-arm64"},"target":{"platform":"ANDROID","architecture":"ARM64","abi":"arm64-v8a","minimumPlatformVersion":31,"dataOnly":false},"api":{"runtime":{"major":2,"builtAgainstMinor":0,"minimumHostMinor":0},"ui":{"major":2,"builtAgainstMinor":0,"minimumHostMinor":0}},"files":[{"path":"runtime/classes.dex","sha256":"$ZERO_SHA","size":0},{"path":"i18n/en.json","sha256":"$ZERO_SHA","size":0}],"payloads":{"runtime":{"code":["runtime/classes.dex"],"nativeLibraries":[]}},"resources":[],"i18n":{"namespace":"dev.ghostlock.backend","defaultLocale":"en","bundles":[{"locale":"en","path":"i18n/en.json"}]},"components":[$component]}
    """.trimIndent()

    private companion object {
        const val ZERO_SHA = "0000000000000000000000000000000000000000000000000000000000000000"
    }
}
