# 43499 · 四个值与归档**逐字相等**（round 111）

## 同一个公式，四处都用它了

```
alias = 0xffffff8000000000 + offset + delta,   delta = phys_load − phys_offset

验算（对照归档真机日志 ✓）：
  init_cred ⇒ 0xffffff802abfd588   （真机 layout value 打印值也是它 ✓）
  init_task ⇒ 0xffffff802ac43400   （页内 waiter.task / pi_top_task ✓）
  root_tg   ⇒ 0xffffff802ad58ac0   （页内 task.group ✓）
```

⇒ 这四处（凭据别名 + 三个锚）现在**不是"与退役一致的推断"**，而是"与归档数值逐字符相等" ✓。

## 真机（本轮）

```
value=0xffffff802abfd588 ✓   route status=0/0 attempts=15/19 calls=1 successes=1 ✓
main: victim comm after len=15 ✗      main: victim uid=2000 ✗
```

## 归档里仍未对照的值（下一步 ✓）

| 归档值 | 用途 |
| --- | --- |
| `slide_logger=ffffff802ab01e28` | 泄漏侧锚点（snitch 用） |
| `bootid_data=ffffff802adc6819` / `sysctl_bootid=ffffff802adc6819` | 同上 |
| `p0 profile … delta=0000000028000000` | 已用作 delta ✓（已对照 ✓） |

⇒ 再往下就是**页面之外**的东西（stamp 的几何已核 ✓、socket/选项已核 ✓、拷贝落地 ✓、暖机 ✓、CPU ✓）⇒
  若仍不落，下一步应转向"**内核侧观测**"（若设备允许 ✓）或换一支 route（`select_stack` / `tcp_zerocopy` ✓）。
