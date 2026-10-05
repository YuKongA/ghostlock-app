# `Ancillary*` → `Plugin*` 类型名收口 真机 43499 门禁 — **PASS**（2026-10-05）

范围：仅**类型/函数名**重命名（`AncillaryStage`→`PluginStage`、`AncillaryController`→`PluginController`、`AncillaryKind`→`PluginKind`、
`AncillaryPolicyDefaults`→`PluginPolicyDefaults`、`AncillaryPolicyFor`→`PluginPolicyFor`、`VivoAncillaryPolicies`→`VivoPluginPolicies`、
`for_each_ancillary_policy`→`for_each_plugin_policy`）；**文件名/测试 target 未改**；对外可见诊断串（`run.countermeasure`、`/countermeasures`）刻意保留。

## 构建 / 方法

- 提交 `d2a526d`；二进制 sha256 前缀 `a435cdea1131730f`；NDK 30.0.16248370，零告警。
- 设备 A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；**冷启**（`kernelsu=0`、Enforcing）；`--load-prebuilt-profile`（免 App/免解锁）。

## 结果

```
EXIT=0
handoff: root shell worker pid=16318
handoff: script open fd=3 errno=0 path=/data/local/tmp/.ghostlock_root.sh
enforce=1 (enforcing)
[+] KernelSU ready
---
1                # kernelsu 已加载
Enforcing
```

同批门禁：host 43 ok / 防火墙 `165 files, 4/4/0/0` / NDK 零告警 / lint 0。
