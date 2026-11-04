package com.ghostlock.app.data.device

import android.annotation.SuppressLint
import android.os.Build
import java.io.File

/** Reads a commercial device name without exposing Android APIs to presentation. */
class AndroidDeviceNameReader {
    /**
     * Resolves a vendor market name and falls back to manufacturer, brand, and model.
     *
     * @return Stable non-empty device label for the current Android build.
     */
    fun read(): String {
        val marketName = when (Build.MANUFACTURER.orEmpty().lowercase()) {
            "samsung" -> readSamsungProductName()
            "xiaomi" -> readSystemProperty("ro.product.marketname").takeIfValid()
            "oppo", "oneplus", "realme", "oplus" ->
                readSystemProperty("ro.vendor.oplus.market.name").takeIfValid()

            "vivo" -> readSystemProperty("ro.vivo.market.name").takeIfValid()
            "honor", "huawei" -> readSystemProperty("ro.config.marketing_name").takeIfValid()
            "zte", "nubia" -> readSystemProperty("ro.vendor.product.ztename").takeIfValid()
            else -> null
        }
        return marketName ?: buildString {
            append(Build.MANUFACTURER)
            if (!Build.BRAND.equals(Build.MANUFACTURER, ignoreCase = true)) {
                append(' ').append(Build.BRAND)
            }
            append(' ').append(Build.MODEL)
        }.trim()
    }

    /** @return Samsung market name from the framework or vendor XML, when available. */
    private fun readSamsungProductName(): String? {
        val name = readSamsungFloatingFeature() ?: readSamsungFloatingFeatureFile()
        val normalized = name.takeIfValid() ?: return null
        return if (normalized.startsWith("Samsung", ignoreCase = true)) {
            normalized
        } else {
            "Samsung $normalized"
        }
    }

    /** @return Samsung framework floating-feature value, or null on unsupported builds. */
    @SuppressLint("PrivateApi")
    private fun readSamsungFloatingFeature(): String? = runCatching {
        val clazz = Class.forName(SAMSUNG_FLOATING_FEATURE_CLASS)
        val instance = clazz.getMethod("getInstance").invoke(null)
        clazz.getMethod("getString", String::class.java)
            .invoke(instance, KEY_SAMSUNG_PRODUCT_NAME) as? String
    }.getOrNull()?.takeIfValid()

    /** @return Samsung vendor XML value, or null when no readable source exists. */
    private fun readSamsungFloatingFeatureFile(): String? {
        val pattern = Regex(
            "<$KEY_SAMSUNG_PRODUCT_NAME>\\s*(.*?)\\s*</$KEY_SAMSUNG_PRODUCT_NAME>",
            RegexOption.DOT_MATCHES_ALL,
        )
        return SAMSUNG_FLOATING_FEATURE_PATHS.firstNotNullOfOrNull { path ->
            runCatching {
                pattern.find(File(path).readText())?.groupValues?.getOrNull(1).takeIfValid()
            }.getOrNull()
        }
    }

    /** @return Android system property value, or an empty string when reflection is unavailable. */
    private fun readSystemProperty(key: String): String = runCatching {
        val clazz = Class.forName("android.os.SystemProperties")
        clazz.getMethod("get", String::class.java).invoke(null, key) as String
    }.getOrDefault("")

    /** @return Trimmed usable device label, or null for framework sentinel values. */
    private fun String?.takeIfValid(): String? = this?.trim()?.takeIf {
        it.isNotEmpty() &&
            !it.equals(Build.UNKNOWN, ignoreCase = true) &&
            !it.equals("null", ignoreCase = true)
    }

    private companion object {
        const val SAMSUNG_FLOATING_FEATURE_CLASS = "com.samsung.android.feature.SemFloatingFeature"
        const val KEY_SAMSUNG_PRODUCT_NAME = "SEC_FLOATING_FEATURE_SETTINGS_CONFIG_BRAND_NAME"
        val SAMSUNG_FLOATING_FEATURE_PATHS = listOf(
            "/vendor/etc/floating_feature.xml",
            "/system/etc/floating_feature.xml",
        )
    }
}
