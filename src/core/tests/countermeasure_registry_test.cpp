/* CM-3 host test: the runtime registry plus the controller's external dispatch.
 *
 * Fake ops and a fake external hook table, no session construction and no
 * device. It covers the registry ordering guarantee, stage isolation, the
 * caller gate, failure-driven module skip, the relative order of the in-tree
 * policies and the external hooks, and the field content of the diagnostics
 * string. */

#include "ancillary/controller.hpp"
#include "ancillary/external_registry.hpp"
#include "contract/countermeasure.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

using namespace ghostlock;

namespace {

    std::vector<std::string> g_calls;

    const char *abi_stage_name(glk_cm_stage stage) {
        switch (stage) {
        case GLK_CM_STAGE_PRE_SPAWN: return "pre_spawn";
        case GLK_CM_STAGE_POST_SPAWN: return "post_spawn";
        case GLK_CM_STAGE_PRE_TERMINAL: return "pre_terminal";
        case GLK_CM_STAGE_PRE_ROUTE: return "pre_route";
        case GLK_CM_STAGE_POST_TERMINAL: return "post_terminal";
        }
        return "unknown";
    }

    std::int32_t record_ok(void *user, glk_cm_stage stage, const glk_host_ops *) {
        g_calls.push_back(std::string(static_cast<const char *>(user)) + "@" +
                          abi_stage_name(stage));
        return 0;
    }

    std::int32_t record_fail(void *user, glk_cm_stage stage, const glk_host_ops *) {
        g_calls.push_back(std::string(static_cast<const char *>(user)) + "@" +
                          abi_stage_name(stage));
        return -7;
    }

    struct EmptyMiddleware final {};

    /* An in-tree policy; its stage is recorded so the test can assert that the
     * built-in channel still runs before the external one. */
    struct BuiltinPolicy : ancillary::AncillaryPolicyDefaults {
        template <class Middleware>
        static Status apply(ancillary::AncillaryStage stage, session::CoreSession &,
                            ancillary::AncillaryOps &, const int &) noexcept {
            g_calls.push_back(
                    std::string("builtin@") +
                    ancillary::countermeasure_stage_name(
                            ancillary::to_countermeasure_stage(stage)));
            return true;
        }
    };

} // namespace

int main() {
    using namespace ancillary;

    static char t_h2[] = "m1.h2";
    static char t_h3[] = "m1.h3";
    static char t_h4[] = "m1.h4";
    static char t_h5[] = "m1.h5";
    static char t_h6[] = "m2.h6";
    static char t_h7[] = "m2.h7";
    static char n_m1[] = "mod.one";
    static char n_m2[] = "mod.two";
    static char v_m1[] = "1.0";
    static char v_m2[] = "2.0";

    glk_cm_hook m1_hooks[] = {
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_PRE_SPAWN, 10u, 0u, &record_ok, t_h2, "h2"},
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_PRE_SPAWN, 10u, 0u, &record_ok, t_h3, "h3"},
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_PRE_SPAWN, 1u, 0u, &record_ok, t_h4, "h4"},
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_POST_SPAWN, 5u, 0u, &record_ok, t_h5, "h5"},
    };
    glk_cm_hook m2_hooks[] = {
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_PRE_SPAWN, 10u, 0u, &record_ok, t_h6, "h6"},
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_POST_SPAWN, 1u, 0u, &record_ok, t_h7, "h7"},
    };

    RuntimeRegistry registry(contract::kHostImplementedCaps,
                             contract::kHostImplementedTriggers);
    assert(registry.register_module(ExternalModuleBinding{
            n_m1, v_m1, contract::Capability::KernelWrite, m1_hooks, 4u}));
    assert(registry.register_module(ExternalModuleBinding{
            n_m2, v_m2, contract::Capability::None, m2_hooks, 2u}));
    assert(registry.modules_registered() == 2u);
    assert(registry.hooks_registered() == 6u);
    assert(registry.modules_rejected() == 0u);

    /* Sorting: (stage, priority, registration). Within PRE_SPAWN: h4 (prio 1),
     * then h2 and h3 (prio 10) in declaration order, then m2.h6 (prio 10,
     * registered later). */
    g_calls.clear();
    registry.for_each(contract::CountermeasureStage::PreSpawn,
                      [](const RegistryHook &hook) {
                          g_calls.push_back(hook.hook_name);
                      });
    const std::vector<std::string> want_pre{"h4", "h2", "h3", "h6"};
    assert(g_calls == want_pre);

    /* Stage isolation: POST_SPAWN sees m2.h7 (prio 1) before m1.h5 (prio 5). */
    g_calls.clear();
    registry.for_each(contract::CountermeasureStage::PostSpawn,
                      [](const RegistryHook &hook) {
                          g_calls.push_back(hook.hook_name);
                      });
    const std::vector<std::string> want_post{"h7", "h5"};
    assert(g_calls == want_post);

    /* A reserved (but unused) stage matches nothing. */
    g_calls.clear();
    registry.for_each(contract::CountermeasureStage::PreTerminal,
                      [](const RegistryHook &hook) {
                          g_calls.push_back(hook.hook_name);
                      });
    assert(g_calls.empty());

    /* Controller setup. The session is only bound so a behavior that ignores it
     * can be called; it is never dereferenced. */
    alignas(void *) unsigned char session_storage[sizeof(void *)]{};
    auto &fake_session = *reinterpret_cast<session::CoreSession *>(session_storage);
    AncillaryOps ops{};
    const int context = 0;
    glk_host_ops host{};
    const auto builtin_gate = []<class P>() { return true; };
    const auto ext_always = [](const RegistryHook &) { return true; };
    const auto ext_never = [](const RegistryHook &) { return false; };

    /* In-tree policies first (unchanged order), then the external hooks. */
    g_calls.clear();
    const Status ordered = AncillaryController<std::tuple<BuiltinPolicy>>::apply<
            EmptyMiddleware>(AncillaryStage::PreSpawn, fake_session, ops,
                             builtin_gate, context, registry, &host, ext_always);
    assert(ordered);
    const std::vector<std::string> want_combined{
        "builtin@pre_spawn", "m1.h4@pre_spawn", "m1.h2@pre_spawn",
        "m1.h3@pre_spawn", "m2.h6@pre_spawn"};
    assert(g_calls == want_combined);

    /* gate == false never calls the external hook. */
    g_calls.clear();
    const Status gated = AncillaryController<std::tuple<>>::apply<EmptyMiddleware>(
            AncillaryStage::PreSpawn, fake_session, ops, builtin_gate, context,
            registry, &host, ext_never);
    assert(gated);
    assert(g_calls.empty());

    /* A non-zero hook return records a failure and disables that module for its
     * later stages, while the stage itself keeps dispatching. */
    static char t_fpre[] = "fail.pre";
    static char t_fpost[] = "fail.post";
    static char n_fail[] = "mod.fail";
    glk_cm_hook fail_hooks[] = {
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_PRE_SPAWN, 0u, 0u, &record_fail, t_fpre, "f_pre"},
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_POST_SPAWN, 0u, 0u, &record_ok, t_fpost, "f_post"},
    };
    RuntimeRegistry failing(contract::kHostImplementedCaps,
                            contract::kHostImplementedTriggers);
    assert(failing.register_module(ExternalModuleBinding{
            n_fail, v_m1, contract::Capability::None, fail_hooks, 2u}));
    g_calls.clear();
    const Status pre = AncillaryController<std::tuple<>>::apply<EmptyMiddleware>(
            AncillaryStage::PreSpawn, fake_session, ops, builtin_gate, context,
            failing, &host, ext_always);
    assert(!pre);
    assert(g_calls.size() == 1u && g_calls[0] == "fail.pre@pre_spawn");
    g_calls.clear();
    const Status post = AncillaryController<std::tuple<>>::apply<EmptyMiddleware>(
            AncillaryStage::PostSpawn, fake_session, ops, builtin_gate, context,
            failing, &host, ext_always);
    assert(post);
    assert(g_calls.empty());
    assert(failing.stage_failures() == 1u);
    assert(failing.modules_skipped() == 1u);

    /* Diagnostics: field content and rejection reasons. */
    const std::string text = format_registry_diagnostics(failing);
    assert(text.find("run.countermeasure registry offered=1 registered=1 "
                     "rejected=0 hooks=2 failures=1 skipped=1") !=
           std::string::npos);
    assert(text.find("run.countermeasure module name=mod.fail version=1.0 "
                     "accepted=1 hooks=2") != std::string::npos);
    assert(text.find("run.countermeasure failure module=mod.fail hook=f_pre "
                     "stage=pre_spawn rc=-7") != std::string::npos);

    /* Fail-closed registration: a reserved capability, stage or trigger rejects
     * the whole module and is recorded. */
    glk_cm_hook cap_hooks[] = {
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_PRE_SPAWN, 0u, 0u, &record_ok, t_fpre, "cap"},
    };
    assert(!failing.register_module(ExternalModuleBinding{
            n_fail, v_m1, contract::Capability::KernelHook, cap_hooks, 1u}));
    glk_cm_hook route_hooks[] = {
        {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_PRE_ROUTE, 0u, 0u, &record_ok, t_fpre, "route"},
    };
    assert(!failing.register_module(ExternalModuleBinding{
            n_fail, v_m1, contract::Capability::None, route_hooks, 1u}));
    glk_cm_hook load_hooks[] = {
        {GLK_CM_TRIGGER_ON_LOAD, GLK_CM_STAGE_PRE_SPAWN, 0u, 0u, &record_ok, t_fpre, "load"},
    };
    assert(!failing.register_module(ExternalModuleBinding{
            n_fail, v_m1, contract::Capability::None, load_hooks, 1u}));
    const std::string rejected = format_registry_diagnostics(failing);
    assert(rejected.find("offered=4 registered=1 rejected=3") != std::string::npos);
    assert(rejected.find("reason=missing_capability") != std::string::npos);
    assert(rejected.find("reason=reserved_stage") != std::string::npos);
    assert(rejected.find("reason=reserved_trigger") != std::string::npos);

    std::puts("countermeasure_registry_test: ok");
    return 0;
}
