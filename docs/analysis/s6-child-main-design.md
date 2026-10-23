# S6 设计：child_main 拆分（2026-10-21）

> 从属 [split-large-functions](../plan/split-large-functions.md) S6。目标：108 行/23 分支 → 小函数；不改 fd close、命令语义、fork/exec 顺序。

## 拆分

```c
static uintptr_t child_init(struct child_pipes *p);
static void child_cmd_C(struct child_pipes *p);
static void child_cmd_F(struct child_pipes *p);
static void child_cmd_M(struct child_pipes *p);
static _Noreturn void child_cmd_P(struct child_pipes *p);
static int  child_handle_cmd(struct child_pipes *p, char cmd); /* 0 continue, 1 stop */
static void child_command_loop(struct child_pipes *p);
static _Noreturn void child_launch_root(struct child_pipes *p);
```

### child_init

1. close task_r/cmd_w/uid_r
2. setpgid(0,0)
3. fcntl(uid_w, FD_CLOEXEC)
4. prctl(PR_SET_NAME,...)
5. perf_find_task 两次一致性检查
6. write task_w my_task；close task_w
7. 返回 my_task（0 失败）

### child_handle_cmd

```text
C → uid write
F → pipe/fork/finit_module probe/waitpid/read/write
M → /proc/self/comm len+first byte
P → close cmd_r/uid_w；park
G/X → return 1（stop）
其他 → return 0
```

### child_command_loop

```text
while (read(cmd_r,&cmd,1)==1)
  if (child_handle_cmd(p,cmd)) break;
close(cmd_r);
```

### child_launch_root

1. getuid != 0 → close uid_w；_exit(1)
2. fd 3..1023 设 FD_CLOEXEC
3. fork worker：
   - child setsid；execl("/system/bin/sh","sh",root_script)；_exit(1)
4. worker < 0 → warning、close uid_w、park
5. close uid_w；park

## child_main

```c
uintptr_t task = child_init(p);
if (!task) _exit(1);
child_command_loop(p);
child_launch_root(p);
```

## 所有权 / 顺序

| fd | close 点 |
|---|---|
| task_r/cmd_w/uid_r | child_init 开头 |
| task_w | child_init 写完后 |
| cmd_r | command_loop 结束或 P |
| uid_w | P 或 launch_root |

- P/G/X 的 close 行为保持。
- root shell fork 前把所有 fd 3..1023 设 CLOEXEC。
- worker setsid/execl 顺序不变。
- child_main 不返回（launch_root park）。

## 风险

- `child_cmd_P` / `child_launch_root` 是 noreturn；不能返回后继续 loop。
- `child_handle_cmd` 的 G/X 返回 stop，其他命令返回 continue。
- `child_init` 失败 close task_w 后 _exit(1)，与当前一致。
- `F` probe 的 fork/pipe/waitpid 顺序不变。
- `M` comm 读取/去换行/report 计算不变。

## 门禁

```text
build 0 warnings
python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent child_main \
  --helper child_init --helper child_cmd_C --helper child_cmd_F \
  --helper child_cmd_M --helper child_cmd_P --helper child_handle_cmd \
  --helper child_command_loop --helper child_launch_root
# COUNT/ORDER PASS；args_diff 仅报告
cmp_disasm --important-only：无新增 COUNT/ORDER FAIL
llvm-nm b/B=0
```
