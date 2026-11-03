package com.ghostlock.app.backend.cve43284glued

import android.content.Context
import com.ghostlock.app.backend.core.ExecutableShizukuHost
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts

/** Shizuku peer adapter for the fixed 43284 Glued executable and input files. */
internal class Glued43284ExecutableHost(context: Context) : ExecutableShizukuHost(
    context = context,
    backendId = ShizukuBackendHosts.CVE_43284_GLUED,
    binaryName = "lib43284Glued.so",
    allowedFiles = setOf("finish.sh", "payload"),
)
