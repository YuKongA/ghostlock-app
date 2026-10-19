package com.ghostlock.app.ui.data.repository

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.ghostlock.app.ui.data.model.TemplateInfo
import com.ghostlock.app.ui.ksuApp
import com.ghostlock.app.ui.util.getAppProfileTemplate
import com.ghostlock.app.ui.util.listAppProfileTemplates
import com.ghostlock.app.ui.util.setAppProfileTemplate
import org.json.JSONArray
import org.json.JSONObject

class TemplateRepositoryImpl : TemplateRepository {

    companion object {
        private const val TAG = "TemplateRepository"
    }

    override suspend fun getTemplates(sync: Boolean): Result<List<TemplateInfo>> = withContext(Dispatchers.IO) {
        runCatching {
            val localTemplateIds = listAppProfileTemplates()
            Log.i(TAG, "localTemplateIds: $localTemplateIds")
            if (localTemplateIds.isEmpty() || sync) {
                fetchRemoteTemplates()
            }
            listAppProfileTemplates().mapNotNull { getTemplateInfoById(it) }
        }
    }

    override suspend fun importTemplates(jsonString: String): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val array = try {
                JSONArray(jsonString)
            } catch (e: Exception) {
                try {
                    val json = JSONObject(jsonString)
                    JSONArray().apply { put(json) }
                } catch (e: Exception) {
                    throw Exception("invalid templates: $jsonString")
                }
            }

            (0 until array.length()).forEach { i ->
                runCatching {
                    val template = array.getJSONObject(i)
                    val id = template.getString("id")
                    template.put("local", true)
                    setAppProfileTemplate(id, template.toString())
                }.onFailure { e ->
                    Log.e(TAG, "ignore invalid template: $array", e)
                }
            }
        }
    }

    override suspend fun exportTemplates(): Result<String> = withContext(Dispatchers.IO) {
        runCatching {
            val templates = listAppProfileTemplates()
                .mapNotNull { getTemplateInfoById(it) }
                .filter { it.local }
            if (templates.isEmpty()) {
                throw Exception("No templates to export")
            }
            JSONArray(templates.map { it.toJSON() }).toString()
        }
    }

    override suspend fun getTemplate(id: String): Result<TemplateInfo> = withContext(Dispatchers.IO) {
        runCatching {
            getTemplateInfoById(id) ?: throw Exception("Template not found: $id")
        }
    }

    /**
     * 远端模板同步（未接线：不访问网络）。
     *
     * 上游会拉 kernelsu.org 的模板索引；下载/联网层随裁剪移除后这里保留空实现，
     * 模板列表完全来自本地固定样例（listAppProfileTemplates）。
     */
    private fun fetchRemoteTemplates() {
        Log.i(TAG, "fetchRemoteTemplates: skipped, port build has no network layer")
    }


    private fun getTemplateInfoById(id: String): TemplateInfo? {
        return runCatching {
            TemplateInfo.fromJSON(JSONObject(getAppProfileTemplate(id)))
        }.onFailure {
            Log.e(TAG, "ignore invalid template: $it", it)
        }.getOrNull()
    }
}
