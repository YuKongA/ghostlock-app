package com.ghostlock.app.flow;

/**
 * FlowHost 的过程回传（oneway，不阻塞 host 执行线程）。
 */
interface IFlowHostCallback {
    /** 一行 host 日志（已带级别/时间格式，由 host 侧格式化）。 */
    oneway void onLine(String line);

    /** 终态通知：code 为 FlowHostProtocol 的稳定终止码（0=ok/1=failed）。 */
    void onTerminal(int code);
}
