/** @file script/sandbox.cpp
 *  Implementation of the A6-1 sandbox: bounded allocator, counting hook,
 *  whitelist assembly over the public Lua C API (the base library is never
 *  opened), surface registration driven by contract::kSurface, and a read-only
 *  cfg snapshot.
 *
 *  The kept globals are implemented here because luaopen_base is forbidden: the
 *  base library registers load/loadfile/dofile/print/setmetatable as well, and
 *  those must not exist at runtime. Each function below therefore uses only the
 *  public API and mirrors the semantics of the stock implementation for the
 *  supported argument forms.
 */
#include "script/sandbox.hpp"

#include <cstdlib>
#include <cstring>

/* The vendored headers are C: include them inside an extern "C" block so the
 *  symbols link against the C objects in liblua-core.a. */
extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "script/lua_whitelist.hpp"

namespace ghostlock::script {
    namespace {
        /* Sampling step of the counting hook: how many instructions pass between
         * two budget checks. This is a GRANULARITY, not a budget value -- the
         * budgets themselves are injected (see ScriptBudget). */
        inline constexpr int kHookGranularity = 1000;

        /* Named refusals, spelled once. */
        inline constexpr const char *kBudgetExceeded = "budget-exceeded";
        inline constexpr const char *kSurfaceUnknownName = "surface-unknown-name";
        inline constexpr const char *kCfgReadOnly = "cfg-read-only";
        inline constexpr const char *kCfgUnknownField = "cfg-unknown-field";
        inline constexpr const char *kPlanSyntax = "plan-syntax";

        /* Forwarding scratch: fixed, so the call path allocates nothing. */
        inline constexpr std::size_t kMaxForwardedArgs = 16;
        inline constexpr std::size_t kMaxForwardedString = 128;

        struct ForwardScratch final {
            ArgView args[kMaxForwardedArgs];
            char strings[kMaxForwardedArgs][kMaxForwardedString];
            std::size_t count = 0;
        };
    }  // namespace

    struct Sandbox::State final {
        lua_State *L = nullptr;
        ScriptBudget budget{};
        CallSink sink{};
        std::uint64_t bytes_used = 0;
        std::uint64_t instructions_left = 0;
        std::size_t registered_globals = 0;
        std::size_t registered_surface = 0;
        std::size_t call_count = 0;
        std::size_t error_count = 0;
        ScriptRejection last_rejection = ScriptRejection::None;
        bool memory_refused = false;
        /* Sticky: a budget breach is an EVENT. Even when the script swallows the
         * error with pcall, the run is reported as budget-exceeded. */
        bool budget_tripped = false;
        ForwardScratch scratch{};
        /* A6-3: the typed handles this sandbox issued. One plan, one backend. */
        HandleTable handles{};
    };

    namespace {
        /* ---- the bounded allocator: the memory hard limit ---- */
        void *bounded_alloc(void *ud, void *ptr, std::size_t osize, std::size_t nsize) noexcept {
            auto *state = static_cast<Sandbox::State *>(ud);
            if (nsize == 0) {
                std::free(ptr);
                state->bytes_used -= static_cast<std::uint64_t>(osize);
                return nullptr;
            }
            const std::uint64_t used = state->bytes_used;
            const std::uint64_t released = static_cast<std::uint64_t>(osize);
            const std::uint64_t want = static_cast<std::uint64_t>(nsize);
            if (want > released && used - released + want > state->budget.max_bytes) {
                /* Refusing here is the memory hard limit: Lua turns it into a memory
                 * error, and the breach is recorded so run() reports it by name even
                 * if the script swallows the error with pcall. */
                state->memory_refused = true;
                state->budget_tripped = true;
                return nullptr;
            }
            void *fresh = std::realloc(ptr, nsize);
            if (fresh == nullptr) {
                state->memory_refused = true;
                state->budget_tripped = true;
                return nullptr;
            }
            state->bytes_used = used - released + want;
            return fresh;
        }

        void raise_named(lua_State *L, const char *code) {
            lua_pushstring(L, code);
            (void) lua_error(L);
        }

        Sandbox::State *sandbox_state_of(lua_State *L);

        /* Raise the budget refusal and remember that it happened. */
        void raise_budget(lua_State *L) {
            Sandbox::State *state = sandbox_state_of(L);
            if (state != nullptr) {
                state->budget_tripped = true;
            }
            raise_named(L, kBudgetExceeded);
        }

        /* ---- the counting hook: instructions, stack depth, memory ---- */
        /* The allocator userdata is the sandbox state: that is how the hook and
         *  the bindings reach it without a global variable. */
        Sandbox::State *sandbox_state_of(lua_State *L) {
            void *ud = nullptr;
            (void) lua_getallocf(L, &ud);
            return static_cast<Sandbox::State *>(ud);
        }

        void budget_hook(lua_State *L, lua_Debug *ar) {
            (void) ar;
            Sandbox::State *state = sandbox_state_of(L);
            if (state == nullptr) {
                return;
            }
            if (state->instructions_left <= static_cast<std::uint64_t>(kHookGranularity)) {
                state->instructions_left = 0;
                raise_budget(L);
                return;
            }
            state->instructions_left -= static_cast<std::uint64_t>(kHookGranularity);
            if (state->memory_refused || state->bytes_used > state->budget.max_bytes) {
                raise_budget(L);
                return;
            }
            lua_Debug frame;
            int depth = 0;
            while (lua_getstack(L, depth, &frame) != 0) {
                ++depth;
                if (static_cast<std::uint32_t>(depth) > state->budget.max_stack_depth) {
                    raise_budget(L);
                    return;
                }
            }
        }

        /* ---- the kept globals, implemented over the public API ---- */
        int kept_assert(lua_State *L) {
            if (lua_toboolean(L, 1)) {
                return lua_gettop(L);
            }
            luaL_checkany(L, 1);
            if (lua_gettop(L) >= 2) {
                lua_settop(L, 2);
                lua_pushliteral(L, "assertion failed: ");
                lua_insert(L, 1);
                lua_concat(L, 2);
            } else {
                lua_pushliteral(L, "assertion failed!");
            }
            return lua_error(L);
        }

        int kept_error(lua_State *L) {
            lua_settop(L, 1);
            return lua_error(L);
        }

        int kept_type(lua_State *L) {
            luaL_checkany(L, 1);
            lua_pushstring(L, luaL_typename(L, 1));
            return 1;
        }

        int kept_tostring(lua_State *L) {
            luaL_checkany(L, 1);
            (void) luaL_tolstring(L, 1, nullptr);
            return 1;
        }

        int kept_tonumber(lua_State *L) {
            int base = static_cast<int>(luaL_optinteger(L, 2, 10));
            if (lua_type(L, 2) == LUA_TNONE) {
                int ok = 0;
                lua_Number value = lua_tonumberx(L, 1, &ok);
                if (ok) {
                    lua_pushnumber(L, value);
                } else {
                    lua_pushnil(L);
                }
                return 1;
            }
            luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
            std::size_t length = 0;
            const char *text = luaL_checklstring(L, 1, &length);
            /* strict integer parse in the given base, leading/trailing spaces allowed */
            std::size_t i = 0;
            while (i < length && text[i] == ' ') {
                ++i;
            }
            bool negative = false;
            if (i < length && (text[i] == '-' || text[i] == '+')) {
                negative = text[i] == '-';
                ++i;
            }
            std::uint64_t value = 0;
            bool any = false;
            for (; i < length; ++i) {
                const char c = text[i];
                int digit = -1;
                if (c >= '0' && c <= '9') {
                    digit = c - '0';
                } else if (c >= 'a' && c <= 'z') {
                    digit = c - 'a' + 10;
                } else if (c >= 'A' && c <= 'Z') {
                    digit = c - 'A' + 10;
                }
                if (digit < 0 || digit >= base) {
                    break;
                }
                value = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
                any = true;
            }
            while (i < length && text[i] == ' ') {
                ++i;
            }
            if (!any || i != length) {
                lua_pushnil(L);
                return 1;
            }
            const lua_Integer signed_value = negative ? -static_cast<lua_Integer>(value)
                                                      : static_cast<lua_Integer>(value);
            lua_pushinteger(L, signed_value);
            return 1;
        }

        int kept_next(lua_State *L) {
            luaL_checktype(L, 1, LUA_TTABLE);
            lua_settop(L, 2);
            if (lua_next(L, 1) != 0) {
                return 2;
            }
            lua_pushnil(L);
            return 1;
        }

        int kept_pairs_iter(lua_State *L) { return kept_next(L); }

        int kept_pairs(lua_State *L) {
            luaL_checktype(L, 1, LUA_TTABLE);
            lua_pushcfunction(L, kept_pairs_iter);
            lua_pushvalue(L, 1);
            lua_pushnil(L);
            return 3;
        }

        int kept_ipairs_iter(lua_State *L) {
            lua_Integer index = luaL_checkinteger(L, 2) + 1;
            lua_pushinteger(L, index);
            lua_pushinteger(L, index);
            lua_gettable(L, 1);
            if (lua_isnil(L, -1)) {
                return 1;
            }
            return 2;
        }

        int kept_ipairs(lua_State *L) {
            luaL_checktype(L, 1, LUA_TTABLE);
            lua_pushcfunction(L, kept_ipairs_iter);
            lua_pushvalue(L, 1);
            lua_pushinteger(L, 0);
            return 3;
        }

        int kept_select(lua_State *L) {
            const int top = lua_gettop(L);
            if (lua_type(L, 1) == LUA_TSTRING && std::strcmp(lua_tostring(L, 1), "#") == 0) {
                lua_pushinteger(L, static_cast<lua_Integer>(top) - 1);
                return 1;
            }
            lua_Integer index = luaL_checkinteger(L, 1);
            if (index < 0) {
                index = static_cast<lua_Integer>(top) + index;
            } else if (index > static_cast<lua_Integer>(top)) {
                index = static_cast<lua_Integer>(top);
            }
            luaL_argcheck(L, 1 <= index, 1, "index out of range");
            return top - static_cast<int>(index);
        }

        int finish_pcall(lua_State *L, int status) {
            if (status != LUA_OK) {
                lua_pushboolean(L, 0);
                lua_insert(L, -2); /* boolean before the error object */
                return 2;
            }
            return lua_gettop(L);
        }

        int kept_pcall(lua_State *L) {
            const int top = lua_gettop(L);
            lua_pushboolean(L, 1);
            lua_insert(L, 1);
            const int status = lua_pcall(L, top - 1, LUA_MULTRET, 0);
            return finish_pcall(L, status);
        }

        int kept_xpcall(lua_State *L) {
            const int top = lua_gettop(L);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            lua_pushboolean(L, 1);
            lua_pushvalue(L, 1);
            lua_rotate(L, 3, 2); /* handler, boolean, function, args... */
            const int status = lua_pcall(L, top - 2, LUA_MULTRET, 2);
            return finish_pcall(L, status);
        }

        int kept_rawget(lua_State *L) {
            luaL_checktype(L, 1, LUA_TTABLE);
            lua_settop(L, 2);
            lua_rawget(L, 1);
            return 1;
        }

        int kept_rawset(lua_State *L) {
            luaL_checktype(L, 1, LUA_TTABLE);
            lua_settop(L, 3);
            luaL_argcheck(L, !lua_isnoneornil(L, 2), 2, "table index is nil");
            lua_rawset(L, 1);
            lua_pushvalue(L, 1);
            return 1;
        }

        int kept_rawequal(lua_State *L) {
            luaL_checkany(L, 1);
            luaL_checkany(L, 2);
            lua_pushboolean(L, lua_rawequal(L, 1, 2));
            return 1;
        }

        int kept_rawlen(lua_State *L) {
            luaL_checkany(L, 1);
            const int type = lua_type(L, 1);
            if (type != LUA_TTABLE && type != LUA_TSTRING) {
                luaL_argerror(L, 1, "table or string expected");
            }
            lua_pushinteger(L, static_cast<lua_Integer>(lua_rawlen(L, 1)));
            return 1;
        }

        int kept_collectgarbage(lua_State *L) {
            struct Option final {
                const char *name;
                int value;
            };
            const Option options[] = {
                {"collect", LUA_GCCOLLECT},       {"stop", LUA_GCSTOP},
                {"restart", LUA_GCRESTART},       {"count", LUA_GCCOUNT},
                {"step", LUA_GCSTEP},             {"isrunning", LUA_GCISRUNNING},
                {"incremental", LUA_GCINC},       {"generational", LUA_GCGEN},
            };
            const char *name = luaL_optstring(L, 1, "collect");
            for (const Option &option : options) {
                if (std::strcmp(option.name, name) == 0) {
                    const lua_Integer argument = luaL_optinteger(L, 2, 0);
                    lua_pushinteger(L, lua_gc(L, option.value, argument));
                    return 1;
                }
            }
            return luaL_argerror(L, 1, "invalid option");
        }

        /* ---- surface registration, driven by kSurface[] ---- */
        /* Refuse a handle by name: the sink hears it and the script sees it. */
        int handle_refuse(lua_State *L, const char *code) {
            Sandbox::State *state = sandbox_state_of(L);
            if (state != nullptr) {
                ++state->error_count;
                if (state->sink.on_error != nullptr) {
                    state->sink.on_error(state->sink.context, code);
                }
            }
            return luaL_error(L, "%s", code);
        }

        /* A handle as the script sees it: three numbers in a plain table. The
         * script may copy it, but a value the host never issued cannot pass
         * check(), so copying buys nothing. */
        void push_handle(lua_State *L, const Handle &handle) {
            lua_createtable(L, 0, 3);
            lua_pushinteger(L, static_cast<lua_Integer>(handle.kind));
            lua_setfield(L, -2, "kind");
            lua_pushinteger(L, static_cast<lua_Integer>(handle.slot));
            lua_setfield(L, -2, "slot");
            lua_pushinteger(L, static_cast<lua_Integer>(handle.generation));
            lua_setfield(L, -2, "generation");
        }

        bool read_handle(lua_State *L, int index, Handle &out) {
            if (!lua_istable(L, index)) {
                return false;
            }
            const int top = lua_gettop(L);
            bool ok = true;
            lua_getfield(L, index, "kind");
            lua_getfield(L, index, "slot");
            lua_getfield(L, index, "generation");
            if (!lua_isinteger(L, -3) || !lua_isinteger(L, -2) || !lua_isinteger(L, -1)) {
                ok = false;
            } else {
                out.kind = static_cast<std::uint32_t>(lua_tointeger(L, -3));
                out.slot = static_cast<std::uint32_t>(lua_tointeger(L, -2));
                out.generation = static_cast<std::uint32_t>(lua_tointeger(L, -1));
            }
            lua_settop(L, top);
            return ok;
        }

        int surface_call(lua_State *L) {
            Sandbox::State *state = sandbox_state_of(L);
            if (state == nullptr) {
                return 0;
            }
            const std::size_t index = static_cast<std::size_t>(lua_tointeger(L, lua_upvalueindex(1)));
            /* A6-3: the acquiring row hands a handle out, every other row that
             * declares a first argument is handed one, and both directions are
             * checked here -- the script cannot make, extend or borrow one. */
            const contract::SurfaceEntry &row = contract::kSurface[index];
            const bool acquires = row.form == contract::SurfaceForm::Call;
            const bool takes_handle = row.arg_count >= 1U;
            Handle handle{};
            if (acquires) {
                const HandleRefusal refusal =
                    state->handles.acquire(static_cast<std::uint32_t>(index), handle);
                if (refusal != HandleRefusal::None) {
                    return handle_refuse(L, handle_refusal_name(refusal));
                }
            } else if (takes_handle) {
                if (!read_handle(L, 1, handle) ||
                    state->handles.check(handle) != HandleRefusal::None) {
                    return handle_refuse(L, "handle-stale");
                }
            }
            ForwardScratch &scratch = state->scratch;
            scratch.count = 0;
            const int top = lua_gettop(L);
            for (int i = 1; i <= top && scratch.count < kMaxForwardedArgs; ++i) {
                ArgView &view = scratch.args[scratch.count];
                if (takes_handle && i == 1) {
                    view.kind = ArgKind::Handle;
                    view.handle = handle;
                    ++scratch.count;
                    continue;
                }
                switch (lua_type(L, i)) {
                    case LUA_TNUMBER:
                        view.kind = ArgKind::U64;
                        view.u64 = static_cast<std::uint64_t>(lua_tointeger(L, i));
                        break;
                    case LUA_TBOOLEAN:
                        view.kind = ArgKind::Bool;
                        view.boolean = lua_toboolean(L, i) != 0;
                        break;
                    case LUA_TSTRING: {
                        view.kind = ArgKind::Str;
                        std::size_t length = 0;
                        const char *text = lua_tolstring(L, i, &length);
                        const std::size_t limit = length < kMaxForwardedString ? length : kMaxForwardedString - 1;
                        std::memcpy(scratch.strings[scratch.count], text, limit);
                        scratch.strings[scratch.count][limit] = '\0';
                        view.str = std::string_view(scratch.strings[scratch.count], limit);
                        break;
                    }
                    default:
                        view.kind = ArgKind::U64;
                        view.u64 = 0;
                        break;
                }
                ++scratch.count;
            }
            ++state->call_count;
            if (state->sink.on_op != nullptr) {
                CallView view{};
                view.surface_index = index;
                view.form = contract::kSurface[index].form;
                view.name = contract::kSurface[index].name;
                view.args = scratch.args;
                view.arg_count = scratch.count;
                state->sink.on_op(state->sink.context, &view);
            }
            if (row.form == contract::SurfaceForm::HandleMethod && row.arg_count == 1U) {
                /* The releasing row: the recording above carries the handle as it
                 * was, and only now does the generation move on. */
                (void)state->handles.release(handle);
            }
            if (acquires) {
                push_handle(L, handle);
                return 1;
            }
            return 0;
        }

        int surface_missing(lua_State *L) {
            Sandbox::State *state = sandbox_state_of(L);
            if (state != nullptr) {
                ++state->error_count;
                if (state->sink.on_error != nullptr) {
                    state->sink.on_error(state->sink.context, kSurfaceUnknownName);
                }
            }
            return luaL_error(L, "%s", kSurfaceUnknownName);
        }

        /* The cfg proxy carries this as __index, with the declared fields as its
         * only upvalue: a read either finds a declared field or is refused by
         * name, so an undeclared name can never look like a nil value. */
        int cfg_index(lua_State *L) {
            Sandbox::State *state = sandbox_state_of(L);
            const char *name = lua_tostring(L, 2);
            if (name != nullptr) {
                lua_getfield(L, lua_upvalueindex(1), name);
                if (!lua_isnil(L, -1)) {
                    return 1;
                }
                lua_pop(L, 1);
            }
            if (state != nullptr) {
                ++state->error_count;
                if (state->sink.on_error != nullptr) {
                    state->sink.on_error(state->sink.context, kCfgUnknownField);
                }
            }
            return luaL_error(L, "%s", kCfgUnknownField);
        }

        int cfg_read_only(lua_State *L) {
            Sandbox::State *state = sandbox_state_of(L);
            if (state != nullptr) {
                ++state->error_count;
                if (state->sink.on_error != nullptr) {
                    state->sink.on_error(state->sink.context, kCfgReadOnly);
                }
            }
            return luaL_error(L, "%s", kCfgReadOnly);
        }

        void register_kept_globals(lua_State *L, std::size_t *count) {
            struct Binding final {
                const char *name;
                lua_CFunction function;
            };
            const Binding bindings[] = {
                {"assert", kept_assert},           {"error", kept_error},
                {"type", kept_type},               {"tostring", kept_tostring},
                {"tonumber", kept_tonumber},       {"pairs", kept_pairs},
                {"ipairs", kept_ipairs},           {"next", kept_next},
                {"select", kept_select},           {"pcall", kept_pcall},
                {"xpcall", kept_xpcall},           {"rawget", kept_rawget},
                {"rawset", kept_rawset},           {"rawequal", kept_rawequal},
                {"rawlen", kept_rawlen},           {"collectgarbage", kept_collectgarbage},
            };
            for (const Binding &binding : bindings) {
                lua_pushcfunction(L, binding.function);
                lua_setglobal(L, binding.name);
                ++(*count);
            }
        }

        void register_surface(lua_State *L, std::size_t *count) {
            lua_createtable(L, 0, static_cast<int>(contract::kSurfaceCount));
            for (std::size_t i = 0; i < contract::kSurfaceCount; ++i) {
                lua_pushinteger(L, static_cast<lua_Integer>(i));
                lua_pushcclosure(L, surface_call, 1);
                lua_setfield(L, -2, contract::kSurface[i].name);
            }
            /* A name outside the table is refused by name, at the call site. */
            lua_createtable(L, 0, 2);
            lua_pushcfunction(L, surface_missing);
            lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, surface_missing);
            lua_setfield(L, -2, "__newindex");
            lua_setmetatable(L, -2);
            lua_setglobal(L, "surface");
            *count = contract::kSurfaceCount;
        }
    }  // namespace

    const char *script_rejection_name(ScriptRejection rejection) noexcept {
        switch (rejection) {
            case ScriptRejection::None: return "none";
            case ScriptRejection::BudgetExceeded: return kBudgetExceeded;
            case ScriptRejection::SurfaceUnknownName: return kSurfaceUnknownName;
            case ScriptRejection::PlanSyntax: return kPlanSyntax;
            case ScriptRejection::CfgReadOnly: return kCfgReadOnly;
            case ScriptRejection::CfgUnknownField: return kCfgUnknownField;
            case ScriptRejection::TuningMissing: return "tuning-missing";
            /* Handle refusals are spelled by the table that raises them. */
            case ScriptRejection::HandleStale: return handle_refusal_name(HandleRefusal::Stale);
            case ScriptRejection::HandleConflict: return handle_refusal_name(HandleRefusal::Conflict);
        }
        return "unknown";
    }

    Sandbox::~Sandbox() noexcept { close(); }

    bool Sandbox::open(const ScriptBudget &budget, CallSink sink) noexcept {
        if (state_ != nullptr) {
            return false;
        }
        /* The configuration boundary: a budget without values is refused HERE, so an
         *  unconfigured run can never be reported later as a script that overran. */
        if (!script_budget_is_configured(budget)) {
            return false;
        }
        state_ = static_cast<State *>(std::calloc(1, sizeof(State)));
        if (state_ == nullptr) {
            return false;
        }
        state_->budget = budget;
        state_->sink = sink;
        state_->instructions_left = budget.max_instructions;
        state_->L = lua_newstate(bounded_alloc, state_, 0U);
        if (state_->L == nullptr) {
            std::free(state_);
            state_ = nullptr;
            return false;
        }
        register_kept_globals(state_->L, &state_->registered_globals);
        register_surface(state_->L, &state_->registered_surface);
        lua_sethook(state_->L, budget_hook, LUA_MASKCOUNT, kHookGranularity);
        return true;
    }

    bool Sandbox::set_cfg_snapshot(const CfgField *fields, std::size_t count) noexcept {
        if (state_ == nullptr || state_->L == nullptr) {
            return false;
        }
        lua_State *L = state_->L;
        /* Absolute indices, each recorded as it is created: the proxy is what the
         * script sees, the fields table is only reachable through the reader. */
        lua_settop(L, 0);
        const int fields_at = lua_gettop(L) + 1;
        lua_createtable(L, 0, static_cast<int>(count));
        for (std::size_t i = 0; i < count; ++i) {
            lua_pushinteger(L, static_cast<lua_Integer>(fields[i].value));
            lua_setfield(L, fields_at, fields[i].name);
        }
        const int proxy_at = lua_gettop(L) + 1;
        lua_createtable(L, 0, 0);
        const int meta_at = lua_gettop(L) + 1;
        lua_createtable(L, 0, 2);
        lua_pushvalue(L, fields_at);
        lua_pushcclosure(L, cfg_index, 1); /* upvalue: the declared fields */
        lua_setfield(L, meta_at, "__index");
        lua_pushcfunction(L, cfg_read_only);
        lua_setfield(L, meta_at, "__newindex");
        lua_setmetatable(L, proxy_at); /* pops the metatable */
        lua_setglobal(L, "cfg");       /* pops the proxy */
        lua_settop(L, 0);
        return true;
    }

    ScriptRejection Sandbox::run(std::string_view script) noexcept {
        if (state_ == nullptr || state_->L == nullptr) {
            return ScriptRejection::None;
        }
        State &state = *state_;
        lua_State *L = state.L;
        state.last_rejection = ScriptRejection::None;
        state.memory_refused = false;
        state.budget_tripped = false;
        state.instructions_left = state.budget.max_instructions;
        if (script.size() > contract::kScriptSizeCeiling ||
            script.size() > static_cast<std::size_t>(state.budget.max_script_bytes)) {
            state.last_rejection = ScriptRejection::BudgetExceeded;
            if (state.sink.on_error != nullptr) {
                state.sink.on_error(state.sink.context, kBudgetExceeded);
            }
            return state.last_rejection;
        }
        if (luaL_loadbufferx(L, script.data(), script.size(), "=script", nullptr) != LUA_OK) {
            lua_settop(L, 0);
            state.last_rejection = ScriptRejection::PlanSyntax;
            return state.last_rejection;
        }
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char *message = lua_tostring(L, -1);
            ScriptRejection rejection =
                state.budget_tripped ? ScriptRejection::BudgetExceeded : ScriptRejection::PlanSyntax;
            if (message != nullptr) {
                if (std::strstr(message, kBudgetExceeded) != nullptr) {
                    rejection = ScriptRejection::BudgetExceeded;
                } else if (std::strstr(message, kSurfaceUnknownName) != nullptr) {
                    rejection = ScriptRejection::SurfaceUnknownName;
                } else if (std::strstr(message, kCfgUnknownField) != nullptr) {
                    rejection = ScriptRejection::CfgUnknownField;
                } else if (std::strstr(message, kCfgReadOnly) != nullptr) {
                    rejection = ScriptRejection::CfgReadOnly;
                } else if (std::strstr(message, handle_refusal_name(HandleRefusal::Stale)) != nullptr) {
                    rejection = ScriptRejection::HandleStale;
                } else if (std::strstr(message, handle_refusal_name(HandleRefusal::Conflict)) != nullptr) {
                    rejection = ScriptRejection::HandleConflict;
                }
            }
            if (state.sink.on_error != nullptr) {
                state.sink.on_error(state.sink.context, script_rejection_name(rejection));
            }
            lua_settop(L, 0);
            state.last_rejection = rejection;
            return rejection;
        }
        lua_settop(L, 0);
        if (state.budget_tripped) {
            /* A breach that a script swallowed with pcall is still a breach. */
            state.last_rejection = ScriptRejection::BudgetExceeded;
            if (state.sink.on_error != nullptr) {
                state.sink.on_error(state.sink.context, kBudgetExceeded);
            }
            return state.last_rejection;
        }
        return ScriptRejection::None;
    }

    void Sandbox::close() noexcept {
        if (state_ == nullptr) {
            return;
        }
        if (state_->L != nullptr) {
            lua_close(state_->L);
            state_->L = nullptr;
        }
        std::free(state_);
        state_ = nullptr;
    }

    bool Sandbox::is_open() const noexcept { return state_ != nullptr && state_->L != nullptr; }
    std::size_t Sandbox::registered_global_count() const noexcept {
        return state_ == nullptr ? 0 : state_->registered_globals;
    }
    std::size_t Sandbox::registered_surface_count() const noexcept {
        return state_ == nullptr ? 0 : state_->registered_surface;
    }
    std::uint64_t Sandbox::bytes_used() const noexcept { return state_ == nullptr ? 0 : state_->bytes_used; }
    std::uint64_t Sandbox::instructions_left() const noexcept {
        return state_ == nullptr ? 0 : state_->instructions_left;
    }
    std::size_t Sandbox::last_call_count() const noexcept { return state_ == nullptr ? 0 : state_->call_count; }
    std::size_t Sandbox::last_error_count() const noexcept { return state_ == nullptr ? 0 : state_->error_count; }
    ScriptRejection Sandbox::last_rejection() const noexcept {
        return state_ == nullptr ? ScriptRejection::None : state_->last_rejection;
    }

    bool Sandbox::global_absent(const char *name) noexcept {
        if (state_ == nullptr || state_->L == nullptr || name == nullptr) {
            return false;
        }
        lua_State *L = state_->L;
        lua_getglobal(L, name);
        const bool absent = lua_type(L, -1) == LUA_TNIL;
        lua_settop(L, 0);
        return absent;
    }

    bool Sandbox::table_keys_match(const char *table_name, const char *const *names,
                                   std::size_t count) noexcept {
        if (state_ == nullptr || state_->L == nullptr) {
            return false;
        }
        lua_State *L = state_->L;
        if (table_name == nullptr) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        } else {
            lua_getglobal(L, table_name);
        }
        if (!lua_istable(L, -1)) {
            lua_settop(L, 0);
            return false;
        }
        std::size_t found = 0;
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING) {
                lua_settop(L, 0);
                return false;
            }
            const char *key = lua_tostring(L, -2);
            bool listed = false;
            for (std::size_t i = 0; i < count; ++i) {
                if (key != nullptr && std::strcmp(key, names[i]) == 0) {
                    listed = true;
                    break;
                }
            }
            if (!listed) {
                lua_settop(L, 0);
                return false;
            }
            ++found;
            lua_pop(L, 1);
        }
        lua_settop(L, 0);
        return found == count;
    }
}  // namespace ghostlock::script
