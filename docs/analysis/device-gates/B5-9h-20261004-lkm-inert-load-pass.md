# B5-9h LKM-2 真机门禁：自建最小 LKM 惰性加载 — PASS

对应提交 `e9eeaa3`（源码/构建脚本/计划）+ 本记录的 `.gitignore`。设备 A301SO（`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）。

## 构建（DDK 容器，与上游同源镜像）

```
podman machine init/start            # applehv VM
podman pull ghcr.io/ylarod/ddk-min:android13-5.15
cd tools/lkm/ghostlock && ENGINE=podman LLVM_OBJCOPY=<ndk>/llvm-objcopy ./build.sh
```

产物 `out/ghostlock-android13-5.15.ko`：**11672 B**，sha256 `9f3d85f3cb8cd0d0c11c0530330c80642e5e4a6606d3b7234a0165af7ffb6b95`。
`modinfo`：`name=ghostlock`、`license=GPL`、`description=GhostLock minimal kernel-side payload`、
`vermagic=5.15.202-android13-5.15.202_r00-dirty SMP preempt mod_unload modversions aarch64`。

**vermagic 相容性**：设备 `CONFIG_MODVERSIONS=y`，内核 `same_magic()` 在有 CRC 时**只比较首个空格之后的尾部**；
本模块尾部 `SMP preempt mod_unload modversions aarch64` 与设备一致 → **无需改写即可加载**（实测确认）。
（顺带澄清：设备上原有的 `/data/local/tmp/dirtyfrag-13-5.15.ko` vermagic 与本模块**同源同串**，说明它就是同一 DDK 镜像构建的产物。）

## 真机惰性加载测试 1：`permissive=0`（不动 SELinux）

```
ghostlock: unknown parameter '/data/local/tmp/glk_lkm_ok' ignored   # insmod 引号被 shell 拆断（测试瑕疵）
ghostlock: permissive=0, SELinux untouched
ghostlock: umh exec returned -13 for cmd=/system/bin/touch          # enforcing 下 UMH 被拒（EACCES）
```

结论：模块加载成功、参数生效、**self-unload 无残留**、SELinux 保持 Enforcing；同时验证了“enforcing 下 UMH 会被拒”，
这正是链必须先置 permissive 的原因。

## 真机惰性加载测试 2：`permissive=1` + 无空格脚本路径

```
insmod /data/local/tmp/ghostlock.ko permissive=1 cmd=/data/local/tmp/glk_lkm_test.sh
[22278.770754] ghostlock: selinux_state set permissive
[22278.833839] ghostlock: umh exec returned 0 for cmd=/data/local/tmp/glk_lkm_test.sh
```

- marker 文件创建成功；脚本内 `id` 输出 `uid=0(root) gid=0(root) context=u:r:kernel:s0` → **UMH 以 root 在内核域运行**。
- `insmod` 返回失败是**设计行为**（`module_init` 返回 `-E2BIG` → 模块被卸载、不驻留）。
- `getenforce` = `Permissive`（按参数生效）。

## 复原

重启后 `getenforce` = **Enforcing**（SELinux 状态与任何页缓存改动均随重启复原）。

## 判定意义

LKM-2 通过：自建、可审计（源码在库内）、11.6KB 的最小内核侧载荷在真机可加载、可执行 root 命令、可自卸载、可复原。
下一步 LKM-3：把该 `.ko` 作为 43284 链的载体载荷，走 `write → trigger → insmod → ksud late-load` 全链真机验证。
