# 43499 · 第 78 轮：ghost disarm 的**落码清单**（已可机械执行）✓✓

## 一、插入点（✓，已定位到函数 ✓）

```
文件：`core/src/backend/cve_2026_43499/seam/multicast_route.hpp` ✓；
线程体：`waiter_entry`（:578-580 ✓）= `static void *waiter_entry(void *self) { return …waiter_main(); }` ✓
⇒ 即 **waiter 线程的函数体 = `waiter_main(...)`** ✓ ⇒ 插入位置 = 它的**收尾**（⇒ 线程退出前 ✓）；
参照物（本树已核对 ✓）：`:379-384` 组装 `route::RouteStatus`，其中 `status.userspace_clean = 1;` ✓
  与 `status.kernel_disarmed = 1;` ✓ ⇒ ⇒ **这两行正是"记账"** ✗（⇒ 退役在同处**还要动手** ✓）。
```

## 二、要插入的代码（✓，与退役逐字同形 ✓）

```cpp
/* ★ 术后 ghost disarm（第 77 轮逐字取得 ✓；退役 race/threads.cpp:65-75 ✓）：
 *  remove_waiter() 已把本线程的 pi_blocked_on 指向**被回收的栈 waiter** ✗ ⇒ 若不摘除，
 *  线程退出会留下"可被 walk 的幽灵"J ⇒ walk 指向它而非页里描述的 target ✓。
 *  "Force one final slow-path removal while the stack frame is still alive" ✓。 */
if (config_.needs_ghost_disarm) {                       /* ⇒ 由 policy.multicast 决定 ✓（不写死 ✗） */
    const std::uint32_t dummy_pi = 0x80000000U | static_cast<std::uint32_t>(::getpid());
    struct timespec expired = {.tv_sec = 0, .tv_nsec = 0};
    errno = 0;
    const long disarm = ::syscall(SYS_futex, &dummy_pi, FUTEX_LOCK_PI, 0, &expired, nullptr, 0);
    std::printf("main: mcast ghost disarm ret=%ld errno=%d", disarm, errno);   /* 与归档同形 ✓ */
}
```

## 三、判据（✓，两步 ✓）

```
① 真机出现与归档**逐字同形**的 `mcast ghost disarm ret=-1 errno=110` ✓
   （⇒ `ret=-1` + `errno=110`(EAGAIN) = 一次真实系统调用 ✓）；
② ⇒ 然后看 **`main: victim uid=0`** ✓（⇒ 这是唯一硬判据 ✓）。
```

## 四、代价与纪律（✓）

```
· 一处插入 + 一个配置字段（来源：政策/文档 ✓，不写死 ✗）；
· 属**攻击关键路径** ⇒ 按守则 §2 已先出计划（本页 + goal76/goal77 ✓）⇒ 落码后按 §8 跑满三门禁 ✓；
· 若无效 ⇒ 回退一行（⇒ 该动作独立 ✓）。
```
