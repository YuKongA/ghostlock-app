# S7a 证据：patch_file_cbc 拆分（2026-10-21）

> 设计：[s7a-cbc-design](../analysis/s7a-cbc-design.md)；技术阻断 0 后实施。

## 拆分

```text
cbc_io_init
cbc_setup
cbc_block
cbc_cleanup
```

`patch_file_cbc` 只保留：

```text
len 校验 → init → setup → for block → cleanup → log
```

## 关键保持

- setup 失败只返回 -1，不 close；cleanup 唯一 owner。
- cleanup 顺序 pfd → file → sk，close 后置 -1，幂等。
- bind 失败只警告，不 return。
- `compute_iv` 使用 `st->aes_key`。
- `do_one_write_cbc` 的 `st->seq++` 仍在 24B header 构造时发生。
- pread 必须 16 字节；short read 失败。
- 日志/返回码与原来一致。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

important_flatten（3 helper）：
  FLAT-SUMMARY base=10 cur=10 args_diff=9
  FLAT-RESULT: PASS

args diff：setsockopt/close x0；由 helper 栈帧/io 结构地址变化引起。

cmp_disasm --important-only：
  IMPORTANT-REMOVED patch_file_cbc（10）为预期搬迁；flatten 已覆盖；
  无新增 COUNT/ORDER FAIL。

llvm-nm 43284 b/B = 0
```

## 文档

- [S6](s6-child-main-split.md)
- [拆分计划](../plan/split-large-functions.md)
- 日志：`build/analysis/flatten-s7a.log`、`build/analysis/important-s7a.log`
