/**
 * @file victim_protocol.h
 * @brief 通用 victim helper 控制协议与 posix_spawn transport 的 C11 ABI（机制-only）。
 *
 * 本模块是 backend-agnostic 组件：只做「进程启动 + fd 协议 + 控制帧边界 + raw 拷贝 +
 * waitpid/reap」的机械操作；**不解析采样/不投票/不选址/不做业务判断/无状态机**。
 *
 * 控制协议 v1（与 Kotlin `core/victim/VictimProtocol.kt` 镜像；本头是跨语言常量权威）：
 * ```
 * wire  = [u32le bodyLen][body]
 * body  = [u8 version][u8 kind][u16le payloadLen][payload][u32le crc32(version..payload)]
 * 命令  = payload[0]=opcode + args
 * 回复  = payload[0]=status + data
 * ```
 * - 父进程通过本头 API 启动 helper（posix_spawn，不 fork），控制通道只在 fd 3 读写、
 *   raw 通道为 fd 4 的共享 memfd；helper 启动后自行关闭 fd≥5（inherited-fd 收敛）。
 * - 帧的语义（opcode/参数/校验）由 Kotlin 权威；本层只校验外层长度/上限并按字节搬运。
 * - raw 数据（perf 环/脚本输出）不在控制帧内传输：helper 写 memfd，父进程按偏移读取。
 *
 * @note fd ABI：control=3、raw=4 是协议常量（与任何 backend 目标参数无关），不得随目标变化。
 * @note 阶段：批 A（task-52）；不接 Flow/JNI 生产路径；零 fork。
 * @note 43284 复用约束：只依赖 libc + 本头；不得 import 本仓库 backend 私有头。
 */
#ifndef GHOSTLOCK_COMMON_VICTIM_PROTOCOL_H
#define GHOSTLOCK_COMMON_VICTIM_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* 协议 ABI v1 常量（Kotlin VictimProtocol 镜像对拍）                          */
/* ------------------------------------------------------------------------- */

/** @brief 控制协议版本（帧头 version）。 */
#define GLK_VICTIM_PROTOCOL_VERSION UINT32_C(1)

/** @brief 控制通道 fd（helper 读命令/写回复）。 */
#define GLK_VICTIM_CONTROL_FD 3

/** @brief raw 通道 fd（共享 memfd）。 */
#define GLK_VICTIM_RAW_FD 4

/**
 * @brief spawn file_actions 的 addclose 判定（N02 冻结规则）。
 *
 * 父进程原 fd 在 spawn 时可能恰好是 3/4；若在 dup2 之后 addclose 原 fd，会把刚
 * 安装的 control/raw 通道关掉。冻结规则：**只 close 原 fd >2 且 !=3 && !=4**，
 * 且所有 addclose 排在 adddup2 **之前**；0/1/2 保留给 helper 的输出/输入。
 *
 * @param fd 父进程原 fd。
 * @return 1=应 addclose；0=保留（fd 0..4）。
 * @note 纯规则函数；宿主单测 `tools/tests/victim_fd_rules_test.c`。
 */
static inline int32_t glk_victim_should_close_fd(int32_t fd) {
    return fd > 2 && fd != GLK_VICTIM_CONTROL_FD && fd != GLK_VICTIM_RAW_FD;
}

/** @brief 控制帧 body 上限（字节；超限拒绝，不截断）。 */
/* 4096 以位移表达：避免把协议容量写成单一魔法字面量。 */
#define GLK_VICTIM_FRAME_MAX_BODY (UINT32_C(1) << 12)

/** @brief 外层长度前缀字节数。 */
#define GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES UINT32_C(4)

/** @brief body 固定头字节数（version+kind+payloadLen）。 */
#define GLK_VICTIM_BODY_HEADER_BYTES UINT32_C(4)

/** @brief body 尾部 CRC32 校验字节数。 */
#define GLK_VICTIM_CHECKSUM_BYTES UINT32_C(4)

/** @brief body 内除 payload 外的固定开销。 */
#define GLK_VICTIM_BODY_OVERHEAD_BYTES \
    (GLK_VICTIM_BODY_HEADER_BYTES + GLK_VICTIM_CHECKSUM_BYTES)

/** @brief 完整控制线帧最大字节数（长度前缀 + body）。 */
#define GLK_VICTIM_FRAME_MAX_WIRE \
    (GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES + GLK_VICTIM_FRAME_MAX_BODY)

/** @brief raw 通道帧头字节数。 */
#define GLK_VICTIM_RAW_HEADER_BYTES UINT32_C(16)

/** @brief raw 通道帧魔数（'V','A','R','1' 小端）。 */
#define GLK_VICTIM_RAW_MAGIC UINT32_C(0x31524156)

/** @brief raw 通道帧版本。 */
#define GLK_VICTIM_RAW_VERSION UINT32_C(1)

/** @brief raw 帧 flags bit0：数据被容量截断。 */
#define GLK_VICTIM_RAW_FLAG_TRUNCATED UINT32_C(1)

/** @brief 帧 kind：命令（父→helper）。 */
#define GLK_VICTIM_FRAME_KIND_COMMAND UINT32_C(1)

/** @brief 帧 kind：回复（helper→父）。 */
#define GLK_VICTIM_FRAME_KIND_REPLY UINT32_C(2)

/** @brief 命令 opcode（机制命令；无业务语义）。 */
enum glk_victim_opcode {
    GLK_VICTIM_OPCODE_PROBE_UID = 1,
    GLK_VICTIM_OPCODE_PERF_SELF_SAMPLE = 2,
    GLK_VICTIM_OPCODE_EXEC_SCRIPT = 3,
    GLK_VICTIM_OPCODE_PARK = 4,
    GLK_VICTIM_OPCODE_BYE = 5,
    /* G04/A2 append-only（不重排 1–5；frame v1/长度/CRC 不变）。 */
    GLK_VICTIM_OPCODE_PROBE_STATUS = 6,
    GLK_VICTIM_OPCODE_PROBE_COMM_SET = 7,
    GLK_VICTIM_OPCODE_PROBE_COMM = 8
};

/**
 * @brief helper 能力位（与 Kotlin `core/victim/VictimCapability` 镜像；稳定 ABI）。
 *
 * 不重排既有 0–6；G04/A2 只在尾部 append bits 7/8/9。位号是跨语言契约，C/Kotlin
 * 对拍由工具目录静态校验（不跑编译）。
 *
 * @note bit7 当前树无 `EXEC_SCRIPT_NO_KILL` 占用（G04 I1 去向登记：按空位用 PROBE_STATUS）。
 */
enum glk_victim_capability {
    GLK_VICTIM_CAPABILITY_PROBE_UID = (1 << 0),
    GLK_VICTIM_CAPABILITY_SPAWN_HELPER = (1 << 1),
    GLK_VICTIM_CAPABILITY_PERF_SELF_SAMPLE = (1 << 2),
    GLK_VICTIM_CAPABILITY_EXEC_SCRIPT = (1 << 3),
    GLK_VICTIM_CAPABILITY_PARK = (1 << 4),
    GLK_VICTIM_CAPABILITY_BYE = (1 << 5),
    GLK_VICTIM_CAPABILITY_DETACH_ROOTED = (1 << 6),
    GLK_VICTIM_CAPABILITY_PROBE_STATUS = (1 << 7),
    GLK_VICTIM_CAPABILITY_PROBE_COMM_SET = (1 << 8),
    GLK_VICTIM_CAPABILITY_PROBE_COMM = (1 << 9)
};

/** @brief PROBE_STATUS raw 上限（4 KiB；超限 IO err，不截断当证据）。 */
#define GLK_VICTIM_PROBE_STATUS_MAX_BYTES UINT32_C(4096)

/**
 * @brief PROBE_STATUS 单帧可回复 raw 上限。
 *
 * frame v1 body 上限 4 KiB（含 8B 头/CRC 与 1B status）⇒ raw data 上限
 * = 4096 - 8 - 1 = 4087；raw 在 (4087,4096] 时按 IO err 失败（不截断/不分片）。
 */
#define GLK_VICTIM_PROBE_STATUS_MAX_REPLY_BYTES \
    (GLK_VICTIM_FRAME_MAX_BODY - GLK_VICTIM_BODY_OVERHEAD_BYTES - UINT32_C(1))

/** @brief PROBE_COMM_SET marker / PROBE_COMM 数据上限（Linux task comm 15B）。 */
#define GLK_VICTIM_PROBE_COMM_MAX_BYTES UINT32_C(15)

/** @brief 回复状态。 */
enum glk_victim_status {
    GLK_VICTIM_STATUS_OK = 0,
    GLK_VICTIM_STATUS_ERROR = 1
};

/** @brief helper 错误码（回复 status=ERROR 时携带）。 */
enum glk_victim_error {
    GLK_VICTIM_ERROR_INVALID_FRAME = 1,
    GLK_VICTIM_ERROR_UNSUPPORTED = 2,
    GLK_VICTIM_ERROR_IO = 3,
    GLK_VICTIM_ERROR_SPAWN = 4,
    GLK_VICTIM_ERROR_PERF = 5,
    GLK_VICTIM_ERROR_EXEC = 6,
    GLK_VICTIM_ERROR_BAD_ARGS = 7,
    GLK_VICTIM_ERROR_TIMEOUT = 8
};

/** @brief 脚本路径最大字节数（协议域；与 Kotlin 镜像）。 */
#define GLK_VICTIM_MAX_SCRIPT_PATH_BYTES UINT32_C(512)

/** @brief 单条 argv/env 项最大字节数。 */
#define GLK_VICTIM_MAX_ARG_BYTES UINT32_C(512)

/** @brief argv/env 条数上限。 */
#define GLK_VICTIM_MAX_ARG_COUNT UINT32_C(16)

/* ------------------------------------------------------------------------- */
/* C API 返回码（0=成功；其余为具名机械错误，不属于 errno）                    */
/* ------------------------------------------------------------------------- */

/** @brief C API 成功。 */
#define GLK_VICTIM_OK INT32_C(0)

/** @brief 参数/域非法（指针空、长度越界、fd 非法）。 */
#define GLK_VICTIM_ERR_INVALID_ARGUMENT INT32_C(-1001)

/** @brief 帧/流边界非法（长度前缀、body 上限、短读）。 */
#define GLK_VICTIM_ERR_BAD_FRAME INT32_C(-1002)

/** @brief 有界等待超时（未消费 handle）。 */
#define GLK_VICTIM_ERR_TIMEOUT INT32_C(-1003)

/** @brief 对端 EOF/子进程已退出。 */
#define GLK_VICTIM_ERR_EOF INT32_C(-1004)

/** @brief 系统调用失败；`*out_system_error` 携带 errno。 */
#define GLK_VICTIM_ERR_SYSTEM INT32_C(-1005)

/* ------------------------------------------------------------------------- */
/* 类型                                                                    */
/* ------------------------------------------------------------------------- */

/** @brief 不透明 helper token；0 永远无效。 */
typedef uint64_t glk_victim_token;

/**
 * @brief helper 启动参数（调用期借用；本层深拷贝 argv/env 指针数组语义为「调用返回前有效」）。
 *
 * @note `argv` 必须以 NULL 结尾且 argv[0] 非空；`envp` 为 NULL 时继承调用方环境。
 * @note `raw_size` 是共享 memfd 容量（≥ GLK_VICTIM_RAW_HEADER_BYTES）。
 */
struct glk_victim_spawn_request {
    const char *helper_path;
    const char *const *argv;
    const char *const *envp;
    uint64_t raw_size;
};

/**
 * @brief helper 启动回执（机械事实）。
 *
 * @property pid 子进程 PID（>0 表示成功）。
 * @property system_error 失败时的 errno（0=无）。
 */
struct glk_victim_spawn_result {
    int32_t pid;
    int32_t system_error;
};

/**
 * @brief 启动一个通用 victim helper（posix_spawn；不 fork）。
 *
 * @param request 显式参数（helper 路径/argv/env/raw 容量）。
 * @param result 必填输出；成功时 pid>0。
 * @return 成功返回非零 token；失败返回 0（result.system_error 携带 errno 或 0）。
 * @note 子进程 fd 集：0/1/2 继承 + control→3 + raw→4；其余由 spawn flag/helper 自检收敛。
 * @note 控制通道 fd3 为双向 socketpair（读命令/写回复）；失败路径关闭已建 socket/memfd、销毁 spawn 属性，不泄漏 fd。
 */
glk_victim_token glk_victim_spawn(const struct glk_victim_spawn_request *request,
                                  struct glk_victim_spawn_result *result);

/**
 * @brief 写入一个完整控制线帧（含 4 字节长度前缀）。
 *
 * @param token 有效 token。
 * @param frame Kotlin 编码的完整线帧（前缀 + body）；前缀长度必须与 body 一致。
 * @param frame_len 线帧总字节数（≤ GLK_VICTIM_FRAME_MAX_WIRE）。
 * @param timeout_ms 有界等待（毫秒；>0）。
 * @return GLK_VICTIM_OK/具名错误。
 * @note 本层只校验外层长度/上限并按字节写入；不解析 opcode/check。
 */
int32_t glk_victim_send(glk_victim_token token, const uint8_t *frame,
                        uint32_t frame_len, int32_t timeout_ms);

/**
 * @brief 读取一个完整控制线帧（长度前缀 + body，拷贝给调用方）。
 *
 * @param token 有效 token。
 * @param out_frame 输出缓冲（容量 ≥ 线帧长度）。
 * @param capacity 输出缓冲容量。
 * @param out_len 必填；写回实际线帧长度。
 * @param timeout_ms 有界等待（毫秒；>0）。
 * @return GLK_VICTIM_OK/具名错误；超时不消费 token。
 */
int32_t glk_victim_receive(glk_victim_token token, uint8_t *out_frame,
                           uint32_t capacity, uint32_t *out_len,
                           int32_t timeout_ms);

/**
 * @brief 从 raw memfd 读取一段字节（不解析）。
 *
 * @param token 有效 token。
 * @param offset memfd 内偏移（≥0；对齐由协议使用方保证）。
 * @param out 输出缓冲（非 NULL）。
 * @param length 读取长度（>0）。
 * @return GLK_VICTIM_OK/具名错误。
 * @note 越界（offset+length>raw_size 或溢出）⇒ INVALID_ARGUMENT。
 */
int32_t glk_victim_read_raw(glk_victim_token token, uint64_t offset,
                            uint8_t *out, uint32_t length);

/**
 * @brief @param token 有效 token。 @param out_size 必填。 @return raw memfd 容量。
 */
int32_t glk_victim_raw_size(glk_victim_token token, uint64_t *out_size);

/**
 * @brief @param token 有效 token。 @param out_pid 必填。 @return helper PID（已退出也返回原 PID）。
 */
int32_t glk_victim_pid(glk_victim_token token, uint32_t *out_pid);

/**
 * @brief 有界等待 helper 退出并回收（waitpid/reap）。
 *
 * @param token 有效 token。
 * @param timeout_ms 等待上限（毫秒；>0）。
 * @param out_exit 必填；写回退出码（WIFEXITED: exit status；信号: 128+signal）。
 * @return GLK_VICTIM_OK / GLK_VICTIM_ERR_TIMEOUT（未退出；不 kill）。
 * @note 成功回收后再次 wait 返回已缓存退出码（不重复 waitpid）。
 */
int32_t glk_victim_wait(glk_victim_token token, int32_t timeout_ms,
                        int32_t *out_exit);

/**
 * @brief 杀掉 helper 并回收（SIGKILL + waitpid）。
 *
 * @param token 有效 token。
 * @param out_exit 必填；写回退出码。
 * @return GLK_VICTIM_OK/具名错误；已回收时返回缓存退出码。
 */
int32_t glk_victim_kill(glk_victim_token token, int32_t *out_exit);

/**
 * @brief 只 detach：关闭父侧 control/raw fd 并 free handle；**不 kill、不 wait**。
 *
 * @param token 有效 token（消费）。
 * @return GLK_VICTIM_OK/INVALID_ARGUMENT。
 * @note rooted child 驻留语义；若子进程随后退出而父进程未 wait，僵尸由进程退出兜底。
 */
int32_t glk_victim_detach(glk_victim_token token);

/**
 * @brief 关闭：best-effort kill+reap+关闭父侧 fd 并 free handle（消费 token）。
 *
 * @param token 有效 token。
 * @return GLK_VICTIM_OK/具名错误。
 */
int32_t glk_victim_close(glk_victim_token token);

/* ------------------------------------------------------------------------- */
/* 通用帧传输 helper（helper 侧阻塞读写；父侧 API 内复用）                     */
/* ------------------------------------------------------------------------- */

/**
 * @brief 阻塞写入一个完整控制线帧（不校验 body 语义）。
 *
 * @param fd 目标 fd（一般是 GLK_VICTIM_CONTROL_FD）。
 * @param frame 完整线帧（前缀+body）。
 * @param frame_len 线帧长度。
 * @return GLK_VICTIM_OK/具名错误。
 */
int32_t glk_victim_write_frame_fd(int32_t fd, const uint8_t *frame,
                                  uint32_t frame_len);

/**
 * @brief 阻塞读取一个完整控制线帧（只做长度前缀/上限）。
 *
 * @param fd 源 fd。
 * @param out_frame 输出缓冲。
 * @param capacity 缓冲容量（≥ GLK_VICTIM_FRAME_MAX_WIRE 才可能容纳最大帧）。
 * @param out_len 必填；写回实际长度。
 * @return GLK_VICTIM_OK/具名错误。
 */
int32_t glk_victim_read_frame_fd(int32_t fd, uint8_t *out_frame,
                                 uint32_t capacity, uint32_t *out_len);

/* ------------------------------------------------------------------------- */
/* E4 零 fork ① 计数（debug/measurement only；非业务状态）                     */
/* ------------------------------------------------------------------------- */
/*
 * CMake **仅在 Debug** 对 app JNI DSO 与 ghostlock_victim_helper 链接
 * `-Wl,--wrap=fork -Wl,--wrap=vfork` 并定义 `GLK_VICTIM_FORK_WRAP=1`；
 * Release/RelWithDebInfo 不传 wrap（严格 ②：产物无 fork/vfork 导入）。
 * `__wrap_fork/__wrap_vfork` 只在 Debug 转发到 `__real_fork/__real_vfork`
 * 并在调用前原子计数。**唯一用途是 E4 主证明 ①**：Debug 测量构建里计数必须
 * 恒为 0（helper 的 posix_spawn 内部 clone/vfork 不经过 fork/vfork 符号）；
 * Release 下计数 API 恒返回 0、reset no-op。计数变量是受控 debug 证据，不是
 * 业务全局状态。
 */

/**
 * @brief @return 本模块链接单元内 `fork()` 调用累计次数（E4 ① 证据）。
 */
uint32_t glk_victim_fork_count(void);

/**
 * @brief @return 本模块链接单元内 `vfork()` 调用累计次数（E4 ① 证据）。
 */
uint32_t glk_victim_vfork_count(void);

/**
 * @brief 清零 fork/vfork 计数（仅 debug 测试 canary 使用；生产路径不调用）。
 * @note helper 侧通过 bye 回执携带计数，不依赖此清零接口。
 */
void glk_victim_fork_count_reset(void);

/**
 * @brief 计算标准 CRC32（IEEE；多项式 0xEDB88320）。
 *
 * @param data 输入字节（NULL 且 len=0 合法）。
 * @param len 字节数。
 * @return 无符号 CRC32 值。
 */
uint32_t glk_victim_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_COMMON_VICTIM_PROTOCOL_H */
