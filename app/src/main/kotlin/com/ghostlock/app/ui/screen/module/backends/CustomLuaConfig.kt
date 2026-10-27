package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import com.ghostlock.app.ui.backend.BackendSessionStore
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedListItem

/**
 *  「自定义」backend 的配置区（2026-10-18）：负责加载 Lua 脚本。
 *
 *  现在只做“选脚本 → 读内容 → 展示摘要”，真正的 Lua 运行时还没有接（见 [LuaScriptHost]）。
 *  这样 UI 与加载路径先定下来，接 LuaJ/自带解释器时只改这个文件。
 */
@Composable
fun CustomLuaConfig(miuix: Boolean = false) {
    val context = LocalContext.current
    val scriptUri = CustomLuaConfigState.uri
    val summary = CustomLuaConfigState.summary

    val picker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            /* OpenDocument 授权可跨重启；拿持久读权限后 backend 才能恢复时读回。 */
            runCatching {
                context.contentResolver.takePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION,
                )
            }
        }
        CustomLuaConfigState.select(context, uri)
        /* task-27：选择变更立即经 backend save 持久化（重启恢复）。 */
        runCatching {
            BackendCustom.saveConfig(
                BackendSessionStore.from(context).configStore(context, BackendCustom),
            )
        }
    }

    val scriptText = scriptUri?.lastPathSegment ?: stringResource(R.string.backend_custom_none)
    if (miuix) {
        Column {
            top.yukonga.miuix.kmp.preference.ArrowPreference(
                title = stringResource(R.string.backend_custom_script),
                summary = scriptText,
                onClick = {
                    picker.launch(arrayOf("text/x-lua", "text/plain", "application/octet-stream"))
                },
            )
            top.yukonga.miuix.kmp.basic.BasicComponent(
                title = stringResource(R.string.backend_custom_runtime),
                summary = LuaScriptHost.runtimeName,
            )
        }
    } else {
        SegmentedColumn(modifier = Modifier.fillMaxWidth()) {
            item {
                SegmentedListItem(
                    headlineContent = { Text(stringResource(R.string.backend_custom_script)) },
                    supportingContent = { Text(scriptText) },
                    onClick = {
                        picker.launch(arrayOf("text/x-lua", "text/plain", "application/octet-stream"))
                    },
                )
            }
            item {
                SegmentedListItem(
                    headlineContent = { Text(stringResource(R.string.backend_custom_runtime)) },
                    supportingContent = { Text(LuaScriptHost.runtimeName) },
                )
            }
        }
    }
    summary?.let { text ->
        Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(text = text, style = MaterialTheme.typography.bodySmall)
        }
    }
}

/**
 * 自定义 backend 的 Lua 脚本选择状态（进程内；持久化由 [BackendCustom.saveConfig] 负责）。
 *
 * 规则（task-27）：
 * - 只保存 URI 字符串（不含脚本内容/密钥）；
 * - 恢复时校验可读性：URI 失效/无持久权限 ⇒ 清空并显示“未选择”（fail-closed）；
 * - 不再用 Composable `remember` 作为唯一来源，避免重启即丢。
 *
 * @note 阶段：task-27；真机未验证。
 */
object CustomLuaConfigState {
    /** 当前选中的脚本 URI；null = 未选择。 */
    var uri: Uri? by mutableStateOf(null)
        private set

    /** 选中脚本的摘要（文件名/大小/首行）；null = 未选择或读取失败。 */
    var summary: String? by mutableStateOf(null)
        private set

    /**
     * 选择/清空脚本（内存态；持久化由 backend 负责）。
     *
     * @param context 应用上下文。
     * @param newUri 新 URI；null 表示清空。
     */
    fun select(context: Context, newUri: Uri?) {
        uri = newUri
        summary = newUri?.let { runCatching { LuaScriptHost.readSummary(context, it) }.getOrNull() }
    }

    /**
     * 从持久化字符串恢复选择；URI 失效/不可读 ⇒ 清空并返回 false。
     *
     * @param context 应用上下文。
     * @param raw 保存的 URI 字符串（空 = 未选择）。
     * @return true 表示恢复出可读脚本；false 表示 fail-closed 显示未选择。
     */
    fun restore(context: Context, raw: String): Boolean {
        val parsed = raw.takeIf { it.isNotBlank() }?.let { value ->
            runCatching { Uri.parse(value) }.getOrNull()
        }
        if (parsed == null) {
            select(context, null)
            return false
        }
        val readable = runCatching {
            context.contentResolver.openInputStream(parsed)?.use { true } ?: false
        }.getOrDefault(false)
        if (!readable) {
            /* URI 失效/无持久授权：fail-closed 显示未选择，不伪造摘要。 */
            select(context, null)
            return false
        }
        select(context, parsed)
        return true
    }
}

/**
 *  Lua 脚本加载器（占位）。
 *
 *  TODO(wire-lua): 接入 Lua 运行时（LuaJ 或自带解释器）后，在这里执行 [scriptUri]，
 *  并把 stdout / 错误回灌到执行页的终端。
 */
object LuaScriptHost {
    /** 运行时名称（展示用）。 */
    const val runtimeName: String = "未接线（默认不执行）"

    /** 读取脚本前 [maxBytes] 字节，返回“文件名 / 大小 / 首行”摘要。 */
    fun readSummary(context: android.content.Context, uri: Uri, maxBytes: Int = 4096): String {
        val name = context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && cursor.moveToFirst()) cursor.getString(idx) else null
        } ?: uri.lastPathSegment ?: "script.lua"
        val text = runCatching {
            context.contentResolver.openInputStream(uri)?.use { input ->
                val buffer = ByteArray(maxBytes)
                val read = input.read(buffer)
                String(buffer, 0, if (read > 0) read else 0)
            }
        }.getOrNull().orEmpty()
        val firstLine = text.lineSequence().firstOrNull { it.isNotBlank() } ?: "(空文件)"
        return name + " · " + text.length + " 字节\n" + firstLine.take(120)
    }
}
