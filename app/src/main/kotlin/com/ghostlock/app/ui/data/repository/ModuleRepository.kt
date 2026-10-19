package com.ghostlock.app.ui.data.repository

import com.ghostlock.app.ui.data.model.Module
import com.ghostlock.app.ui.data.model.ModuleUpdateInfo

interface ModuleRepository {
    suspend fun getModules(): Result<List<Module>>
    suspend fun checkUpdate(module: Module): Result<ModuleUpdateInfo>
}
