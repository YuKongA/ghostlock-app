# 计划验证：真值 vs 文档值 = 0/3（round 230）✓✓

## 设备读数（三跑，两台夹具都取到 ✓）

| 跑 | 夹具 | layout（真值 ✓） | doc（文档 ✗） | same |
| --- | --- | --- | --- | --- |
| vc1 | canonical | `0xffff800000008100` ✓ | `0x4141414141414141` ✗ | **0** ✓ |
| vc2 | canonical | `0xffff800000010100` ✓ | `0x4141…` ✗ | **0** ✓ |
| vc5 | select | `0xffff800000048100` ✓ | `0x4141…` ✗ | **0** ✓ |

⇒ ⇒ 真值**恒等于**"页基址 + 0x100" ✓（与 parity 判据里的 `candidate = base + 0x100` **同式** ✓）；
⇒ ⇒ 而它**从未**被喂给路由/阶段 ✗（`:1046` 用的是文档值 ✓）⇒ 与计划页的判断完全一致 ✓。

## 顺带的一条强旁证（vc2 ✓）

```
vc2 过了门（accepts=1 ✓）并跑到 w1 的 route ✓：
  main: route status=0/0 attempts=16 calls=1 **successes=1** stamp=-1/99 requeue=-1 waiter=-1 owner=0
⇒ canonical 这条在**机制层已有一次 success** ✓（但 `enforce` 仍 1 ✗ —— 因为写值仍是毒值 ✓）；
⇒ ⇒ 说明"写不落地"与"机制跑不跑"是**两件事** ✓，而写值这一项**直接决定**后者 ✓。
```

## 结论与请求（✓，按 AGENTS §2）

```
计划页：docs/analysis/plan-43499-write-value.md ✓（动机/影响文件/行为差异/验证计划 四要素齐全 ✓）；
本轮又补了**设备实测**（same=0 ×3 ✓）⇒ 计划已可执行 ✓；
⇒ **请认可后我落码**：两处赋值改用真值 ✓ ⇒ 真机判据：`enforce` 1→0 ✓ ⇒ `uid=0` ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
