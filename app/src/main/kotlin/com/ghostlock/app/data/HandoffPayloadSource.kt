package com.ghostlock.app.data

import android.content.Context
import android.util.Log
import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.data.profile.HandoffPayload

/**
 * A6-4 device-side resolve for document segment three.
 *
 * HOCON declares intent; the payloads themselves are produced HERE, on the
 * device, because only the device knows which KMI label its kernel needs. The
 * rules are:
 *
 * - the name leads with the terminal the document selected (a selectable
 *   terminal from the catalogue, never a literal), so a handoff can only name
 *   the payload of a terminal that exists;
 * - an artifact that is not in the APK is ABSENT: nothing is declared and no
 *   placeholder is written (an empty payload is refused by the native check, so
 *   inventing one would be a lie with a digest);
 * - a terminal without closing content in the tree contributes nothing today
 *   (umh_forward has none yet), which is the same "absent" answer.
 *
 * [forTerminal] takes the asset reader so the rules are testable off-device;
 * [assetReader] is the Android binding.
 */
internal object HandoffPayloadSource {
    private const val TAG = "HandoffPayload"

    /** Directory inside the APK assets that carries the finishing script. */
    const val PAYLOAD_ROOT = "payload"

    /** File name of the finishing script asset (packaged by the Gradle build). */
    const val SCRIPT_ASSET = "root_cmd.sh"

    /** Suffixes of the two deliverable names (terminal + underscore + suffix). */
    const val SCRIPT_SUFFIX = "script"
    const val MODULE_SUFFIX = "module"

    /**
     * The payloads the document hands over for one terminal.
     *
     * @param release the device release the KMI label is derived from.
     * @param terminal the terminal token the document selected, or null.
     * @param readAsset asset reader; null means "this artifact is not packaged".
     * @return the payloads, empty when this terminal has no closing content or
     *   none of it is packaged (absence is expressed by absence).
     * @throws IllegalArgumentException only when an artifact is packaged under a
     *   name the payload rule refuses (a build defect, not a device condition).
     */
    fun forTerminal(
        release: String,
        terminal: String?,
        /**
         * Where a refusal is reported. Empty by default so the rule stays a pure
         * function (no Android logging in the decision path); the device call site
         * passes [androidWarning], and a test can capture the reason instead of a
         * refusal happening silently. Declared BEFORE the reader on purpose: the
         * reader stays the last parameter, so a call site can still pass it as a
         * trailing lambda.
         */
        onWarning: (String) -> Unit = {},
        readAsset: (String) -> ByteArray?,
    ): List<HandoffPayload> {
        if (terminal == null) return emptyList()
        /* Only a declared, selectable terminal may lead a payload name; anything
         * else is a caller defect and contributes nothing rather than throwing on
         * a hot path (the document build runs on every profile load). */
        if (terminal !in HandoffPayload.TERMINALS) {
            onWarning("terminal without a payload rule: " + terminal)
            return emptyList()
        }
        if (terminal != FrontendKind.RootChild.token) {
            /* No closing content exists for this terminal yet: absence, not a
             * placeholder. Adding content later means adding an asset and a row
             * here - the rule itself does not change. */
            return emptyList()
        }
        val payloads = mutableListOf<HandoffPayload>()
        readAsset(PAYLOAD_ROOT + "/" + SCRIPT_ASSET)?.let { bytes ->
            payloads += HandoffPayload.of(terminal + "_" + SCRIPT_SUFFIX, "script", bytes)
        }
        val label = LkmImageProvisioner.labelFor(release)
        if (label != null) {
            readAsset(LkmImageProvisioner.assetPath(label))?.let { bytes ->
                payloads += HandoffPayload.of(terminal + "_" + MODULE_SUFFIX, "module", bytes)
            }
        }
        return payloads
    }

    /** The Android binding for refusals (device-side call sites only). */
    fun androidWarning(): (String) -> Unit = { message -> Log.w(TAG, message) }

    /** The Android binding: an unreadable asset is an absent artifact. */
    fun assetReader(context: Context): (String) -> ByteArray? = { path ->
        runCatching { context.assets.open(path).use { stream -> stream.readBytes() } }
            .getOrElse {
                Log.w(TAG, "payload asset not packaged: " + path)
                null
            }
    }
}
