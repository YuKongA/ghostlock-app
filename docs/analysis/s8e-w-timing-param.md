# S8e 证据：W 阶段 attempts/settle 参数化（2026-10-21）

## 改动

`Glk43499Config`（既有字段 + 两个新字段）：

```text
w1_attempts=15
w1_settle_us=100000
w1_scratch_repair_attempts=3
w2_attempts=15
w2_settle_us=100000
w3_chain_rounds=3
w3_attempts=6
w3_settle_us=50000
w3_backoff_us=100000   ← 新增
w3_dir_attempts=4      ← 新增
```

`main.c` 替换：

- W1 attempts/settle/scratch repair count + settle。
- W2 attempts/settle + 日志。
- W3 attempts/settle/backoff/dir attempts。
- W3 chain rounds + park settle。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
flatten S1：base=19 cur=19 args_diff=16 PASS
llvm-nm 43499 b/B = 0
```

## 未完成

- `retry_write_stage` 内部失败 fallback 仍为 100000/50000；
- post-root 的 100000/500000 等待仍是调用点常量；
- perf sample period 5000 仍为局部常量。

## 文档

- [S8d](s8d-timing-param.md)
- [S8c](s8c-transport-param-split.md)
