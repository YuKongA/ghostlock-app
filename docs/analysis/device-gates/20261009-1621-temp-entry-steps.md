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
