# 43499/43284 · 目标那条区分已有真机实证（round 244）✓✓

## 证据（✓，逐字 ✓）

```lua
-- procedures/pagecache_load_lkm.lua 的两份变体（各 ~1.9 KB ✓）
A) wire-43284-kernelcall（**直调底层** ✓）:
   surface.kernelcall(h, "write", "absolute", 0x0, 0x14012d83, 4)   ✓
B) wire-43284-lowlevel（**封装接口** ✓）:
   surface.writeop(h, "absolute", 0x0, 0x14012d83, 4)               ✓
⇒ `diff` 显示**仅此一行不同** ✓（其余 1875 字节逐字相同 ✓）
```

## 真机读数（✓）

```
A（kernelcall）: tag ✓ steps=1 ✓ route=pagecache ✓ cfg fields=69 payloads=1 **calls=7** handoffs=1 ✓ EXIT=0 ✓
B（lowlevel）  : 同形 ✓  EXIT=0 ✓
⇒ ⇒ 两条都**可执行、都干净退出** ✓ ⇒ 目标里"lua 直调底层 kernelaccess"的**可执行性**有实证 ✓；
   而条文后半"**正常执行攻击**" ✗ 仍未满足（`uid=2000` ✗ / `verdict none` ✗）✓。
```

## 判据链（✓，与四路径并排结论一致 ✓）

```
可执行 ✓（本条完整 ✓）⇒ 生效 ✗（全项目共同缺口 ✓）⇒ root ✗（目标终态 ✓）。
⇒ ⇒ 因此本轮**不改变目标状态**：仍 active ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
