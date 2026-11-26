package org.lsposed.lsprivilege

import android.app.Application
import org.lsposed.lsprivilege.data.AndroidLSPrivilegeRepository
import org.lsposed.lsprivilege.domain.repository.LSPrivilegeRepository

/** Application composition root. It is the only place that binds data implementations to domain ports. */
class LSPrivilegeApplication : Application() {
    fun createRepository(): LSPrivilegeRepository = AndroidLSPrivilegeRepository(this)
}
