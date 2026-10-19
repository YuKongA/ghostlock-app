# 43284 应用 uid 全链设计（草案，待用户认可）

> 裁定（2026-10-15）：43284 必须从**我们 app 自己的 uid** 起链，不许经 Shizuku(uid=2000)。
> 判据基准：退役树 B5-9h（`6f7ded87` / `B5-9h-20261004-full-chain-lkm-pass.md`）—— 冷机、KernelSU 未加载，
> 终态 `su -c id` = uid=0(root) context=u:r:ksu:s0、`getenforce`=Enforcing、`/proc/modules` 有 kernelsu。

## 一、已取得的真机事实（本轮及前几轮实测，均有日志）

| 事实 | 值 | 出处 |
|---|---|---|
| hook 目标可读（app uid） | `/system/lib64/libc++.so` 1083168 B，open=ok | 本轮探针 |
| patch#1 目标可读（app uid） | `/apex/com.android.runtime/bin/crash_dump64` 482904 B，open=ok | 本轮探针 |
| 开发载体可读写（app uid） | `/data/local/tmp/ghostlock-carrier` 16384 B，open=ok | 本轮探针 |
| 生产载体被拒 | `/vendor/lib64/libbinderdebug.so` EACCES | 本轮探针 |
| 拼装可跑通（app uid） | open=1 / socket=1 / surface=1 / landing=1 / bringUp=true | 阶段①日志 |
| Access 线可跑通（app uid） | access=1、accessInit=true、write 写满 16 B | 阶段③日志 |
| 写不落盘的直接原因 | 回读比较失败（step-verify-failed） | 阶段③日志 |

**关键推论**：退役树那次 PASS 的顺序是「先把 `.ko` 放进载体、把 **patch#1 打进 crash_dump64**（splice helper）」，
之后对目标的页缓存写才经 helper 落地；我此前的落地写没有 helper 就位，所以写被接受而内容不落 —— 两条现象一致。

## 二、stage 划分与**每步的权限来源**（app uid）

| stage | 做什么 | 权限来源 | 依据 |
|---|---|---|---|
| 0 准备 | 读 `/data/local/tmp/ghostlock-carrier` 写入 LKM 镜像（12304 B） | app 自身文件权限（可读写 ✓） | 本轮探针 |
| 1 write | patch#1 → `crash_dump64`（页缓存写，O_RDONLY 打开目标即可） | 只读打开 + 页缓存原语（不需写权限） | 探针（可读 ✓）+ real_ops:75 注释 |
| 2 trigger | hook `libc++.so` @ 0xb70a8（跳过 bti c，打在 0xb70ac），double-fork 让 init 命中，shellcode 以 vendor_modprobe 执行 `insmod 载体` | 只读打开 + 页缓存原语 | 同上 |
| 3 LKM | kprobe 取 kallsyms_lookup_name → selinux permissive → UMH 跑 ksud late-load → 恢复 Enforcing → 自卸载 | 内核态（LKM 自身） | 退役树已证 |
| 4 wait | 轮询 marker，最后 `id` | app 自身 | 退役树已证 |

**没有任何一步需要 shell 特权**：读侧已实测可读，写侧走页缓存（不要求对目标有写权限），
特权动作（permissive / UMH / 模块加载）全部发生在 LKM 起来之后、由内核态承担。

## 三、形态（不违反裁定）

- native 只留薄原语：现有 35 个覆盖绝大部分（resolveTarget / makeSurface / makeLanding / bringUp /
  landBlock / readLandingBlock / makeAccess / accessInitialize / writeImage / writeHook / trigger / waitTerminal / moduleCheck …）；
- **stage 顺序、目标选择、载体选择、重试与收尾全在 Kotlin**（`Assembled43284`）；
- 若移植中发现某能力接口不足 ⇒ 按裁定**改接口**，不往 native 加逻辑。

## 四、开工前要核实的四件（只读勘察）

1. `ghostlock.ko` 的**源码与构建链**是否在本树（AGENTS 提到 `buildLkmImages` / `verifyLkmLedger`）；
2. ksud 发现脚本（退役树用 `/data/local/tmp/.ghostlock_lkm_cmd.sh`）是否在本树，或需重新生成；
3. shellcode（480 B）与 payload_max=2520 的来源文件；
4. 退役树 `step/chain` 的 stage 语义与 `GLK_STATUS` 字段在本树是否有对应实现（或需按薄原语重写）。

## 五、判据（门禁）

- 每个 stage 一条真机判据（含具名失败）；
- 终态对齐 B5-9h：`id` = uid=0 + `getenforce` = Enforcing + `/proc/modules` 含 kernelsu；
- 归档 `docs/analysis/device-gates/`（单份 ≤8 KB），commit 信息写明 stage 与退出码。

## 六、待用户确认

1. 本设计（尤其「载体用 /data/local/tmp 开发载体、patch#1 打进 crash_dump64」这两条）是否认可；
2. 若认可，我按下节顺序开工：先只读勘察第四节四件 → 再落 stage=write → 真机判据 → stage=trigger → …（前一 stage 未过真机不进下一 stage）。

## 七、开工前四件勘察的结果（2026-10-15 只读）

| # | 问题 | 结果 |
|---|---|---|
| 1 | ghostlock.ko 的源码与构建链 | **本树已有**：core/src/backend/cve_2026_43284/lkm/（另有 tools/lkm）；Gradle 里没找到 buildLkmImages/verifyLkmLedger 这两个任务名（需再确认构建入口） |
| 2 | ksud 发现脚本 | **不在本树**（只有 profile/key_catalog.hpp 提到 ksud）⇒ 需按裁定在 **Kotlin 侧生成**脚本、再交给 LKM 阶段执行 |
| 3 | shellcode 与 hook 载荷 | **本树已有**：core/src/backend/cve_2026_43284/embed/libcxx_blob.{hpp,cpp}（libc++ 载荷）+ steps/pagecache_steps.* + 测试 sentry_hook_test.cpp |
| 4 | stage 语义与步骤实现 | 本树有 steps/pagecache_step_runner.cpp；**退役树的 chain / crash_dump / elf_hook / hook_patch 四个步骤本树没有** ⇒ 需移植（作为薄原语；顺序与收尾按裁定放 Kotlin） |

**移植清单（按依赖顺序）**：

1. crash_dump（patch#1 的 helper 步骤：把 helper 页写进 crash_dump64 并校验）；
2. elf_hook + hook_patch（libc++ sentry 的定位与打补丁：0xb70a8 → 0xb70ac，payload 用本树已有的 libcxx_blob）；
3. chain（staged 顺序）—— 按裁定**不移植为 native 逻辑**：顺序/目标/收尾写在 Kotlin（Assembled43284），native 只出上面两个薄原语；
4. LKM 阶段沿用本树 core/src/backend/cve_2026_43284/lkm/ 的源码与既有构建入口（构建入口下一轮确认）。

### 七.补 LKM 构建入口的勘察（2026-10-15 续）

- core/src/backend/cve_2026_43284/lkm/ 里只有 module_facts.cpp 与 module_facts.hpp —— 那是「关于模块的事实」的桥，
  不是 LKM 源码本身；它被编进 core（core/CMakeLists.txt 第 71 行）；
- LKM 源码在 tools/lkm/ghostlock/ 目录下；
- Gradle 侧没有退役树的 buildLkmImages 与 verifyLkmLedger 两个任务 ⇒ LKM 的构建入口需要重建，
  并且按项目规则「构建逻辑一律 Gradle KTS」，应做成 Gradle 任务（DDK android13-5.15 构建，产出 ghostlock.ko 并核对 sha256）。

因此开工前的最后一环：把 tools/lkm/ghostlock 的构建接到 Gradle 任务上，产出 ghostlock.ko 并记账
（退役树那次是 12304 字节，sha256 以 d557fe11 开头）。这一步做完，移植的四个项目就都有落点。

## 八、载荷库与 hook 目标都做成可选（用户 2026-10-15 提问后的设计修正）

### 8.1 两组候选（都放档案，选择在 Kotlin）

| 组 | 候选 | 作用 |
|---|---|---|
| 载荷载体（要 insmod 的东西从哪来） | dirtyfrag 那四个 vendor 库 + 开发载体 /data/local/tmp/ghostlock-carrier | 把 .ko 的字节写进去，再由特权域 insmod 这个路径 |
| hook 目标（sentry 模式所在库） | /system/lib64/libc++.so、/system/lib64/libutils.so、/system/lib64/libbinder.so | 在它的 sentry 构造处打 4 字节跳转，让 init 的某次输出命中 shellcode |

### 8.2 「可用」的判定准则（每条都能在 Kotlin 里读出来）

1. **存在**（exists）；
2. **我们能只读打开**（canRead 并且真的 open 成功）—— 页缓存写的前提（写经 splice，不要求写权限，但必须能只读打开）；
3. 载体额外条件：**该路径必须被允许的域 insmod**（SELinux 只放 vendor 路径，/data/local/tmp/*.ko 会被拒）
   ⇒ 载体候选是「可被特权域 insmod 的路径」集合，而不是随便一个库；
4. hook 目标额外条件：库里**必须含目标模式**（sentry 符号或对应字节序列）⇒ 读符号表/读字节即可验证（纯读，不改任何东西）。

### 8.3 本机 app uid 实测（2026-10-15，逐条真机）

    /system/lib64/libc++.so                  exists 可读 1083168 B   ← hook 首选 ✓
    /system/lib64/libutils.so                exists 可读 118344 B    ← hook 备选 ✓
    /system/lib64/libbinder.so               exists 可读 755256 B    ← hook 备选 ✓
    /apex/.../crash_dump64                   exists 可读 482904 B    ← patch1 helper ✓
    /vendor/lib64/libbinderdebug.so          exists **拒读** EACCES   ← dirtyfrag 候选① ✗
    /vendor/lib64/libstagefrighthw.so        exists **拒读** EACCES   ← dirtyfrag 候选② ✗
    /vendor/lib64/libstagefright_aidl_bufferpool2.so  不存在 ENOENT  ← 候选③ ✗
    /vendor/lib64/libbsp_module.so                    不存在 ENOENT  ← 候选④ ✗
    /data/local/tmp/ghostlock-carrier        exists 可读 16384 B    ← 开发载体 ✓

**结论**：在我们这台设备的 app uid 下，dirtyfrag 那四个 vendor 载体**全部不可用**（两个被 SELinux 拒读、两个本机不存在），
而**开发载体可用**；hook 侧 libc++ 可用且有两条系统库备选 ⇒「libcxx 不可用」时能自动换。
因此设计**必须**把「开发载体 + LKM 放置」这条路当主路（退役树也是这么做的），vendor 载体只作为可选增强。

### 8.4 动态寻找（照上面的准则逐条试，失败具名）

Kotlin 按档案里的候选顺序，对每个候选做 8.2 的四条检查（1/2 纯 Kotlin；4 读字节或符号表），
第一个通过者即选定，并把**为什么选它、其它为什么被拒**逐条记进日志（具名，不许沉默）。
全部失败 ⇒ 具名失败（例如 carrier-none-usable）+ 回退开发载体路径；不猜、不默认。

### 8.5 补一条漏掉的关键条件：hook 目标要**尾部有空间**（用户 2026-10-15 提醒）

payload（退役树那次：shellcode 480 B，payload_max=2520）要放进目标库里，而库里唯一「不破坏正常功能」
的落点是**最后一个页面的尾部 slack**（文件长度不按页对齐时，末页多出来的那段字节）。
因此可用性判定必须再加一条：

    slack = size - floor(size / 4096) * 4096       // 末页剩余字节
    可用 ⇔ slack >= payload_len（外加对齐余量）

本机实测（尺寸取自 §8.3 的真机探针，slack 由上式算出）：

| 候选 | 尺寸 | 末页 slack | 480 B payload 是否放得下 |
|---|---|---|---|
| /system/lib64/libc++.so | 1083168 | 1824 | 放得下 ✓ |
| /system/lib64/libutils.so | 118344 | 3656 | 放得下 ✓（余量最大） |
| /system/lib64/libbinder.so | 755256 | 1592 | 放得下 ✓ |

⇒ 三个 hook 候选**都够**；其中 **libutils 余量最大**（3656），libc++ 次之（1824）。
这也说明「libcxx 不可用就换库」这件事在数据上是可行的（换 libutils 反而更宽松）。

**并且这条判定也必须由原语给出**（Kotlin 连尺寸都拿不到，因为 open 被拒）：
probeReadPath 除了「打开 + splice」还应当回**文件尺寸**（fstat），由 Kotlin 算 slack 并比较 payload 长度；
这样候选选择就完全建立在「链子自己能碰到的事实」上。

### 8.6 白名单是「经验账」，不是上游四件（用户 2026-10-15 修正）

规则：**任何有通过经验的库都进白名单**，与它出自哪个上游无关；白名单优先检测，不合格再用枚举项逐个试。
每条经验都要带**出处**（哪次通过 / 哪台设备 / 什么证据），以后每有一处新通过就追加一条，只增不减（可标注失效）。

| 角色 | 路径 | 出处（通过经验） | 本机 app uid 现状 |
|---|---|---|---|
| hook 目标 | /system/lib64/libc++.so | 退役树 B5-9h 就是打在它的 sentry 上并通过（0xb70a8 → 0xb70ac） | 实测可读，slack 1824 ✓ |
| carrier | /vendor/lib64/libbinderdebug.so | 退役树 B5-9h 的载体并通过（.ko 写在它里面，再由 vendor_modprobe insmod） | Kotlin 视角 EACCES，**待原语判定** |
| carrier | /vendor/lib64/libstagefrighthw.so | dirtyfrag 的上游通过经验 | Kotlin 视角 EACCES，待原语判定 |
| carrier | /vendor/lib64/libstagefright_aidl_bufferpool2.so | dirtyfrag 的上游通过经验 | 本机不存在 |
| carrier | /vendor/lib64/libbsp_module.so | dirtyfrag 的上游通过经验 | 本机不存在 |
| carrier（回退） | /data/local/tmp/ghostlock-carrier | 我们自己的开发载体（本机可读写可 splice） | ✓ |

**检测顺序**（按用户修正）：

1. **枚举**：/vendor/lib64（先一层；不够再算子目录）与 /system/lib64 —— 由原语 probeListDir 做（Kotlin 列目录可能被拒）；
2. **白名单优先**：上表的条目逐个跑五步判定（存在 / 原语级只读打开 / 从该 fd splice / 含目标模式 / 末页 slack 足够）；
3. **枚举项兜底**：白名单都不合格时，对其余枚举出来的 .so 逐个试同一判定；
4. **全败**：具名 carrier-none-usable，回退开发载体并由 LKM/UMH 阶段把它放到可 insmod 的路径。

**判定的两把尺子分开**：hook 目标看「含 sentry 模式 + 末页 slack」；carrier 看「可写进字节（页缓存）+ 所在路径可被允许的域 insmod」。
同一个路径可以同时被标两种角色，但判定各按各的尺子。

### 8.7 定案：hook 目标固定 libc++，carrier 保留动态（用户 2026-10-15 裁定）

**为什么 hook 不动态**：它的可用性有两条从我们的 uid **观测不到** ——
「被哪个特权进程映射」与「打上之后会不会真被执行」（后者要等链子跑一次才知道）。
既然探测不到，就别做假探测：**固定用 libc++**。

定案内容：

| 角色 | 策略 | 依据 |
|---|---|---|
| hook 目标 | **固定 /system/lib64/libc++.so**（不枚举、不换） | 退役树 B5-9h 就是用它的 sentry 打钩并通过；本机实测可读、1083168 B、末页 slack 1824 放得下 480 B payload |
| carrier | **动态**：枚举 /vendor/lib64 → 白名单优先 → 其余逐个试 → 全败回退开发载体 | 它的机械条件（可读、可 splice、可写页缓存、路径可 insmod）都能在本地判定 |
| helper（patch#1） | 固定 /apex/com.android.runtime/bin/crash_dump64 | 同样是「谁调用」观测不到，且退役树已证 |

**流程因此简化**（去掉 hook 侧的运行时判据）：

    ① 固定三件：hook 目标 = libc++.so；helper = crash_dump64；payload = 本树的 libcxx_blob + shellcode
    ② 动态一件：carrier（枚举 vendor → 白名单 → 兜底 → 回退）
    ③ 预检（只读、便宜）：libc++.so 与 crash_dump64 的存在/可读/size/slack —— 不满足就当场具名失败

**原语需求也随之收窄**：probeReadPath（判定 carrier 的机械条件 + 预检三件）与 probeListDir（枚举 vendor）两个就够；
sentry 模式扫描不必做（hook 固定，不筛）。

**经验账仍然管用**：carrier 每在一台设备上跑通，就把那条路径追加进白名单（§8.6），下次优先试。

### 8.8 纠正：/data/local/tmp 不能当 insmod 目标；「谁能写」要按域分开看（用户 2026-10-15 指出）

我此前写「全败则回退开发载体 /data/local/tmp，由 LKM/UMH 阶段把它放到可 insmod 的路径」是**错的**：
SELinux 不允许从 /data/local/tmp 加载内核模块，所以那里的文件只能当**暂存（源）**，不能当**装载路径**。

退役树的做法其实正是这么分的：

    --run-cve-2026-43284 /data/local/tmp/ghostlock.ko  /vendor/lib64/libbinderdebug.so
                             ^^^^^^^^^^^ 源（读到字节）      ^^^^^^^^^^^^^^^^^^ 装载路径（insmod 的就是它）

⇒ carrier 的两个条件必须**同时**成立：

1. **有人能把字节写进它**（页缓存写）；
2. **它的路径在允许的域里 insmod 得动**（vendor 等，/data/local/tmp 不行）。

**关键纠正：条件 1 的「人」不必须是我们进程**。链子里的写有两条路：

- 我们自己（app uid）直接只读打开 + splice —— 对 /vendor 会被 EACCES 挡；
- **helper（crash_dump64）替我们写** —— 它处在更宽的域里（crash_dump），这正是 patch#1 存在的理由
  （退役树中的 real_chain_hook_helper_splice16 就是「让 helper 去 splice」）。

因此：**vendor 路径对我们 EACCES 并不等于 carrier 不可用** —— 它是否可用取决于**helper 的域**能不能碰它，
而这一点只能在「helper 就位之后、用 helper 那条路」去实测。这也把探测的视角问题讲清了：

| 视角 | 代表 | 能回答什么 |
|---|---|---|
| Kotlin | app 进程的 File/Os | 存在性（已实测够用） |
| 我们进程的原语 | app uid 的 open/splice | 我们能不能**自己**碰它（vendor 全 EACCES） |
| **helper 域** | crash_dump64 patch#1 之后 | 链子真正用它写的那些路径可不可用 ← **carrier 的真正判词** |

**修正后的流程**：

1. 枚举 /vendor/lib64（原语 probeListDir）→ 白名单优先；
2. 对每个候选判「路径可 insmod」（按路径类别）与「我们能不能自己写」（probeReadPath，仅作参考）；
3. carrier 的最终确认放在 **patch#1 装好之后**：用 helper 那条路写一份可校验的字节进去、再回读比对；
4. 若 helper 也碰不到任何 vendor 路径 ⇒ 如实报告「此设备此前提下没有可用 carrier」，不再拿 /data/local/tmp 充当装载路径。

### 8.9 简化：vendor/system 一律视为可 insmod，不再逐条判定（用户 2026-10-15 裁定）

裁定：**路径类别可 insmod 这一条不再判定** —— vendor 与 system 下的路径一律按「可 insmod」处理。

证据（不是猜的）：退役树 B5-9h 就在本机把 /vendor/lib64/libbinderdebug.so 当装载路径
（shellcode 里 exec /vendor/bin/insmod /vendor/lib64/libbinderdebug.so）并成功起来；
而 /data/local/tmp 不行（§8.8）。所以「vendor/system 可、tmp 不可」这条边界有直接实证。

**于是 carrier 只由一条判词决定：链子能不能把字节写进去。**

    carrier 判定 = ① 枚举 /vendor/lib64（+ 必要时 system）
                 → ② 白名单优先（经验账 §8.6）
                 → ③ **用链子自己的写路径**（patch#1/helper 就位之后）写一份可校验字节并回读比对
                 → ④ 全败则如实报告：此设备此前提下无可用 carrier

**两个只读原语的定位也随之降级**：

- probeListDir：仍有用（枚举候选宇宙，白名单之外的那部分靠它发现）；
- probeReadPath：降为**参考信息**（告诉我们 app uid 自己能不能碰它），**不再是 carrier 的判词** ——
  判词在 helper 写测试（第 ③ 步）。

**因此移植顺序**：先 patch#1（helper 步骤）→ 再做 carrier 的写测试 → 再 trigger/LKM。

## 九、helper 机制读清了（退役树 steps/crash_dump.hpp，2026-10-15）

### 9.1 两半：打补丁 + 用 helper 碰文件

    patch_crash_dump(ops)：
        pad16(splicehelper) 写到 crash_dump64 + 0（逐 16 B 块，注入面 contract::FileCacheWriteOps，典型绑定=页面缓存原语）
        然后验证 offset 16 的 16 字节窗口（短读按上游语义视为 verify skipped）

    把补丁后的 crash_dump64 **exec** 起来（argv = {offset, path}，stdout 接我们的管道）：
        它就在 **crash_dump 域**里替我们 splice(2) 那一页 ⇒ 我们读回 16 字节
        （real_crash_dump_bridge()；上游 exp.c do_one_write_cbc use_helper=1）

**这就是「谁来调用什么函数」的答案**：不是我们直接碰 vendor 文件，而是**我们 fork/execv 被补丁过的 crash_dump64**，
由它在更宽的域里完成读写；我们的 app 只做「执行它 + 读它的 stdout」。

### 9.2 常量与判据（照抄退役树）

- 路径：/apex/com.android.runtime/bin/crash_dump64
- 块宽 16 B；验证窗口在 offset 16（kCrashDumpVerifyOffset = 16）
- 错误枚举：None / NotAvailable / WriteFailed / VerifyMismatch
- is_vendor_path()：(/vendor/... 或 /system/vendor/...) —— 生产载体的路径策略

### 9.3 移植清单（本树需要什么）

| 退役树的件 | 本树是否已有 | 备注 |
|---|---|---|
| embed/splicehelper_blob.hpp（内嵌 helper 字节） | **待查** | 本树已知有 embed/libcxx_blob.*（那是 hook 载荷），splicehelper 要单独确认 |
| contract::FileCacheWriteOps（注入面：write16） | **待查** | 我现有的 landBlock/readLandingBlock 是「落地对象」那条路，需对照它是不是同一注入面 |
| crash_dump 域桥（fork/execv + 管道 + splice 读回） | **需移植** | 这是新能力：本树目前没有任何「exec 一个系统二进制并读它 stdout」的原语 |
| is_vendor_path() | 需移植（很小） | 路径策略 |

**按裁定的分工**：域桥本身是「内核/进程交互」，留在 native 做薄原语（execv + 管道 + 读回）；
而「先打补丁、再 exec、再读回、失败重试」的顺序与选择**全在 Kotlin**。

### 9.4 四处待查的结果（2026-10-15 只读）

| 件 | 结果 |
|---|---|
| embed/splicehelper_blob.{cpp,hpp} | **本树已有** ✓（core/src/backend/cve_2026_43284/embed/splicehelper_blob.*） |
| 引用 splicehelper 的地方 | core/src/tools/pagecache_load_lkm.cpp、**core/src/device/43284/ops.cpp**、steps/pagecache_step_runner.cpp ⇒ 本树已有一条用到它的路 |
| contract::FileCacheWriteOps（退役树的注入面） | **本树没有** ✗ —— 注入面类型不在这里（contract/capabilities.hpp 也不存在） |

**结论与下一步**：

1. helper 的字节（splicehelper）本树有 ✓，patch#1 不需要重新生成载荷；
2. 退役树的「注入面（FileCacheWriteOps）」在本树不存在 ⇒ 本树用自己的写法：
   先看 **core/src/device/43284/ops.cpp** 是怎么用 splicehelper 的（它就在 JNI 目录里，很可能已经是设备侧的接线），
   据此决定「patch#1 写成什么形态」：若 ops_43284_device.cpp 已经提供写路径，patch#1 只需一个薄原语；否则要先补一个注入面等价物。
3. 域桥（exec crash_dump64 + 管道 + splice 读回）本树确实没有 ⇒ 仍是**需移植**的新原语。

### 9.5 移植面缩小：两个载荷都已经在边界上，patch#1 可能不需要新 native 代码（2026-10-15）

读 core/src/device/43284/ops.cpp：它已经通过 embeddedImage(role) 提供**两份真实内容**：

    role=0（kRoleSpliceHelper）  → embed::splicehelper_bytes()（padded）      = patch#1 要写的那份 helper
    role=1（kRoleLandingPayload）→ hook::build_sentry_payload(device_sentry_plan()) = 钩子载荷（含两台参数已填）

而 Kotlin 侧**已经在调** embeddedImage / hookPlan（探针里取 helper 镜像就是这个）。

### patch#1 因此可以完全用已有原语在 Kotlin 里编排：

    1) 把 door（目标）对准 /apex/com.android.runtime/bin/crash_dump64 —— 它对我们可读（实测 482904 B）✓
    2) bringUp 落地对象（沿用分步拼装那条路）
    3) 用 embeddedImage(0) 的字节按 16 B 块 landBlock 到 offset 0（逐块）
    4) 回读 offset 16 的窗口与源字节比对（上游语义：短读=verify skipped，不符=VerifyMismatch）
    5) 全部由 Kotlin 决定：目标路径、块数、重试、失败具名

**不需要**退役树的 contract::FileCacheWriteOps（那是它当年的注入面写法）；
本树用「door + 落地对象」这条既有的写路径就够 —— 与裁定一致（拼装/写目标在 Kotlin）。

### 仍然真正缺的 native 能力只有一件：**域桥**

    fork/execv 被补丁过的 crash_dump64（argv = {offset, path}），stdout 接管道，读回它 splice 出的 16 字节。
    它是进程/内核交互，本树没有任何等价物 ⇒ 按裁定做成**薄原语**（Kotlin 决定何时调、调哪个 path/offset）。

**结论**：patch#1 → 纯 Kotlin 编排（可立刻做）；域桥 → 一个新原语（下一步做）；
carrier 写测试的判词就在这两件之后（§8.9）。

### 9.6 patch#1 第一次真机：门打不开 —— 缺口被精确命名（2026-10-15）

真机逐行：

    typed: patch1 door=0                                     ← openTargetStepwise(crash_dump64) 没开成门
    typed: patch1 surface=0 / landing=0 / access=0           ← 连带
    typed: patch1 init=true refusal=                         ← 我的 Kotlin bug：没有句柄也返回 true（要修）
    typed: patch1 wrote=false refusal=image-write:image:no-access-handle helperBytes=1440
    typed: patch1 accessDestroy=true / teardown=true

两个事实：

1. **helper 字节在边界上是好的**：helperBytes=1440（= 90 块 × 16 B，padded）✓；
2. **门打不开**：对着 /apex/.../crash_dump64 这条路径，现有的 door 原语开不了它（载体那条 /data/local/tmp 路径能开 ✓）。
   最可能的原因：door 需要**可写**打开，而系统文件我们只能只读；而上游那条路（页面缓存原语）恰恰不需要写权限。

**缺口因此被精确命名（这就是要加的接口）**：

    一个「把 16 字节写进**只读打开**的文件页缓存」的原语 —— 退役树里叫 contract::FileCacheWriteOps.write16，
    它绑定的正是 crash_dump64 的 fd。这与裁定一致：内核交互留 native（薄原语），而写什么、写几次、验证与否全在 Kotlin。

**同时暴露我一个 Kotlin bug**：initializeAccessAndRemember 在 accessId=0 时仍返回 true ⇒ 要改成「无句柄即失败并具名」。

**下一批**：

1. 修那个 Kotlin bug（无句柄即失败）；
2. 加「页面缓存写」薄原语（只读 fd + 偏移 + 16 字节 → 写进页缓存；回读校验由 Kotlin 调另一个原语或同原语返回旧值）；
3. 用它重跑 patch#1（目标 crash_dump64），判据：offset 16 窗口与源字节一致。

### 9.7 那个「缺的原语」本树已有：landing::real_splice_io，还有一个用它的工具（2026-10-15）

勘察（core/src/tools/pagecache_load_lkm.cpp 的 include 与 using 段）：

    #include "backend/cve_2026_43284/embed/splicehelper_blob.hpp"    ← helper 字节（已确认本树有）
    #include "backend/cve_2026_43284/landing/real_splice_io.hpp"     ← **真正的 splice I/O = 页面缓存原语**

也就是说 §9.6 里我命名的那个缺口（「把 16 字节写进只读打开的文件页缓存」）**不需要新写内核代码** ——
本树的 landing::real_splice_io 就是它；现成的工具 tools/pagecache_load_lkm.cpp 正在用它（名字即用途：经页缓存加载 LKM）。

**因此第 2 步的正确形态不是「加一个新原语」，而是「把已有的 real_splice_io 经 JNI 暴露成薄原语」**，
由 Kotlin 决定目标/偏移/块数与验证 —— 与裁定一致（能放在 Kotlin 的都在 Kotlin，native 只留接口）。

**下一步先读 tools/pagecache_load_lkm.cpp 的主流程**：它很可能已经把「patch#1 + 写载体 + 装载」这条序列跑通过，
读清它用到的原语与参数，就知道要暴露哪几个（而不是凭想象设计）。

### 9.8 那个工具就是完整驱动器；它填的那一段参数就是缺的 DTO 字段（2026-10-15）

读 tools/pagecache_load_lkm.cpp:445-505（main 的前半）：它按阶段跑（phase_credentials → phase_module → 组装 AccessParams → 开跑），
代码注释自己写明：这一段就是「caller 的那半接口」—— 也就是**该由 Kotlin 提供**的东西；它用 params.consistent() 自检，不一致就拒绝。

**它填的字段（即 AccessParams43284 的完整清单）**：

| 字段 | 我现在的契约 | 说明 |
|---|---|---|
| sa | 有（Init43284 单独传） | 会话材料 |
| carrier / carrier_base / dev_carrier / patch_target | 有 | 载体与 patch#1 目标 |
| hook_image | 缺 | hook 目标路径（固定 libc++.so，仍应由 Kotlin 给） |
| kernel_release | 缺 | uname 的 release（Kotlin 可读设备信息） |
| kernel_preempt / kernel_modversions / kernel_module_force_unload | 缺（三个布尔） | 内核属性 |
| hook_plan 与 hook_plan_set | 缺 | sentry 计划（参数应由 Kotlin 给，native 现有 device_sentry_plan 可作默认） |
| trigger_delay_ms / wait_ms | 缺 | 时延（Kotlin 决定） |
| success_marker / failure_marker / in_flight_marker / module_marker | 缺（四条路径） | LKM 阶段的 marker |

**下一步**：把 AccessParams43284 契约组从 6 个字段扩到这份清单（约 18 个），字段名全局唯一、字节字段用 GL_ATTACK_BYTES_FIELD 带上限、LONG/BOOLEAN 照现有规则；
然后 native 侧把 DTO 翻成 AccessParams（照 9.5 的既有写法），Kotlin 侧填值（常量先内置，profile 以后再说）。
这正是裁定要的形态：工具里写死的「调用方常量」搬到 Kotlin，native 只留接口。
