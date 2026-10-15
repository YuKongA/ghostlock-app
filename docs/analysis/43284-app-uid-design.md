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
