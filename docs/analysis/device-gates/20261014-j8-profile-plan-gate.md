# 设备门禁：画像装载与计划（J8）· 2026-10-14

## 命令

```sh
./gradlew :app:assembleDebug
adb install -r build/app/outputs/apk/debug/GhostLock-v1.3(917)-arm64-v8a-debug.apk
adb logcat -c && adb shell am start -n com.ghostlock.app/.tempui.TempUiActivity
adb logcat -d -s GhostlockPlan:I
```

## 判据（真机原始日志，逐字）

```text
10-14 20:34:37.170 I GhostlockPlan: plan: profile=5.15.189-android13-8-00016-g51bba4309aac-ab14546557.conf release=5.15.189-android13-8-00016-g51bba4309aac-ab14546557 generic=false schema=3
10-14 20:34:37.172 I GhostlockPlan: plan: cve_2026_43499 declared=w1,w2,w3 route=multicast_waiter numbers=selinux_enforcing=47885704,security_hook_heads=35018304,init_task=46412800,init_cred=46126472
10-14 20:34:37.174 I GhostlockPlan: plan: cve_2026_43499 step=w1 outcome=Refused reason=precondition:w1-address calls=
10-14 20:34:37.175 I GhostlockPlan: plan: cve_2026_43499 profile=5.15.189-…conf declared=w1 ran=0 stopped=w1:Refused:precondition:w1-address phase=0 cleanup=CLEAN
10-14 20:34:37.176 I GhostlockPlan: plan: cve_2026_43284 declared=patch1,carrier,hook,trigger,wait route=none numbers=
10-14 20:34:37.176 I GhostlockPlan: plan: cve_2026_43284 step=patch1 outcome=Refused reason=precondition:patch1-path calls=
10-14 20:34:37.177 I GhostlockPlan: plan: cve_2026_43284 profile=5.15.189-…conf declared=patch1 ran=0 stopped=patch1:Refused:precondition:patch1-path phase=0 cleanup=CLEAN
```

（`profile=` 那一行是完整文件名；为行宽计此处对上表两处做了省略，原始 logcat 为完整字符串。）

## 这几行证明什么

| 行 | 证明 |
|---|---|
| profile=…-g51bba4309aac-…conf generic=false schema=3 | 应用按**这台设备**的 release 选中了精确档（不是通用档），且 schema 为 3 |
| 43499 declared=w1,w2,w3 route=multicast_waiter numbers=… | 步骤表与路由名来自画像；画像里的镜像偏移被真的读出来（4 个数字） |
| 43284 declared=patch1,carrier,hook,trigger,wait | 画像里的 pagecache_write 经 PlanCatalog 别名展开成五步 |
| reason=precondition:w1-address / patch1-path，calls= 为空 | 输入不再用占位地址：缺什么就按名拒绝，**零原生调用** |
| phase=0 cleanup=CLEAN | 终态交接读的是会话自己报的相位与清理状态 |

## 边界（这份证据**不**证明什么）

- 不证明任何写原语：两条计划都在预检处停下（地址/路径要等运行期发现与门，属 J3b/J4b）。
- 不证明 JNI 新函数面（4084 的类型化函数面当时尚未接到 Kotlin；那属 B/2/D2）。
- 不证明 plan 的内部顺序：`StepEngineTest` 以假面覆盖（含 hook 两步顺序、重试与冻结）。
