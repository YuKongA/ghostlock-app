package com.ghostlock.app.log

/**
 *  落到**硬盘**的去处：只做一件事 —— 把整行交给 [LogFile]。
 *
 *  写盘的位置、候选路径与打开方式都在 [LogFile]（老逻辑：应用私有 files 目录优先，其次外置专属目录；
 *  写失败只当没写成 —— 日志不该把主流程拖下水）。
 */
object FileSink : LogSink {
    override fun write(level: LogLevel, tag: String, message: String, line: String) {
        LogFile.appendLine(line)
    }
}
