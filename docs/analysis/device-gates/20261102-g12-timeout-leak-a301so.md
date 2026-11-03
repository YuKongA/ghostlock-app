# 2026-11-02 G12 设备门禁：43284 生产 entry 超时 → runtime-leaked + keepalive + A11 不 kill 载荷

- 目的：用 slow fixture（`lib43284Glued.so` 替换为 shell 脚本 `fixture=ghostlock-exec-sleep-v1`，sleep 6）在**生产 43284 entry + 真实 AndroidIpSecRuntime** 上触发 `exec.timeout_ms=2000`，验证 B4b 设计的 A11 资源保全路径（此前只验证 host 侧 evidence 机制，未走 43284 entry）。
- 设备：A301SO（`QV770MFGJ1`）Android 15 / `5.15.189` / `Enforcing` / `RUNNING_UNLOCKED`。
- 产物：debug APK `b0eba4ab4ad9a735a0a6229ef8de04ce5fbf3d3090cd49acf83343356960a744`；androidTest APK `043c35662651492b188f7f217452026226580b6fbeb7665a44d006100eef38bd`；slow fixture APK `0319635c86ee0abda2cfba691037ceb1f0cde5f7002109edc29aa2590d6cb93a`；normal no-op fixture APK `10e9c3df9c625234a652f901f2eaec1e21a456ff7ddc5a03ddf737d814656137`。

## 产物构建（slow fixture）

- 在 production APK 副本中：43499 仍为 no-op fixture ELF；43284 替换为 3 行 shell 脚本（`echo fixture=ghostlock-exec-sleep-v1 ...; sleep 6`），zipalign -p -f 4 + debug 证书重签；安装 Success。
- 该 APK 只用于 G12 测试；normal fixture/production APK 下测试按 marker `assumeTrue` skip。

## 命令与结果

```text
pm clear com.ghostlock.app ; install slow fixture APK + androidTest APK → Success
am instrument -w -e class …Flow43284TimeoutKeepaliveTest …            → OK (1 test)  Time 5.860  EXIT=0
am instrument -w -e class …FlowHostProcessSmokeTest …                  → OK (4 tests) Time 7.513  EXIT=0
ps -A | grep -E "flowhost|lib43284Glued"                               → 无（exit 1）

# normal no-op fixture 回归
pm clear ; install normal fixture APK
am instrument -w -e class …Flow43499GluedHostSmokeTest …               → OK (1 test)  EXIT=0
am instrument -w -e class …Flow43284GluedHostSmokeTest …               → OK (1 test)  EXIT=0
# 恢复生产 APK
am instrument -w -e class …FlowHostProcessSmokeTest …                  → OK (4 tests) EXIT=0
```

## 判据（G12 测试断言）

| 判据 | 结果 | 说明 |
|---|---|---|
| 生产 43284 entry 在真实 IpSecRuntime 下命中超时 | PASS | `Finished(Failed reason=flow-entry:43284:timeout)`（`exec.timeout_ms=2000`） |
| runtime 不 close（A11 资源保全） | PASS | evidence `ipsec=runtime-leaked` |
| host 保活、跳过清理 | PASS | evidence `host_keepalive=true`、`host_run_dir_cleanup=skipped`；`/proc/<hostPid>` 返回后仍存在 |
| A11 不 kill 载荷 | PASS | 结果后 ≤2s 扫描 `/proc/*/cmdline` 命中含 `lib43284Glued.so` 的 payload 进程（≠host pid） |
| 测试后回收 + 新 run 新进程 | PASS | kill host 后 `/proc` 消失；payload 自然退出（sleep 6）；fresh DiagnosticOkFlow ⇒ Finished.Ok 且新 host pid |
| 生产/常规 fixture 回归 | PASS | B2 OK(4)；B4a/B4b fixture OK(1) |
| 设备无残留 | PASS | `flowhost`/`lib43284Glued` 进程扫描为空 |

## UNVERIFIED

- 真实漏洞 43284 ELF 的超时/取消语义（本轮 slow fixture 是脚本，不是漏洞载荷）；B4-real 仍待用户授权。
- Shizuku host 的 keepalive/超时路径（B3b，设备无 Shizuku）。
- keepalive host/run_dir 无自动回收 watchdog（技术债）；slow fixture 测试内已人工回收。
- `exec.timeout_ms` 属测试用直接 launcher 调用参数；UI/dispatcher 生产路径不暴露该参数（本门禁只证明 entry+host 层）。
