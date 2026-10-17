package com.ghostlock.app

import android.os.Environment
import android.util.Log
import java.io.File

/** v12.5: rescue the native O_SYNC tee (filesDir/native-osync.log) into the
 * debug export area. Runs on every app start from [GhostlockApplication] —
 * the file is ext4 (survives panics/reboots) but app-private; adb cannot
 * read it and a reinstall deletes it. The v11.8e MediaStore-based hook in
 * the repository never produced output; this uses the same direct file
 * write the profile.conf sidecar already relies on. */
object OsyncRescue {
    private const val TAG = "OsyncRescue"

    fun run() {
        try {
            val filesDir = File("/data/data/com.ghostlock.app/files")
            val osync = File(filesDir, "native-osync.log")
            if (!osync.isFile || osync.length() == 0L) {
                Log.i(TAG, "no tee file to rescue")
                return
            }
            val base = File(
                Environment.getExternalStorageDirectory(),
                "Download/ghostlock-debug-log",
            )
            if (!base.isDirectory && !base.mkdirs()) {
                Log.w(TAG, "export base missing: ${base.absolutePath}")
                return
            }
            val out = File(base, "osync-rescue-${System.currentTimeMillis()}.log")
            osync.copyTo(out, overwrite = true)
            Log.i(TAG, "rescued ${osync.length()} bytes -> ${out.absolutePath}")
            osync.delete()
            Log.i(TAG, "private tee removed")
        } catch (t: Throwable) {
            Log.w(TAG, "rescue failed", t)
        }
    }
}
