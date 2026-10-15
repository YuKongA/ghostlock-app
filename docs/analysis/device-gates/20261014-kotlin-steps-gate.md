# 设备门禁：Kotlin 步骤引擎（J6）· 2026-10-14

## 环境与命令

设备 A301SO（5.15.189-android13-8-00016-g51bba4309aac-ab14546557，Android 15，SELinux Enforcing）。

```sh
./gradlew :app:assembleDebug
adb install -r build/app/outputs/apk/debug/GhostLock-v1.3(917)-arm64-v8a-debug.apk
adb logcat -c && adb shell am start -n com.ghostlock.app/.tempui.TempUiActivity
adb logcat -d -s GhostlockPlan:I
```

## 判据（真机原始日志，逐字）

```text
10-14 20:24:54.502 I GhostlockPlan: plan: cve_2026_43499 step=w1 outcome=Refused reason=op-not-wired:write_word calls=write_word
10-14 20:24:54.503 I GhostlockPlan: plan: cve_2026_43284 step=patch1 outcome=Refused reason=op-not-wired:resolve_target calls=resolve_target
```

## 这两行各自证明了什么

| 行 | 证明 |
|---|---|
| step=w1 ... calls=write_word | 引擎按声明顺序走到 w1、组出了那笔 write_word（地址/形状/偏移来自输入）、把 native 的具名拒绝原样带出来 |
| outcome=Refused 且**没有 w2/w3 的行** | 拒绝即停：后面的步骤一次都没发（引擎规则 1） |
| reason=op-not-wired:... | 拒绝是**具名**的，不是静默失败，也不是看起来成功 |
| 两个 backend 各一行 | 计划表按 backend 分开（43499=w1/w2/w3，43284=patch1…wait），路由正确 |

## 边界（这份证据**不**证明什么）

- 不证明任何写原语：w1 停在 op-not-wired:write_word（43499 的设备 seam 属 J3b），patch1 停在
  op-not-wired:resolve_target（43284 的门属 J4b）。
- 不证明 device seam 之后的顺序（carrier → hook → trigger → wait）：计划在第一步就停了，后面的顺序由
  StepEngineTest 的假面单测覆盖（含 hook 步骤「先算计划、再解析目标」的两调用顺序）。
- 不证明重试与冻结在真机上的行为：它们在单测里用假面验过（attempts=3 打三次、indeterminate ⇒ 冻结）。

## 本批新增的代码与测试

- core/steps/：OpsInvoker（具名事务面）、StepId/StepResult（一步的词表与结局）、StepInputs（无默认值，
  缺值即拒绝）、PlanCatalog（声明顺序 + 未知 token 具名拒绝）、StepPlans（一步 → 调用序列）、StepEngine（四条规则）。
- data/nativeaccess/JniOpsInvoker：token ↔ 线上编号的翻译（编号权威仍是 native 导出的 manifest）。
- StepEngineTest（6 例）：拒绝即停、缺前置零调用、indeterminate 冻结且不再发调用、重试只按输入、
  hook 步骤的两调用顺序与留痕、未知步名具名拒绝。
