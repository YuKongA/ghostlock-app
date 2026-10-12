# 脚本接口与纯 Lua 重写 — 软件侧门禁（2026-10-10）

**状态**：软件侧全绿；**真机待跑**（本文下半部分给出命令与判据）。攻击路径改动按规矩：真机门禁 + 门禁记录缺一不可，本文是记录的前半。

## 一、这一批做了什么（一句话各一条）

- `main` 成为 wire 驱动的 Lua 宿主：长度前缀 framing → 有界 JSON（`script/WireDoc`）→ base64 的 Lua chunk → 沙箱（白名单 + 四预算）
- 脚本可读只读 `cfg`（点分名）与调用 surface 的 8 行：其中第 8 行 `step(handle, name)` 是本批新加的封装步骤入口
- 步骤名由 wire 声明（`backend.<id>.steps`），执行体在后端单元里：43284 五个（`patch1`/`carrier`/`hook`/`trigger`/`wait`）、43499 三个（`w1`/`w2`/`w3`）
- 工具 `pagecache_load_lkm` 的七个 phase 与脚本接口**共用同一实现**（`steps/pagecache_steps.cpp` 的块循环、hook、trigger、wait；runner 单独一个 TU）
- `procedures/pagecache_load_lkm.lua` 是工具的**纯 Lua 重写**：只走底层行，不调封装接口，用来证明底层可访问性
- 会话帧（运行时密钥）由 wire 指明文件、`main` 读入并交给 `AccessParams`；值从不进文档

## 二、软件侧证据（本轮全绿）

```
make -C core check     → RC=0，108 PASS（含 wire 读取器、procedure 装载器、pagecache 步骤、窗口阶段四个新测试）
make -C core android   → RC=0，0 error/warning
./gradlew markdownlint → LINT_RC=0
```

host 上六条路径的实测输出（fixture 在 `core/fixtures/`）：

```
干净路径（cfg + settle + handoff + close）  → calls=4 handoffs=1 steps=0 verdict none   EXIT=0
43284 步骤（合法伪造帧 + targets）          → step door=/apex/…/crash_dump64 steps=5 refused (device-absent)
43499 步骤（w1/w2/w3，数字从 wire 读）      → steps=3 refused (device-absent)
底层直调（backend + writeop）               → calls=2 steps=0 refused (device-absent)
纯 Lua 重写（六行全打到，不调接口）          → calls=6 handoffs=1 steps=0 refused (device-absent)
缺帧                                        → refused (session-frame-missing) / (session-frame-path-missing)
```

## 三、真机待跑（命令与判据）

前置：冷机、固定 CPU 对、单 route、KernelSU 未加载；`adb` 真机已连接。

1. 构建设备二进制（NDK 路径按本机解析）：`make -C core android`
2. 推二进制与脚本：`adb push build/native/ghostlock /data/local/tmp/ghostlock`、`adb push procedures/pagecache_load_lkm.lua /data/local/tmp/`
3. 造帧（由 App 的临时启动器产生，或按 `session_frame.hpp` 的冻结布局写入 `/data/local/tmp/ghostlock-sa.frame`）
4. 跑**封装步骤**一路：wire 里 `step.backend=cve_2026_43284`、`backend.cve_2026_43284.steps=[patch1,carrier,hook,trigger,wait]`、`targets.*` 与 `execution.*` 按既有真机值；期望：`step door=…` 打印出载体路径，五步依次 `Ran`，`markers.loaded` 出现；
5. 跑**纯 Lua**二路：wire 的 `code` 用 `procedures/pagecache_load_lkm.lua`；期望：六行调用全部到达（`calls=6`），不改封装接口；
6. 判据三连（沿用既有）：`su -c id` = `uid=0 context=u:r:ksu:s0`、`su -c getenforce` = `Enforcing`、`grep -c kernelsu /proc/modules` = `1`；
7. 归档：本文追加"真机结果"节（原始日志 + 退出码），单份 ≤8 KB。

## 四、未决两条（卡住"完整链"的只有它们）

- **字节型载荷**：模块 / helper 镜像不能用一个 u64 表达；现在只能按名交给宿主。决定：进文档（base64）还是继续按名（宿主解析）？
- **写入回读**：底层八行里没有 read 行；回读现在在宿主侧（工具与单元里）。决定：是否为脚本加一个只读行，还是保持"回读属宿主"。

## 五、沿革

- 工具逻辑**没有整段搬进 main**（用户裁定：太复杂）⇒ 改为后端单元 + 名字驱动；工具与接口共用实现。
- `step` 是 surface 第 8 行（原 7 行冻结）：契约表 + pin 一并更新（`surface-manifest-regen`）。
- 本批两处自我纠正：`required-param-unknown` 曾被挤出 else ⇒ 合法 `sa` 也被拒；`step-runner-missing` 兜底曾挡在 43284 路径之前 ⇒ 该路径不可达。均已修，并有对应证据。

## 六、真机结果（2026-10-10 23:2x，`QV770MFGJ1`，shell uid）

构建：`make -C core android`（NDK_LUA 22 对象 + 设备二进制）→ 推 `/data/local/tmp/ghostlock`。

```
安全路径（cfg + settle + handoff + close，无任何设备写入）
  ./ghostlock < wire-43499.framed
  → cfg fields=61 (skipped 3) payloads=2 calls=4 handoffs=1 steps=0  verdict none  EXIT=0
43499 步骤（w1/w2/w3）
  → steps=3 refused (step-attempt-missing)  EXIT=9
43284 纯 Lua 重写
  → wire refused (session-frame-missing)   EXIT=5
```

- 结论一：整套运行时在 aarch64 上成立（wire 读取、JSON、base64、沙箱、cfg 快照、surface 转发、报告）。
- 结论二：`step-attempt-missing` 是设备上跑出来的**准确**名字——原先 43499 分支写的是 `device-absent`，在设备上等于说假话，已改。
- 结论三：43284 的帧由 App 的临时启动器产生；设备上现在没有它，所以那一路停在读帧之前（未做任何写入）。

## 七、43499 `attempt` 钩子的接线规格（下一批）

钩子签名（已在单元里）：`bool (*attempt)(void *context)`，`context` 由调用方给。要接的真竞态在：

- `race/pi_race.hpp`：一次 PI 竞态尝试（`RaceOutcome` 五条出口 + `FutexSeam` 四个 futex 调用）
- `race/futex_seam_linux.hpp`：设备上的 seam；`race/pi_window.hpp`：窗口守卫（窗口内零分配/零日志/零间接调用）
- 前置状态（chain / target 两个 futex 字）来自 `memory/` 那套：leak 搜索 → arm → prepare 页；`pipeline/run_pipeline_temp.cpp` 是把它们串起来的现成参照

因此钩子的实现＝「用已准备好的两个字跑一次 `pi_race` 的尝试」，成败由 `RaceOutcome` 判；窗口守卫必须在同一次尝试内进出。

## 八、后续轮次追加的证据

- `steps/window_attempt.{hpp,cpp}`：43499 的「尝试一次」本体（两个 prepared futex 字 + 窗口守卫；判据＝内核真的搬了一个等待者），假 seam 的主机测试（`window_attempt_test`）已进 check。
- `procedures/43499_window.lua`：43499 三步的 Lua 驱动（不碰底层行），fixture `core/fixtures/wire-43499-window.framed`；主机与设备都跑到 `step-attempt-missing`（准确名字：准备好的两个字还没接上）。
- 构建纪律：`window_attempt.cpp` 依赖 `linux/futex.h` ⇒ 属 `ANDROID_ONLY_SRCS`，不进主机源列表。
- `steps/prepare_mapping.{hpp,cpp}`（计划 §三 的第一刀）：15 个准备输入 → schema 键 → 常量拒绝名（`prepare-value-missing:<输入名>`）的表驱动映射；`prepare_mapping_test` 进 check，并有与 `profile-schema.json` 的独立对拍（15/15 键存在）。
- `steps/window_runner.{hpp,cpp}`：阶段节奏 + 真尝试 + 窗口守卫粘合成执行体；缺两个字/守卫时给 `step-words-unprepared`（设备上实测过这个名字）。
- 剩余（计划 §二 的 3–7 段）：发现（`run_snitch_search`，模板化、要假的 HeapSeam/SnitchSeam/FutexBuckets 才能主机测）、区域与边界、回收/整形、武装、产出两个字。
