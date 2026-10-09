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
