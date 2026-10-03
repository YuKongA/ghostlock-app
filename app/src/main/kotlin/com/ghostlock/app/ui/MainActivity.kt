package com.ghostlock.app.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.os.Bundle
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.ghostlock.app.GhostlockApplication

class MainActivity : ComponentActivity() {
    private val viewModel by viewModels<GhostlockViewModel> {
        viewModelFactory {
            initializer { GhostlockViewModel((application as GhostlockApplication).createRepository()) }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        viewModel.initialize()
        setContent { GhostlockRoute(viewModel, ::handleEffect) }
        setupSystemBars()
    }

    override fun onResume() {
        super.onResume()
        viewModel.refreshAccessStatus()
    }

    private fun handleEffect(effect: GhostlockEffect) {
        when (effect) {
            is GhostlockEffect.Toast -> Toast.makeText(this, effect.resourceId, Toast.LENGTH_SHORT).show()
            is GhostlockEffect.Clipboard -> {
                getSystemService(ClipboardManager::class.java)
                    ?.setPrimaryClip(ClipData.newPlainText("ghostlock-log", effect.text))
            }
            is GhostlockEffect.KeepScreenAwake -> {
                if (effect.enabled) {
                    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                } else {
                    window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
            }
        }
    }

    private fun setupSystemBars() {
        val controller = window.decorView.windowInsetsController ?: return
        val lightStatus = if (resources.getBoolean(com.ghostlock.app.R.bool.window_light_status_bar)) {
            WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
        } else {
            0
        }
        val lightNavigation = if (resources.getBoolean(com.ghostlock.app.R.bool.window_light_navigation_bar)) {
            WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
        } else {
            0
        }
        controller.setSystemBarsAppearance(
            lightStatus or lightNavigation,
            WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS,
        )
    }
}

@Composable
private fun GhostlockRoute(
    viewModel: GhostlockViewModel,
    onEffect: (GhostlockEffect) -> Unit,
) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    LaunchedEffect(viewModel.effects) {
        viewModel.effects.collect(onEffect)
    }

    GhostlockApp(
        state = state,
        actions = object : GhostlockActions {
            override fun onRun() = viewModel.onRun()
            override fun onCloseExecutionSheet() = viewModel.onCloseExecutionSheet()
            override fun onCopyLogs() = viewModel.copyLogs()
        },
    )
}
