# 临时固定入口（2026-10-22）

## 目的

先弃用 `main.c` 的运行时 profile 选择，给 A301SO 固定参数，
用临时入口模拟未来 JNI 的 create/run/destroy 调用层级。

## 入口

`app/src/main/cpp/gl43499/temp_entry.c`：

```text
temp_jni_create()  -> glk43499_handle_create(fixed target/config/env)
temp_jni_run()     -> glk43499_handle_run(handle)
temp_jni_destroy() -> glk43499_handle_destroy(handle)
```

固定 release：

```text
5.15.189-android13-8-00016-g51bba4309aac-ab14546557
```

固定 home：

```text
/data/local/tmp
```

`main.c` 的 CLI main/run_with_session 用 `GLK_TEMP_ENTRY` 排除；
`write_root_script` 导出为 `glk43499_write_root_script`，供临时入口使用。

## 尚未弃用

- `temp_entry.c` 仍通过 `known_offsets` 查固定 release；
  目标最终是删除 native profile 表，由 Kotlin 传入。
- `main.c` 的 `select_offsets/offsets_json/publish/detect_soc` 仍在源码中，
  只是临时入口不调用。
- `run_exploit` 仍由 native 编排；六目标逐函数 JNI 尚未实现。

## 验证

```text
CMake build 0 warnings
important_flatten S1 19/19 PASS
43499 b/B=0
```

设备：

```text
cold full temp run：设备 reboot，未取得日志（不计 PASS）
warm W1-only：rc=0
  SELinux permissive
  W1b scratch repaired
  W1-only diagnostic complete

full retest（fixed2/fixed4，同一 binary）：
  run2：W1b 后设备 reboot（概率性）
  run3：child is root! -> exploit complete -> enforce=1 -> KernelSU ready
```

run3 原始 logcat：`/tmp/fixed-full3-logcat.log`（主机临时文件，未入库）。

## 2026-10-22 重测阻塞

尝试再次冷机时：

```text
adb reboot -> reboot: Permission denied
adb shell reboot -> reboot: Permission denied
adb shell cmd power reboot -> Can't find service: power
adb shell setprop sys.powerctl reboot -> Failed to set property
adb shell echo b > /proc/sysrq-trigger -> Permission denied
```

设备仍在线，`/proc/uptime` 约 220s，`getprop sys.boot_completed=1`；
`adb shell id` 显示 `context=?`，`getenforce=Permissive`。
这是设备/环境异常，不是本轮代码结果；需要手动断电重启后再验证。

## 设备稳定等待

设备重启后必须等待约 20 秒再执行；否则 boot 后服务/时序未稳定。

## profile-free 试验

用 `GLK_NO_KNOWN_OFFSETS` + 排除 `offsets_json.c` 构建 89,608 字节 temp。
构建成功，但 A301 上 W1-only 连续 reboot，未通过；暂不作为验证 binary。
稳定验证仍用含单条 fixed profile 的 temp。

## 下一步

1. 把固定参数从 `known_offsets` 复制成 `temp_entry.c` 的静态常量，去掉 profile 表依赖。
2. 按六个 cmp 目标拆 JNI 形状；先 inline，再非 inline。
3. 每次拆分保持冷机可验证；W1-only 作为快速回归，完整冷机作为门禁。
