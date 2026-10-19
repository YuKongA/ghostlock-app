package com.ghostlock.app.ui

import android.os.Parcelable
import androidx.annotation.Keep
import androidx.compose.runtime.Immutable
import kotlinx.parcelize.Parcelize
import kotlinx.serialization.Serializable
import com.ghostlock.app.R
import com.ghostlock.app.ui.Natives.Profile.RootProfileFlag
import com.ghostlock.app.ui.util.rootAvailable

/**
 * @author weishu
 * @date 2022/12/8.
 *
 * 未接线假实现（2026-10-18 恢复移植）：不加载 libkernelsu，也没有任何 external 声明；
 * 全部返回固定样例值，保证 UI 能跑、不触达内核。
 */
object Natives {
    // minimal supported kernel version
    // 10915: allowlist breaking change, add app profile
    // 10931: app profile struct add 'version' field
    // 10946: add capabilities
    // 10977: change groups_count and groups to avoid overflow write
    // 11071: Fix the issue of failing to set a custom SELinux type.
    // 12143: breaking: new supercall impl
    // 32310: new get_allow_list ioctl
    // 32336: new set_sepolicy ioctl
    // 32377: add set_init_pgrp ioctl
    // 32513: add uapi version
    const val MINIMAL_SUPPORTED_KERNEL = 32513

    const val KERNEL_SU_DOMAIN = "u:r:ksu:s0"

    const val ROOT_UID = 0
    const val ROOT_GID = 0

    /** 假内核版本号（未接线：0）。 */
    val version: Int = 0

    /** 安全模式（未接线：false）。 */
    val isSafeMode: Boolean = false

    /** LKM 模式（未接线：false）。 */
    val isLkmMode: Boolean = false

    /** LKM 是否内置（未接线：false）。 */
    val isLkmBundled: Boolean = false

    /** 晚加载模式（未接线：false）。 */
    val isLateLoadMode: Boolean = false

    /** 是不是管理器（未接线：恒 true，让 UI 走"已安装"分支）。 */
    val isManager: Boolean = true

    /** 是否 PR 构建（未接线：false）。 */
    val isPrBuild: Boolean = false

    /**
     * 该 uid 是否应 umount 模块（未接线）。
     *
     * @param uid 目标 uid，被忽略。
     * @return 恒为 false。
     */
    fun uidShouldUmount(uid: Int): Boolean = false

    /**
     * Get the profile of the given package.
     * @param key usually the package name
     * @return 未接线：恒返回默认 Profile。
     */
    fun getAppProfile(key: String?, uid: Int): Profile = Profile()

    /**
     * 写入应用配置（未接线）。
     *
     * @param profile 配置，被忽略。
     * @return 恒为 true。
     */
    fun setAppProfile(profile: Profile?): Boolean = true

    /**
     * `su` compat 状态（未接线）。
     *
     * @return 恒为 true。
     */
    fun isSuEnabled(): Boolean = true

    /**
     * 设置 `su` compat（未接线）。
     *
     * @param enabled 目标状态。
     * @return 原样返回 enabled。
     */
    fun setSuEnabled(enabled: Boolean): Boolean = enabled

    /**
     * 模块 umount 状态（未接线）。
     *
     * @return 恒为 true。
     */
    fun isKernelUmountEnabled(): Boolean = true

    /**
     * 设置模块 umount（未接线）。
     *
     * @param enabled 目标状态。
     * @return 原样返回 enabled。
     */
    fun setKernelUmountEnabled(enabled: Boolean): Boolean = enabled

    /**
     * SELinux hide 状态（未接线）。
     *
     * @return 恒为 false。
     */
    fun isSelinuxHideEnabled(): Boolean = false

    /**
     * 设置 SELinux hide（未接线）。
     *
     * @param enabled 目标状态。
     * @return 1 = 成功，0 = enabled。
     */
    fun setSelinuxHideEnabled(enabled: Boolean): Int = if (enabled) 1 else 0

    /**
     * Get the user name for the uid（未接线）。
     *
     * @param uid 目标 uid。
     * @return 固定样例 "uid <n>"。
     */
    fun getUserName(uid: Int): String? = "uid " + uid

    /**
     * 超级用户授权数量（未接线）。
     *
     * @return 恒为 0。
     */
    fun getSuperuserCount(): Int = 0

    private const val NON_ROOT_DEFAULT_PROFILE_KEY = "$"
    private const val NOBODY_UID = 9999

    fun setDefaultUmountModules(umountModules: Boolean): Boolean {
        Profile(
            NON_ROOT_DEFAULT_PROFILE_KEY,
            NOBODY_UID,
            false,
            umountModules = umountModules
        ).let {
            return setAppProfile(it)
        }
    }

    fun isDefaultUmountModules(): Boolean {
        getAppProfile(NON_ROOT_DEFAULT_PROFILE_KEY, NOBODY_UID).let {
            return it.umountModules
        }
    }

    /** 内核 UAPI 版本（未接线：0）。 */
    val kernelUAPIVersion: Int = 0

    /** 管理器 UAPI 版本（未接线：0）。 */
    val managerUAPIVersion: Int = 0

    fun isFullFeatured(): Boolean {
        return isManager && kernelUAPIVersion == managerUAPIVersion && rootAvailable()
    }

    @Keep
    @Immutable
    @Parcelize
    @Serializable
    data class Profile(
        // and there is a default profile for root and non-root
        val name: String,
        // current uid for the package, this is convivent for kernel to check
        // if the package name doesn't match uid, then it should be invalidated.
        val currentUid: Int = 0,

        // if this is true, kernel will grant root permission to this package
        val allowSu: Boolean = false,

        // these are used for root profile
        val rootUseDefault: Boolean = true,
        val rootTemplate: String? = null,
        val uid: Int = ROOT_UID,
        val gid: Int = ROOT_GID,
        val groups: List<Int> = mutableListOf(),
        val capabilities: List<Int> = mutableListOf(),
        val context: String = KERNEL_SU_DOMAIN,
        val namespace: Int = Namespace.INHERITED.ordinal,

        val nonRootUseDefault: Boolean = true,
        val umountModules: Boolean = true,
        var rules: String = "", // this field is save in ksud!!

        val flags: Long = FLAG_KSU_NO_NEW_PRIVS,
    ) : Parcelable {
        @Keep
        enum class RootProfileFlag(val display: String, val desc: Int) {
            NO_NEW_PRIVS(
                "NO_NEW_PRIVS",
                R.string.profile_flags_desc_no_new_privs
            )
        }

        enum class Namespace {
            INHERITED,
            GLOBAL,
            INDIVIDUAL,
        }

        constructor() : this("")
    }

    const val FLAG_KSU_NO_NEW_PRIVS = 1L
}

fun List<RootProfileFlag>.toRawFlags(): Long =
    fold(0L) { acc, flag -> acc.or(1L.shl(flag.ordinal)) }

fun List<RootProfileFlag>.toOrdinalList(): List<Int> =
    map { it.ordinal }

fun Long.toRootProfileFlags(): List<RootProfileFlag> =
    RootProfileFlag.entries.filter { 1L.shl(it.ordinal).and(this) != 0L }.toList()
