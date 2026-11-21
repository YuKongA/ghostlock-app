package com.ghostlock.app.data.run

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import com.ghostlock.app.R
import com.ghostlock.app.core.log.LogHub
import com.ghostlock.app.domain.run.LogDocumentFormatter
import com.ghostlock.app.domain.run.LogFormatConfig
import com.ghostlock.app.domain.run.RunPresentationPort
import com.ghostlock.app.domain.run.initialLogRecords
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Android presentation-I/O adapter for run sharing, abort text, and private font resolution.
 *
 * @param context Application context used for resources, private files, FileProvider, and chooser launch.
 * @param logHub Sole source of the current flushed private-log prefix and terminal state.
 * @param formatter Canonical GLLOG document formatter used when no accepted run exists yet.
 * @param uriProvider Narrow Android URI boundary; production uses FileProvider.
 * @param intentLauncher Narrow Android launch boundary; production starts the chooser activity.
 */
class AndroidRunPresentationAdapter internal constructor(
    context: Context,
    private val logHub: LogHub,
    private val formatter: LogDocumentFormatter,
    private val uriProvider: (Context, File) -> Uri,
    private val intentLauncher: (Context, Intent) -> Unit,
) : RunPresentationPort {
    private val applicationContext = context.applicationContext

    /** Creates the production FileProvider and Android chooser boundary. */
    constructor(
        context: Context,
        logHub: LogHub,
        formatter: LogDocumentFormatter,
    ) : this(
        context = context,
        logHub = logHub,
        formatter = formatter,
        uriProvider = { owner, file ->
            FileProvider.getUriForFile(
                owner,
                owner.packageName + ".ghostlock.fileprovider",
                file,
            )
        },
        intentLauncher = { owner, intent -> owner.startActivity(intent) },
    )

    /** Returns the existing localized non-abortable warning. */
    override fun abortDisabledMessage(): String =
        applicationContext.getString(R.string.run_abort_disabled)

    /** Resolves only a relative file beneath the canonical application files directory. */
    override fun resolveTerminalFont(reference: String?): String? {
        val value = reference ?: return null
        if (File(value).isAbsolute) return null
        return runCatching {
            val root = applicationContext.filesDir.canonicalFile
            val candidate = File(root, value).canonicalFile
            val prefix = root.path + File.separator
            candidate.takeIf { file -> file.path.startsWith(prefix) && file.isFile }?.absolutePath
        }.getOrNull()
    }

    /** Shares the exact flushed private-file prefix without materializing the complete log. */
    override suspend fun shareTerminal(chooserTitle: String): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            val directory = File(applicationContext.cacheDir, "logs").apply {
                check(mkdirs() || isDirectory)
            }
            val file = File(directory, "ghostlock-terminal-" + System.currentTimeMillis() + ".log")
            val prefix = logHub.filePrefix()
            if (prefix == null) {
                file.outputStream().buffered().use { output ->
                    formatter.formatHeader(null).forEach(output::write)
                    val state = logHub.state.value
                    val config = LogFormatConfig(showTimestamp = state.showTimestamp)
                    initialLogRecords(state.minimumLogLevel).forEach { record ->
                        output.write(formatter.formatRecord(record, config))
                    }
                    formatter.formatTrailer(state.phase).forEach(output::write)
                }
            } else {
                copyLogPrefix(prefix, file)
            }
            val uri = uriProvider(applicationContext, file)
            val send = Intent(Intent.ACTION_SEND).apply {
                type = "text/x-log"
                putExtra(Intent.EXTRA_STREAM, uri)
                putExtra(Intent.EXTRA_TITLE, file.name)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            val chooser = Intent.createChooser(send, chooserTitle).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            intentLauncher(applicationContext, chooser)
        }.isSuccess
    }
}
