package com.ghostlock.app.ui.platform

import android.content.pm.ApplicationInfo

/**
 * Applies the activity predictive-back opt-in exposed by newer Android framework builds.
 *
 * @param applicationInfo Current application metadata object.
 * @param enabled Whether callbacks should be enabled before recreating the activity.
 * @note The hidden method is optional; unsupported framework builds leave the current behavior unchanged.
 */
fun setPredictiveBackCallbackEnabled(applicationInfo: ApplicationInfo, enabled: Boolean) {
    runCatching {
        val method = ApplicationInfo::class.java.getDeclaredMethod(
            "setEnableOnBackInvokedCallback",
            Boolean::class.javaPrimitiveType,
        )
        method.isAccessible = true
        method.invoke(applicationInfo, enabled)
    }
}
