package com.ghostlock.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ghostlock.app.R
import top.yukonga.miuix.kmp.basic.ButtonDefaults
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.CardDefaults
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixTheme
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Close
import top.yukonga.miuix.kmp.icon.extended.Copy
import top.yukonga.miuix.kmp.overlay.OverlayBottomSheet

interface GhostlockActions {
    fun onRun()
    fun onCloseExecutionSheet()
    fun onCopyLogs()
}

@Composable
internal fun GhostlockApp(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    MiuixTheme(
        colors = if (isSystemInDarkTheme()) {
            top.yukonga.miuix.kmp.theme.darkColorScheme()
        } else {
            top.yukonga.miuix.kmp.theme.lightColorScheme()
        },
    ) {
        Scaffold(
            modifier = Modifier.fillMaxSize(),
            topBar = {
                TopAppBar(title = "GhostLock")
            },
        ) { paddingValues ->
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(paddingValues)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    StatusCard(state)
                    TextButton(
                        text = if (state.running) stringResource(R.string.action_running) else stringResource(R.string.action_run),
                        enabled = !state.running && state.kernelSupported,
                        colors = ButtonDefaults.textButtonColorsPrimary(),
                        onClick = actions::onRun,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
                GhostlockExecutionSheet(state = state, actions = actions)
            }
        }
    }
}

@Composable
private fun StatusCard(state: GhostlockUiState) {
    Card(
        colors = CardDefaults.defaultColors(
            color = if (state.kernelSupported) {
                MiuixTheme.colorScheme.secondaryContainer
            } else {
                MiuixTheme.colorScheme.errorContainer
            },
        ),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = if (state.deviceName.isEmpty()) "Device: Detecting..." else state.deviceName,
                fontSize = 18.sp,
                fontWeight = FontWeight.SemiBold,
                color = MiuixTheme.colorScheme.onSurface,
            )
            Text(
                text = "SoC: ${if (state.socName.isEmpty()) "Detecting..." else state.socName}",
                fontSize = 14.sp,
                color = MiuixTheme.colorScheme.onSurface.copy(alpha = 0.72f),
            )
            Text(
                text = "Kernel: ${if (state.kernelRelease.isEmpty()) "Detecting..." else state.kernelRelease}",
                fontSize = 14.sp,
                color = MiuixTheme.colorScheme.onSurface.copy(alpha = 0.72f),
            )
            Text(
                text = if (state.kernelSupported) {
                    if (state.autoProfileReady) "Status: Ready - Tap Run to execute" else "Status: Profile unavailable"
                } else {
                    "Status: Kernel not supported"
                },
                fontSize = 13.sp,
                color = MiuixTheme.colorScheme.onSurface.copy(alpha = 0.72f),
                fontWeight = FontWeight.Medium,
            )
        }
    }
}

@Composable
private fun GhostlockExecutionSheet(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    OverlayBottomSheet(
        show = state.executionSheetVisible,
        title = stringResource(R.string.log_title),
        allowDismiss = state.executionSheetDismissible,
        onDismissRequest = actions::onCloseExecutionSheet,
        startAction = {
            IconButton(onClick = actions::onCopyLogs) {
                Icon(
                    imageVector = MiuixIcons.Copy,
                    contentDescription = stringResource(R.string.action_copy),
                    tint = MiuixTheme.colorScheme.onBackground,
                )
            }
        },
        endAction = {
            IconButton(
                enabled = state.executionSheetDismissible,
                onClick = actions::onCloseExecutionSheet,
            ) {
                Icon(
                    imageVector = MiuixIcons.Close,
                    contentDescription = stringResource(R.string.action_close),
                )
            }
        },
        content = {
            LogPanel(
                lines = state.logLines,
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = 240.dp, max = 520.dp)
                    .navigationBarsPadding(),
            )
        },
    )
}

@Composable
private fun LogPanel(
    lines: List<GhostlockLogLine>,
    modifier: Modifier = Modifier,
) {
    val listState = rememberLazyListState()
    LaunchedEffect(lines.size) {
        if (lines.isNotEmpty()) listState.scrollToItem(lines.lastIndex)
    }
    val displayLines = if (lines.isEmpty()) {
        listOf(GhostlockLogLine("Ready for execution", 1))
    } else {
        lines
    }
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0xFF0B1220))
            .padding(12.dp),
    ) {
        SelectionContainer {
            LazyColumn(
                state = listState,
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                items(displayLines) { line ->
                    Text(
                        text = line.text.trimEnd('\r', '\n'),
                        color = when (line.color) {
                            1 -> Color(0xFFB7E4C7)  // Success/Default (green)
                            2 -> Color(0xFFFFC857)  // Info (yellow)
                            3 -> Color(0xFFFF6B6B)  // Error (red)
                            else -> Color(0xFFEAF2FF)  // Light blue
                        },
                        fontFamily = FontFamily.Monospace,
                        fontSize = 12.sp,
                        lineHeight = 16.sp,
                    )
                }
            }
        }
    }
}
