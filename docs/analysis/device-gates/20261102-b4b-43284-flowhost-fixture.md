# 2026-11-02 B4b-b 设备门禁：43284Glued 生产 Flow 经新进程通道（fixture no-op ELF，A301SO）

- 计划：`docs/plan/flow-process-launcher-plan.md` v5 + `annex-b4a.md`（B4b 例外指针）+ `annex-b4b.md` v2（host-local Context + A11 runtime 生命周期）
- 设备：A301SO（`QV770MFGJ1`），Android 15，kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，`Enforcing`，`RUNNING_UNLOCKED`
- 工作树：HEAD `b1190859` + 未提交新架构工作树；`versionName=2.0.12/versionCode=1342`

## 产物身份（sha256）

- 生产 debug APK：`b51c7579…`（B4a 门禁时代）→ 本批重建为 **`1999bc5227eb2e2b27f480780f6900e7323bb936ddaac3dd9f8a2403944a8603`**
- fixture APK（两 Glued ELF 替换为 `fixture=ghostlock-exec-noop-v1`、arsc Stored、重签）：`385d4fa520a8523dcda01c44e743125f3d73c1519a01577642383e520da9d0c6`
- androidTest APK：`e0adf4a02278cf571d1e4997b981851da27bb3b953e084751d40724d60617de2`
- 门禁后已**恢复生产 APK**（`1999bc52…`）并回归，见下。

## 命令与结果

```text
adb -s QV770MFGJ1 shell pm clear com.ghostlock.app            → Success
adb -s QV770MFGJ1 install -r -t /tmp/GhostLock-b4b-fixture.apk → Success
adb -s QV770MFGJ1 install -r -t <androidTest.apk>              → Success
# B4b 43284 门禁
am instrument -w -e class …Flow43284GluedHostSmokeTest …       → OK (1 test)  Time 4.038  EXIT=0
# B4a 43499 回归（HostProcessExec 重构后）
am instrument -w -e class …Flow43499GluedHostSmokeTest …       → OK (1 test)  Time 4.402  EXIT=0
# 恢复生产 APK 后 B2 回归
adb install -r -t <production.apk>                             → Success
am instrument -w -e class …FlowHostProcessSmokeTest …          → OK (4 tests)  Time 7.558  EXIT=0
```

原始输出：`INSTRUMENT-43284.txt`、`INSTRUMENT-43499-regression.txt`、`LOGCAT.txt`（同目录）。

## 判据与证据

| 判据（annex-b4b §A4） | 结果 | 证据 |
|---|---|---|
| 43284 gate allowed / Executed / `Finished.Ok` | PASS | 测试断言 |
| 生产 43284 entry 被执行（fixture marker + backend + argv） | PASS | 测试断言输出含 `fixture=ghostlock-exec-noop-v1`、`backend=43284Glued`、**`argv_count=8`**（4 组 IPsec option/value 存在） |
| `evidence["ipsec"]=="runtime-closed"`（正常生命周期） | PASS | 测试断言 |
| host 进程 ≠ app 进程；两次 run 新 host pid；`host_run_dir` 非空 | PASS | 测试断言 |
| **`:flowhost` 内真实 IpSecManager create/close** | PASS | logcat：`IpSecService Allocated SPI 1573376268` → `IpSecTransform Added Transform with Id 6` → `Removing Transform with Id 6` → `Closing port 45248`（进程 30875；第二次 run 31000 复现） |
| HostProcessExec 重构后 B4a 43499 生产 entry 回归 | PASS | `Flow43499GluedHostSmokeTest` OK (1 test) |
| fixture→生产 APK 恢复 + B2 回归 | PASS | 生产 APK 重装 Success；`FlowHostProcessSmokeTest` **OK (4 tests)** |
| host 进程回收 | PASS | 门禁后 `ps -A | grep flowhost` 为空（exit 1） |

## UNVERIFIED（不得据此标 supported）

- **真实 43284 攻击**（DFRoot KO/root handoff/Finish）未运行；属 B5/生产门禁，需用户授权。
- keepalive 路径（超时/取消 ⇒ `ipsec=runtime-leaked` + host 进程保活、跳过自杀/清理）未在设备触发；host 长期保留/回收策略待 B5。
- Shizuku flavor（B3b）：A301SO 无 Shizuku；shell `userServiceContext` 的 IpSec 可用性、`nativeLibDir`、keepalive 受 `unbind(remove=true)` 影响未验证。
- fixture no-op ELF 不消费 IPsec SA/transform 流量；真实 ELF 的资源时序属 B5。
- 超时/取消、大 stdio、profile 预算拒绝未在设备触发。

## 附注

- design-critic task-148 的唯一阻断（B4a 附录仍写“不增加 Context”）已同批修复：`annex-b4a.md:21` 增加“B4b 例外：仅 43284Glued 使用 host-local `FlowHostEnv.context`；43499 永不使用”。
- 本门禁覆盖的是「生产 entry + host-local Context + IPsec 真实创建/关闭 + 新进程 + argv」；不构成 43284 攻击 supported 结论。
