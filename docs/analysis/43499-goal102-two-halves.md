# 43499 · 第 102 轮：最后一步的"两半"关系（只做一半会更糟）✓✓

## 一、现状（✓，第 101 轮后 ✓）

```
旧处（现行 `if (repair_target != 0U && !repair_prebuilt) {` ✓）体内依次是：
  prepare ✓ → 建页 ✓ → **stash/activate** ✓ → send ✓ → route ✓；
⇒ 而 `!repair_prebuilt` 这个守卫是**第 98 轮**加的 ✓ ⇒ 它的本意是"已预建就不再准备" ✓；
⇒ ⇒ 但**旧的块里还含着"激活 + 发送 + 路由"** ✗ ⇒ ⇒ **⇒ 一旦 `repair_prebuilt = true` ⇒ 整块被跳过** ✗✗
   ⇒ ⇒ **⇒ 修复路由也不会跑** ✗ ⇒ ⇒ **⇒ 比"不做"更糟** ✓。
```

## 二、因此必须同批（✓✓）

```
① **预建**：把「prepare + 建页」搬进主路由**之前**的预建块 ✓（⇒ 成功后 `repair_prebuilt = true;` ✓）；
② **旧处改写**：把 `activate` + send + route **移出**那个被守卫包裹的块 ✓（⇒ 改成"预建则直接激活" ✓）⇒
   ⇒ 即旧处变为 `if (repair_prebuilt) { …activate + send + route… }` ✓；
③ 出口（含早退）补 `discard` ✓（守则 §5 ✓）；④ 该次路由进入延迟取 **5000 µs** ✓（第 90 轮 ✓）。
⇒ ⇒ ⇒ **⇒ 两半在*同一批*里做完 + 跑满三门禁（含 host ✓）** ✓。
```

## 三、判据（✓）

```
真机出现归档同形：`main: fast repair payload prebuilt step=… base=0x…` ✓
               与 `main: W1b: firing prebuilt init_cred+8 repair`（w2 时 ✓）✓；
⇒ 且**不得**出现"修复路由没跑"的形态（⇒ 旧有的 `main: repair prepared base=…` 仍应出现 ✓）；
⇒ 终判据：`main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
