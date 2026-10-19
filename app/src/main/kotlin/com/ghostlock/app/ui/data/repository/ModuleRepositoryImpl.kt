package com.ghostlock.app.ui.data.repository

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.ghostlock.app.ui.data.model.Module
import com.ghostlock.app.ui.data.model.ModuleUpdateInfo
import com.ghostlock.app.ui.util.listModules
import org.json.JSONArray

class ModuleRepositoryImpl : ModuleRepository {

    companion object {
        private const val TAG = "ModuleRepository"
    }

    override suspend fun getModules(): Result<List<Module>> = withContext(Dispatchers.IO) {
        runCatching {
            val result = listModules()
            val array = JSONArray(result)
            (0 until array.length())
                .asSequence()
                .map { array.getJSONObject(it) }
                .map { obj ->
                    Module(
                        id = obj.getString("id"),
                        name = obj.optString("name"),
                        author = obj.optString("author", "Unknown"),
                        version = obj.optString("version", "Unknown"),
                        versionCode = obj.optInt("versionCode", 0),
                        description = obj.optString("description"),
                        enabled = obj.getBoolean("enabled"),
                        update = obj.optBoolean("update"),
                        remove = obj.getBoolean("remove"),
                        updateJson = obj.optString("updateJson"),
                        hasWebUi = obj.optBoolean("web"),
                        hasActionScript = obj.optBoolean("action"),
                        metamodule = (obj.optInt("metamodule") != 0) || obj.optBoolean("metamodule"),
                        actionIconPath = obj.optString("actionIcon").takeIf { it.isNotBlank() },
                        webUiIconPath = obj.optString("webuiIcon").takeIf { it.isNotBlank() },
                        preinit = obj.optString("preinit")
                    )
                }.toList()
        }
    }

    /**
     * 检查模块更新（未接线：不访问网络）。
     *
     * 上游会拉模块自带 updateJson；联网层随裁剪移除后恒返回 [ModuleUpdateInfo.Empty]，
     * 即模块列表里不会出现"可更新"标记。
     *
     * @param module 目标模块，本实现不使用。
     * @return 恒为成功且内容为空。
     */
    override suspend fun checkUpdate(module: Module): Result<ModuleUpdateInfo> = withContext(Dispatchers.IO) {
        Result.success(ModuleUpdateInfo.Empty)
    }
}
