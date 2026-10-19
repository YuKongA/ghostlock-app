package com.ghostlock.app.ui.screen.module

import android.Manifest
import android.os.Build
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.Dp
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import kotlinx.coroutines.launch
import com.ghostlock.app.R
import com.ghostlock.app.ui.data.script.ScriptRepository
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.component.SearchStatus
import com.ghostlock.app.ui.navigation3.LocalNavigator
import com.ghostlock.app.ui.navigation3.Route
import com.ghostlock.app.ui.screen.scriptrun.ScriptRunArg
import com.ghostlock.app.ui.viewmodel.ModuleViewModel

@Composable
fun ModulePager(
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
) {
    /* 2026-10-18 用户裁定：本页改为 backend 选择 + 设定，不再有搜索框与模块卡片。 */
    when (LocalUiMode.current) {
        UiMode.Material -> BackendPageMaterial(bottomInnerPadding)
        UiMode.Miuix -> BackendPageMiuix(bottomInnerPadding)
    }
}
