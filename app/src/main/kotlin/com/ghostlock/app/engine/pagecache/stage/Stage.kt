package com.ghostlock.app.engine.pagecache.stage

enum class StageCode {
    OK, ENV_MISSING, HELPER_VERIFY, MODULE_PARTIAL,
    HOOK_PARTIAL, TRIGGER_FAILED, WAIT_TIMEOUT, RESTORE_FAILED, CANCELLED,
}

enum class StageState { New, Running, Aborting, Closed }

data class Evidence(
    val written: Int = 0,
    val verified: Int = 0,
    val site: Long = 0,
    val landing: Long = 0,
    val firstInstOk: Boolean = false,
    val jumpOk: Boolean = false,
    val restored: Boolean = false,
    val source: String = "",
)

data class StageResult(val code: StageCode, val reason: String = "", val evidence: Evidence = Evidence())

interface Stage {
    suspend fun run(): StageResult
    fun abort(code: StageCode)
    fun close()
}
