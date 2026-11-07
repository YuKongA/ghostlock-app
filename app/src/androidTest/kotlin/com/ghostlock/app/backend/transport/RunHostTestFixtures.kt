package com.ghostlock.app.backend.transport

import android.content.Context
import android.os.ParcelFileDescriptor
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunInputArtifactParcel
import java.io.Closeable
import java.io.File
import java.security.MessageDigest
import java.util.UUID

/** Owned Diagnose request fixture for typed RunHost instrumentation cases. */
internal class RunHostTestFixture private constructor(
    val request: RunHostRequest,
    private val profile: File,
) : Closeable {
    /** Closes application-owned descriptors and deletes the private source file. */
    override fun close() {
        request.artifacts.forEach { artifact -> runCatching { artifact.fd.close() } }
        runCatching { profile.delete() }
    }

    companion object {
        /** Creates one profile-only diagnostic request in the target app cache. */
        fun create(context: Context): RunHostTestFixture {
            val runId = UUID.randomUUID().toString().replace("-", "")
            val profile = File(context.cacheDir, "$runId-profile-v3.json")
            val bytes = "{\"backend_key\":\"DIAGNOSE\",\"schema_version\":3}\n"
                .toByteArray(Charsets.UTF_8)
            profile.writeBytes(bytes)
            val digest = MessageDigest.getInstance("SHA-256").digest(bytes)
                .joinToString(separator = "") { byte ->
                    (byte.toInt() and 0xff).toString(16).padStart(2, '0')
                }
            val artifact = RunInputArtifactParcel().also { parcel ->
                parcel.logicalId = "profile"
                parcel.kind = RunHostArtifactKind.PROFILE.wireValue
                parcel.byteCount = bytes.size.toLong()
                parcel.sha256 = digest
                parcel.fd = ParcelFileDescriptor.open(profile, ParcelFileDescriptor.MODE_READ_ONLY)
            }
            val request = RunHostRequest().also { parcel ->
                parcel.runId = runId
                parcel.generation = 1L
                parcel.backendKey = "DIAGNOSE"
                parcel.profileArtifactId = "profile"
                parcel.payloadItems = emptyList()
                parcel.finishItems = emptyList()
                parcel.artifacts = listOf(artifact)
            }
            return RunHostTestFixture(request, profile)
        }
    }
}
