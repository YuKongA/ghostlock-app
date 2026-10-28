/*
 * glk98260_markers - helper stdout 的稳定 marker 行前缀（append-only 契约）。
 *
 * app/service 侧 `Cve98260Markers` 按本表解析；**未知行忽略**，半行留到下一轮。
 * 约束：单行 ≤512 B；ASCII；不含密钥；一行一次 write（logf 每次 fflush）。
 *
 * 顺序契约（详见计划附录 A §I）：
 *   ENV -> PREFLIGHT=ok -> ARMED=1 -> SELINUX_OFF=1 -> PANIC_ON_WARN=<n>
 *   -> FAKE_CRED=1 -> FAKE_UID=1 -> (脚本) KSU_LOADED|KSU_MISSING -> PARKED=<reason>
 * ARMED 之后任何失败路径必须 park 并打印 PARKED；不得退出。
 */
#ifndef GLK98260_MARKERS_H
#define GLK98260_MARKERS_H

#define GLK_MARK_ENV "GLK98260_ENV"
#define GLK_MARK_PREFLIGHT "GLK98260_PREFLIGHT"
#define GLK_MARK_ARMED "GLK98260_ARMED"
#define GLK_MARK_SELINUX_OFF "GLK98260_SELINUX_OFF"
#define GLK_MARK_PANIC_ON_WARN "GLK98260_PANIC_ON_WARN"
#define GLK_MARK_FAKE_CRED "GLK98260_FAKE_CRED"
#define GLK_MARK_FAKE_UID "GLK98260_FAKE_UID"
#define GLK_MARK_PARKED "GLK98260_PARKED"
#define GLK_MARK_RESULT "GLK98260_RESULT"

#endif /* GLK98260_MARKERS_H */
