package com.ghostlock.app.backend.cve43499

import java.io.File
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * G19 pselect fd install 窗口收缩契约（task-79）——**先写测试、不运行**。
 *
 * host-parseable 源码契约：prepare 不得 install/dup2；execute(trigger) 必须在
 * consumer/首轮 select 前 install、attempt 结束后 restore；失败 rollback/retained。
 * 不加载 .so、不编译、不触 Android/真机。
 */
class PselectWindowContractTest {

    @Test
    fun `prepare does not install or dup2 target fds`() {
        val source = readPselect()
        val prepare = slice(source, "int32_t glk43499_carrier_pselect_prepare(",
            "/**\n * @brief PI 窗口内执行 Pselect 触发")
        assertTrue("prepare must collect/backup", prepare.contains(
            "glk43499_pselect_backup_targets(window, table)"))
        assertFalse("G19 prepare must not install", prepare.contains(
            "glk43499_pselect_install_targets("))
        assertFalse("G19 prepare must not call dup2 directly", prepare.contains("dup2("))
        assertTrue("prepare selected_fds_installed=0", prepare.contains(
            "window->selected_fds_installed = 0;"))
    }

    @Test
    fun `trigger installs before consumer and restores after attempts`() {
        val source = readPselect()
        val trigger = slice(source, "void glk43499_carrier_pselect_trigger(",
            "int32_t glk43499_carrier_pselect_disarm(")
        val installIndex = trigger.indexOf("glk43499_pselect_install_targets(")
        val attemptIndex = trigger.indexOf("for (uint64_t attempt = 1;")
        val restoreIndex = trigger.indexOf("glk43499_pselect_restore_backups(window)")
        assertTrue("install before attempts", installIndex in 0..<attemptIndex)
        assertTrue("restore after attempts", restoreIndex > attemptIndex)
        assertTrue(trigger.contains("selected_fds_installed = 1;"))
        assertTrue(trigger.contains("selected_fds_installed = 0;"))
        assertTrue(trigger.contains("window->retained = 1;"))
        assertTrue(trigger.contains("restore_clean == 0"))
        assertTrue(trigger.contains("GLK43499_CARRIER_STEP_IO"))
    }

    @Test
    fun `install failure rolls back installed entries and names errno`() {
        val source = readPselect()
        val install = slice(source, "static int32_t glk43499_pselect_install_targets(",
            "/**\n * @brief 释放 Carrier 的全部 fd 与对象内缓冲")
        assertTrue(install.contains("failure_errno"))
        assertTrue(install.contains("*failure_errno = errno != 0 ? errno : EIO;"))
        assertTrue(install.contains("glk43499_pselect_restore_backups(window)"))
        assertTrue(install.contains("window->retained = 1;"))
    }

    @Test
    fun `task-132 snapshots cloexec and forced slot before install`() {
        val source = readPselect()
        val backup = slice(source, "static int32_t glk43499_pselect_backup_targets(",
            "/**\n * @brief 把每个目标 fd 覆盖成永不就绪")
        assertTrue("task-132 must snapshot FD_GETFD", backup.contains("F_GETFD"))
        assertTrue("task-132 backup entry flags table", source.contains("int32_t (*entries)[4]"))
        assertTrue("task-132 must include forced slot",
            source.contains("entries[count][0] = nfds - 1;"))
        val restore = slice(source, "static int32_t glk43499_pselect_restore_backups(",
            "/**\n * @brief 释放尚未安装时的备份 fd")
        assertTrue("task-132 must restore FD_CLOEXEC", restore.contains("F_SETFD"))
        assertTrue("dup2 then FD_SETFD", restore.indexOf("dup2(backup, target)") in
            0..<restore.indexOf("F_SETFD"))
    }

    @Test
    fun `dispose fallback does not restore already restored entries`() {
        val source = readPselect()
        val dispose = slice(source, "static int32_t glk43499_pselect_dispose(",
            "/**\n * @brief 准备 Pselect Carrier")
        assertTrue(dispose.contains("if (window->selected_fds_installed != 0)"))
        assertTrue(dispose.contains("glk43499_pselect_close_backups(window)"))
        assertTrue(dispose.contains("window->selected_fds_installed = 0;"))
    }

    /** @return carrier_pselect.c 文本。 */
    private fun readPselect(): String {
        val relative = "app/src/main/cpp/gl43499/core/carrier_pselect.c"
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        return (candidates.firstOrNull { it.isFile } ?: error("missing $relative")).readText()
    }

    /**
     * @param source 文本。
     * @param start 起始锚点（含）。
     * @param end 结束锚点（不含）。
     * @return 区间文本。
     */
    private fun slice(source: String, start: String, end: String): String {
        val startIndex = source.indexOf(start)
        assertTrue("missing start anchor: $start", startIndex >= 0)
        val endIndex = source.indexOf(end, startIndex + start.length)
        assertTrue("missing end anchor: $end", endIndex > startIndex)
        return source.substring(startIndex, endIndex)
    }
}
