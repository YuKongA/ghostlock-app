package com.ghostlock.app.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.core.net.toUri
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.ghostlock.app.GhostlockApplication
import com.ghostlock.app.R

class MainActivity : ComponentActivity() {
    private val viewModel by viewModels<GhostlockViewModel> {
        viewModelFactory {
            initializer { GhostlockViewModel((application as GhostlockApplication).createRepository()) }
        }
    }

    private var pendingDocumentRequest: DocumentRequest? = null
    private val documentPicker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        val request = pendingDocumentRequest
        pendingDocumentRequest = null
        if (uri != null && request != null) viewModel.onDocumentResult(request, uri.toString())
    }
    private val documentsPicker =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris: List<Uri> ->
            val request = pendingDocumentRequest
            pendingDocumentRequest = null
            if (uris.isNotEmpty() && request != null) {
                viewModel.onDocumentsResult(request, uris.map(Uri::toString))
            }
        }

    private val folderPicker = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
        viewModel.onDebugExportLocationPicked(uri?.let(::documentTreeRelativePath))
    }

    private val profileDocumentCreator =
        registerForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri: Uri? ->
            viewModel.onExportProfileDocumentPicked(uri?.toString())
        }

    /** Maps a SAF tree URI to the external-storage-relative MediaStore path. */
    private fun documentTreeRelativePath(uri: Uri): String? {
        val documentId = runCatching { DocumentsContract.getTreeDocumentId(uri) }.getOrNull()
            ?: return null
        if (!documentId.startsWith("primary:")) return null
        return documentId.substringAfter(':').trim('/').ifEmpty { null }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        viewModel.initialize()
        setContent {
            GhostlockRoute(viewModel, ::handleEffect)
        }
        setupSystemBars()
    }

    override fun onResume() {
        super.onResume()
        viewModel.refreshAccessStatus()
    }

    private fun handleEffect(effect: GhostlockEffect) {
        when (effect) {
            is GhostlockEffect.PickDocument -> {
                pendingDocumentRequest = effect.request
                val mimeTypes = when (effect.request) {
                    DocumentRequest.ImportOffsetsHocon ->
                        arrayOf("text/plain", "application/octet-stream")
                    DocumentRequest.ImportOffsetsJson ->
                        arrayOf("application/json", "text/plain", "application/octet-stream")
                    else -> arrayOf("*/*")
                }
                when (effect.request) {
                    DocumentRequest.ImportOffsetsHocon, DocumentRequest.ImportOffsetsJson ->
                        documentsPicker.launch(mimeTypes)
                    else -> documentPicker.launch(mimeTypes)
                }
            }

            GhostlockEffect.PickDebugFolder -> folderPicker.launch(null)

            is GhostlockEffect.CreateProfileDocument ->
                profileDocumentCreator.launch(effect.suggestedName)

            is GhostlockEffect.Share -> shareOffsets(effect.uri.toUri())
            is GhostlockEffect.Toast -> Toast.makeText(this, effect.resourceId, Toast.LENGTH_SHORT).show()
            is GhostlockEffect.Clipboard -> {
                getSystemService(ClipboardManager::class.java)
                    ?.setPrimaryClip(ClipData.newPlainText("ghostlock-log", effect.text))
            }

            is GhostlockEffect.KeepScreenAwake -> if (effect.enabled) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            } else {
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }

            GhostlockEffect.OpenShizuku -> packageManager
                .getLaunchIntentForPackage(SHIZUKU_PACKAGE)
                ?.let(::startActivity)
        }
    }

    private fun shareOffsets(uri: Uri) {
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        startActivity(Intent.createChooser(intent, getString(R.string.export_share)))
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

    private companion object {
        const val SHIZUKU_PACKAGE = "moe.shizuku.privileged.api"
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
            override fun onProfileInvalid() = viewModel.onProfileInvalid()
            override fun onStatusClick() = viewModel.onStatusClick()
            override fun onCloseExecutionSheet() = viewModel.onCloseExecutionSheet()
            override fun onCopyLogs() = viewModel.copyLogs()
            override fun onImportOffsetsHocon() = viewModel.importOffsetsHocon()
            override fun onImportOffsetsJson() = viewModel.importOffsetsJson()
            override fun onDocumentsResult(request: DocumentRequest, uris: List<String>) =
                viewModel.onDocumentsResult(request, uris)
            override fun onParseOta() = viewModel.promptParseUrl()
            override fun onParseImage() = viewModel.parseOffsets()
            override fun onCpuPairSelected(index: Int) = viewModel.selectCpuPair(index)
            override fun onSafeModeChanged(enabled: Boolean) = viewModel.toggleSafeMode(enabled)
            override fun onShizukuChanged(enabled: Boolean) = viewModel.toggleShizuku(enabled)
            override fun onDialogItemSelected(index: Int) = viewModel.onDialogItemSelected(index)
            override fun onDialogInputChange(value: String) = viewModel.onDialogInputChange(value)
            override fun onDialogConfirm(value: String) = viewModel.onDialogConfirm(value)
            override fun onDialogDismiss() = viewModel.onDialogDismiss()
            override fun onDialogDismissFinished() = viewModel.onDialogDismissFinished()
            override fun onOverwriteConfirm() = viewModel.onOverwriteConfirm()
            override fun onOverwriteDismiss() = viewModel.onOverwriteDismiss()
            override fun onExecutionFieldChanged(path: String, value: String) =
                viewModel.updateExecutionField(path, value)
            override fun onRouteChanged(index: Int) = viewModel.onRouteChanged(index)
            override fun onFallbackChanged(index: Int) = viewModel.onFallbackChanged(index)
            override fun onExportProfile() = viewModel.onExportProfile()
            override fun onSaveProfileEdits() = viewModel.onSaveProfileEdits()
            override fun onSaveProfileAs() = viewModel.onSaveProfileAs()
            override fun onExportProfileEdits() = viewModel.onExportProfileEdits()
            override fun onRevertProfileEdits() = viewModel.onRevertProfileEdits()
            override fun onOpenAdvanced() = viewModel.onOpenAdvanced()
            override fun onCloseAdvanced() = viewModel.onCloseAdvanced()
            override fun onShowAbout() = viewModel.onShowAbout()
            override fun onCloseAbout() = viewModel.onCloseAbout()
            override fun onDebugExportChanged(enabled: Boolean) = viewModel.onDebugExportChanged(enabled)
            override fun onDebugExportLocationPick() = viewModel.onDebugExportLocationPick()
            override fun onDebugKernelLogChanged(enabled: Boolean) =
                viewModel.onDebugKernelLogChanged(enabled)
            override fun onOpenParameters() = viewModel.onOpenParameters()
            override fun onCloseParameters() = viewModel.onCloseParameters()
            override fun onOpenLoadConfig() = viewModel.onOpenLoadConfig()
            override fun onCloseLoadConfig() = viewModel.onCloseLoadConfig()
            override fun onOpenUserProfileDetail(name: String) =
                viewModel.onOpenUserProfileDetail(name)
            override fun onCloseUserProfileDetail() = viewModel.onCloseUserProfileDetail()
            override fun onLoadUserProfile(name: String) = viewModel.onLoadUserProfile(name)
            override fun onUnloadUserProfile() = viewModel.onUnloadUserProfile()
            override fun onEditUserProfile(name: String) = viewModel.onEditUserProfile(name)
            override fun onUserProfileRename(name: String) = viewModel.onUserProfileRename(name)
            override fun onUserProfileExport(name: String) = viewModel.onUserProfileExport(name)
            override fun onConvertUserProfile(name: String) = viewModel.onConvertUserProfile(name)
            override fun onUserProfileDelete(name: String) = viewModel.onUserProfileDelete(name)
            override fun onUserProfileDeleteConfirm() = viewModel.onUserProfileDeleteConfirm()
            override fun onUserProfileDeleteDismiss() = viewModel.onUserProfileDeleteDismiss()
            override fun onOpenBuiltinProfiles() = viewModel.onOpenBuiltinProfiles()
            override fun onCloseBuiltinProfiles() = viewModel.onCloseBuiltinProfiles()
            override fun onSelectBuiltinProfile(release: String?) =
                viewModel.onSelectBuiltinProfile(release)
            override fun onOpenProfileOverrides() = viewModel.onOpenProfileOverrides()
            override fun onCloseProfileOverrides() = viewModel.onCloseProfileOverrides()
            override fun onOpenAdvancedOverrides() = viewModel.onOpenAdvancedOverrides()
            override fun onCloseAdvancedOverrides() = viewModel.onCloseAdvancedOverrides()
            override fun onProfileOverrideChanged(path: String, value: String) =
                viewModel.onProfileOverrideChanged(path, value)
        },
    )
}
