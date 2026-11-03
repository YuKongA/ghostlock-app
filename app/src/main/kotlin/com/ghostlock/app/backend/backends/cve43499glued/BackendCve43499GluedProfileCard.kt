package com.ghostlock.app.backend.backends.cve43499glued

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
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
import com.ghostlock.app.ui.component.material.SegmentedDropdownItem
import com.ghostlock.app.ui.component.material.SegmentedListItem
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.OverlayDropdownPreference

/** 43499 card controls the built-in upstream profile or imported `.conf` used by the exec flow. */
@Composable
internal fun Backend43499GluedProfileCard(miuix: Boolean) {
    val context = LocalContext.current
    val backend = BackendCve43499Glued
    val store = remember(context) {
        BackendSessionStore.from(context).configStore(context, backend)
    }
    val builtins = remember(context) {
        runCatching { Glued43499ProfileCodec.builtins(context) }.getOrDefault(emptyList())
    }
    var invalidProfile by remember { mutableStateOf(false) }
    val profileReference = backend.profileReference
    val importedName = remember(context, profileReference) {
        profileReference.takeIf { it.startsWith("uri:") }
            ?.removePrefix("uri:")
            ?.let { raw -> runCatching { displayName(context, Uri.parse(raw)) }.getOrNull() }
            .orEmpty()
    }
    val importedOption = importedName.takeIf { it.isNotBlank() }
        ?.let { stringResource(R.string.backend_43499_imported_profile, it) }
    val options = buildList {
        add(stringResource(R.string.backend_43499_profile_none))
        builtins.forEach { add(it.release) }
        importedOption?.let(::add)
    }
    val selectedOption = when {
        profileReference.startsWith("builtin:") ->
            builtins.indexOfFirst { "builtin:${it.release}" == profileReference }
                .takeIf { it >= 0 }?.plus(1) ?: 0

        importedOption != null -> options.lastIndex
        else -> 0
    }
    var importError by remember { mutableStateOf(false) }

    LaunchedEffect(store) {
        backend.restoreConfig(store)
        if (backend.profileReference.isNotBlank()) {
            invalidProfile = runCatching { backend.resolveProfile(context) }.isFailure
        }
    }

    fun chooseBuiltin(index: Int) {
        if (index == 0) {
            backend.clearProfile(store)
            invalidProfile = false
            importError = false
            return
        }
        val asset = builtins.getOrNull(index - 1) ?: return
        val reference = "builtin:${asset.release}"
        runCatching { Glued43499ProfileCodec.resolve(context, reference) }
            .onSuccess {
                backend.selectProfile(reference, store)
                invalidProfile = false
                importError = false
            }
            .onFailure {
                invalidProfile = true
                importError = false
            }
    }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            runCatching {
                context.contentResolver.takePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION,
                )
                Glued43499ProfileCodec.validateImported(context, uri)
                backend.selectProfile("uri:$uri", store)
                invalidProfile = false
                importError = false
            }.onFailure {
                invalidProfile = false
                importError = true
            }
        }
    }

    val selectorTitle = stringResource(R.string.backend_43499_profile_label)
    val selectorSummary = when {
        profileReference.startsWith("builtin:") -> profileReference.removePrefix("builtin:")
        importedOption != null -> importedOption
        else -> stringResource(R.string.backend_43499_profile_required)
    }
    if (miuix) {
        Column {
            OverlayDropdownPreference(
                title = selectorTitle,
                summary = selectorSummary,
                items = options,
                selectedIndex = selectedOption,
                onSelectedIndexChange = ::chooseBuiltin,
            )
            ArrowPreference(
                title = stringResource(R.string.backend_43499_import_profile),
                summary = stringResource(R.string.backend_43499_import_profile_summary),
                onClick = { picker.launch(arrayOf("text/plain", "application/octet-stream", "*/*")) },
            )
        }
    } else {
        SegmentedColumn(modifier = Modifier.fillMaxWidth()) {
            item {
                SegmentedDropdownItem(
                    title = selectorTitle,
                    summary = selectorSummary,
                    items = options,
                    selectedIndex = selectedOption,
                    onItemSelected = ::chooseBuiltin,
                )
            }
            item {
                SegmentedListItem(
                    headlineContent = { Text(stringResource(R.string.backend_43499_import_profile)) },
                    supportingContent = { Text(stringResource(R.string.backend_43499_import_profile_summary)) },
                    onClick = { picker.launch(arrayOf("text/plain", "application/octet-stream", "*/*")) },
                )
            }
        }
    }
    if (invalidProfile || importError) {
        Text(
            text = stringResource(R.string.backend_43499_profile_invalid),
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            color = MaterialTheme.colorScheme.error,
        )
    }
}

/** Reads the provider display name for the imported profile summary. */
private fun displayName(context: Context, uri: Uri): String =
    context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
        val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
        if (nameIndex >= 0 && cursor.moveToFirst()) cursor.getString(nameIndex) else null
    } ?: uri.lastPathSegment ?: "profile.conf"
