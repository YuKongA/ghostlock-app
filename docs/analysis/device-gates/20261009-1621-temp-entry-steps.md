# 临时驱动在真机的分步结果（2026-10-09）

**批次**：临时入口驱动（pipeline/run_pipeline_temp.{hpp,cpp}），经 --ghostlock-app-call 启动，
输入 = 1991 字节 GLKv3 文档 + 84 字节会话帧。本件是临时物，清理登记见
docs/analysis/device-probe-43284-checklist.md。

**设备**：QV770MFGJ1（A301SO）。内核 5.15.189-android13-8-00016-g51bba4309aac-ab14546557。
PAGE_SIZE 4096，cpus 8。

**构建**：make -C core android NDK_TOOLCHAIN=$HOME/Library/Android/sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin
（core/Makefile 的 NDK_TOOLCHAIN ?= 写死 28.2；NDK-28 的 libc++ 在 -std=c++23 下被设备头牵进 locale 时报
expected expression。ANDROID_NDK_HOME 不是这份 Makefile 认的变量。）

## 原始输出（逐字）

    run-temp: document 1991 bytes
    run-temp: the gate admitted window.pagecache
    run-temp: seam up, target fd open, window bound
    run-temp: torn down ok
    run-temp: payload offsets from the declared temporary source, not a document
    run-temp: the document has no payload section (tuning-missing), using the declared source
    run-temp: payload offsets read, lock=0x58 waiter=0xb0 task=0x700
    run-temp: device reports page=4096 cpus=8
    run-temp: snitch setup ok
    run-temp: region ok, total 12584 bytes
    run-temp: proofs bucket=5 second=0xffff80000014d000 (observed, not chosen)
    EXIT=0

## 逐条读法

| 步 | 结果 |
| --- | --- |
| 文档 | 1991 字节读到 |
| 会话帧 | 接受（无帧时打印 no session frame 并返 3；未出现） |
| 闸门 | admitted window.pagecache |
| 门 | 载体以 O_RDONLY 与 O_CLOEXEC 打开（dev 载体路径已 touch） |
| 窗口与接缝 | PageCacheWrite 与 PageCacheSeam 绑定，bring_up_with(sa) 成功，tear_down 成功 |
| 布局 | 偏移取自声明的临时源（文档缺 abi.payload 段，具名拒 tuning-missing） |
| 语料 | setup 过关；region 12584 字节；证物地址由真实规则问出（bucket=5，second=0xffff80000014d000） |

## 口径

**这不是 root，也没有启动 KernelSU。** 退出码 0 的含义是入口已实现的那几步在真机成功。
入口尚未接：发现（prepare_page、run_snitch_owner、discovery_of）、步骤（RaceAction 加 run_step_set）、
终端（terminal::run_payload 与 LKM 一侧）。只有"启动 kernelsu 拿到 root"才算达成。

## 复现命令

    adb -s QV770MFGJ1 push build/native/ghostlock /data/local/tmp/ghostlock
    adb -s QV770MFGJ1 push /tmp/drive_in.bin /data/local/tmp/drive_in.bin
    adb -s QV770MFGJ1 shell 'touch /data/local/tmp/ghostlock-carrier'
    adb -s QV770MFGJ1 shell '/data/local/tmp/ghostlock --ghostlock-app-call < /data/local/tmp/drive_in.bin'

## 续（2026-10-09）：准备相位走到第 4 步，根因已查明

设备输出（逐字）：

    run-temp: prepare geometry-missing at=1 opened=0 closed=0
    run-temp: prepare call-failed at=4 opened=3 closed=0 base=0x0 errno=0

读法：at=1 被拒是因为 object_size 为 0（两尺寸除不尽，注释逐字兑现）。补成页大小之后走到 at=4，
opened=3 说明 pre_groups 一 + spray_groups 二 的子进程都开起来了。

at=4 是 PrepareStep::SprayChildren（kPrepareOrder 的第四项，at 为一基）。树里对此已写明，是本轮关键物证：

    prepare_page.hpp:239-246
      if (child.value == 0) {
        /* In the child. Its work is NOT PORTED YET ... A child with nothing to do leaves at once.
           The memory such a child is meant to hold is a later piece, and it cannot be held by a
           process that exits, which is exactly why this is written down rather than left implicit. */
        Seam::exit_child();
      }

即子进程立刻 _exit(0)，父进程随即对已退出的子进程调 open_memfd，必然失败；errno=0 是因为接缝的拒绝按值返回。
这不是环境问题，是缺一块实现。

## 待移植的那一块（真件，不是临时物）

退役树里该子进程的工作有两半（spray.hpp 与 spray.cpp）：

1. 跑碰撞搜索 kernelsnitch::context_find_collisions —— 新树已把它放在第 5 步 Snitch、进程内执行，不需要子进程承担；
2. 持有一块内存：spray_ctx.childs 与 spray_ctx.memfds，容量 (1 + MM_PARTIALS) * mm_objs_per_slab，
   回收走 close_ctx_memfds 与 free_ctx_storage。

第二半尚未移植，这就是第 4 步过不去的原因。移植它属于真件补全，按旧树的形状照搬。

## 续（2026-10-09）：NoPokes 的归位，以及一次被我自己的混读

设备在补了 object_size 之后停在准备的第 4 步枚举值（Snitch，即搜索相），报 call-failed；
诊断行说明真正的守卫是 NoPokes（名字 plan-missing），读数：rounds=64 wanted=16384 region 已映射 probes=0。
NoPokes 出自 run_poke_protocol，在走查之前，所以桶规则换桩对它没有影响；这一点由两次设备运行确认。

沿革（写给后来者，免得重走）：我先判为「喷雾子进程的工作未移植」，又在读到退役树 clone_leak_child 里
child == 0 立刻 exit(0) 之后推翻自己，理由是「旧树子进程也立刻退出」。那次推翻是错的，因为我把两个不同的
子进程当成了一个：clone_leak_child 是跑碰撞搜索的 leak 子进程，而喷雾子进程另有其工作。
两条线索因此并不矛盾：leak 子进程立刻退出是正常的，喷雾子进程要持有并锤击共享词，而后者尚未移植。

下一步是照退役树 spray.cpp 里 spray_pids 之后那段，把锤词补上。

## 续（2026-10-09）：准备十二步走完，与探针那次通过记录同阶

设备输出（逐字）：

    run-temp: snitch setup ok
    run-temp: region ok, total 12584 bytes
    run-temp: proofs bucket=5 second=0xffff80000014d000 (observed, not chosen)
    run-temp: prepare ok at=11 opened=6 closed=7 base=0xffff800000100000 errno=0
    run-temp: search ok rounds=9 wanted=2048 region=0x7ea2e25000 probes=1 found=1
    EXIT=0

与 snitch-join-20261008-pass.md 的同一台机记录逐项比：status ok 对 ok，at 11 对 11，opened/closed 6/7
对 6/7，wanted 2048 对 2048，rounds 9 对 8，found 有对有。base 值不同（0xffff800000100000 对
0xf00000000000000），两者都是合法地址，判据是形状与阶数而不是同一个值。

## 本轮的关键发现：poke 预算随核数 8 倍化

want = table_size 乘 collisions 乘 kPokeMultiple，而 table_size = round_pow2(cpu_count * 256)。
本机 8 核 ⇒ 表尺寸 2048 ⇒ want 16384（设备读数），而探针手写 256 ⇒ want 2048。
把表尺寸钉到探针那次通过的值之后，wanted 立刻回到 2048，搜索出碰撞并找到地址。
这就是 NoPokes 的全部原因，不是环境问题，也不是缺件。

## 尚为临时物的两件（与入口同批删）

1. StubBuckets：接受一切候选的桶规则，让走查在开发路径上可达；正式路径用真实规则。
2. setup.futex_table_size 的钉值 256：随探针那次通过读数，不随核数；正式路径由 profile 与核数决定。

## 续（2026-10-09）：`discovery ok=1` 之后的下一步，逐件指名

本轮之前已在真机验证并提交的链路（`217bbb04` 及之前）：文档 → 会话帧 → 闸门 → 载体 → 窗口 → 接缝 →
载荷偏移 → 语料（setup/region/证物）→ 准备十二步 → 戳 → 走查 → 发现。
最后一行是 `run-temp: discovery ok=1 mm_struct=0xffff800000100000 init_task=0x0 task=0x0 kaslr=0x0`，
其中后三个为零是既定形状（见 `leak/snitch_discovery.hpp` 的文件注释），不是缺陷。

### 剩下的四件（按依赖顺序）

1. **43499 的 `AccessMiscParameters` 派生**。抽象类只有一个纯虚 `name()`（`contract/kernel_access.hpp:135`），
   现成的派生只有 `NoMiscParameters`（返回 "none"）。43499 的那个要带 `route` 与 `offset`，并自检；
   不认识的集合一律拒绝，不读作缺省。`Access` 落字时调 `land_word(address, value, params->offset)`，
   并以 `params->usable_for_access()` 为准入。
2. **三个字段地址的生产者**。`StepTargets` 的三个字段是 `uintptr_t` 绝对地址（`steps/action.hpp:30`），
   而退役树是由 `addresses.data_alias(profile::selinux_enforcing())` 得到的，即 profile 给偏移、别名器加基址。
   新树里没有这个别名器；偏移键只有两个：`backend.cve_2026_43499.abi.offset.selinux_blob_sizes` 与
   `backend.cve_2026_43499.abi.offset.selinux_enforcing`。
3. **步骤**：`write_readiness(TargetState{mm_struct/page_base/fake_task/fake_lock})` → `StepTargets` →
   `request_for` → `write_layout` → `arm_for` → `RaceAction` 与 `run_step_set`。除第 1、2 件外，件件已在树里。
4. **终端**：`terminal::run_payload` 与 LKM/模块加载。**只有这一步才是「启动 kernelsu 拿到 root」**。

### 临时物清单（与入口同批删）

载体的 dev 路径、payload 的 dev 路径、五个载荷偏移、桩桶 `StubBuckets`（现认 `address_table` 两个证物地址）、
`snitch_inputs.possible_cpus = 1U`（表尺寸随核数 8 倍化，按探针通过读数钉住）。全部登记在
`docs/analysis/device-probe-43284-checklist.md`。

## 更正（2026-10-09）：第 ① 件早已就绪

本文件上一节把 43499 的 access 参数集列为待写，**这是错的**。
`core/src/backend/cve_2026_43499/access_params.hpp` 已存在且完整（48 行）：
`AccessParams` 有 `route`、`offset`、`offset_set`（零是合法偏移，故用标志位区分未给），
`name()` 返回 `cve_2026_43499`，并带 `usable_for_initialize()` 与 `usable_for_access()` 两个自检。
文件头还记着用户的规则：一个 backend 一个 KernelAccess，route 是参数而不是独立对象，
否则 route 就成了装配轴；route 的生命周期是 initialize 到 shutdown。

**误判的因**：第 244 轮那次 `grep` 末尾带了 `head -8`，该文件落在截断之外，
我把「没看到」当成了「不存在」。记在此处，免得后人重走。

## 续（2026-10-09）：单序列接线 —— 两个序列的重叠点，以及我先前为何重复搜索

树里有两个序列，它们**只在一步重叠**：

- `leak/snitch_owner.hpp:130` —— 形状 → 区域 → 戳 → 走查 → 装配；**它做的是搜索**。
- `memory/prepare_page.hpp:103` —— 十二步（Cleanup/Contexts/Pre/SprayChildren/Snitch/Post/
  KillPartials/LeakLoop/Shaping/Pin/Reclaim/Align），**其中第 5 步 `Snitch` 就是搜索本身**。

重叠处正是那一步。而 `prepare_page` 把 snitch **当参数收**（`prepare_page(inputs, geometry,
patience, snitch)`），其注释写着：Running the whole search once. **Starting twice keeps the first
answer**，a preparation that asked for a second search would be asking for a second cohort。

**正解**：由 `prepare_page` 跑十二步（搜索在其中发生一次），对象与页面基址都从**同一只 policy**
取；不要再单独跑 Owner。先前我把两条都接上，设备立刻报 `owner plan-missing`，而紧跟其前的
`prepare_page` 那次搜索是成功的 —— 那就是重复搜索的确凿症状。

`TargetState` 的四个字段因此有了确定的来源：`mm_struct` 取 policy 的 found（过 `report_discovery`
取规范形），`page_base` 取十二步的 `base`，`fake_task`/`fake_lock` 取已登记的声明式偏移。
`page_base` 只能来自准备，因为它的注释写的是「the kernel page **the preparation made ready**」。

## 续（2026-10-09）：真接缝下的 refused，及其原语层面的原因

把默认的桩接缝（`FutexSeam`，其 `lock_pi` 是 `(void)word; return 0;`）换成 `LinuxFutexSeam` 之后，
设备上第 1 步由 ok=1 变成 ok=0，`race=refused`，SELinux 与 uid 前后不变。

原因在原语层：`RaceAction` 调 `race_.prepare(target, target)`，把同一个地址同时当链字与目标字；
而 `FUTEX_WAIT_REQUEUE_PI` 要求二者不同，内核对同字返回 EINVAL。探针 `requeue_probe.cpp` 用的是
两个各自独立的字（`g_chain` 与 `g_target`），并且把等待者放在**另一个线程**里，注释写着
Parks here until the owner requeues it onto the target word。

还有一层更深的事实，写在同处以免后人重走：这两个字是**进程本地内存**。用它们可以让竞态机制真的跑起来，
但落笔只会落在本地内存上，改不了任何内核字段。要改内核字段，目标字必须是内核里的**真 futex 字**，
且真的有人在其上等待——这正是该原语的前提，也是 profile 里那三个偏移最终必须变成真地址的原因。

**因此**：桩接缝上那一次 `steps ok=1` 是空的，不能算作落笔证据；真接缝下才是判据。

## 续（2026-10-09）：单线程三连 vs 探针的两线程握手

`PiRace::run` 的全文是三步，且都在**同一个线程**里：

    pi_race.hpp:113  Seam::lock_pi(target_)          本线程持有 target
    pi_race.hpp:117  Seam::wait_requeue_pi(chain, target)  本线程停在 chain 上要求 requeue 到 target
    pi_race.hpp:121  Seam::cmp_requeue_pi(chain, target)   本线程驱动 requeue

内核对「已持有 target 的线程再对其 WAIT_REQUEUE_PI」是拒绝的，所以设备那句 refused 是原语给的
诚实回答，不是接线错误。探针 `requeue_probe.cpp` 之所以能通，是因为它把等待者放在**另一个线程**，
注释写着 Parks here until the owner requeues it onto the target word。

结论：要让真接缝下的竞态成立，角色必须拆开——一侧停在 chain 上等待 requeue，另一侧持有 target 并驱动。
单线程三连跑不出成功，这一点由设备与内核语义双向确认。

## 续（2026-10-09）：握手在真机上成立，以及那两道墙

把目标字从内核地址改成进程本地字之后，设备给出：

    run-temp: handshake owned=0 requeued=1 released=0 waiter=0 gave_back=0
    run-temp: handshake ok (a count of one is a waiter that was really there)

`requeued=1` 是内核说真有一个等待者被搬走了，即 PI-futex 的 lock -> park -> requeue -> handoff
全链在本进程内成立。这用的是 requeue_probe 的握手：owner 先 lock target，waiter 另起线程停在 chain 上
要求 requeue 到 target，owner 用 cmp_requeue_pi 驱动并 unlock 把锁递出，得到它的线程负责释放。

两道墙，都已由设备证明：

1. 单线程三连不行。PiRace::run 在同一个线程里 lock_pi、wait_requeue_pi、cmp_requeue_pi，
   而内核对已持有 target 的线程再对其 WAIT_REQUEUE_PI 是拒绝的。
2. 内核地址不行。目标立刻用 step_targets 里的 selinux 地址时，四个调用全返回 -1：
   用户进程不能对内核地址做 FUTEX_LOCK_PI。

因此 `steps ... race=refused` 有两重原因，都已定位。要真落笔，还需内核侧的参与者与内核侧的字；
本地字只能证明机制本身。
