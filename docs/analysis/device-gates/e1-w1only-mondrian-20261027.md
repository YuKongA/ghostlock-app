# E1 W1-only — mondrian 冷机门禁（task-58）— **NOT RUN / UNVERIFIED**

- 日期：2026-10-28（host America/Toronto）。
- 设备目标：mondrian（POCO 23013RK75C / `b4541958` / Android 16 / kernel `5.10.252-gki-gabcbe4b2bc10`；
  设备事实来自 task-58 的 full.conf/历史 probe，**本次未连接**）。
- **状态：未执行（设备离线）**。`adb devices -l` 实跑输出仅为 `List of devices attached`（无设备行）；
  E0/E4/E1 均未运行；**无 W1 写、无 reboot、无 panic、无 perf 启用**；`perf.enabled=0`。
- 预置（已实现/单测，未真机）：
  - `app/src/debug/assets/profile/43499/debug-mondrian.conf`（8,573 B；逐字段来源见文件头）；
  - loader 设备选择（A301SO 默认不变、mondrian 新增、未知/歧义 fail-closed、release 校验）；
  - placement floor `objects_per_slab=allocation/mm`（A301SO=32 不变、mondrian=34 余 128；legacy2 `support.cpp:564`）；
  - Native host `tools/tests/placement_floor_test.c` EXIT=0；Kotlin `MondrianDebugConfigTest` 6/0/0。
- 待设备恢复后按 task-58 strict 顺序执行（不得以静态结果替代）：
  1) E0 快照：fingerprint/kernel release/`perf_event_paranoid`/uid/Seccomp/`/proc/modules`+KSU/
     `getenforce`/`dumpsys user=RUNNING_UNLOCKED`/interactive/device_idle/uptime；
  2) E4（不 W1）：JNI victim helper spawn/fd/往返/execScript/延迟；
  3) E1：冷机 W1-only 一次（不 W1b/W2/W3、不 spawn victim、不 perf）→ evidence → **立即 reboot**；
     记录 reboot 前后 uptime/enforce；失败/panic 先 reboot+pstore；
  4) 回填 review §8.4/approval/v2 plan 与此文件；不 commit、不启用生产。
- 结论：**UNVERIFIED（不可判定）**；不改变 A301SO 既有行为；未 commit/push。
