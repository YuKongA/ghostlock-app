package com.ghostlock.app.ui.screen.module.backends

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
    var scriptUri by remember { mutableStateOf<Uri?>(null) }
    var summary by remember { mutableStateOf<String?>(null) }

    val picker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
    ) { uri ->
        scriptUri = uri
        summary = uri?.let { LuaScriptHost.readSummary(context, it) }
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
