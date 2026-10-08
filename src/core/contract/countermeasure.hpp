#ifndef GHOSTLOCK_CONTRACT_COUNTERMEASURE_HPP
#define GHOSTLOCK_CONTRACT_COUNTERMEASURE_HPP

/* C++ mapping of the countermeasure plugin ABI (CM-1).
 *
 * The ABI header (contract/abi/glk_contract_abi.h) is the wire truth; this header gives
 * the host C++ code a typed view of the same PODs without relaxing the ABI. The
 * static_assert block at the bottom pins the enum values, struct sizes,
 * alignments and every field offset, so a drift on either side breaks the build
 * instead of silently corrupting the cross-boundary data.
 *
 * The contract layer must not include backend/, pipeline/, platform/ or
 * terminal/ (ADR-0004 R1, enforced by include_firewall_test.cpp); this header
 * only depends on the self-contained ABI header.
 *
 * CM-1 is declaration only: there is no loader, no registry and no runtime
 * behavior here. kHostImplementedCaps / kHostImplementedTriggers name the
 * adjudicated subset the v1 host implements; the reserved ABI entries stay
 * absent from those sets, and a module that requires one must be rejected at
 * registration (fail-closed, see glk_contract_abi.h). */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "contract/abi/glk_contract_abi.h"

namespace ghostlock::contract {

/* Same numeric values as glk_stage; the static_asserts below keep them 1:1.
 * PRE_ROUTE and POST_TERMINAL are declared but not implemented by the v1 host. */
enum class CountermeasureStage : std::uint32_t {
    PreSpawn = static_cast<std::uint32_t>(GLK_STAGE_PRE_SPAWN),
    PostSpawn = static_cast<std::uint32_t>(GLK_STAGE_POST_SPAWN),
    PreTerminal = static_cast<std::uint32_t>(GLK_STAGE_PRE_TERMINAL),
    PreRoute = static_cast<std::uint32_t>(GLK_STAGE_PRE_ROUTE),
    PostTerminal = static_cast<std::uint32_t>(GLK_STAGE_POST_TERMINAL),
};

/* Same numeric values as glk_trigger; only OnStage is implemented in v1. */
enum class CountermeasureTrigger : std::uint32_t {
    OnStage = static_cast<std::uint32_t>(GLK_TRIGGER_ON_STAGE),
    OnLoad = static_cast<std::uint32_t>(GLK_TRIGGER_ON_LOAD),
    OnBootReady = static_cast<std::uint32_t>(GLK_TRIGGER_ON_BOOT_READY),
    Periodic = static_cast<std::uint32_t>(GLK_TRIGGER_PERIODIC),
};

/* Capability bitmask values; the reserved bits are declared and reject. */
enum class Capability : std::uint32_t {
    None = 0u,
    KernelRead = static_cast<std::uint32_t>(GLK_CAP_KERNEL_READ),
    KernelWrite = static_cast<std::uint32_t>(GLK_CAP_KERNEL_WRITE),
    Alias = static_cast<std::uint32_t>(GLK_CAP_ALIAS),
    ChildTask = static_cast<std::uint32_t>(GLK_CAP_CHILD_TASK),
    FileCacheWrite = static_cast<std::uint32_t>(GLK_CAP_FILE_CACHE_WRITE),
    Exec = static_cast<std::uint32_t>(GLK_CAP_EXEC),
    KernelHook = static_cast<std::uint32_t>(GLK_CAP_KERNEL_HOOK),
    /* Host logging (append-only, GLK_ABI_VERSION unchanged). */
    Log = static_cast<std::uint32_t>(GLK_CAP_LOG),
};

[[nodiscard]] constexpr Capability operator|(Capability lhs, Capability rhs) noexcept {
    return static_cast<Capability>(static_cast<std::uint32_t>(lhs) |
                                   static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr Capability operator&(Capability lhs, Capability rhs) noexcept {
    return static_cast<Capability>(static_cast<std::uint32_t>(lhs) &
                                   static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr Capability operator^(Capability lhs, Capability rhs) noexcept {
    return static_cast<Capability>(static_cast<std::uint32_t>(lhs) ^
                                   static_cast<std::uint32_t>(rhs));
}

/* True when every bit of a capability bit is present in a capability set. */
[[nodiscard]] constexpr bool has_capability(Capability set, Capability bit) noexcept {
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(bit)) != 0u;
}

/* Every capability the ABI declares (all eight bits). */
inline constexpr Capability kAllCapabilities =
    Capability::KernelRead | Capability::KernelWrite | Capability::Alias |
    Capability::ChildTask | Capability::FileCacheWrite | Capability::Exec |
    Capability::KernelHook | Capability::Log;

/* Declaration order of every capability: the ONE list the probe's caps_list()
 * iterates (a second hardcoded list is how a new bit silently disappears from
 * the host_caps TSV column). Its OR must equal kAllCapabilities. */
inline constexpr Capability kCapabilityCatalog[] = {
    Capability::KernelRead,  Capability::KernelWrite, Capability::Alias,
    Capability::ChildTask,   Capability::FileCacheWrite, Capability::Exec,
    Capability::KernelHook,  Capability::Log,
};
inline constexpr std::size_t kCapabilityCatalogCount =
    sizeof(kCapabilityCatalog) / sizeof(kCapabilityCatalog[0]);

/* Adjudicated v1 subset: four capability bits plus the host logging channel.
 * FILE_CACHE_WRITE / EXEC / KERNEL_HOOK stay reserved; requiring one must
 * reject the module at registration. */
inline constexpr Capability kHostImplementedCaps =
    Capability::KernelRead | Capability::KernelWrite | Capability::Alias |
    Capability::ChildTask | Capability::Log;

/* Bit N is set when the host implements the trigger with numeric value N. The
 * v1 host implements only OnStage; the other three bits stay clear. */
[[nodiscard]] constexpr std::uint32_t trigger_bit(CountermeasureTrigger trigger) noexcept {
    return std::uint32_t{1} << static_cast<std::uint32_t>(trigger);
}

inline constexpr std::uint32_t kHostImplementedTriggers =
    trigger_bit(CountermeasureTrigger::OnStage);

/* POD wrappers of the ABI hook/module. Field order and types mirror the C
 * structs exactly; the static_asserts pin size, alignment and every offset.
 * There is no runtime behavior here, only the typed declaration. */
struct Hook final {
    CountermeasureTrigger trigger = CountermeasureTrigger::OnStage;
    CountermeasureStage stage = CountermeasureStage::PreSpawn;
    std::uint32_t priority = 0;
    std::uint32_t period_ms = 0;
    glk_stage_fn fn = nullptr;
    void *user = nullptr;
    const char *name = nullptr;
};

/* POD wrapper of glk_param (P1 tail append): one parameter or extract entry.
 * Field order and types mirror the C struct exactly. */
struct Param final {
    const char *name = nullptr;
    std::uint32_t type = 0;
    std::uint32_t required = 0;
    std::uint64_t default_value = 0;
    const char *default_str = nullptr;
    const char *doc = nullptr;
};

struct Module final {
    std::uint32_t abi_version = GLK_ABI_VERSION;
    std::uint32_t size = sizeof(glk_module);
    const char *name = nullptr;
    const char *version = nullptr;
    Capability required_caps = Capability::None;
    std::uint32_t hook_count = 0;
    const Hook *hooks = nullptr;
    /* ---- P1 tail append (S4 3.14.7.3). Read only when size >=
     * GLK_MODULE_SIZE_V2; a v1 module reports the smaller v1 size. */
    std::uint32_t param_count = 0;
    const Param *params = nullptr;
    std::uint32_t extract_count = 0;
    const Param *extract = nullptr;
    std::uint32_t stage_mask = 0;
};

/* ---- ABI <-> C++ consistency ------------------------------------------- */

static_assert(GLK_ABI_VERSION == 1u,
              "CM-1: this mapping only describes ABI version 1");

/* Enum values are 1:1 with the C ABI. */
static_assert(static_cast<std::uint32_t>(CountermeasureStage::PreSpawn) ==
              GLK_STAGE_PRE_SPAWN);
static_assert(static_cast<std::uint32_t>(CountermeasureStage::PostSpawn) ==
              GLK_STAGE_POST_SPAWN);
static_assert(static_cast<std::uint32_t>(CountermeasureStage::PreTerminal) ==
              GLK_STAGE_PRE_TERMINAL);
static_assert(static_cast<std::uint32_t>(CountermeasureStage::PreRoute) ==
              GLK_STAGE_PRE_ROUTE);
static_assert(static_cast<std::uint32_t>(CountermeasureStage::PostTerminal) ==
              GLK_STAGE_POST_TERMINAL);

static_assert(static_cast<std::uint32_t>(CountermeasureTrigger::OnStage) ==
              GLK_TRIGGER_ON_STAGE);
static_assert(static_cast<std::uint32_t>(CountermeasureTrigger::OnLoad) ==
              GLK_TRIGGER_ON_LOAD);
static_assert(static_cast<std::uint32_t>(CountermeasureTrigger::OnBootReady) ==
              GLK_TRIGGER_ON_BOOT_READY);
static_assert(static_cast<std::uint32_t>(CountermeasureTrigger::Periodic) ==
              GLK_TRIGGER_PERIODIC);

static_assert(static_cast<std::uint32_t>(Capability::KernelRead) ==
              GLK_CAP_KERNEL_READ);
static_assert(static_cast<std::uint32_t>(Capability::KernelWrite) ==
              GLK_CAP_KERNEL_WRITE);
static_assert(static_cast<std::uint32_t>(Capability::Alias) == GLK_CAP_ALIAS);
static_assert(static_cast<std::uint32_t>(Capability::ChildTask) ==
              GLK_CAP_CHILD_TASK);
static_assert(static_cast<std::uint32_t>(Capability::FileCacheWrite) ==
              GLK_CAP_FILE_CACHE_WRITE);
static_assert(static_cast<std::uint32_t>(Capability::Exec) == GLK_CAP_EXEC);
static_assert(static_cast<std::uint32_t>(Capability::KernelHook) ==
              GLK_CAP_KERNEL_HOOK);
static_assert(static_cast<std::uint32_t>(Capability::Log) == GLK_CAP_LOG);
/* GLK_CAP_LOG is the next append-only slot: the ABI version does not move. */
static_assert(static_cast<std::uint32_t>(Capability::Log) == (1u << 7));

/* The exported entry and the callback typedef keep their ABI shape. */
static_assert(std::is_same_v<decltype(glk_entry(std::uint32_t{})),
                             const glk_module *>);
static_assert(std::is_invocable_r_v<const glk_module *, decltype(&glk_entry),
                                    std::uint32_t>);
static_assert(std::is_same_v<glk_stage_fn,
                             std::int32_t (*)(void *, glk_stage, const glk_contract_ops *)>);

/* The PODs are trivially copyable, standard-layout and sized/aligned like the
 * ABI structs they wrap, with identical field offsets. */
static_assert(std::is_trivially_copyable_v<Hook>);
static_assert(std::is_standard_layout_v<Hook>);
static_assert(sizeof(Hook) == sizeof(glk_hook));
static_assert(alignof(Hook) == alignof(glk_hook));
static_assert(offsetof(Hook, trigger) == offsetof(glk_hook, trigger));
static_assert(offsetof(Hook, stage) == offsetof(glk_hook, stage));
static_assert(offsetof(Hook, priority) == offsetof(glk_hook, priority));
static_assert(offsetof(Hook, period_ms) == offsetof(glk_hook, period_ms));
static_assert(offsetof(Hook, fn) == offsetof(glk_hook, fn));
static_assert(offsetof(Hook, user) == offsetof(glk_hook, user));
static_assert(offsetof(Hook, name) == offsetof(glk_hook, name));

static_assert(std::is_trivially_copyable_v<Param>);
static_assert(std::is_standard_layout_v<Param>);
static_assert(sizeof(Param) == sizeof(glk_param));
static_assert(alignof(Param) == alignof(glk_param));
static_assert(offsetof(Param, name) == offsetof(glk_param, name));
static_assert(offsetof(Param, type) == offsetof(glk_param, type));
static_assert(offsetof(Param, required) == offsetof(glk_param, required));
static_assert(offsetof(Param, default_value) == offsetof(glk_param, default_value));
static_assert(offsetof(Param, default_str) == offsetof(glk_param, default_str));
static_assert(offsetof(Param, doc) == offsetof(glk_param, doc));
static_assert(static_cast<std::uint32_t>(GLK_PARAM_UINT) == 0u);
static_assert(static_cast<std::uint32_t>(GLK_PARAM_INT) == 1u);
static_assert(static_cast<std::uint32_t>(GLK_PARAM_BOOL) == 2u);
static_assert(static_cast<std::uint32_t>(GLK_PARAM_STR) == 3u);

static_assert(std::is_trivially_copyable_v<Module>);
static_assert(std::is_standard_layout_v<Module>);
static_assert(sizeof(Module) == sizeof(glk_module));
static_assert(alignof(Module) == alignof(glk_module));
static_assert(offsetof(Module, abi_version) == offsetof(glk_module, abi_version));
static_assert(offsetof(Module, size) == offsetof(glk_module, size));
static_assert(offsetof(Module, name) == offsetof(glk_module, name));
static_assert(offsetof(Module, version) == offsetof(glk_module, version));
static_assert(offsetof(Module, required_caps) ==
              offsetof(glk_module, required_caps));
static_assert(offsetof(Module, hook_count) == offsetof(glk_module, hook_count));
static_assert(offsetof(Module, hooks) == offsetof(glk_module, hooks));
/* P1 tail append: v1 offsets are frozen, the new fields follow them, and the
 * size macro names the struct the host must see before reading the tail. */
static_assert(offsetof(Module, param_count) == offsetof(glk_module, param_count));
static_assert(offsetof(Module, params) == offsetof(glk_module, params));
static_assert(offsetof(Module, extract_count) == offsetof(glk_module, extract_count));
static_assert(offsetof(Module, extract) == offsetof(glk_module, extract));
static_assert(offsetof(Module, stage_mask) == offsetof(glk_module, stage_mask));
static_assert(GLK_MODULE_SIZE_V2 == sizeof(glk_module));

/* The C structs are POD in the C++ sense as well. */
static_assert(std::is_trivially_copyable_v<glk_contract_ops>);
static_assert(std::is_standard_layout_v<glk_contract_ops>);
static_assert(std::is_trivially_copyable_v<glk_hook>);
static_assert(std::is_standard_layout_v<glk_hook>);
static_assert(std::is_trivially_copyable_v<glk_module>);
static_assert(std::is_standard_layout_v<glk_module>);

/* The host-implemented sets are exactly the adjudicated subset; a reserved bit
 * accidentally marked implemented fails here (and in the host test). */
static_assert(kAllCapabilities == (Capability::KernelRead | Capability::KernelWrite |
                                   Capability::Alias | Capability::ChildTask |
                                   Capability::FileCacheWrite | Capability::Exec |
                                   Capability::KernelHook | Capability::Log));
static_assert(kHostImplementedCaps == (Capability::KernelRead |
                                       Capability::KernelWrite | Capability::Alias |
                                       Capability::ChildTask | Capability::Log));
static_assert(!has_capability(kHostImplementedCaps, Capability::FileCacheWrite));
static_assert(!has_capability(kHostImplementedCaps, Capability::Exec));
static_assert(!has_capability(kHostImplementedCaps, Capability::KernelHook));
/* The catalog is complete: no ABI capability may be missing from the list the
 * probe iterates, and no list entry may be invented. */
[[nodiscard]] constexpr Capability catalog_union() noexcept {
    Capability bits = Capability::None;
    for (std::size_t i = 0u; i < kCapabilityCatalogCount; ++i) {
        bits = bits | kCapabilityCatalog[i];
    }
    return bits;
}
static_assert(catalog_union() == kAllCapabilities);
static_assert(kCapabilityCatalogCount == 8u);

static_assert(kHostImplementedTriggers == trigger_bit(CountermeasureTrigger::OnStage));
static_assert((kHostImplementedTriggers & trigger_bit(CountermeasureTrigger::OnLoad)) == 0u);
static_assert((kHostImplementedTriggers & trigger_bit(CountermeasureTrigger::OnBootReady)) == 0u);
static_assert((kHostImplementedTriggers & trigger_bit(CountermeasureTrigger::Periodic)) == 0u);

/* S4 P1: the token spelling of the three vocabularies. These strings are the
 * frozen glue between the C ABI values, the probe TSV (contract-design
 * 3.14.7.2), the plugin manifest and the Kotlin descriptor parser, so they live
 * once, here, next to the enums they name. */
[[nodiscard]] constexpr std::string_view stage_token(
        CountermeasureStage stage) noexcept {
    switch (stage) {
        case CountermeasureStage::PreSpawn: return "pre_spawn";
        case CountermeasureStage::PostSpawn: return "post_spawn";
        case CountermeasureStage::PreTerminal: return "pre_terminal";
        case CountermeasureStage::PreRoute: return "pre_route";
        case CountermeasureStage::PostTerminal: return "post_terminal";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view trigger_token(
        CountermeasureTrigger trigger) noexcept {
    switch (trigger) {
        case CountermeasureTrigger::OnStage: return "on_stage";
        case CountermeasureTrigger::OnLoad: return "on_load";
        case CountermeasureTrigger::OnBootReady: return "on_boot_ready";
        case CountermeasureTrigger::Periodic: return "periodic";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view capability_token(
        Capability capability) noexcept {
    switch (capability) {
        case Capability::None: return "-";
        case Capability::KernelRead: return "kernel_read";
        case Capability::KernelWrite: return "kernel_write";
        case Capability::Alias: return "alias";
        case Capability::ChildTask: return "child_task";
        case Capability::FileCacheWrite: return "file_cache_write";
        case Capability::Exec: return "exec";
        case Capability::KernelHook: return "kernel_hook";
        case Capability::Log: return "log";
    }
    return "unknown";
}

} // namespace ghostlock::contract

#endif
