# 43499 · 根因证实（三步逐字读数）+ 两条修法（round 252）✓✓✓

## 证实（✓，一次跑内的三步读数 ✓）

```
w1: value=0xffff800000010100 mode=1 ✓   ⇒ step result **won** ✓
w2: value=0xffffff802abfd588 mode=2 ✓   ⇒ step result **won** ✓
w3: value=**0x0** ✗            mode=1     ⇒ step result **route-config-missing:value** ✗
⇒ ⇒ 前两步值与模式都合理 ✓；**w3 的写值是 0** ✗ ⇒ 与 `Zero` 模式一致 ✓，却被校验器当成"缺失" ✗。
```

## 两条修法（都小 ✓，但取向不同 ⇒ 需你定 ✓）

| # | 修法 | 影响面 | 判据 |
| --- | --- | --- | --- |
| ① | `needed()` 对 `value` 放宽：**`Zero` 模式下允许 0** ✓ | 只动校验语义 ✓ | `w3 => won` ✓ ⇒ `uid=0` ✓ |
| ② | `w3` 分支**显式给一个非零写值** ✗ | 动"写什么"✗ ⇒ 可能改变页元组 ✓ | 同上，但风险更高 ✗ |

```
倾向 ①（✓）：`Zero` 的语义就是"把目标清零" ✓ ⇒ 写值为 0 是**合法**的 ✓，并非缺失 ✗；
而 ② 会改到元组内容（= 改攻击语义 ✗）⇒ 与快照/归档的对照会失据 ✗。
```

## 若采纳 ① 的最小改动（三处 ✓）

```
a. `route_config.cpp` 的 `needed(values.value, "value", out)` ⇒ 改为"允许 0"的校验 ✓；
b. 复核同类字段：`target` ✓ / `arm_hold` 等位移类（注释已写"位移可以合法地为 0" ✓）；
c. 真机判据：`main: step result w3 => won` ✓ ⇒ 然后看 `main: victim uid=` ✓ 是否 0 ✓。
```

## 门禁（未改码 ✓）

```
check → 108 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
