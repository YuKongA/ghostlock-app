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
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.ghostlock.app.R
import com.ghostlock.app.data.BootAutoRootPreferences
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.DropdownItem
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.basic.rememberTopAppBarState
import top.yukonga.miuix.kmp.preference.OverlaySpinnerPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.darkColorScheme
import top.yukonga.miuix.kmp.theme.lightColorScheme
import top.yukonga.miuix.kmp.utils.overScrollVertical

class BootSettingsActivity : ComponentActivity() {
    private lateinit var bootPrefs: BootAutoRootPreferences

    private val notificationPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted) {
            bootPrefs.autoRunAtBoot = true
        } else {
            bootPrefs.autoRunAtBoot = false
            Toast.makeText(this, R.string.boot_auto_root_notification_denied, Toast.LENGTH_LONG).show()
        }
        recreate()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setupSystemBars()
        bootPrefs = BootAutoRootPreferences(this)
        bootPrefs.syncRuntimeFromApp()
        setContent {
            BootSettingsScreen(
                initialAutoRun = bootPrefs.autoRunAtBoot,
                initialRunBeforeUnlock = bootPrefs.runBeforeUnlock,
                initialMaxAttempts = bootPrefs.maxAttempts,
                onAutoRunChanged = ::onAutoRunChanged,
                onRunBeforeUnlockChanged = { enabled ->
                    bootPrefs.runBeforeUnlock = enabled
                    bootPrefs.syncRuntimeFromApp()
                },
                onMaxAttemptsChanged = {
                    bootPrefs.maxAttempts = it
                    bootPrefs.syncRuntimeFromApp()
                },
                onBack = ::finish,
            )
        }
    }

    private fun onAutoRunChanged(enabled: Boolean) {
        if (!enabled) {
            bootPrefs.autoRunAtBoot = false
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val granted = ContextCompat.checkSelfPermission(
                this,
                Manifest.permission.POST_NOTIFICATIONS,
            ) == PackageManager.PERMISSION_GRANTED
            if (!granted) {
                notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
                return
            }
        }
        bootPrefs.autoRunAtBoot = true
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
private fun BootSettingsScreen(
    initialAutoRun: Boolean,
    initialRunBeforeUnlock: Boolean,
    initialMaxAttempts: Int,
    onAutoRunChanged: (Boolean) -> Unit,
    onRunBeforeUnlockChanged: (Boolean) -> Unit,
    onMaxAttemptsChanged: (Int) -> Unit,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    val prefs = BootAutoRootPreferences(context)
    var autoRun by rememberSaveable { mutableStateOf(initialAutoRun) }
    var runBeforeUnlock by rememberSaveable { mutableStateOf(initialRunBeforeUnlock) }
    var maxAttempts by rememberSaveable {
        mutableIntStateOf(
            initialMaxAttempts.coerceIn(BootAutoRootPreferences.MIN_ATTEMPTS, BootAutoRootPreferences.MAX_ATTEMPTS),
        )
    }
    val attemptLabels = (BootAutoRootPreferences.MIN_ATTEMPTS..BootAutoRootPreferences.MAX_ATTEMPTS)
        .map { it.toString() }
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())

    MiuixTheme(
        colors = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme(),
    ) {
        Scaffold(
            modifier = Modifier.fillMaxSize(),
            topBar = {
                TopAppBar(
                    title = stringResource(R.string.boot_settings_title),
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
                item(key = "auto_run") {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        SwitchPreference(
                            checked = autoRun,
                            onCheckedChange = { enabled ->
                                onAutoRunChanged(enabled)
                                autoRun = prefs.autoRunAtBoot
                            },
                            title = stringResource(R.string.boot_auto_run_label),
                            summary = stringResource(R.string.boot_auto_run_summary),
                        )
                    }
                }
                item(key = "before_unlock") {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        SwitchPreference(
                            checked = runBeforeUnlock,
                            onCheckedChange = { enabled ->
                                runBeforeUnlock = enabled
                                onRunBeforeUnlockChanged(enabled)
                            },
                            title = stringResource(R.string.boot_run_before_unlock_label),
                            summary = stringResource(R.string.boot_run_before_unlock_summary),
                        )
                    }
                }
                item(key = "before_unlock_note") {
                    AnimatedVisibility(
                        visible = runBeforeUnlock,
                        enter = fadeIn() + expandVertically(),
                        exit = fadeOut() + shrinkVertically(),
                    ) {
                        BootBeforeUnlockNoteCard(modifier = Modifier.fillMaxWidth())
                    }
                }
                item(key = "max_attempts") {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        OverlaySpinnerPreference(
                            title = stringResource(R.string.boot_max_attempts_label),
                            summary = stringResource(R.string.boot_max_attempts_summary),
                            items = attemptLabels.map { DropdownItem(icon = null, title = it) },
                            selectedIndex = maxAttempts - BootAutoRootPreferences.MIN_ATTEMPTS,
                            showValue = true,
                            onSelectedIndexChange = { index ->
                                val value = index + BootAutoRootPreferences.MIN_ATTEMPTS
                                maxAttempts = value
                                onMaxAttemptsChanged(value)
                            },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun BootBeforeUnlockNoteCard(modifier: Modifier = Modifier) {
    Card(
        modifier = modifier,
        insideMargin = PaddingValues(16.dp),
    ) {
        Text(
            text = stringResource(R.string.boot_before_unlock_note_title),
            fontSize = 18.sp,
            fontWeight = FontWeight.Medium,
            color = MiuixTheme.colorScheme.onSurface,
        )
        Text(
            text = stringResource(R.string.boot_before_unlock_note_body),
            modifier = Modifier.padding(top = 8.dp),
            fontSize = 14.sp,
            color = MiuixTheme.colorScheme.onSurface.copy(alpha = 0.68f),
        )
    }
}
