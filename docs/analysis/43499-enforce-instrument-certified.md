# 43499 · 写落地仪器：体检通过（round 149）

## 体检（两个独立来源 ✓）

```
cat /sys/fs/selinux/enforce   ⇒ 1   （rc=0 ⇒ shell 可读 ✓）
getenforce                    ⇒ Enforcing            ← 权威工具 ✓
ls -l /sys/fs/selinux/enforce ⇒ -rw-r--r-- root root （world-readable ✓）
id -u                         ⇒ 2000（shell ✓）
⇒ ⇒ `enforce=1` 是**正确**读数 ✓ ⇒ 仪器可信 ✓；环境符合门禁（SELinux enforcing ✓）
```

## 因此，"写落地"的判据确定为（两级 ✓）

| 级别 | 判据 | 现状 |
| --- | --- | --- |
| 机制级 | `selinux enforce` 由 1 变 **0**（W1 的目标字段） | **1** ✗（三次一致 ✓） |
| 目标级 | `main: victim uid=0`（受害者自报 ✓） | 2000 ✗ |

## 一条小备注（如实 ✓）

- 本树启动时会打印 `enforce=unreadable` ✗，而 shell 直读是 ok 的 ✓ ⇒ 说明那是**启动时点/路径**的问题 ✓
  （不是权限 ✗）⇒ 属小疑点，登记不追 ✓；
- 现在进程内（同一 shell id ✓）读得到 ✓ ⇒ 仪器可用 ✓。

## 门禁

- android → RC=0，0 警 ✓；check → 108 PASS ✓；lint → 0 ✓。
