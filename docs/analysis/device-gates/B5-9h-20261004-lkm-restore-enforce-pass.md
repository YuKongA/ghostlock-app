# B5-9h LKM-3a：LKM 收尾恢复 SELinux enforcing — 真机 PASS

对应提交 `5f2c3da`（预检按内核 `same_magic()` 语义修正）+ LKM 新增 `restore_enforce`（默认 1）。

## 设备与前置

- A301SO，KernelSU 已加载（`su` 可用），前置状态 `getenforce` = **Enforcing**。
- 模块：`tools/lkm/ghostlock/out/ghostlock-android13-5.15.ko`（12304 B，sha256 `d557fe11…`）。

## 测试

```
insmod /data/local/tmp/ghostlock.ko permissive=1 cmd=/data/local/tmp/glk_enforce.sh
```

脚本内容：`echo during=$(getenforce) > /data/local/tmp/glk_enforce.txt`

## 结果

```
during=Permissive
[300.249999] ghostlock: selinux_state set permissive
[300.295974] ghostlock: umh exec returned 0 for cmd=/data/local/tmp/glk_enforce.sh
[300.295979] ghostlock: selinux_state restored to enforcing
```

- 模块退出后 `getenforce` = **Enforcing**（已恢复）。
- `insmod` 报 `Argument list too long` 是设计行为（`module_init` 返回 `-E2BIG` → 自卸载）。

## 语义

`UMH_WAIT_PROC` 会等命令结束，因此模块知道 KernelSU 的 `late-load` 已完成；此时把
`selinux_state` 的首字节（enforcing 标志）写回 `true`，与 43499 root script 的
`echo 1 > /sys/fs/selinux/enforce` 收尾等价（同一效果、内核侧实现）。
仅当本模块**曾**把它置为 permissive 时才恢复（`permissive=0` 时是纯 no-op）；可用 `restore_enforce=0` 关闭。
