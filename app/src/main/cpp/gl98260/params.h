/*
 * glk98260_params - CVE-2026-98260 payload 的每构建参数（单一权威）。
 *
 * 来源：GhostLock backend profile（debug-only HOCON，经 Kotlin 严格解析后编码为
 *       一个 argv 参数 `--params <S>` 传入）。**本文件不内置任何目标内核数值**：
 *       缺参/超界/低 21 位不符 = 具名拒绝（fail-closed）。
 *
 * 权威规则：`struct glk98260_params` 的字段名+顺序+类型是本批跨端唯一权威；
 *           Kotlin 侧 `Cve98260Profile.PARAM_ORDER` 为其镜像，单测解析本头逐字段对拍。
 *
 * argv 协议：
 *   app -> helper      : `--params <S> --mode <fake|real|ids|leak>`
 *   child re-exec      : `<exe> --params <S> wait <mode>`（上游 timer.c:1435-1439 语义）
 *   target exec 信号   : `<exe>` 且 argv[0]==""（上游 timer.c:1631-1632 语义）——不解析 params。
 *   `<S>` = 逗号分隔十六进制数值段 `|` 四个字符串段：
 *           数值段字段顺序即本结构体字段顺序（schema_version 起 26 项）；
 *           字符串段 = manager_package|kmi|late_load_args|ksud_fallbacks。
 *           整串字符集 `[0-9a-fx,|A-Za-z0-9._/:-]`；`|`/`,` 不得出现在字符串段内部；
 *           列表型 token（late_load_args/ksud_fallbacks）内部用 `:` 分隔，≤8 token，
 *           每 token ≤128 B；late_load_args 必含 `--allow-shell`。
 *
 * 安全：本结构体只含内核地址/偏移/尺寸与 KernelSU 路径信息，不含密钥/令牌；
 *       helper 只在 shell 域进程内使用，不写日志（AIDL/JNI 回执在 app 侧）。
 *
 * 阶段：B1（payload 移植）。上游：YuKongA/CVE-2026-98260 @ ad1f4e7（MIT），
 *       timer/timer.c:67-104 的 #define 常量被本结构体取代。
 */
#ifndef GLK98260_PARAMS_H
#define GLK98260_PARAMS_H

#include <stddef.h>

/** 参数 schema 版本；不匹配 ⇒ 具名拒绝。 */
#define GLK98260_PARAMS_SCHEMA 1UL

/** argv 数值段字段数（= 结构体字段数；与 Kotlin PARAM_ORDER 对拍）。 */
#define GLK98260_PARAM_COUNT 26U

/** 字符串段个数：manager_package|kmi|late_load_args|ksud_fallbacks。 */
#define GLK98260_PARAM_STRING_COUNT 4U

/** 单参数串最大长度（argv）；超限 ⇒ 拒绝。 */
#define GLK98260_PARAM_MAX_BYTES 2048U

/** 字符串单段最大长度；列表单 token 最大长度/个数。 */
#define GLK98260_PARAM_STR_MAX 256U
#define GLK98260_PARAM_TOKEN_MAX 128U
#define GLK98260_PARAM_TOKENS_MAX 8U

/** 解析后的每构建参数（字段顺序 = 跨端权威；只追加不改序）。 */
struct glk98260_params {
    unsigned long long schema_version;
    unsigned long long page_size;
    unsigned long long cpu;
    unsigned long long sockbuf;
    unsigned long long slab_order;
    unsigned long long slide_max;
    unsigned long long base_min;
    unsigned long long base_max;
    unsigned long long static_timer_site;
    unsigned long long timer_site_low21;
    unsigned long long timer_alloc;
    unsigned long long cred_req;
    unsigned long long cred_alloc;
    unsigned long long cred_uid_off;
    unsigned long long off_task_cred;
    unsigned long long copy_process_low21;
    unsigned long long task_alloc_bytes;
    unsigned long long fullcap;
    unsigned long long selinux_state_static;
    unsigned long long panic_on_warn_static;
    unsigned long long root_user_static;
    unsigned long long init_user_ns_static;
    unsigned long long init_ucounts_static;
    unsigned long long init_groups_static;
    unsigned long long selinux_blob_off;
    unsigned long long selinux_sid_kernel;
    /* 字符串段（固定长度缓冲；解析时校验字符集/上限）。 */
    char manager_package[GLK98260_PARAM_STR_MAX];
    char kmi[GLK98260_PARAM_STR_MAX];
    char late_load_args[GLK98260_PARAM_STR_MAX];
    char ksud_fallbacks[GLK98260_PARAM_STR_MAX];
};

/** 全局唯一参数实例（进程启动期写入一次，之后只读；见 main）。 */
extern struct glk98260_params P;

/**
 * @brief 解析 `--params <S>` 的参数串。
 * @param out 输出结构体（仅成功后有效）。
 * @param arg 参数串（非 NULL）。
 * @return NULL=成功；否则返回稳定具名 reason（`glk98260-params:<id>`），调用方打印后退出。
 * @note 失败时 `out` 内容未定义；不分配堆内存；无副作用（不写文件/不发信号）。
 */
const char *glk98260_params_parse(struct glk98260_params *out, const char *arg);

/**
 * @brief 对已解析结构体做语义/边界校验（地址窗口、低 21 位、尺寸/偏移上限、字符串集）。
 * @param p 待校验结构体。
 * @return NULL=通过；否则稳定具名 reason（`glk98260-params:<id>`）。
 * @note 纯函数；可在打印 ENV marker 之前调用，保证失败时未 armed。
 */
const char *glk98260_params_validate(const struct glk98260_params *p);

/* ---- 供 timer.c 原样沿用的宏（把上游 #define 常量重定向到运行时 P） ---- */
#define STATIC_TIMER_SITE (P.static_timer_site)
#define TIMER_SITE_LOW21 (P.timer_site_low21)
#define TIMER_ALLOC (P.timer_alloc)
#define CRED_REQ (P.cred_req)
#define CRED_ALLOC (P.cred_alloc)
#define CRED_UID_OFF (P.cred_uid_off)
#define OFF_TASK_CRED (P.off_task_cred)
#define COPY_PROCESS_LOW21 (P.copy_process_low21)
#define TASK_ALLOC_BYTES (P.task_alloc_bytes)
#define FULLCAP (P.fullcap)
#define SELINUX_STATE_STATIC (P.selinux_state_static)
#define PANIC_ON_WARN_STATIC (P.panic_on_warn_static)
#define ROOT_USER_STATIC (P.root_user_static)
#define INIT_USER_NS_STATIC (P.init_user_ns_static)
#define INIT_UCOUNTS_STATIC (P.init_ucounts_static)
#define INIT_GROUPS_STATIC (P.init_groups_static)
#define SELINUX_BLOB_OFF (P.selinux_blob_off)
#define SELINUX_SID_KERNEL ((unsigned)P.selinux_sid_kernel)
#define SLIDE_MAX (P.slide_max)
#define BASE_MIN (P.base_min)
#define BASE_MAX (P.base_max)
#define SOCKBUF ((size_t)P.sockbuf)
#define PAGE ((unsigned long)P.page_size)

/* KernelSU 阶段字符串（来自 profile；不写入日志的密钥不存在）。 */
#define KSU_MANAGER_PACKAGE (P.manager_package)
#define KSU_KMI (P.kmi)
#define KSU_LATE_LOAD_ARGS (P.late_load_args)
#define KSU_KSUD_FALLBACKS (P.ksud_fallbacks)

#endif /* GLK98260_PARAMS_H */
