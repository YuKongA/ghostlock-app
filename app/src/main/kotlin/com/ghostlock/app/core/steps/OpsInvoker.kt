package com.ghostlock.app.core.steps

/**
 * 步骤引擎看到的一次原生事务。
 *
 * 为什么是「op 名 + 具名字段」而不是裸的线上编号：core 这一层不依赖线格式（那是 data 层的
 * 事），只依赖"有一个东西能跑一笔事务并回答一个具名结果"。适配器把 token 翻译成 op/key 编号，
 * 于是步骤代码在两端读到的是同一批名字，而编号仍然只有一处（native 导出的 manifest）。
 */
interface OpsInvoker {
    /**
     * 跑一笔。
     *
     * @param opName 线上 op 名（snapshot / write_word / resolve_target ...）。
     * @param values u64 字段，键是线上 key 名。
     * @param blobs  字节字段，键是线上 key 名。
     */
    fun invoke(
        opName: String,
        values: Map<String, ULong> = emptyMap(),
        blobs: Map<String, ByteArray> = emptyMap(),
    ): OpsReply
}

/** 一笔事务的回答：状态名、具名拒绝（若有）、两类字段。 */
data class OpsReply(
    val status: String,
    val refusal: String?,
    val values: Map<String, ULong>,
    val blobs: Map<String, ByteArray>,
) {
    /** 只有 ok 才算成功；任何别的状态都不算（未知状态也走这里，fail-closed）。 */
    val ok: Boolean get() = status == "ok"
}
