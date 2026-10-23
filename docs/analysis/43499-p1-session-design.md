# P1 施工设计 v2：GlkSession 骨架与全局搬入

> 从属：[43499-43284-cpp-parameterization-plan.md](../plan/43499-43284-cpp-parameterization-plan.md) P1。只定类型/签名/顺序/门禁，不改算法。P1 启动仍须用户批准 C1（生产范围）；C8 因 AGENTS §4 删除 C++23 条文已关闭。
> 快照：HEAD `d2778a1a`；基线 `e0fc7fe5…` / `5d922ffb…`。本版关闭 design-critic P1-01～P1-07。

## 1. 目标与不变量

`main.c` 27、`fops.c` 13、`util.c` 26 个可变全局搬进 `Glk43499Session`；15 个核心函数收 session（+ 请求值）；**不设进程级句柄全局**，session 由调用方持有。**不改**控制流/重试/默认值/futex/页布局/join-close 顺序。

## 2. 句柄与所有权（P1-01/P1-06）

~~~c
typedef enum { GLK_BACKEND_NONE = 0, GLK_BACKEND_43499, GLK_BACKEND_43284 } GlkBackend;

typedef struct Glk43499Session Glk43499Session;

typedef struct {
    GlkBackend backend;
    union { Glk43499Session *p43499; void *p43284; } u;
} GlkSession;

/* No process-wide handle: the caller owns GlkSession. */

struct Glk43499Session {
    Glk43499Target target;
    Glk43499Config config;
    Glk43499Env env;
    Glk43499Runtime rt;
    Glk43499Result result;
    GlkLogSink log;
    atomic_bool stopping;
};
~~~

- **内层 `Glk43499Session` 由调用方分配/释放**（P1 CLI 用栈；P5 JNI 用 heap）。外层 `GlkSession` 只借用 `u.p43499`，不 free。
- API：
  - `int glk_session_create_43499(GlkSession *outer, Glk43499Session *inner, const Glk43499Target *t, const Glk43499Config *c, const Glk43499Env *e);`
  - `void glk_session_destroy(GlkSession *outer);`
- `create`：初始化 inner → 填 outer；不 publish、不 CAS；返回 `GLK_OK/GLK_BAD_PARAM`。
- `destroy`：停线程/释放资源（§6）→ 清零 outer；不 free inner。
- P1 只支持单线程 CLI；**不提供 generation/stale-handle 保护**，该字段 P5 再加。P1 不宣称 JNI 并发安全。
- CLI 壳（P1b 起；P1a 仍走旧 `main`，不调用 create）：
  ```c
  Glk43499Session inner = {0}; GlkSession outer = {0};
  if (glk_session_create_43499(&outer, &inner, &target, &cfg, &env) != 0) return 1;
  int rc = run_exploit(outer.u.p43499);
  glk_session_destroy(&outer);
  ```

## 3. Target 初始化：禁止按值复制（P1-02）

`struct kernel_offsets.uname_r` 是 `const char *`；`Glk43499Target` 重指到自己的 `release[192]`，所以**不能按值复制**：

~~~c
typedef struct {
    uintptr_t self;                 /* == (uintptr_t)this; copy 后失效 */
    struct kernel_offsets offsets;  /* uname_r -> release */
    char release[192];
    Glk43499Layout layout;
} Glk43499Target;

void glk43499_target_init(Glk43499Target *dst, const struct kernel_offsets *src,
                          const char *release);
int  glk43499_target_valid(const Glk43499Target *t); /* self == t */
~~~

- `init` 在 dst 原地复制 offsets、拷贝 release、`dst->offsets.uname_r=dst->release`、设 `self`。
- 函数参数一律 `const Glk43499Target *`；禁止 `Glk43499Target x = *t;`、`memcpy`、按值返回。
- 进入攻击函数前 `assert(glk43499_target_valid(&s->target))`；运行期发现 self 不符立即 fail-closed。

## 4. Runtime 分组（P1-07）

| 组 | 原符号 | 新字段 |
|---|---|---|
| futex | `f_wait/f_pi_target/f_pi_chain` | `rt.futex.*` |
| 同步 | `waiter_*/owner_*/consumer_*/route_done/punch_*` | `rt.sync.*` |
| 全局线程 | 三主线程、`mr_tx/mr_ty` | `rt.threads.*` + `started` |
| 路由 | `mr_*`、`route_last_*` | `rt.route.*` |
| 页 | `page_base/fake_*/prebuilt_*` | `rt.page.*` |
| 喷溅 | `ks/mm_objs_per_slab/skb_buf/*_ctx/child_leak/memfd_leak/reclaim*` | `rt.spray.*` |
| 计时 | `t0`、`fast_repair_route` | `rt.timing.*` |
| 目标 | `active_offsets/p0_kernel_phys_load/g_init_cred_image` | `target.` |
| CPU | `g_core_main/g_core_consumer` | `config.` |

- `standard_io_backup[3]` 保持 route 栈局部；`g_external_offsets/release` 只在 create 内临时。
- **请求唯一权威 = `GlkWriteRequest`**：`target/value/mode/preserve_child/label`；不再有 `pselect_custom_write/target/child_node` 全局。请求经 `RouteThreadArg` 传给 waiter，`rt.route` 不存副本。
- `tcp_punch_thread` 的 `pthread_t puncher` 与 `TcpPunchState` 保持 `do_tcp` 栈局部，不进 `rt.threads`。

## 5. 函数签名（15 + helper）

~~~c
typedef struct { Glk43499Session *s; const GlkWriteRequest *req; } GlkRouteThreadArg;
void *waiter_thread(void *arg);   /* arg = GlkRouteThreadArg* */
void *owner_thread(void *arg);    /* arg = Glk43499Session* */
void *consumer_thread(void *arg); /* arg = Glk43499Session* */
void *mr_x(void *arg);            /* arg = Glk43499Session* */
void *mr_y(void *arg);
void *tcp_punch_thread(void *arg);/* arg = {Glk43499Session*, TcpPunchState*} */

int   run_main_route_threads(Glk43499Session *s, const GlkWriteRequest *req);
int   do_one_write(Glk43499Session *s, const GlkWriteRequest *req);
void  do_kernel5_fake_lock_route(Glk43499Session *s, const GlkWriteRequest *req);
void  do_tcp_fake_lock_route(Glk43499Session *s, const GlkWriteRequest *req);
void  do_pselect_fake_lock_route(Glk43499Session *s, const GlkWriteRequest *req);
int   kernel5_resident_start(Glk43499Session *s);
int   kernel5_resident_write(Glk43499Session *s, uintptr_t target, uintptr_t value);
void  kernel5_resident_stop(Glk43499Session *s);
int   prepare_skb_payload(Glk43499Session *s, const GlkWriteRequest *req, uintptr_t base);
uintptr_t prepare_kernel_page(Glk43499Session *s, const GlkWriteRequest *req);
uintptr_t prepare_good_kernel_page(Glk43499Session *s, const GlkWriteRequest *req);
int   run_exploit(Glk43499Session *s);
~~~

- `run_main_route_threads` 构造 `GlkRouteThreadArg` 传 waiter；三个 route 从 `arg->req` 取请求。
- `mr_y` 用 `s` 起 `mr_x`；`kernel5_resident_start` 传 `s`。
- `tcp_punch_thread` 只用栈上的 `TcpPunchState`；join 由 `do_tcp` 负责。
- `GlkWriteRequest.label` 是借用指针，只在同步调用内有效；核心不保存、不跨线程。

## 6. 初始化/销毁顺序

Create：target init → config/env 拷贝 → rt 清零/原子初始化 → outer 填。
Destroy：
```
stopping=1
→ 停 consumer/owner/waiter（置 stop，join）
→ 停 resident（mr_stop，join mr_tx/mr_ty）
→ 停 tcp puncher（若本调用创建，join）
→ close reclaim/quarantine/prebuilt（附件 B 顺序）
→ 释放 ctx/memfd/skb/page
→ 清零 outer
```
禁止 join 前 close/unmap；`leak-on-stuck` 仅 pselect `pipefd/block_fd`。

## 7. P1a–P1d 分层门禁（P1-04）

| 子批 | 内容 | nm/构建判据 |
|---|---|---|
| P1a | 仅 types/target init/session；旧 `run_exploit(int,char**)` 与 main 不动 | 编译；无新增可写全局；旧全局仍在（允许） |
| P1b | P1b-1 CLI create/destroy；P1b-2 main.c 运行态全局 + 15 签名（目标字段延 P3） | main.c 无可写全局；fops/util 旧全局仍在（允许） |
| P1c | fops.c 13 全局 → `rt.route`；mr/tcp 收 ctx | fops.c 无可写全局；util 旧全局仍在（允许） |
| P1d | util.c 26 全局 → `rt.page/spray`；prepare 收 req | 全目标可写段只剩目标/P2 项；最终清零 |

- P1a 不改 15 函数签名，避免 CLI 无桥；每子批单独构建/对拍。

## 8. cmp_disasm 差异政策（P1-05）

- P1b-2a 用 `cmp_disasm --count-delta 32`；P1b-2b 状态搬迁用 `--important`：重要 call/syscall 的**数量与顺序必须不变**，参数差异只允许 session/布局指针。
- count-delta 为诊断：P1b-2b 每目标 **≤64**；**禁止**增删/重排重要 call/syscall、join/close 顺序变化。
- 逐目标记录 arg-diff 理由；`--reviewed` 只用于数据地址；无法解释 = 停批。

## 9. 风险

| # | 风险 | 处置 |
|---|---|---|
| R1 | 指针改变寄存器/栈 | 逐目标 cmp_disasm；只接受 §8 白名单 |
| R2 | Target 按值复制导致 uname_r 悬垂 | `self` 校验 + 禁止赋值/memcpy/按值返回 |
| R3 | 无真机 | 标 `device-gate-pending`，不把 AVD 当 supported |
