# 43499/43284 · 三支夹具的收束读数：机制通、效果未现（round 242）✓✓

| 路径 | 机制读数 | 效果读数 |
| --- | --- | --- |
| 43499 multicast（canonical） | `status=0/0 calls=1 successes=1 stamp=-1/99` ✓ | `enforce=1` ✗ / `uid=2000` ✗ |
| 43499 select | `status=0/61 attempts=4 calls=4 lock=0 park=-1/110 setattr=0` ✓ | `enforce=1` ✗ |
| 43284 kernelcall（读法② ✓） | `cfg fields=69 payloads=1 calls=7 handoffs=1` ✓ EXIT=0 ✓ | `uid=2000` ✗ / `verdict none` ✗ |
| 43284 lowlevel（Lua 重写 ✓） | 同形 ✓ EXIT=0 ✓ | `uid=2000` ✗ / `verdict none` ✗ |
| 43284 steps | `patch1 => step-verify-failed/splice=0` ✗ | —— |

## 统一结论（✓，本晚最有用的一条）

```
四条路径（43499 两条 + 43284 两条）**都在机制层跑通并干净退出** ✓（各自有硬读数 ✓），
而**没有一条**改变了权限状态 ✗（`uid` 恒 2000 ✗ / `enforce` 恒 1 ✗ / `verdict none` ✗）；
⇒ ⇒ 因此**共同拦路者**是"**写/生效**"这一层 ✓ —— 而不是任何单条路径的机制 ✗。
```

## 目标条文的现状（✓，与 round 241 的清单一致 ✓）

```
· "lua 直调底层 kernelaccess"：**可执行** ✓（`kernelcall` 夹具 EXIT=0 ✓、`calls=7` ✓）；
  但条文写的是"**正常执行攻击**" ✗ ⇒ 攻击**未生效** ✗ ⇒ 该条**未完全满足** ✓；
· 43499 拿 root ✗ 同样未满足 ✓ ⇒ ⇒ **目标保持 active** ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
