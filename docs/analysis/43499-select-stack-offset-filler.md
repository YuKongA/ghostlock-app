# 43499 · `select_stack`：偏移 → 词号 换算已落地（round 171）✓

## 落地（纯函数 ✓，主机可测 ✓）

```cpp
struct SelectStackOffsets { head_word, head_next, head_prev, pc, right, left,
                            task, lock, wake_state, prio, deadline, ww_ctx };   // 全部来自文档 ✓

const char *fill_select_stack_words(const SelectStackOffsets &, SelectStackConfig &);
  ⇒ 按 8 字节一词换算 ✓；非词边界 ⇒ **具名拒绝**（如 "waiter-task-not-a-word" ✓）；
  ⇒ 先填 pi 侧、再镜像 tree 侧 ✓（顺序错会让镜像读到旧值 ✗ —— 本轮自己踩到并修掉 ✓）
```

## 判据（本轮 ✓）

```
build/host/select_stack_route_test ⇒ "select-stack route: word table and fd_set sites hold" ✓  TEST_RC=0 ✓
新增断言（都用**本机几何** ✓）：
  pc = 词 3 ✓（tree 侧镜像同值 ✓）；task = 词 6 ✓（快照 6.1 说 8 ✗）；lock = 词 7 ✓（快照说 9 ✗）；
  deadline = 9 ✓、ww_ctx = 10 ✓；非词边界（task=0x31 ✗）被具名拒绝 ✓
check → RC=0，108 PASS ✓；android → RC=0，0 警 ✓；lint → 0 ✓
```

## 移植进度（照计划 ✓）

| 步骤 | 状态 |
| --- | --- |
| 1. route 核心（词表 + fd_set 词位 ✓） | ✓（round 168-171 ✓） |
| 2. 词号**数据化** + 由文档换算 ✓ | ✓（round 169-171 ✓） |
| 3. 接 `pselect` + 三演员 + fd/pipe 生命周期 | ✗ 下一步（按 AGENTS §5 逐条列 ✓） |
| 4. 接线选择器 ⇒ 从 `route-not-ported:select_stack` 变成本 route 报告 ✓ | ✗ |
| 5. 三级真机判据（多次统计 ✓）：奇偶/窗口 ⇒ `enforce` 1→0 ⇒ `uid=0` | ✗ |
