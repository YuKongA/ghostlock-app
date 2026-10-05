# 职能对比：我们的 `ghostlock.ko` vs 43499 的 root script

> 目的：在把自建 LKM 接入 43284 链（LKM-3）之前，明确两者各自负责什么、是否重叠、
> 以及 43284 路径该复用哪些既有逻辑。

## 1. 两者是什么

| | **43499 root script** | **我们的 `ghostlock.ko`** |
|---|---|---|
| 位置 | [terminal/root_script.cpp](src/core/terminal/root_script.cpp)（12KB 脚本，写到 `root_script_path`） | [tools/lkm/ghostlock/ghostlock.c](tools/lkm/ghostlock/ghostlock.c) |
| 执行者 | root child 交接后由 shell 执行（W2 已成功，uid 0） | 内核 `insmod` 加载（43284 链在 init/vendor_modprobe 域调用） |
| 上下文 | **userspace，root**（su/ksu 域） | **kernel context**（`module_init`） |
| 生效时长 | 进程存活期；改的是 /data、/sysfs、SELinux 策略加载 | 数毫秒；改的是内核内存（`selinux_state`），随后自卸载 |

## 2. 职能逐项对比

| 职能 | 43499 root script | 我们的 LKM | 是否重叠 |
|---|---|---|---|
| 拿到 root 能力 | **不需要**（W2 已给出 uid 0 root child） | **不提供**（它本身已是内核代码；其目的不是拿 uid 0，而是解除内核侧限制） | 互补 |
| SELinux | **修复**：`load_policy` 重新加载策略（W1 的 64 位写在相邻布尔上留下脏值）、`checkreqprot=0`、最多 10 次重试并校验 `/sys/fs/selinux/status` | **降级**：`WRITE_ONCE(*selinux_state,false)` → permissive | **方向相反**：一个是恢复策略、一个是关 SELinux；场景不同 |
| KernelSU 加载 | **自主完成**：多管理器官网包发现 ksud（KernelSU / KSU-Next-pr / ReSukiSU / SuperManager）+ 回退路径 → 解析 `uname -r` 得 KMI → `ksud late-load`；已加载则跳过 | **不内置**：只执行加载方给的 `cmd`（链上会是 `<ksud> late-load …`） | 功能等价但**粒度不同**：脚本是全自动，LKM 是「一条命令」 |
| KMI 处理 | 从 `uname -r` 解析 `androidN`+`X.Y`，交给 ksud | 无（模块自身按 KMI 构建；加载器负责 vermagic/CRC） | 不重叠 |
| 诊断/可观测 | 丰富：日志、`--dump-kernel-log` 归档（uname/cmdline/modules/dmesg/pstore/iomem）、iomem 缓存（原子 rename） | 仅 `dmesg` 四步 `pr_info` | 不重叠（脚本更全） |
| 安全模式 | 有：`/data/adb/modules/*/disable` 全部禁用后再 exec ksud | 无 | 不重叠 |
| 三星 Defex | 无 | 可选 kprobe（默认关闭） | LKM 独有 |
| 自卸载/持久化 | 脚本文件留在磁盘（不驻留进程） | `return -E2BIG` 自卸载（不驻留模块） | 各有一套 |

## 3. 结论：**互补，不重叠，不能互相替代**

- **43499 路径**：userspace 已经有 root → 脚本的价值在于「**修 SELinux + 找 ksud + 载 KernelSU**」，
  其中 SELinux 部分是**为 W1 的副作用做修复**。
- **43284 路径**：一开始在 userspace **没有 root**，靠页缓存写把代码注入到 init 域；此时缺的是
  **内核侧**能力（解除 SELinux enforcing、绕过 UMH 限制）→ 必须由我们的 LKM 提供。
  拿到 LKM 后，**userspace 侧的「找 ksud + late-load」仍然需要有人做**，办法是把 ksud 命令作为
  LKM 的 `cmd` 参数由 UMH 执行（我们的 LKM 实测 UMH 以 **uid 0 / `u:r:kernel:s0`** 运行）。

## 4. 对 LKM-3 的直接影响（设计决定）

1. **不要照搬 43499 脚本**：它的 `load_policy` 修复逻辑是为 W1 的脏写定制的；在 43284 路径上我们
   已经把 SELinux 置 permissive，再去 `load_policy` 反而可能把 enforcing 策略状态搅乱。
2. **要移植的是「ksud 发现 + KMI 解析」**：这在 43499 脚本里已写好（多管理器官网包 + 回退路径 + `uname -r` 解析），
   可以抽出一个**精简脚本**（例如 `/data/local/tmp/.ghostlock_lkm_cmd.sh`）作为 LKM 的 `cmd`：
   - 发现 ksud → 解析 KMI → `ksud late-load --kmi <KMI>` → 写 marker（`/dev/dfm0` 风格）；
   - 不包含 policy 修复、不动 `/data/adb/modules`（安全模式留作显式选项）。
3. **LKM 参数**：`permissive=1`（43284 必需）、`cmd=<精简脚本路径>`（无空格，避免 insmod 参数被拆）、
   `defex=0`（非三星）。
4. 复用 43499 的**诊断经验**：marker 文件 + `dmesg` 行 + 结果日志，便于真机判定。

## 5. 后续可选收敛

- 把「ksud 发现 + KMI 解析」从 43499 脚本抽成**共用的 root 侧逻辑**（脚本或 native helper），
  让 43499 与 43284 两条链共享同一份实现，避免两处维护。
- 若将来 43284 需要「恢复 SELinux」而不是「置 permissive」，再考虑把 policy 修复逻辑按需引入。
