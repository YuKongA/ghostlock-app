package com.ghostlock.app.ui.component.profile

import androidx.annotation.StringRes
import androidx.compose.runtime.Immutable
import com.ghostlock.app.R

/**
 * Storage- and backend-neutral editable profile consumed by reusable profile components.
 *
 * @property name User-facing profile name.
 * @property rootUseDefault Whether the elevated profile inherits the backend default.
 * @property rootTemplate Optional selected template identifier.
 * @property uid Requested process user identifier.
 * @property gid Requested process primary group identifier.
 * @property groups Requested supplementary group identifiers.
 * @property capabilities Requested Linux capability identifiers.
 * @property context Requested SELinux execution context.
 * @property namespace Mount namespace mode ordinal.
 * @property nonRootUseDefault Whether the non-elevated profile inherits the backend default.
 * @property umountModules Whether module mounts should be hidden from the process.
 * @property rules Additional SELinux policy statements.
 * @property flags Raw reusable profile-option bit set.
 * @note This model deliberately contains no native bridge, manager-specific, Android parcel, or persistence type.
 */
@Immutable
data class ProfileDraft(
    val name: String = "",
    val rootUseDefault: Boolean = true,
    val rootTemplate: String? = null,
    val uid: Int = 0,
    val gid: Int = 0,
    val groups: List<Int> = emptyList(),
    val capabilities: List<Int> = emptyList(),
    val context: String = "",
    val namespace: Int = 0,
    val nonRootUseDefault: Boolean = true,
    val umountModules: Boolean = true,
    val rules: String = "",
    val flags: Long = ProfileFlag.NO_NEW_PRIVILEGES.mask,
)

/** Immutable template choice supplied by a caller-owned catalog. */
@Immutable
data class ProfileTemplateOption(
    val id: String,
    val name: String = id,
)

/** Reusable profile-option catalog independent of a backend-native enum. */
enum class ProfileFlag(
    val bit: Int,
    val display: String,
    @get:StringRes val description: Int,
) {
    NO_NEW_PRIVILEGES(
        bit = 0,
        display = "NO_NEW_PRIVS",
        description = R.string.profile_flags_desc_no_new_privs,
    );

    /** Raw bit mask persisted by the surrounding profile adapter. */
    val mask: Long = 1L.shl(bit)
}

/** Converts a raw profile bit set into its known reusable component flags. */
fun Long.toProfileFlags(): List<ProfileFlag> =
    ProfileFlag.entries.filter { flag -> flag.mask.and(this) != 0L }

/** Converts selected reusable component flags back into their raw bit set. */
fun Iterable<ProfileFlag>.toRawProfileFlags(): Long =
    fold(0L) { value, flag -> value.or(flag.mask) }

/** Converts a raw profile bit set into the ordinal list used by the legacy template wire. */
fun Long.toProfileFlagOrdinals(): List<Int> =
    toProfileFlags().map(ProfileFlag::bit)
