# 生命周期管理对比：ref（纯 C `641aaf6`）vs 本树（`Access`）

## 一、ref 的载体：file-scope static 变量（无对象、无所有者）

```c
// fops.c（逐字 ✓）
static atomic_int mr_respray, mr_sprayed, mr_stop, mr_x_done, mr_y_tid;   // :20
static int mr_fd = -1, mr_ready, mr_policy, mr_lock_slot;                 // :22
⇒ 状态用**静态变量**表达；生命周期由**标志位**管（mr_ready / mr_stop）
⇒ 线程循环：while (!atomic_load(&mr_stop)) { … }  ⇒ 退出时 **close(mr_fd); mr_fd = -1;**   // :55-68
   ⇒ ⇒ **fd 由线程自己关**（所有权在线程）
```

```c
kernel5_resident_start():
    if (mr_ready) return 1;                    // ★ 幂等：已上电直接返回
    atomic_store(mr_x_wait,0); (mr_y_done,0); (mr_stop,0);   // 复位标志
    if (sigaction(SIGUSR1,&sa,NULL) || pthread_create(&mr_ty,mr_y) ||
        pthread_create(&mr_tx,mr_x)) return 0;              // ✗ 中途失败**直接返回**，已建的不回收
    … 等四标志 / requeue / tgkill / mr_adjust / usleep(100000)
    mr_ready = 1;                              // 上电完成
kernel5_resident_write(t,v): if(!mr_ready) return 0; … mr_adjust()==0
kernel5_resident_stop():
    if(!mr_ready) return;                      // 未上电直接返回
    atomic_store(mr_stop,1); pthread_join(mr_ty); pthread_join(mr_tx); mr_ready=0;
    close_reclaim_sockets(); cleanup_page_prepare_state();   // 集中清理
```

## 二、ref 的每轮路由与 child

```c
// main.c（逐字 ✓）
reset_main_route_state()   // :403  每轮先把路由状态复位
run_main_route_threads():  // :418
    reset_main_route_state();
    race.start_threads(waiter_thread, owner_thread, consumer_thread, &request);
    if (error)  ⇒ fail_stop_dirty_race("partial PI worker startup cleanup", error);   // ★ 3 处 dirty failure
    status = race.run();
    if (dirty)  ⇒ fail_stop_dirty_race("route_done deadline", …);
    race.request_stop(); race.join();
    if (join_error) ⇒ fail_stop_dirty_race("PI worker join", join_error);
// child：
int child_alive = 1;                     // :1384  **局部标志**
kill(-child, SIGKILL); waitpid(child, NULL, 0); child_alive = 0;   // :1418 杀进程组+收尸+清标志
waitpid(child, NULL, WNOHANG);           // :1491 非阻塞探测
```

## 三、逐维对比

| 维度 | ref（纯 C） | 本树 `Access` |
| --- | --- | --- |
| 状态载体 | **file-scope static**（`mr_ready`/`mr_fd`/`mr_stop`） | **对象成员**（`phase_`/`seam_`/`target_`/`route_`） |
| 上电 | `kernel5_resident_start()`；**幂等**（`if (mr_ready) return 1`） | `initialize()`；**非幂等**（`phase_==Up ⇒ Failed`） |
| 上电中途失败 | **直接 `return 0`**，已建线程/fd **不回收** | `phase_ = Broken` **先记录**再返回 `Failed` |
| 下电 | `stop()`；未上电 `return`（无副作用） | `shutdown()`；非 Up ⇒ **`NotInitialized` 具名拒绝** |
| fd 所有者 | **线程自身**（`mr_x` 退出时 `close(mr_fd)`） | **seam**（`tear_down()` 全面负责） |
| 线程回收 | `atomic_store(mr_stop,1)` + `pthread_join` ×2 | `seam_->tear_down()`（内部 stop/join） |
| 每轮路由 | `reset_main_route_state()` + `start_threads` + `request_stop/join` | 同（`execute()`） |
| 失败路径表达 | `fail_stop_dirty_race(reason,err)` ×**3 处**（startup/deadline/join） | **单一 `phase_=Broken`** + 具名 status |
| child 生命周期 | **局部标志 `child_alive`** + `kill(-child)` + `waitpid` | `TargetState`（**非拥有**）+ seam 的 `tear_down` |
| **状态可观察性** | **不可观察**（static ✗） | **可观察**（`state()` ⇒ Declared/Initializing/Ready/Closed/Failed ✓） |
| 重复上电语义 | 幂等 ✓ | 拒绝（Failed）—— 语义不同 |
| 未初始化时调用 | `if(!mr_ready) return 0`（**静默 false**） | `NotInitialized`/`Unsupported` **具名** ✓ |
| 清理集中度 | 线程内自清 **+** `stop()` 里 `close_reclaim_sockets`/`cleanup_page_prepare_state` | `tear_down()` **一处** ✓ |
| route 数量 | `mr_fd` 等是**全局**（同时只有一条 route） | 收进 **seam**（一 backend 一对象，route 在参数 ✓） |

## 四、三条关键差异

```
① **所有者不同**：ref 的 fd 由**线程自己**关（所有权在线程内部）；本树由 **seam** 关
   ⇒ 本树把"资源归谁"从"谁创建谁关"改为"接缝统一持有" ⇒ 可单独测试、可整体替换 ✓
② **失败的可观察性**：ref 上电中途失败只 return 0，**残留线程/fd 无人回收**
   ⇒ 这正是 `fail_stop_dirty_race` 与 "dirty failure" 命名的由来（⇒ 旧实现**知道**这是弱点 ✓）
   本树改为 `phase_=Broken` **先落状态** ⇒ 调用方事后可查询并决定是否 `shutdown` ✓
③ **生命周期状态的可见性**：ref 无（static 变量，外部问不到）；本树有 `state(channel)`
   ⇒ 这正是"**能力是 state 而非 boolean**"的由来；也是"read 需 object found + page prepared，
      在此之前按名拒绝"这一诚实性的载体 ✓
```

## 五、对"接入"的含义（GAP-1）

```
⇒ 接入 = 用一个 `AccessSeam` 实现，把旧 C 的四件事**包成 seam 的四个方法**：
   · `bring_up`   ← `kernel5_resident_start`（＋ offset 选择 / 绑核 / iomem / child / perf）
   · `tear_down`  ← `kernel5_resident_stop`（＋ close_reclaim_sockets / cleanup_page_prepare_state /
                     kill(-child) + waitpid）
   · `land_word`  ← `kernel5_resident_write` / `do_one_write`
   · `can_read` / `read_word` ← 由 route 声明（旧 C 的 `kernelsnitch` 具备读能力时）
⇒ **必须补的两点**（衔接 §四的②③）：
   a. `bring_up` 中途失败 ⇒ **必须回收已建资源**（旧 C 的弱点 ✗ ⇒ 接入时补齐 ✓）
   b. 旧 C 的 static 全局 ⇒ **必须收进 seam 成员**（否则"一 backend 一对象"不成立 ✗）
```
