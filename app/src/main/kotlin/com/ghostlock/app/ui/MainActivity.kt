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
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.domain.model.ExecutionMode

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
        /* octet-stream, not text/plain: SAF appends ".txt" to a text/plain
         * document whose name is not a recognised text extension, which turned
         * "…conf" into "…conf.txt". */
        registerForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri: Uri? ->
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
                    DocumentRequest.ImportOffsetsHocon,
                    DocumentRequest.ImportOffsetsJson,
                    DocumentRequest.PayloadKo,
                    -> documentsPicker.launch(mimeTypes)

                    else -> documentPicker.launch(mimeTypes)
                }
            }

            GhostlockEffect.PickDebugFolder -> folderPicker.launch(null)

            is GhostlockEffect.CreateProfileDocument ->
                profileDocumentCreator.launch(effect.suggestedName)

            is GhostlockEffect.Share -> shareOffsets(effect.uri.toUri())
            is GhostlockEffect.Toast -> Toast.makeText(this, effect.resourceId, Toast.LENGTH_SHORT).show()
            is GhostlockEffect.ToastArgs ->
                Toast.makeText(
                    this,
                    getString(effect.resourceId, effect.arg),
                    Toast.LENGTH_LONG,
                ).show()
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

            is GhostlockEffect.ShowRootManager -> showRootManager(effect)
        }
    }

    /**
     * The root-manager step after a run: open its UI, or say it cannot be
     * opened (never nothing). The decision itself is the pure
     * [rootManagerAction]; only the lookup and the launch happen here.
     */
    private fun showRootManager(effect: GhostlockEffect.ShowRootManager) {
        val intent = effect.packageName?.let { packageManager.getLaunchIntentForPackage(it) }
        val action = rootManagerAction(
            succeeded = effect.succeeded,
            rootProduced = effect.rootProduced,
            packageName = effect.packageName,
            launchable = intent != null,
        )
        when (action) {
            is RootManagerAction.Launch -> if (intent != null) {
                /* NEW_TASK: the activity is started from outside a task of its own. */
                startActivity(intent.apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) })
            }

            RootManagerAction.Hint ->
                Toast.makeText(this, R.string.root_manager_unavailable, Toast.LENGTH_LONG).show()

            RootManagerAction.Skip -> Unit
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
            override fun onForceAttackTestChanged(enabled: Boolean) =
                viewModel.toggleForceAttackTest(enabled)

            override fun onExecutionModeChanged(mode: ExecutionMode) =
                viewModel.setExecutionMode(mode)

            override fun onBackendChanged(kind: BackendKind) =
                viewModel.setBackendKind(kind)

            override fun onCombinationChanged(spec: CombinationSpec) =
                viewModel.setCombination(spec)
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
            override fun onImportPlugin() = viewModel.onImportPlugin()
            override fun onPluginParamEdit(id: String, name: String, current: String) =
                viewModel.onPluginParamEdit(id, name, current)

            override fun onPluginBoolChanged(id: String, name: String, value: Boolean) =
                viewModel.onPluginBoolChanged(id, name, value)

            override fun onPayloadPickScript() = viewModel.onPayloadPickScript()

            override fun onPayloadPickKo() = viewModel.onPayloadPickKo()

            override fun onPayloadKoMove(index: Int, delta: Int) =
                viewModel.onPayloadKoMove(index, delta)

            override fun onPayloadKoRemove(index: Int) = viewModel.onPayloadKoRemove(index)

            override fun onOpenPayload() = viewModel.onOpenPayload()

            override fun onClosePayload() = viewModel.onClosePayload()

            override fun onPayloadTierChanged(tier: PayloadTier?) =
                viewModel.onPayloadTierChanged(tier)

            override fun onPayloadCommandChanged(command: String) =
                viewModel.onPayloadCommandChanged(command)

            override fun onPayloadManagerChanged(manager: RootManager?) =
                viewModel.onPayloadManagerChanged(manager)

            override fun onOpenPluginDetail(id: String) = viewModel.onOpenPluginDetail(id)

            override fun onClosePluginDetail() = viewModel.onClosePluginDetail()

            override fun onRecheckPlugin(id: String) = viewModel.onRecheckPlugin(id)

            override fun onClearPluginOverrides(id: String) = viewModel.onClearPluginOverrides(id)

            override fun onPluginRunSelected(id: String, selected: Boolean) =
                viewModel.onPluginRunSelected(id, selected)

            override fun onPluginRunSelectAll() = viewModel.onPluginRunSelectAll()

            override fun onPluginRunSelectNone() = viewModel.onPluginRunSelectNone()
            override fun onOpenPlugins() = viewModel.onOpenPlugins()
            override fun onClosePlugins() = viewModel.onClosePlugins()
            override fun onPluginEnabledChanged(id: String, enabled: Boolean) =
                viewModel.onPluginEnabledChanged(id, enabled)
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
