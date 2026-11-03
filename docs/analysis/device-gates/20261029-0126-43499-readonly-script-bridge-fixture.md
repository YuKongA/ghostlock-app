# 20261029-0126 43499 只读脚本桥接 fixture — PASS（非 exploit gate）

对应工作树基线：`b119085915748a940740d7ce919b5a30ed8e9346`（`2.0.12`）；验证时间：2026-10-29 01:26 America/Toronto。

## 设备与入口

| 设备 | Android/API | kernel | App 域 |
|---|---|---|---|
| AVD `emulator-5554` (`sdk_gphone64_arm64`) | 15 / 35 | `6.6.30-android15-8-gdd9c02ccfe27-ab11987101-4k` | `run-as com.ghostlock.app` |
| A301SO `adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp` | 15 / 35 | `5.15.189-android13-8-00016-g51bba4309aac-ab14546557` | `run-as com.ghostlock.app` |

每台设备分别在 App UID 私有 `files/.ghostlock_root.sh` 与 shell UID 的 `/data/local/tmp/.ghostlock_root.sh` 上运行同一无副作用 fixture。fixture 写入 `0444` 脚本，子 shell 尝试 `: > <path>`，再由 `/system/bin/sh <path>` 执行脚本；脚本只写 `fixture-ok` 标记。设备命令形状：

```sh
adb -s <serial> shell [run-as com.ghostlock.app] sh -s <<'DEVICE_SCRIPT'
p=<同域临时路径>/.ghostlock_root.sh
m=<同域临时路径>/.ghostlock-root-script-fixture
rm -f "$p" "$m"
printf '%s\n' '#!/system/bin/sh' "echo fixture-ok > $m" > "$p"
chmod 0444 "$p"
before=$(cat "$p")
if /system/bin/sh -c ": > '$p'" >/dev/null 2>&1; then trunc_rc=0; else trunc_rc=$?; fi
after=$(cat "$p")
/system/bin/sh "$p"
exec_rc=$?
marker=$(cat "$m")
printf 'TRUNCATE_EXIT=%s\nCONTENT_UNCHANGED=%s\nSCRIPT_EXEC_EXIT=%s\nMARKER=%s\n' \
  "$trunc_rc" "$([ "$before" = "$after" ] && echo yes || echo no)" "$exec_rc" "$marker"
rm -f "$p" "$m"
DEVICE_SCRIPT
```

`<serial>` 分别替换为上表序列号；App 域路径相对 `run-as` 的 package root 为 `files/...`，shell 域路径为 `/data/local/tmp/...`。

## 结果

AVD 与 A301SO 的 App UID、shell UID 四次运行均返回：

```text
TRUNCATE_EXIT=1
CONTENT_UNCHANGED=yes
SCRIPT_EXEC_EXIT=0
MARKER=fixture-ok
```

四组 fixture 文件均在清理后确认不存在。`0444` 文件在同 UID 下拒绝 `O_TRUNC` 写打开，原内容保持不变，仍可由 `/system/bin/sh` 读取执行。

## 源码对照与边界

- `app/src/main/cpp/43499glued/attack/ops.cpp:226-234`：`write_root_script()` 在 `open(O_WRONLY|O_CREAT|O_TRUNC)` 失败时记录并返回。
- `app/src/main/cpp/43499glued/session/backend/cve_2026_43499_backend.cpp:453-458`：调用后继续 `apply_iomem_cache()` 与后续 setup。
- `app/src/main/cpp/43499glued/session/victim_process.cpp:142-153`：victim fork/setsid 后对相同路径 `O_RDONLY` 打开，再执行 `/system/bin/sh <path>`。
- 本次只运行文件系统/脚本 fixture；没有启动 Glued ELF、CVE 代码或真实 exploit。未验证生产 launcher 的 runDir、最终 owner/mode/inode、实际 native handoff、App SELinux 域与 shell UserService staging。该 fixture 不构成部署或 exploit 支持门通过。
- 本次未修改 C/C++ 源文件。
