# 43499 · `select_stack`：fd_set 组装（纯）已落地（round 173）✓

## 落地（两个纯函数 ✓，无 syscall ✓）

```cpp
bool select_stack_put_word(fd_set *set, std::int32_t word_index, std::uint64_t value);   // 词位越界 ⇒ false ✓
const char *select_stack_build_fdsets(const SelectStackConfig &, fd_set &in, fd_set &out, fd_set &ex);
  ⇒ 三组先 FD_ZERO ✓；按词表逐词找 (组, 词位) ✓；**超出三组覆盖范围 ⇒ 跳过** ✓（与快照一致 ✓）；
  ⇒ 组内越界 ⇒ 具名拒绝（select-stack-in/out/ex-set-overflow ✓）。
```

## 测试量出的语义（重要 ✓）

```
waiter_shift = -2 ✓ ⇒ **waiter 词 w 落到 fd_set 的词位 w - 2** ✓（测试用"词 3 ⇒ 词位 1"钉住 ✓）
⇒ 这正是 shift 的意义所在 ✓；下一批接 pselect 时，`NFDS` 的推导依赖它 ✓。
```

## 判据（本轮 ✓）

```
build/host/select_stack_route_test ⇒ 通过 ✓（含 3 条新断言 + 1 条"跳过"断言 ✓）  TEST_RC=0 ✓
check → RC=0，108 PASS ✓；android → RC=0，0 警 ✓；lint → 0 ✓
```

## 本轮自己踩到并修掉的两处（登记 ✓）

1. **定义顺序**：新函数用了后面才定义的 `select_stack_compact_words` ⇒ 编译错 ✗ ⇒ 加前置声明 ✓；
2. **测试期望忘了 shift** ✗：以为"词 3 落词位 3" ⇒ 实际是词位 1 ✓ ⇒ 修期望而非改实现 ✓（实现与快照一致 ✓）。

## 移植进度

| 步骤 | 状态 |
| --- | --- |
| 1-2. route 核心 + 词号数据化 + 偏移换算 | ✓ |
| 3a. **fd_set 组装（纯 ✓）** | ✓ 本轮 |
| 3b. 接 `pselect` + 三演员 + fd/pipe 生命周期（契约见资源清单 ✓） | ✗ 下一步 |
| 4. 接线选择器 ⇒ 从 `route-not-ported:select_stack` 变成本 route 报告 | ✗ |
| 5. 三级真机判据（多次统计 ✓） | ✗ |
