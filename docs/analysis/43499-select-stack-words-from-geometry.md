# 43499 · `select_stack` 的词号来源：**本机几何**（round 170）✓

## 设备侧词表（5.15 ✓，全部来自夹具已有的 `payload.*` 键 ✓）

| 夹具键 | 偏移 | waiter 词号 | 用途 |
| --- | --- | --- | --- |
| `waiter_head_word` | 0x00 | 0 | 链表头 word |
| `waiter_head_next` | 0x08 | 1 | 链表 next |
| `waiter_head_prev` | 0x10 | 2 | 链表 prev |
| `waiter_pc` | 0x18 | **3** | tree_pc / pi_pc 侧 |
| `waiter_right` | 0x20 | **4** | right（页派生值 ✓） |
| `waiter_left` | 0x28 | **5** | left（target ✓） |
| `waiter_task` | 0x30 | **6** | task 指针 |
| `waiter_lock` | 0x38 | **7** | lock 指针 |
| `waiter_wake_state` | 0x40 | 8 | wake_state |
| `waiter_prio` | 0x44 | （非词边界 ✗） | prio（与 wake_state 同词 ✓） |
| `waiter_deadline` | 0x48 | **9** | deadline |
| `waiter_ww_ctx` | 0x50 | **10** | ww_ctx |

## 与快照（6.1 ✗）的差异（处处不同 ✓）

```
快照 6.1：pc = 词 2、task = 词 8、lock = 词 9
本机 5.15：pc = 词 3、task = 词 6、lock = 词 7        ⇒ 直接搬快照数字会全错 ✗
```

## 结论（改法 ✓）

1. `SelectStackConfig` 的 11 个词号字段**保留** ✓（它们让"换内核"这件事在数据里 ✓）；
2. 但**不新增文档键** ✗：词号由**已有的** `payload.waiter_*` 键**除以 8** 得出 ✓ ✓；
3. ⇒ 移植时 main 侧只多一处"读表 → 填 config" ✓（一张 12 项的映射 ✓），代码不硬编码任何词号 ✓；
4. 判据不变 ✓：主机自测已用"换一套词号也能工作"这条钉住 ✓（round 169 ✓）。
