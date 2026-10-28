/*
 * glk98260_params - 参数串解析与边界校验（fail-closed，纯函数）。
 *
 * 输入来自 app 侧 profile（debug-only HOCON）编码后的单个 argv 参数；
 * 本文件不访问文件/网络/信号，不分配堆内存。任何缺参、非法字符、超界、
 * 低 21 位不符 ⇒ 返回稳定具名 reason，helper 在打印 ENV/arm 之前退出。
 *
 * 跨端顺序权威：`params.h` 的结构体字段顺序 = Kotlin `PARAM_ORDER`；
 * 单测（B2）解析本头对拍，禁止手抄两套。
 */
#include "params.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** 全局唯一参数实例（main 启动期写入一次，之后只读）。 */
struct glk98260_params P;

/** 字符集：十六进制、包名/KMI/路径安全字符；禁空白/引号/反斜杠/换行。 */
static int glk_char_ok(char c)
{
    if (c >= '0' && c <= '9') {
        return 1;
    }
    if (c >= 'a' && c <= 'z') {
        return 1;
    }
    if (c >= 'A' && c <= 'Z') {
        return 1;
    }
    switch (c) {
    case 'x':
    case 'X':
    case ',':
    case '|':
    case '.':
    case '/':
    case ':':
    case '-':
    case '_':
        return 1;
    default:
        return 0;
    }
}

/**
 * @brief 有限长度拷贝并校验字符集/上限。
 * @param dst 目标缓冲（GLK98260_PARAM_STR_MAX）。
 * @param src 源段（不一定 NULL 结尾，长度由 len 给出）。
 * @param len 源段长度。
 * @param allow_empty 1 允许空串。
 * @return NULL=成功；否则具名 reason。
 */
static const char *copy_str(char *dst, const char *src, size_t len, int allow_empty)
{
    if (len == 0U) {
        if (!allow_empty) {
            return "glk98260-params:empty-string";
        }
        dst[0] = '\0';
        return NULL;
    }
    if (len >= GLK98260_PARAM_STR_MAX) {
        return "glk98260-params:string-too-long";
    }
    for (size_t i = 0U; i < len; i++) {
        if (!glk_char_ok(src[i])) {
            return "glk98260-params:bad-char";
        }
        if (src[i] == ',' || src[i] == '|') {
            return "glk98260-params:bad-char-in-string";
        }
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return NULL;
}

/** 校验 `:` 分隔列表（元素数/长度/字符集；不含 `,`/`|`/`:`/空白）。 */
static const char *check_list(const char *s, int allow_empty)
{
    unsigned count = 0U;
    const char *p = s;

    if (*p == '\0') {
        return allow_empty ? NULL : "glk98260-params:empty-list";
    }
    for (;;) {
        const char *sep = strchr(p, ':');
        size_t n = sep ? (size_t)(sep - p) : strlen(p);

        if (n == 0U) {
            return "glk98260-params:empty-token";
        }
        if (n > GLK98260_PARAM_TOKEN_MAX) {
            return "glk98260-params:token-too-long";
        }
        for (size_t i = 0U; i < n; i++) {
            if (!glk_char_ok(p[i]) || p[i] == ',' || p[i] == '|' || p[i] == ':') {
                return "glk98260-params:bad-token";
            }
        }
        count++;
        if (count > GLK98260_PARAM_TOKENS_MAX) {
            return "glk98260-params:too-many-tokens";
        }
        if (!sep) {
            break;
        }
        p = sep + 1;
    }
    return NULL;
}

/**
 * @brief 解析数值段 `0x%llx` 逗号分隔（恰好 GLK98260_PARAM_COUNT 项）。
 * @return NULL=成功；否则具名 reason。
 */
static const char *parse_numbers(const char *part, struct glk98260_params *out)
{
    unsigned long long *fields[GLK98260_PARAM_COUNT];
    size_t used = 0U;
    const char *p = part;

    fields[0] = &out->schema_version;
    fields[1] = &out->page_size;
    fields[2] = &out->cpu;
    fields[3] = &out->sockbuf;
    fields[4] = &out->slab_order;
    fields[5] = &out->slide_max;
    fields[6] = &out->base_min;
    fields[7] = &out->base_max;
    fields[8] = &out->static_timer_site;
    fields[9] = &out->timer_site_low21;
    fields[10] = &out->timer_alloc;
    fields[11] = &out->cred_req;
    fields[12] = &out->cred_alloc;
    fields[13] = &out->cred_uid_off;
    fields[14] = &out->off_task_cred;
    fields[15] = &out->copy_process_low21;
    fields[16] = &out->task_alloc_bytes;
    fields[17] = &out->fullcap;
    fields[18] = &out->selinux_state_static;
    fields[19] = &out->panic_on_warn_static;
    fields[20] = &out->root_user_static;
    fields[21] = &out->init_user_ns_static;
    fields[22] = &out->init_ucounts_static;
    fields[23] = &out->init_groups_static;
    fields[24] = &out->selinux_blob_off;
    fields[25] = &out->selinux_sid_kernel;

    for (;;) {
        char *end = NULL;
        unsigned long long v;

        if (used >= GLK98260_PARAM_COUNT) {
            return "glk98260-params:too-many-numbers";
        }
        if (p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) {
            return "glk98260-params:number-not-hex";
        }
        if (!((p[2] >= '0' && p[2] <= '9') || (p[2] >= 'a' && p[2] <= 'f') ||
              (p[2] >= 'A' && p[2] <= 'F'))) {
            return "glk98260-params:number-empty";
        }
        v = strtoull(p, &end, 16);
        if (end == p) {
            return "glk98260-params:number-invalid";
        }
        *fields[used] = v;
        used++;
        if (*end == '\0') {
            break;
        }
        if (*end != ',') {
            return "glk98260-params:number-separator";
        }
        p = end + 1;
    }
    if (used != GLK98260_PARAM_COUNT) {
        return "glk98260-params:number-count";
    }
    return NULL;
}

/** 解析字符串段（恰好 4 段，`|` 分隔）。 */
static const char *parse_strings(const char *strings, struct glk98260_params *out)
{
    char *dsts[GLK98260_PARAM_STRING_COUNT];
    const char *p = strings;

    dsts[0] = out->manager_package;
    dsts[1] = out->kmi;
    dsts[2] = out->late_load_args;
    dsts[3] = out->ksud_fallbacks;

    for (unsigned i = 0U; i < GLK98260_PARAM_STRING_COUNT; i++) {
        const char *sep;
        size_t len;
        const char *reason;

        if (p == NULL) {
            return "glk98260-params:string-count";
        }
        sep = strchr(p, '|');
        len = sep ? (size_t)(sep - p) : strlen(p);
        /* late_load_args/ksud_fallbacks 允许空；包名/KMI 不允许。 */
        reason = copy_str(dsts[i], p, len, i >= 2U);
        if (reason != NULL) {
            return reason;
        }
        p = sep ? sep + 1 : NULL;
    }
    if (p != NULL) {
        return "glk98260-params:too-many-strings";
    }
    return NULL;
}

const char *glk98260_params_parse(struct glk98260_params *out, const char *arg)
{
    const char *sep;
    const char *reason;
    char numeric[GLK98260_PARAM_MAX_BYTES];
    size_t n;

    if (out == NULL || arg == NULL) {
        return "glk98260-params:null";
    }
    n = strlen(arg);
    if (n == 0U || n >= GLK98260_PARAM_MAX_BYTES) {
        return "glk98260-params:length";
    }
    for (size_t i = 0U; i < n; i++) {
        if (!glk_char_ok(arg[i])) {
            return "glk98260-params:bad-char";
        }
    }
    sep = strchr(arg, '|');
    if (sep == NULL) {
        return "glk98260-params:missing-strings";
    }
    if ((size_t)(sep - arg) >= sizeof(numeric)) {
        return "glk98260-params:length";
    }
    memcpy(numeric, arg, (size_t)(sep - arg));
    numeric[(size_t)(sep - arg)] = '\0';
    memset(out, 0, sizeof(*out));
    reason = parse_numbers(numeric, out);
    if (reason != NULL) {
        return reason;
    }
    reason = parse_strings(sep + 1, out);
    if (reason != NULL) {
        return reason;
    }
    reason = check_list(out->late_load_args, 1);
    if (reason != NULL) {
        return reason;
    }
    if (out->late_load_args[0] != '\0' &&
        strstr(out->late_load_args, "--allow-shell") == NULL) {
        return "glk98260-params:missing-allow-shell";
    }
    reason = check_list(out->ksud_fallbacks, 1);
    if (reason != NULL) {
        return reason;
    }
    return glk98260_params_validate(out);
}

/** @return 1=内核地址形态（top 16 位全 1）。 */
static int kernel_ptr(unsigned long long v)
{
    return (v >> 48) == 0xffffULL;
}

const char *glk98260_params_validate(const struct glk98260_params *p)
{
    if (p == NULL) {
        return "glk98260-params:null";
    }
    if (p->schema_version != GLK98260_PARAMS_SCHEMA) {
        return "glk98260-params:schema";
    }
    if (p->page_size != 4096ULL) {
        return "glk98260-params:page-size";
    }
    if (p->cpu > 63ULL) {
        return "glk98260-params:cpu";
    }
    if (p->sockbuf == 0ULL || p->sockbuf > (1ULL << 20) ||
        (p->sockbuf & (p->sockbuf - 1ULL)) != 0ULL) {
        return "glk98260-params:sockbuf";
    }
    if (p->slab_order == 0ULL || p->slab_order > 6ULL ||
        (p->page_size << p->slab_order) != p->sockbuf) {
        return "glk98260-params:slab-order";
    }
    if (p->slide_max == 0ULL || p->slide_max > 0x40000000ULL ||
        (p->slide_max & 0x1fffffULL) != 0ULL) {
        return "glk98260-params:slide-max";
    }
    if (p->base_min < 0xffffff0000000000ULL ||
        p->base_max > 0xffffffc000000000ULL || p->base_min >= p->base_max) {
        return "glk98260-params:base-window";
    }
    if (!kernel_ptr(p->static_timer_site) ||
        (p->static_timer_site & 0x1fffffULL) != p->timer_site_low21) {
        return "glk98260-params:timer-site";
    }
    if (p->timer_alloc == 0ULL || p->timer_alloc > 0x1000ULL) {
        return "glk98260-params:timer-alloc";
    }
    if (p->cred_req == 0ULL || p->cred_alloc < p->cred_req ||
        p->cred_alloc > 0x1000ULL) {
        return "glk98260-params:cred-alloc";
    }
    if (p->cred_uid_off > 0x100ULL || p->off_task_cred > 0x2000ULL ||
        p->selinux_blob_off > 0x100ULL) {
        return "glk98260-params:offsets";
    }
    if (p->copy_process_low21 > 0x1fffffULL || p->task_alloc_bytes == 0ULL ||
        p->task_alloc_bytes > 0x4000ULL) {
        return "glk98260-params:task";
    }
    if (p->fullcap == 0ULL || p->selinux_sid_kernel == 0ULL ||
        p->selinux_sid_kernel > 0xffffffffULL) {
        return "glk98260-params:caps";
    }
    if (!kernel_ptr(p->selinux_state_static) ||
        !kernel_ptr(p->panic_on_warn_static) ||
        !kernel_ptr(p->root_user_static) ||
        !kernel_ptr(p->init_user_ns_static) ||
        !kernel_ptr(p->init_ucounts_static) ||
        !kernel_ptr(p->init_groups_static)) {
        return "glk98260-params:symbols";
    }
    if (p->manager_package[0] == '\0' || p->kmi[0] == '\0') {
        return "glk98260-params:strings";
    }
    return NULL;
}
