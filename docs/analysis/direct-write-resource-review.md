# 直写/DFRoot 路径资源与生命周期审查

- 日期：2026-10-19
- 范围：`native_raw_esp.cpp`、`NativeRawEsp.kt`、`RawEspWriter.kt`、`TypedBackendProbe` 的 DFRoot 分支、`native_payload_probe.cpp`
- 规则：AGENTS §5（所有权/生命周期、UAF、终结点与顺序）

## 1. 对象与所有权

| 对象 | 创建 | 所有者 | 借用者 | 终结点 | 释放者 |
|---|---|---|---|---|---|
| `RawEsp` | `NativeRawEsp.open` | JNI 返回的 handle | `RawEspWriter` | `close(handle)` | `NativeRawEsp.close` |
| socket fd | `RawEsp::open` | `RawEsp` | `writeBlock` | `close(handle)` | `close_handle` |
| file fd | `writeBlock` 内 `open` | 调用栈 | `splice` | 函数返回前 | 函数自身 |
| pipe fds | `writeBlock` 内 `pipe` | 调用栈 | `vmsplice/splice` | 函数返回前 | 函数自身 |
| `DfrootLibcxx` bytes | Kotlin 复制 | 调用栈 | raw writer | 函数返回 | GC |
| page cache 页 | 内核 | 文件 inode | raw writer | 页回收/重启 | 内核 |
| `payload_probe` mmap | child `mmap` | child | 执行 | `_exit` | 进程退出 |

## 2. UAF

- `RawEspWriter` 不缓存 fd/指针，每块重新 open/free；无 UAF。
- `NativeRawEsp.writeBlock` 的 file/pipe 在 `splice` 完成前保持有效；无 UAF。
- `native_payload_probe` 的 payload 字节在 `fork` 前分配，child 继承；parent 只在自己进程释放；child `_exit`；无跨进程 UAF。
- `hookPage`/native session 不归 raw writer 所有；raw writer 不关闭它；无 UAF。

## 3. 发现

### R1（已修复）rawHandle 异常/早退泄漏

- `dfrootRawHandle` 提升到 `hookOutcome` 外层作用域；
- 外层 `finally` 无条件 `RawEspWriter.close(dfrootRawHandle)`；
- `planGate` 失败、异常、提前返回都会关闭。

### R2（已修复）site 恢复只在 triggerCode==0

- 恢复块不再要求 `triggerCode == 0`；触发失败也恢复 site/stage；
- 仅当 `triggerCode == 0` 时轮询 `/dev/df`，避免失败路径多等 5s。

### R3（建议）部分写无回滚

- `RawEspWriter.writeImage` 失败时返回已写块数，调用方只记日志，不回滚已写块。
- DFRoot 同样无回滚；可接受，但应登记为已知行为。

### R4（建议）native_payload_probe 的 mmap 未 munmap

- child `_exit` 回收映射；parent 不映射。无实际泄漏，但建议显式 munmap 以便审查。

## 4. 终结点顺序

目标顺序：

```text
payload → site → trigger → 等 /dev/df（≤5s） → 恢复 site → 恢复 stage → close
```

当前顺序一致；但 R1/R2 的失败路径未覆盖。

## 5. 结论

- 写路径本身无 UAF；
- R1/R2 需要补齐，才能称为 AGENTS §5 完整审查通过；
- R3/R4 登记为已知/建议项。

