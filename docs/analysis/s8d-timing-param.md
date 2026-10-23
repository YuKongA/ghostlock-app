# S8d 证据：fast-repair/route-poll 参数化（2026-10-21）

## 改动

`Glk43499Config` 增：

```text
select_fast_repair_us = 5000
select_route_poll_us = 5000
```

main：

- fast-repair route delay 读 `config.select_fast_repair_us`。
- fast/normal consumer sleep 读 `config.select_fast_repair_us` / `select_enter_delay_us`。
- route_done 轮询读 `config.select_route_poll_us`。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
flatten S1：base=19 cur=19 args_diff=16 PASS
llvm-nm 43499 b/B = 0
```

## 未完成

- perf sample period 5000 仍为局部常量；
- 其余 50/100/200ms 等待仍是调用点常量，后续按 config settle 字段归并。

## 文档

- [S8c](s8c-transport-param-split.md)
