# 新旧实现数据流图（Mermaid）· 43499

> 旧 = `ghostlock-app-ref/src/core/{main,util,fops}.c`（纯 C，`641aaf6` = mcast 首次成功）；
> 新 = 本树 `core/src/**`（C++23）。

## 一、旧实现（纯 C）

```mermaid
flowchart TD
    subgraph OS["机器 / 内核"]
        IOMEM["/proc/iomem<br/>System RAM 跨度"]
        UNAME["uname -r<br/>5.15.189-android13-8-…"]
        SOC["soc 检测"]
        PERF["perf_event kernelsnitch 泄漏"]
    end
    subgraph OLD["旧实现（纯 C · 641aaf6 · mcast 首次成功）"]
        MAIN["main.c main()"]
        OFFS["detect_soc → select_offsets<br/>validate_offsets_profile → publish_active_offsets"]
        AO["active_offsets · struct kernel_offsets"]
        CPU["init_cpu_config<br/>g_core_main=0 · g_core_consumer=1"]
        DM["apply_iomem_cache → g_direct_map_end"]
        VIC["spawn_victim<br/>fork child + pipes{cmd_w, uid_r}"]
        PFT["perf_find_task → child_task"]
        RT{"route 选择"}
        PS["do_pselect_fake_lock_route · fd_set"]
        TCPZ["do_tcp_fake_lock_route · zerocopy"]
        MC["do_kernel5_fake_lock_route<br/>MCAST_BLOCK_SOURCE"]
        MCS["kernel5_resident_start<br/>waiter/owner/consumer 三线程<br/>CMP_REQUEUE_PI + tgkill(SIGUSR1) + mr_adjust"]
        MCW["kernel5_resident_write<br/>mr_respray → mr_adjust()==0"]
        DOW["do_one_write(target, mode, leaf)"]
        PKP["prepare_kernel_page (util.c)"]
        FT["fake_task 八项<br/>usage/prio/nprio/pi_lock/pi_waiters/tg/pi_top/pi_blocked=0"]
        RL["RIGHT_OFF / LEFT_OFF 各三字<br/>= {fake_parent, 0, 0}"]
        CC["fill_profile_cred_copy<br/>CRED_COPY_OFF: usage + caps + refs(data_alias)"]
        RMRT["run_main_route_threads<br/>reset → start → run/requeue → route_done → join"]
        IDM["in_direct_map 守卫"]
        VER{"verify: child_uid == 0"}
        RETRY["retry_write_stage<br/>首轮 slab_drain / prebuild / attack / activate / verify"]
        ROOT(["uid 0 ✓"])
    end
    UNAME --> OFFS
    SOC --> OFFS
    IOMEM --> DM
    PERF --> PFT
    MAIN --> OFFS
    MAIN --> CPU
    MAIN --> DM
    MAIN --> VIC
    OFFS --> AO
    AO --> MCS
    AO --> DOW
    VIC --> PFT
    PFT --> RT
    RT --> PS
    RT --> TCPZ
    RT --> MC
    MC --> MCS
    MCS --> MCW
    MCW -->|"成功判据 mr_adjust()==0"| DOW
    DOW --> PKP
    PKP --> FT
    PKP --> RL
    PKP --> CC
    DOW --> RMRT
    DOW --> IDM
    RMRT --> VER
    PKP --> VER
    IDM --> VER
    VER -->|否| RETRY
    RETRY --> DOW
    VER -->|是| ROOT
```

## 二、新实现（本树 C++）

```mermaid
flowchart TD
    subgraph SCRIPT["脚本层"]
        LUA["Lua steps · 执行顺序权威"]
    end
    subgraph NEW["新实现（本树 C++23）"]
        MAINCPP["main.cpp step 驱动"]
        SNITCH["snitch 泄漏"]
        VSPAWN["victim spawn + perf_find_task"]
        PREP["prepare（页）"]
        PP["payload_page.cpp<br/>waiter{pc,right,left,task,lock,wake,prio,deadline,ww_ctx}<br/>fake_task 八项 + right/left 各三字"]
        WE["write_encoder.cpp arm_for<br/>preserve ⇒ {pc=target−8, right=value, left=0}"]
        CI["credential_image.cpp<br/>usage + caps + refs → cred_copy"]
        MR["MulticastRoute · multicast_route.hpp"]
        ST["start_threads · waiter/owner/consumer"]
        RQ["requeue: chain_word=1 → CMP_REQUEUE_PI"]
        WM["waiter_main: lock_pi(wait_word) → park → trigger"]
        RD["route_done → stop_threads"]
        VER2{"verify: victim_uid == 0<br/>哨兵 0xffffffff"}
        RETRY2["step retry × 15"]
        ROOT2(["uid 0 ✓"])
    end
    LUA --> MAINCPP
    MAINCPP --> SNITCH
    SNITCH --> VSPAWN
    MAINCPP --> PREP
    PREP --> PP
    PREP --> WE
    PREP --> CI
    MAINCPP --> MR
    MR --> ST
    ST --> RQ
    RQ --> WM
    WM --> RD
    WE --> VER2
    PP --> VER2
    CI --> VER2
    RD --> VER2
    VER2 -->|否| RETRY2
    RETRY2 --> MAINCPP
    VER2 -->|是| ROOT2
```

## 三、契约层与两处 GAP（本目标）

```mermaid
flowchart TD
    KA["contract/kernel_access.hpp · ADR-0014<br/>脚本唯一可触<br/>AccessChannel: PiWindow/KernelModule/PageCache<br/>write64 / read64"]
    AF["pipeline/access_factory.cpp:19<br/>create(BackendId)"]
    ACC["backend/cve_2026_43499/kernel_access.hpp<br/>class Access : KernelAccess（:65）<br/>AccessSeam（:36）<br/>Phase: NeverUp→ComingUp→Up→Down→Broken"]
    AP["access_params.hpp:29<br/>AccessParams: route + offset"]
    EXEC["执行层<br/>MulticastRoute + payload_page + write_encoder"]
    LEG["backend/legacy43499/（纯 C）<br/>main.c 59KB / fops.c 27KB / util.c 27KB<br/>common.h / target.h / offset.h / offsets_json.{c,h}<br/>runtime_struct_offsets.h / kernelsnitch(4 头)<br/>offsets.h 9551B + kernels/5.15.189-…（真机）"]
    KA --> AF
    AF --> ACC
    ACC --> AP
    ACC -.->|"GAP-1 尚未驱动执行层（现由 main.cpp 直驱）"| EXEC
    LEG -.->|"GAP-2 尚未接入 Makefile"| ACC
    LEG -.-> EXEC
```

## 四、两处 GAP 与下一步

| # | 缺口 | 现状 | 下一步 |
| --- | --- | --- | --- |
| **GAP-2** | `legacy43499` 未编译 | 未进 `Makefile`；`-fsyntax-only` 两处报错 | ① `-DTARGET_CONFIG_H=` 指向 **5.15.189**；② `kernels/offsets.h:111` 仍 include `6.1.115-…` ⇒ 改指真机那份 |
| **GAP-1** | `kernel_access` 未驱动执行层 | `Access` / `AccessSeam` 已在；`main.cpp` 直驱 | 让 `write64` 走 `do_one_write` |
