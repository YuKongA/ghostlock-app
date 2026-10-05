# δ 批 LKM 请求通道（内核侧）真机验证 — **PASS**（2026-10-05）

验证对象：`tools/lkm/ghostlock/ghostlock.c` 的 `resident=1` 常驻通道（`/dev/glk` + `ioctl`），
**绕开链**直接验证内核侧语义；链内接线（δ-2）另计。

## 方法

```sh
# 1) 后台 insmod（module_init 阻塞等待 UNLOAD）
adb shell "su -c 'nohup insmod /data/local/tmp/glk-lkm.ko resident=1 >/dev/null 2>&1 &'"
# 2) 窗口内打 ioctl（工具：tools/glkctl，静态 aarch64）
adb shell 'su -c "/data/local/tmp/glkctl ping 1; /data/local/tmp/glkctl ping 2; \
  /data/local/tmp/glkctl read 0xffffff802ac43400 8; /data/local/tmp/glkctl write 0xffffff802ac43400 8; \
  /data/local/tmp/glkctl zerofail 0xdead000000000000; /data/local/tmp/glkctl unload"'
```

- 设备 A301SO / `5.15.189-android13-8-00016-…-ab14546557`；LKM 产物 22992 B，sha256 `bf4aad4e…d491e`；
- `glkctl` sha256 前缀 `1df61d2774876881`。

## 结果（原始输出）

```
crw------- 1 root root 10, 118 … /dev/glk          # 常驻窗口内设备节点存在
ping abi=1 rc=0 status=0                            # 版本匹配
ping abi=2 rc=-71 status=0                          # -EPROTO：版本不匹配 → fail-closed
read  0xffffff802ac43400 len=8 rc=0 data=0800000000000000   # 合法 direct-map 读回
write 0xffffff802ac43400 len=8 rc=0 (same-bytes)            # 幂等写回成功
zerofail 0xdead000000000000 rc=-14                          # -EFAULT：越界地址被拒
unload rc=0 status=0
ls: /dev/glk: No such file or directory             # 卸载后设备节点消失
0                                                   # /proc/modules 无 ghostlock 残留
insmod: failed to load …: Argument list too long    # return -E2BIG（既有自卸载路径，零回归）
```

**兜底超时也被实测**：跨调用间隔超过 `GLK_LKM_RESIDENT_TIMEOUT_MS=5000` 时，模块自行卸载、`/dev/glk` 消失、`/proc/modules` 无残留——即 native 未接线时不会留下驻留模块。

## 事后核验

```
$ avbcheck avb-verify --slot _a  →  ok=12 fail=0 skip=0 err=0 sig_ok=5   AVB_VERIFY=OK
```

## 覆盖情况

| 内核侧验证点 | 结果 |
|---|---|
| 设备节点在窗口内出现、卸载后消失 | ✅ |
| `PING` 版本校验（匹配通过 / 不匹配 `-EPROTO`） | ✅ |
| `READ` 合法 direct-map 地址读回 | ✅ |
| `WRITE` 幂等写回 | ✅ |
| 越界/非 direct-map 地址 `-EFAULT`（地址合法性独立于「已授权」） | ✅ |
| `UNLOAD` → 自卸载 → 无残留 | ✅ |
| 兜底超时自卸载 | ✅ |
| **链内**窗口（open/close 回调、`lkm_window` 计数、插件消费槽） | ⏳ 见 δ-2 接线与后续链内门禁 |

## 复现

```sh
# 构建工具
$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang -O2 -static \
  -o tools/glkctl/build/glkctl-aarch64 tools/glkctl/glkctl.c
adb push tools/glkctl/build/glkctl-aarch64 /data/local/tmp/glkctl
# 然后按上文 §方法 执行
```
