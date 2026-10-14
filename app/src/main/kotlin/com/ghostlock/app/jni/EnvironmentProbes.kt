package com.ghostlock.app.jni

import java.io.FileInputStream

/**
 * The three probes the native side used to run itself.
 *
 * Two of them read /sys/fs/selinux/enforce and one reads /proc/self/status. None of them needs a
 * syscall, a kernel interaction or a privilege the app does not already have, so they belong on
 * this side: the native code takes the three answers as fields (EnvironmentFacts) instead of
 * asking. The bodies are the C originals, line for line.
 *
 * Sources: bddfea4:src/core/main.c:400-417 (check_selinux_off, enforce_readable) and :419-435
 * (process_has_seccomp).
 *
 * The third one is the interesting one. Its original comment says why it exists:
 *   "The app flow runs inside zygote, whose seccomp filter blocks finit_module(2). The adb/shell
 *    flow has no filter (Seccomp: 0), and fork() inherits that, so the W2 child does not need W3
 *    there."
 * So the answer decides whether the W3 stage runs at all -- it is the fork point of the path.
 */
object EnvironmentProbes {
    /** The file whose first byte says whether SELinux is enforcing. */
    private const val SELINUX_ENFORCE = "/sys/fs/selinux/enforce"

    /** The file whose Seccomp line says whether a filter is installed. */
    private const val PROC_STATUS = "/proc/self/status"

    /** The prefix of the line that carries the answer, from the original strncmp(..., 8). */
    private const val SECCOMP_PREFIX = "Seccomp:"

    /**
     * Whether SELinux is off, as the original asked it.
     *
     * A file that cannot be opened means "not off": untrusted_app often cannot read enforce while
     * enforcing, so a failure is the answer "still enforcing", not an error to report.
     *
     * @return true when the file exists and its first byte is the character zero.
     */
    fun selinuxOff(): Boolean {
        /* The original opened first and treated a failure as "not off"; there is no separate
         *  readability check to run beforehand. */
        return try {
            val first = FileInputStream(SELINUX_ENFORCE).use { it.read() }
            first == ZERO_CHAR
        } catch (ignored: Exception) {
            false
        }
    }

    /**
     * Whether the enforce file is readable at all.
     *
     * The original opened it and closed it without reading. This opens it too -- canRead()
     * would be an accessibility check, which is close but not the same question.
     *
     * @return true when the file could be opened.
     */
    fun enforceReadable(): Boolean {
        return try {
            FileInputStream(SELINUX_ENFORCE).close()
            true
        } catch (ignored: Exception) {
            false
        }
    }

    /**
     * Whether this process has a seccomp filter.
     *
     * @return true when the status file carries a Seccomp line whose value is not zero.
     * @note A missing file or a missing line both answer false, as the original did.
     */
    fun processHasSeccomp(): Boolean {
        /* The original opened the file with fopen and answered false when that failed; it did
         *  not run a readability check first. */
        return try {
            FileInputStream(PROC_STATUS).bufferedReader().useLines { lines ->
                val value = lines.firstOrNull { it.startsWith(SECCOMP_PREFIX) }
                    ?.substring(SECCOMP_PREFIX.length)?.trim()?.toIntOrNull()
                /* A missing line is not a filter, and neither is an unparsable value. The
                 *  null comparison is spelled out because "null != 0" would be true. */
                value != null && value != 0
            }
        } catch (ignored: Exception) {
            false
        }
    }

    /** The character the enforce file carries when SELinux is off. */
    private const val ZERO_CHAR = 0x30
}
