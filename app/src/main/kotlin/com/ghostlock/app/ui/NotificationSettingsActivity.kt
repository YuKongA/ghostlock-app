package com.ghostlock.app.ui

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.ghostlock.app.R
import com.ghostlock.app.data.BootAutoRootPreferences
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.basic.rememberTopAppBarState
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.darkColorScheme
import top.yukonga.miuix.kmp.theme.lightColorScheme
import top.yukonga.miuix.kmp.utils.overScrollVertical

class NotificationSettingsActivity : ComponentActivity() {
    private lateinit var bootPrefs: BootAutoRootPreferences

    private val notificationPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (!granted) {
            bootPrefs.notifyProgressEnabled = false
            bootPrefs.notifyProgressDetailed = false
            bootPrefs.notifyResultSuccess = false
            bootPrefs.notifyResultFailure = false
            bootPrefs.notifyResultSkipped = false
            Toast.makeText(this, R.string.boot_auto_root_notification_denied, Toast.LENGTH_LONG).show()
        }
        recreate()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setupSystemBars()
        bootPrefs = BootAutoRootPreferences(this)
        setContent {
            NotificationSettingsScreen(
                initialProgress = bootPrefs.notifyProgressEnabled,
                initialProgressDetailed = bootPrefs.notifyProgressDetailed,
                initialSuccess = bootPrefs.notifyResultSuccess,
                initialFailure = bootPrefs.notifyResultFailure,
                initialSkipped = bootPrefs.notifyResultSkipped,
                onProgressChanged = ::setNotifyProgress,
                onProgressDetailedChanged = { bootPrefs.notifyProgressDetailed = it },
                onSuccessChanged = { bootPrefs.notifyResultSuccess = it },
                onFailureChanged = { bootPrefs.notifyResultFailure = it },
                onSkippedChanged = { bootPrefs.notifyResultSkipped = it },
                onBack = ::finish,
            )
        }
    }

    private fun setNotifyProgress(enabled: Boolean) {
        if (enabled && !ensureNotificationPermission()) return
        bootPrefs.notifyProgressEnabled = enabled
    }

    private fun ensureNotificationPermission(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return true
        val granted = ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.POST_NOTIFICATIONS,
        ) == PackageManager.PERMISSION_GRANTED
        if (!granted) {
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
            return false
        }
        return true
    }

    private fun setupSystemBars() {
        val controller = window.decorView.windowInsetsController ?: return
        val lightStatus = if (resources.getBoolean(R.bool.window_light_status_bar)) {
            WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
        } else {
            0
        }
        val lightNavigation = if (resources.getBoolean(R.bool.window_light_navigation_bar)) {
            WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
        } else {
            0
        }
        controller.setSystemBarsAppearance(
            lightStatus or lightNavigation,
            WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or
                WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS,
        )
    }
}

@Composable
private fun NotificationSettingsScreen(
    initialProgress: Boolean,
    initialProgressDetailed: Boolean,
    initialSuccess: Boolean,
    initialFailure: Boolean,
    initialSkipped: Boolean,
    onProgressChanged: (Boolean) -> Unit,
    onProgressDetailedChanged: (Boolean) -> Unit,
    onSuccessChanged: (Boolean) -> Unit,
    onFailureChanged: (Boolean) -> Unit,
    onSkippedChanged: (Boolean) -> Unit,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    var progress by rememberSaveable { mutableStateOf(initialProgress) }
    var progressDetailed by rememberSaveable { mutableStateOf(initialProgressDetailed) }
    var success by rememberSaveable { mutableStateOf(initialSuccess) }
    var failure by rememberSaveable { mutableStateOf(initialFailure) }
    var skipped by rememberSaveable { mutableStateOf(initialSkipped) }
    val prefs = BootAutoRootPreferences(context)
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())

    MiuixTheme(
        colors = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme(),
    ) {
        Scaffold(
            modifier = Modifier.fillMaxSize(),
            topBar = {
                TopAppBar(
                    title = stringResource(R.string.notification_settings_title),
                    scrollBehavior = scrollBehavior,
                    navigationIcon = {
                        IconButton(onClick = onBack) {
                            Icon(
                                imageVector = Icons.AutoMirrored.Rounded.ArrowBack,
                                contentDescription = stringResource(R.string.boot_settings_back),
                                tint = MiuixTheme.colorScheme.onBackground,
                            )
                        }
                    },
                )
            },
        ) { paddingValues ->
            LazyColumn(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(paddingValues)
                    .overScrollVertical()
                    .nestedScroll(scrollBehavior.nestedScrollConnection)
                    .imePadding()
                    .navigationBarsPadding(),
                contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                item(key = "progress") {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        SwitchPreference(
                            checked = progress,
                            onCheckedChange = { enabled ->
                                onProgressChanged(enabled)
                                progress = prefs.notifyProgressEnabled
                            },
                            title = stringResource(R.string.notify_progress_label),
                            summary = stringResource(R.string.notify_progress_summary),
                        )
                    }
                }
                item(key = "progress_detailed") {
                    AnimatedVisibility(
                        visible = progress,
                        enter = fadeIn() + expandVertically(),
                        exit = fadeOut() + shrinkVertically(),
                    ) {
                        Card(modifier = Modifier.fillMaxWidth()) {
                            SwitchPreference(
                                checked = progressDetailed,
                                onCheckedChange = {
                                    progressDetailed = it
                                    onProgressDetailedChanged(it)
                                },
                                title = stringResource(R.string.notify_progress_detailed_label),
                                summary = stringResource(R.string.notify_progress_detailed_summary),
                            )
                        }
                    }
                }
                item(key = "success") {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        SwitchPreference(
                            checked = success,
                            onCheckedChange = {
                                success = it
                                onSuccessChanged(it)
                            },
                            title = stringResource(R.string.notify_result_success_label),
                            summary = stringResource(R.string.notify_result_success_summary),
                        )
                    }
                }
                item(key = "failure") {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        SwitchPreference(
                            checked = failure,
                            onCheckedChange = {
                                failure = it
                                onFailureChanged(it)
                            },
                            title = stringResource(R.string.notify_result_failure_label),
                            summary = stringResource(R.string.notify_result_failure_summary),
                        )
                    }
                }
                item(key = "skipped") {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        SwitchPreference(
                            checked = skipped,
                            onCheckedChange = {
                                skipped = it
                                onSkippedChanged(it)
                            },
                            title = stringResource(R.string.notify_result_skipped_label),
                            summary = stringResource(R.string.notify_result_skipped_summary),
                        )
                    }
                }
                item(key = "note") {
                    BootInfoNoteCard(
                        modifier = Modifier.fillMaxWidth(),
                        titleRes = R.string.notify_settings_note_title,
                        bodyRes = R.string.notify_settings_note_body,
                    )
                }
            }
        }
    }
}
