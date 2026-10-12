# 43499 · `select_stack`：低编号 fd 借用账本（纯）已落地（round 174）✓

## 落地（纯 ✓，无 syscall ✓）

```cpp
struct SelectStackFdPlan { low_fds[64]; low_count; backup_fds[64]; backup_count; installed_at; };
const char *select_stack_plan_fds(const std::int32_t *selected, std::size_t count, std::int32_t nfds,
                                  SelectStackFdPlan &out);
  ⇒ 三条规矩（与快照一致 ✓）：①**安装位（nfds-1）本身不借** ✗；②同一 fd 只备份一次（去重 ✓）；
     ③升序输出（还原顺序因此固定 ✓）；④nfds <= 1 ⇒ "select-stack-nfds-too-small" ✓。
```

## 判据（本轮 ✓）

```
测试用例 {5,1,3,1,7,3} + nfds=8 ⇒ low_count = **3**（1/3/5 ✓；7 = 安装位被排除 ✓）；
  升序 ✓；installed_at = 7 ✓；nfds=1 ⇒ 具名拒绝 ✓。
build/host/select_stack_route_test ⇒ 通过 ✓  TEST_RC=0 ✓；check → 108 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```

## 一条自我观察（登记 ✓）

```
本轮与上一轮，我的**测试期望**写错两次 ✗（词位忘 shift ✗ / 安装位当低编号 ✗）；
两次都是**实现与快照一致** ✓、期望不符 ✗ ⇒ 说明"先写期望"容易带主观臆断，
下一批仍按此纪律：**期望必须能指回快照的某一行** ✓（否则先核快照再写断言 ✓）。
```

## 移植进度

| 步骤 | 状态 |
| --- | --- |
| 1-2. route 核心 + 词号数据化 + 偏移换算 | ✓ |
| 3a. fd_set 组装（纯 ✓） | ✓ |
| 3b-1. **fd 借用账本（纯 ✓）** | ✓ 本轮 |
| 3b-2. 接 `pselect` + 三演员 + 真正 close/dup2（契约见资源清单 ✓） | ✗ 下一步 |
| 4. 接线选择器 | ✗ |
| 5. 三级真机判据（多次统计 ✓） | ✗ |
