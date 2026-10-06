# 插件运行时接线（step 3a）真机门禁 — **PASS**（2026-10-05）

驱动方式：**adb-only 试验台**（冷启 → 43499 bootstrap 取得 root → 合成 IpSec SA + UDP 封装 socket → 生产 `--ghostlock-app-call` 路径），**不需要 App、不需要解锁屏幕**。
构建：`67c32185`（含 UML 同批更新 `00667d12`）。设备：A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，slot `_a`。

## 覆盖范围

step 3a = 插件宿主的**运行时接线**：组合根构造 `PluginHost`（只登记）→ **仅 43284** 在 bind 前 `open(WindowState::WaiterClosed)` → LKM 驻留窗口内经中性 `PluginStageSink` 派发 `POST_TERMINAL`（fail-soft）→ pipeline 之后 `close()` → 诊断**仅 `registered() > 0`** 时打印。**43499 的 `pre_terminal` 属 step 3b，未接线。**

## 结果（五条用例，退出码全为 0）

### 正例：`glk.probe` stage=post_terminal

```
lkm_window opened=1 closed=1 calls=4 abi_version=1 unload_ok=1
run.plugin host loaded=1 load_failed=0 rejected=0 called=1 hook_failed=0 skipped=0 open_rejected=0 stage_unavailable=0
[countermeasure] plugin log(1): glk.probe stage=4 va=0xffffff802ac43400 val=0x8 read=0 bytes=0 write=0 bad_rejected=1
```

`calls=4` 与 `bad_rejected=1` 证明走的是真实链：**插件 → `glk_contract_ops` → LkmProxy → `/dev/glk`**（与 δ-4 门禁同形态，但本次是**生产 app-call 路径**）。

### 负例 B：hook 失败不影响链

```
lkm_window opened=1 closed=1 calls=3 abi_version=1 unload_ok=1
run.plugin host loaded=1 load_failed=0 rejected=0 called=1 hook_failed=1 skipped=0 open_rejected=0 stage_unavailable=0
run.plugin record reason=HookFailed id=glk.probe stage=post_terminal rc=-1
[countermeasure] plugin log(1): glk.probe stage=4 va=0xdead000000000000 val=0x0 read=-5 bytes=-5 write=-1 bad_rejected=1
```

### 负例 C：stage 在当前 backend 不可用（实例级拒绝）

```
lkm_window opened=1 closed=1 calls=0 abi_version=1 unload_ok=1
run.plugin host loaded=0 load_failed=0 rejected=1 called=0 hook_failed=0 skipped=0 open_rejected=0 stage_unavailable=0
run.plugin record reason=StageUnavailableOnBackend id=glk.probe stage=pre_terminal
```

### 负例 D：制品被篡改（哈希不符）

```
lkm_window opened=1 closed=1 calls=0 abi_version=1 unload_ok=1
run.plugin host loaded=0 load_failed=1 rejected=0 called=0 hook_failed=0 skipped=1 open_rejected=0 stage_unavailable=0
run.plugin record reason=LoadFailed id=glk.probe stage=post_terminal status=HashMismatch
```

负例 C/D 均 `calls=0` 且 `loaded=0` ⇒ **未 dlopen**。

### 无插件回归（同文档去掉 `plugin` 段）

```
lkm_window opened=1 closed=1 calls=0 abi_version=1 unload_ok=1
```

`run.plugin` 行数 **0**；与正例的 stdout/stderr diff **各只有 1 行**（正例多出的即 `run.plugin host …` 与插件 stderr 日志）⇒ 无插件时**零新增字节**。

### 事后设备健康

```
avbcheck avb-verify --slot _a   →  ok=12 fail=0 skip=0 err=0   AVB_VERIFY=OK
ls /dev/glk                      →  不存在（无窗口泄漏）
grep -c kernelsu /proc/modules   →  1
grep -c ghostlock /proc/modules  →  0        （LKM 自卸载）
getenforce                       →  Enforcing
```

## 门禁过程发现（两条，均可复现）

1. **`kmi` 字段必须与 release 派生值一致**：自造文档里写 `kmi=5150`（沿用了测试值），而设备 release `5.15.189-…` 派生出的 KMI 是 **5015**（运行期 stderr 自证：`profile_resolved kmi=5015`）⇒ `lkm_policy.cpp:44-48` 判定 **`KmiFieldMismatch`**（`LkmPolicyError=5`），链在进入 LKM 前失败、`called=0`。**修法**：不手写 `kmi`，用工具 `--drop backend.cve_2026_43284 kmi` 让 schema 派生生效。
   - ⚠️ **过程教训**：本轮曾把该错误码**误判为模块 vermagic 不匹配**。反证是既有 PASS 记录 `43284-production-20261005-pass.md`——同一台设备、同一份 5.15.202 的 `helper.ko`、`EXIT=0`。**结论必须由错误码枚举 + 既有物证共同支持**，不得凭现象自造根因。
2. **试验台清理必须包含镜像标记**（扩展既有修复 ②）：只清 `/dev/df`、`/dev/dfm0` **不够**——新引入的 shell 可读标记 `/data/local/tmp/.ghostlock_lkm_ok` 会**跨运行残留**，使 `WaitResult` 误判 `LkmLoaded` → 窗口 `open()` 失败（`lkm_window opened=0`）而**链仍 `EXIT=0`**，**静默吃掉 `called`**。每次运行前须清：

```sh
su -c 'rm -f /dev/df /dev/dfm0 /dev/dfm1 /data/local/tmp/.ghostlock_lkm_ok /data/local/tmp/.ghostlock_lkm_fail'
```

## 仓内 gate 工具（本批新增）

`src/core/tests/gate_doc_tool.cpp` + `make -C src plugin-gate-doc` → `build/host-test/plugin_gate_doc`（**TOOL，不进 `NATIVE_HOST_TESTS`**）：

```sh
plugin_gate_doc --verify <doc.bin>
plugin_gate_doc --base <doc.bin> [--drop <section> <key>] [--plugin <id> <stage> <path> <sha256>] \
                [--no-plugin] [--frame <frame.bin>] [--stdin-payload] --out <file>
```

它用**生产编码器**（`glkv3::encode`/`decode_neutral`）并在写出前以 `frame_v3` + `validate_plugin_wire` **自证 native 会接受**，拒绝则非 0 退出且不写文件——本次正是靠它把 `kmi` 问题在数分钟内定位。

## 未覆盖 / 下一步

- **step 3b**：43499 的 `pre_terminal` anchor（+ `child_task` 上下文）未接线；
- **App 真实路径复跑**：本门禁走试验台（合成 SA），未走 App 的 `IpSecManager`；App 路径需在下一轮用 App 复跑（同一 Pipeline、同一链）；
- 试验台的 SA 与封装 socket 不跨重启，需重建（见 `43284-production-adb-harness.md` §5）。

## 参考

- 设计与用例：`docs/analysis/plugin-runtime-integration-design.md`（§11.5 用例、§11.6 准入状态）；
- 错误码：`src/core/backend/cve_2026_43284/lkm/lkm_policy.hpp`（`LkmPolicyError`）；
- 既有 43284 生产门禁：`docs/analysis/device-gates/43284-production-20261005-pass.md`；
- 同批 UML：`00667d12`（IPO C4e/C6b/C7b、状态机宿主生命周期、Class `PluginStageSink`、Sequence §4.4）；
- 原始日志（主机）：`/tmp/g3{pos,negb,negc,negd,noplugin}.{out,err}`。