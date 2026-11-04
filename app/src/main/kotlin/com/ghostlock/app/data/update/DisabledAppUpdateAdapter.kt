package com.ghostlock.app.data.update

import com.ghostlock.app.domain.update.AppUpdateInfo
import com.ghostlock.app.domain.update.AppUpdatePort

/** Explicit data adapter used while remote release lookup is not connected. */
class DisabledAppUpdateAdapter : AppUpdatePort {
    /** @return Empty update data without network or filesystem access. */
    override suspend fun check(): AppUpdateInfo = AppUpdateInfo()
}
