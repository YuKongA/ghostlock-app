# 43499 · W2b 完整落地（round 118）：结构补完，但写仍未落

## 真机逐字

```
main: repair prepared base=0xffff800000010000 page=0xffff80000000f180 sent=65536/0
main: repair route status=0/0 attempts=15 calls=1 successes=1     ← 第二轮 ✓ 与第一轮同形 ✓
main: step result w1 => won / w2 => won / w3 => won
main: victim comm after len=15 ✗        main: victim uid=2000 ✗
```

## 与退役的结构对照（现在**逐轮同形** ✓）

```
退役： W2（Credential 写）→ W2b（Zero + leaf=1，target = init_cred+8）→ child uid=0
本树： w2 步骤内 = 第一轮（Credential ✓）+ 第二轮（Zero + leaf=1，target = init_cred 别名+8 ✓）
       ⇒ 结构一致 ✓（两轮各自 prepare + 建页 + 发送 + 配置 + 跑）
```

## 结论（如实）

- **结构缺口已补完** ✓：W2b（第二轮修复）不再是缺失项 ✓；
- 但 **`uid=0` 仍未达成** ✗、`comm after len=15` ✗ ⇒ "写不落"这件事**不是**轮次结构造成的 ✗；
- ⇒ 也就是说：我此前把 W2b 当作"最强候选"的判断**被本轮否证** ✗（它是对的结构 ✓，
  但不是那个杠杆 ✗）。

## 现在的状态（43499）

| 项 | 状态 |
| --- | --- |
| 移植差异 | 14 处修齐 ✓ |
| 与归档逐字相等的值 | 4 个（init_cred 别名 + 三个锚 ✓） |
| 探针 | 0 ✓ |
| 轮次结构（W2 + W2b） | 与退役一致 ✓（本轮 ✓） |
| 写落地 | 仍未发生 ✗ |
| `uid=0` | **从未** ✗ |
