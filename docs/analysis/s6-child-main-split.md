# S6 证据：child_main 拆分（2026-10-21）

> 设计：[s6-child-main-design](../analysis/s6-child-main-design.md)；技术阻断 0 后实施。

## 拆分

```text
child_init
child_cmd_C
child_cmd_F
child_cmd_M
child_cmd_P（noreturn）
child_handle_cmd
child_command_loop
child_launch_root（noreturn）
```

`child_main`：

```c
uintptr_t task = child_init(p);
if (!task) _exit(1);
child_command_loop(p);
child_launch_root(p);
```

## 保持

- child_init 开头 close task_r/cmd_w/uid_r
- task_w 写完后 close
- P 关闭 cmd_r/uid_w 后 park
- command_loop 结束 close cmd_r
- launch_root：getuid 检查、fd 3..1023 CLOEXEC、fork、setsid、execl、worker 失败 park
- F probe 的 pipe/fork/finit_module/waitpid/read 顺序
- M 的 comm 读取/去换行/report

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

important_flatten（8 helper）：
  FLAT-SUMMARY base=24 cur=24 args_diff=7
  FLAT-RESULT: PASS

args diff：syscall x4、close/fcntl、execl x2；
由 helper 栈帧/寄存器来源变化引起。

cmp_disasm --important-only：
  IMPORTANT-REMOVED child_main 为预期搬迁；flatten 已覆盖；
  其余无 COUNT/ORDER FAIL。

llvm-nm 43499 b/B = 0
```

## 文档

- [S5](s5-skb-payload-split.md)
- [拆分计划](../plan/split-large-functions.md)
- 日志：`build/analysis/flatten-s6.log`、`build/analysis/important-s6.log`
