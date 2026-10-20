package com.ghostlock.app.engine.pagecache.stage

data class EnvironmentInput(
    val kmi: String,
    val carrier: String,
    val spi: Int,
    val aesKey: ByteArray,
    val hmacKey: ByteArray,
)

/** 只校验/选择，不写页缓存。 */
class EnvironmentStage(
    private val input: EnvironmentInput,
    private val log: StageLog = silentStageLog,
) : Stage {
    override suspend fun run(): StageResult {
        log.verbose("EnvironmentStage.run", "kmi" to input.kmi, "carrier" to input.carrier, "spi" to input.spi)
        val kmiOk = Regex("android\\d+-\\d+\\.\\d+").matches(input.kmi)
        if (!kmiOk) {
            log.debug("EnvironmentStage.run", "kmi-invalid", "kmi" to input.kmi)
            return StageResult(StageCode.ENV_MISSING, "env-invalid")
        }
        if (input.carrier.isBlank()) {
            log.debug("EnvironmentStage.run", "carrier-blank")
            return StageResult(StageCode.ENV_MISSING, "env-invalid")
        }
        if (input.spi == 0) {
            log.debug("EnvironmentStage.run", "spi-zero")
            return StageResult(StageCode.ENV_MISSING, "env-invalid")
        }
        if (input.aesKey.size != 32) {
            log.debug("EnvironmentStage.run", "aes-size", "size" to input.aesKey.size)
            return StageResult(StageCode.ENV_MISSING, "env-invalid")
        }
        if (input.hmacKey.size != 32) {
            log.debug("EnvironmentStage.run", "hmac-size", "size" to input.hmacKey.size)
            return StageResult(StageCode.ENV_MISSING, "env-invalid")
        }
        log.debug("EnvironmentStage.run", "ok", "carrier" to input.carrier)
        return StageResult(StageCode.OK, evidence = Evidence(source = "env"))
    }

    override suspend fun abort(code: StageCode) {
        log.debug("EnvironmentStage.abort", "enter", "code" to code)
    }

    override suspend fun close() {
        log.debug("EnvironmentStage.close", "done")
    }
}
