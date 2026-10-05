/* Host contract test for the countermeasure plugin ABI (CM-1).
 *
 * It pins the ABI against the adjudicated plan (.4/.9/.10): the version, every
 * stage/trigger/capability numeric value, the struct size/alignment/offsets, the
 * POD properties and -- most importantly -- the exact host-implemented subset.
 * kHostImplementedCaps / kHostImplementedTriggers are compared with a literal
 * copy of the adjudicated mask, so marking a reserved item as implemented makes
 * this test FAIL (the header's static_asserts would fail even earlier).
 *
 * The header itself is compiled as C99 by the Makefile rule before this test, so
 * a project include or non-C99 syntax leaking into glk_contract_abi.h fails the build.
 * There is no loader here and no runtime behavior: CM-1 is declaration only. */

#include "contract/countermeasure.hpp"
#include "contract/abi/glk_contract_abi.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using ghostlock::contract::Capability;
using ghostlock::contract::CountermeasureStage;
using ghostlock::contract::CountermeasureTrigger;
using ghostlock::contract::has_capability;
using ghostlock::contract::Hook;
using ghostlock::contract::kAllCapabilities;
using ghostlock::contract::kHostImplementedCaps;
using ghostlock::contract::kHostImplementedTriggers;
using ghostlock::contract::Module;
using ghostlock::contract::trigger_bit;

/* Compile-time half of the contract (mirrors the header asserts so this test
 * fails if they are ever removed; also proves the C structs are POD). */
static_assert(GLK_ABI_VERSION == 1u);
static_assert(std::is_trivially_copyable_v<Hook>);
static_assert(std::is_standard_layout_v<Hook>);
static_assert(sizeof(Hook) == sizeof(glk_hook));
static_assert(alignof(Hook) == alignof(glk_hook));
static_assert(std::is_trivially_copyable_v<Module>);
static_assert(std::is_standard_layout_v<Module>);
static_assert(sizeof(Module) == sizeof(glk_module));
static_assert(alignof(Module) == alignof(glk_module));
static_assert(std::is_trivially_copyable_v<glk_contract_ops>);
static_assert(std::is_standard_layout_v<glk_contract_ops>);
static_assert(std::is_trivially_copyable_v<glk_hook>);
static_assert(std::is_standard_layout_v<glk_hook>);
static_assert(std::is_trivially_copyable_v<glk_module>);
static_assert(std::is_standard_layout_v<glk_module>);

/* The literal adjudicated masks (plan .9 ruling 1). A reserved item switched to
 * implemented must not match these literals. */
namespace {
    constexpr std::uint32_t kAdjudicatedCaps = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);
    constexpr std::uint32_t kAdjudicatedTriggers = 1u << 0;
    constexpr std::uint32_t kReservedCaps = (1u << 4) | (1u << 5) | (1u << 6);
} // namespace

int32_t main(void) {
    /* ABI version. */
    assert(GLK_ABI_VERSION == 1u);
    assert(Module{}.abi_version == GLK_ABI_VERSION);
    assert(Module{}.size == sizeof(glk_module));

    /* Stage enum: every value maps 1:1 to the C ABI. */
    assert(static_cast<std::uint32_t>(CountermeasureStage::PreSpawn) ==
           GLK_STAGE_PRE_SPAWN);
    assert(static_cast<std::uint32_t>(CountermeasureStage::PostSpawn) ==
           GLK_STAGE_POST_SPAWN);
    assert(static_cast<std::uint32_t>(CountermeasureStage::PreTerminal) ==
           GLK_STAGE_PRE_TERMINAL);
    assert(static_cast<std::uint32_t>(CountermeasureStage::PreRoute) ==
           GLK_STAGE_PRE_ROUTE);
    assert(static_cast<std::uint32_t>(CountermeasureStage::PostTerminal) ==
           GLK_STAGE_POST_TERMINAL);
    assert(GLK_STAGE_PRE_SPAWN == 0);
    assert(GLK_STAGE_POST_SPAWN == 1);
    assert(GLK_STAGE_PRE_TERMINAL == 2);
    assert(GLK_STAGE_PRE_ROUTE == 3);
    assert(GLK_STAGE_POST_TERMINAL == 4);

    /* Trigger enum: every value maps 1:1 to the C ABI. */
    assert(static_cast<std::uint32_t>(CountermeasureTrigger::OnStage) ==
           GLK_TRIGGER_ON_STAGE);
    assert(static_cast<std::uint32_t>(CountermeasureTrigger::OnLoad) ==
           GLK_TRIGGER_ON_LOAD);
    assert(static_cast<std::uint32_t>(CountermeasureTrigger::OnBootReady) ==
           GLK_TRIGGER_ON_BOOT_READY);
    assert(static_cast<std::uint32_t>(CountermeasureTrigger::Periodic) ==
           GLK_TRIGGER_PERIODIC);
    assert(GLK_TRIGGER_ON_STAGE == 0);
    assert(GLK_TRIGGER_ON_LOAD == 1);
    assert(GLK_TRIGGER_ON_BOOT_READY == 2);
    assert(GLK_TRIGGER_PERIODIC == 3);

    /* Capability bits: the declared universe and every value. */
    assert(static_cast<std::uint32_t>(Capability::KernelRead) == (1u << 0));
    assert(static_cast<std::uint32_t>(Capability::KernelWrite) == (1u << 1));
    assert(static_cast<std::uint32_t>(Capability::Alias) == (1u << 2));
    assert(static_cast<std::uint32_t>(Capability::ChildTask) == (1u << 3));
    assert(static_cast<std::uint32_t>(Capability::FileCacheWrite) == (1u << 4));
    assert(static_cast<std::uint32_t>(Capability::Exec) == (1u << 5));
    assert(static_cast<std::uint32_t>(Capability::KernelHook) == (1u << 6));
    assert(static_cast<std::uint32_t>(kAllCapabilities) == 0x7Fu);
    assert(static_cast<std::uint32_t>(kAllCapabilities) ==
           (kAdjudicatedCaps | kReservedCaps));

    /* Adjudicated host subset: exactly KERNEL_READ|KERNEL_WRITE|ALIAS|CHILD_TASK,
     * and no reserved capability. This is the fail-on-drift check. */
    assert(static_cast<std::uint32_t>(kHostImplementedCaps) == kAdjudicatedCaps);
    assert((static_cast<std::uint32_t>(kHostImplementedCaps) & kReservedCaps) == 0u);
    assert(has_capability(kHostImplementedCaps, Capability::KernelRead));
    assert(has_capability(kHostImplementedCaps, Capability::KernelWrite));
    assert(has_capability(kHostImplementedCaps, Capability::Alias));
    assert(has_capability(kHostImplementedCaps, Capability::ChildTask));
    assert(!has_capability(kHostImplementedCaps, Capability::FileCacheWrite));
    assert(!has_capability(kHostImplementedCaps, Capability::Exec));
    assert(!has_capability(kHostImplementedCaps, Capability::KernelHook));

    /* Same check for triggers: only ON_STAGE, no reserved bit set. */
    assert(kAdjudicatedTriggers == trigger_bit(CountermeasureTrigger::OnStage));
    assert(kHostImplementedTriggers == kAdjudicatedTriggers);
    assert((kHostImplementedTriggers & trigger_bit(CountermeasureTrigger::OnLoad)) == 0u);
    assert((kHostImplementedTriggers & trigger_bit(CountermeasureTrigger::OnBootReady)) == 0u);
    assert((kHostImplementedTriggers & trigger_bit(CountermeasureTrigger::Periodic)) == 0u);

    /* Size/alignment of both C structs and their C++ wrappers. */
    assert(sizeof(glk_hook) == sizeof(Hook));
    assert(alignof(glk_hook) == alignof(Hook));
    assert(sizeof(glk_module) == sizeof(Module));
    assert(alignof(glk_module) == alignof(Module));

    /* Every field offset is identical, so a reinterpret at the boundary would be
     * layout-safe even though no runtime code does it in CM-1. */
    assert(offsetof(Hook, trigger) == offsetof(glk_hook, trigger));
    assert(offsetof(Hook, stage) == offsetof(glk_hook, stage));
    assert(offsetof(Hook, priority) == offsetof(glk_hook, priority));
    assert(offsetof(Hook, period_ms) == offsetof(glk_hook, period_ms));
    assert(offsetof(Hook, fn) == offsetof(glk_hook, fn));
    assert(offsetof(Hook, user) == offsetof(glk_hook, user));
    assert(offsetof(Hook, name) == offsetof(glk_hook, name));
    assert(offsetof(Module, abi_version) == offsetof(glk_module, abi_version));
    assert(offsetof(Module, size) == offsetof(glk_module, size));
    assert(offsetof(Module, name) == offsetof(glk_module, name));
    assert(offsetof(Module, version) == offsetof(glk_module, version));
    assert(offsetof(Module, required_caps) == offsetof(glk_module, required_caps));
    assert(offsetof(Module, hook_count) == offsetof(glk_module, hook_count));
    assert(offsetof(Module, hooks) == offsetof(glk_module, hooks));

    /* Defaults describe the v1 host state and never claim a reserved capability
     * or a non-ON_STAGE trigger. */
    Module empty{};
    assert(empty.required_caps == Capability::None);
    assert(empty.hook_count == 0u);
    assert(empty.hooks == nullptr);
    Hook hook{};
    assert(hook.trigger == CountermeasureTrigger::OnStage);
    assert(hook.stage == CountermeasureStage::PreSpawn);
    assert(hook.priority == 0u);
    assert(hook.period_ms == 0u);
    assert(hook.fn == nullptr);
    assert(hook.user == nullptr);
    assert(hook.name == nullptr);

    puts("countermeasure_abi_test: ok");
    return 0;
}
