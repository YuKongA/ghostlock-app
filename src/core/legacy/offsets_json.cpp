/* Resolved-profile transport decoder.
 *
 * Native receives one fully resolved profile file from Kotlin and performs
 * strict decoding plus defensive validation. The parser is a bounded
 * string_view cursor: every helper consumes from or returns a view into the
 * caller-owned std::string, so no pointer pair can drift out of bounds.
 * Configuration-source selection and merging stay on the Kotlin side.
 */
#include "legacy/offsets_json.h"
#include "support/native_resource.hpp"

#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <iterator>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

using namespace ghostlock;

namespace ghostlock::legacy {
    inline constexpr std::size_t kProfileJsonMaxSize = 1U << 20;

    static support::Result<std::string> read_profile_file(const char *path) {
        support::UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
        if (!fd.valid()) {
            return std::unexpected(support::SysError::from_errno());
        }

        std::string buffer(kProfileJsonMaxSize, '\0');
        size_t used = 0;
        while (used < buffer.size()) {
            const ssize_t count = read(fd.get(), buffer.data() + used,
                                       buffer.size() - used);
            if (count > 0) {
                used += static_cast<size_t>(count);
                continue;
            }
            if (count == 0) break;
            if (errno == EINTR) continue;
            return std::unexpected(support::SysError::from_errno());
        }
        if (used == 0 || used == kProfileJsonMaxSize) {
            errno = used == kProfileJsonMaxSize ? EFBIG : EINVAL;
            return std::unexpected(support::SysError::from_errno());
        }
        buffer.resize(used);
        return buffer;
    }

    static std::string_view json_skip_ws(std::string_view in) {
        size_t i = 0;
        while (i < in.size() &&
               (in[i] == ' ' || in[i] == '\t' || in[i] == '\n' || in[i] == '\r')) {
            i++;
        }
        return in.substr(i);
    }

    /* Match the string literal at the front of `in` against key; on success
 * consume it from `in`. A failed match leaves `in` untouched. */
    static bool json_match_key(std::string_view &in, std::string_view key) {
        const std::string_view probe = json_skip_ws(in);
        if (probe.empty() || probe.front() != '"') return false;
        size_t i = 1;
        while (i < probe.size() && probe[i] != '"') {
            if (probe[i] == '\\') i++;
            i++;
        }
        if (i >= probe.size()) return false;
        const std::string_view name = probe.substr(1, i - 1);
        if (name.size() != key.size() ||
            memcmp(name.data(), key.data(), name.size()) != 0) {
            return false;
        }
        in = probe.substr(i + 1);
        return true;
    }

    /* Advance `in` past one JSON value (string, number, object, array, literal). */
    static bool json_skip_value(std::string_view &in) {
        in = json_skip_ws(in);
        if (in.empty()) return false;
        if (in.front() == '"') {
            size_t i = 1;
            while (i < in.size() && in[i] != '"') {
                if (in[i] == '\\') i++;
                i++;
            }
            if (i >= in.size()) return false;
            in.remove_prefix(i + 1);
            return true;
        }
        if (in.front() == '{' || in.front() == '[') {
            const char open = in.front();
            const char close = (open == '{') ? '}' : ']';
            size_t i = 1;
            int32_t depth = 1;
            while (i < in.size() && depth > 0) {
                if (in[i] == '"') {
                    i++;
                    while (i < in.size() && in[i] != '"') {
                        if (in[i] == '\\') i++;
                        i++;
                    }
                    if (i >= in.size()) return false;
                    i++;
                } else if (in[i] == open) {
                    depth++;
                    i++;
                } else if (in[i] == close) {
                    depth--;
                    i++;
                } else {
                    i++;
                }
            }
            if (depth != 0) return false;
            in.remove_prefix(i);
            return true;
        }
        size_t i = 0;
        while (i < in.size() && in[i] != ',' && in[i] != '}' && in[i] != ']' &&
               in[i] != ' ' && in[i] != '\t' && in[i] != '\n' && in[i] != '\r') {
            i++;
        }
        in.remove_prefix(i);
        return true;
    }

    /* Return the exact source span of the value at the front of `at`, or nullopt
 * when the value is malformed. `at` is not modified. */
    static std::optional<std::string_view> json_value_span(std::string_view at) {
        at = json_skip_ws(at);
        const char *start = at.data();
        if (!json_skip_value(at)) return std::nullopt;
        return std::string_view(start,
                                static_cast<size_t>(at.data() - start));
    }

    /* Return the value of member `key` at depth 1 inside the object at the front
 * of `object`, or nullopt when absent. */
    static std::optional<std::string_view> json_member_value(
        std::string_view object, std::string_view key) {
        object = json_skip_ws(object);
        if (object.empty() || object.front() != '{') return std::nullopt;
        object.remove_prefix(1);
        for (;;) {
            object = json_skip_ws(object);
            if (object.empty() || object.front() != '"') return std::nullopt;
            if (!json_match_key(object, key)) {
                /* json_match_key left the member name at the front of object. */
                if (!json_skip_value(object)) return std::nullopt;
                object = json_skip_ws(object);
                if (object.empty() || object.front() != ':') return std::nullopt;
                object.remove_prefix(1);
                object = json_skip_ws(object);
                if (!json_skip_value(object)) return std::nullopt;
                object = json_skip_ws(object);
                if (!object.empty() && object.front() == ',') {
                    object.remove_prefix(1);
                    continue;
                }
                return std::nullopt;
            }
            object = json_skip_ws(object);
            if (object.empty() || object.front() != ':') return std::nullopt;
            object.remove_prefix(1);
            object = json_skip_ws(object);
            return object.empty()
                       ? std::nullopt
                       : std::optional<std::string_view>(object);
        }
    }

    static std::optional<std::string_view> json_object_span(
        std::string_view object, const char *name) {
        const auto value = json_member_value(object, name);
        if (!value || value->empty() || value->front() != '{') return std::nullopt;
        return json_value_span(*value);
    }

    static bool json_parse_int(std::string_view in, int64_t *out);

    /* First member name and value span of the object at the front of `object`. */
    static bool json_first_member(std::string_view object,
                                  std::string_view *name, std::string_view *value) {
        object = json_skip_ws(object);
        if (object.empty() || object.front() != '{') return false;
        object.remove_prefix(1);
        object = json_skip_ws(object);
        if (object.empty() || object.front() != '"') return false;
        size_t i = 1;
        while (i < object.size() && object[i] != '"') {
            if (object[i] == '\\') i++;
            i++;
        }
        if (i >= object.size()) return false;
        *name = object.substr(1, i - 1);
        object.remove_prefix(i + 1);
        object = json_skip_ws(object);
        if (object.empty() || object.front() != ':') return false;
        object.remove_prefix(1);
        object = json_skip_ws(object);
        const auto span = json_value_span(object);
        if (!span) return false;
        *value = *span;
        return true;
    }

    /* Namespaced groups shared by the flat profile maps below. */
    struct ProfileGroups {
        std::optional<std::string_view> task_struct;
        std::optional<std::string_view> cred;
        std::optional<std::string_view> offset;
        std::optional<std::string_view> mcast;
    };

    /* Resolve one flat map name against the namespaced groups first
 * ("task_prio" -> task.prio); the flat key stays as a legacy fallback. */
    static bool read_namespaced_scalar(std::string_view object, const char *name,
                                       const ProfileGroups &groups, int64_t *num) {
        const std::optional<std::string_view> *group = nullptr;
        std::string_view field = name;
        if (field.starts_with("task_")) {
            group = &groups.task_struct;
            field.remove_prefix(5);
        } else if (field.starts_with("cred_")) {
            group = &groups.cred;
            field.remove_prefix(5);
        } else if (field.starts_with("off_")) {
            group = &groups.offset;
            field.remove_prefix(4);
        } else if (field.starts_with("mcast_")) {
            group = &groups.mcast;
            field.remove_prefix(6);
        }
        if (group != nullptr && group->has_value()) {
            const auto nested = json_member_value(**group, field);
            if (nested && json_parse_int(*nested, num)) return true;
        }
        const auto flat = json_member_value(object, name);
        return flat && json_parse_int(*flat, num);
    }

    /* Copy the JSON string at the front of `in` (escapes stripped) into dst. */
    static bool json_read_string(std::string_view &in, char *dst, size_t cap) {
        in = json_skip_ws(in);
        if (in.empty() || in.front() != '"') return false;
        size_t i = 1;
        size_t len = 0;
        while (i < in.size() && in[i] != '"') {
            char c = in[i];
            if (c == '\\') {
                i++;
                if (i >= in.size()) return false;
                c = in[i];
            }
            if (len + 1 >= cap) return false;
            dst[len++] = c;
            i++;
        }
        if (i >= in.size()) return false;
        dst[len] = '\0';
        in.remove_prefix(i + 1);
        return true;
    }

    static bool json_parse_int(std::string_view in, int64_t *out) {
        in = json_skip_ws(in);
        if (in.empty()) return false;
        size_t i = 0;
        bool neg = false;
        if (in[i] == '-') {
            neg = true;
            i++;
        }
        uint64_t v = 0;
        if (i + 2 <= in.size() && in[i] == '0' &&
            (in[i + 1] == 'x' || in[i + 1] == 'X')) {
            i += 2;
            int32_t digits = 0;
            while (i < in.size() && isxdigit(static_cast<unsigned char>(in[i]))) {
                const char c = in[i];
                const int32_t d = (c <= '9')
                                  ? (c - '0')
                                  : (tolower(static_cast<unsigned char>(c)) - 'a' + 10);
                v = v * 16 + static_cast<uint64_t>(d);
                digits++;
                i++;
            }
            if (!digits) return false;
        } else {
            int32_t digits = 0;
            while (i < in.size() && in[i] >= '0' && in[i] <= '9') {
                v = v * 10 + static_cast<uint64_t>(in[i] - '0');
                digits++;
                i++;
            }
            if (!digits) return false;
        }
        *out = neg ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
        return true;
    }

    /* The JSON decoder assigns through a member expression, so no
 * offset/reinterpret_cast pair is needed and the declared member type defines
 * the truncation exactly like the old width-driven switch did. */
    struct scalar_field {
        const char *name;

        void (*store)(profile::kernel_offsets &, int64_t);
    };

    /* The member pointer is the template argument: the declared member type
     * defines the truncation exactly like the old member-expression macro did,
     * with no per-field closure spelled out. */
    template<auto Member>
    void store_scalar(profile::kernel_offsets &offsets, int64_t value) {
        offsets.*Member =
                static_cast<std::remove_reference_t<decltype(offsets.*Member)>>(value);
    }

    template<auto Member>
    constexpr scalar_field scalar_store(const char *name) {
        return {name, &store_scalar<Member>};
    }

    static constexpr struct scalar_field g_symbol_map[] = {
        scalar_store<&profile::kernel_offsets::off_init_task>("off_init_task"),
        scalar_store<&profile::kernel_offsets::off_init_cred>("off_init_cred"),
        scalar_store<&profile::kernel_offsets::off_empty_zero_page>("off_empty_zero_page"),
        scalar_store<&profile::kernel_offsets::off_mcast_fake_bss>("off_mcast_fake_bss"),
        scalar_store<&profile::kernel_offsets::off_root_task_group>("off_root_task_group"),
        scalar_store<&profile::kernel_offsets::off_selinux_enforcing>("off_selinux_enforcing"),
        scalar_store<&profile::kernel_offsets::off_selinux_blob_sizes>("off_selinux_blob_sizes"),
        scalar_store<&profile::kernel_offsets::off_security_hook_heads>("off_security_hook_heads"),
        scalar_store<&profile::kernel_offsets::off_slide_nfulnl_logger>("off_slide_nfulnl_logger"),
        scalar_store<&profile::kernel_offsets::off_slide_loggers_0_1>("off_slide_loggers_0_1"),
        scalar_store<&profile::kernel_offsets::off_slide_boot_id>("off_slide_boot_id"),
    };

    static constexpr struct scalar_field g_task_map[] = {
        scalar_store<&profile::kernel_offsets::task_prio>("task_prio"),
        scalar_store<&profile::kernel_offsets::task_normal_prio>("task_normal_prio"),
        scalar_store<&profile::kernel_offsets::task_sched_task_group>("task_sched_task_group"),
        scalar_store<&profile::kernel_offsets::task_pi_lock>("task_pi_lock"),
        scalar_store<&profile::kernel_offsets::task_pi_waiters>("task_pi_waiters"),
        scalar_store<&profile::kernel_offsets::task_pi_top_task>("task_pi_top_task"),
        scalar_store<&profile::kernel_offsets::task_pi_blocked_on>("task_pi_blocked_on"),
        scalar_store<&profile::kernel_offsets::task_pid>("task_pid"),
        scalar_store<&profile::kernel_offsets::task_tgid>("task_tgid"),
        scalar_store<&profile::kernel_offsets::task_atomic_flags>("task_atomic_flags"),
        scalar_store<&profile::kernel_offsets::task_real_cred>("task_real_cred"),
        scalar_store<&profile::kernel_offsets::task_cred>("task_cred"),
        scalar_store<&profile::kernel_offsets::task_comm>("task_comm"),
        scalar_store<&profile::kernel_offsets::task_tasks>("task_tasks"),
        scalar_store<&profile::kernel_offsets::task_seccomp>("task_seccomp"),
    };

    static constexpr struct scalar_field g_profile_map[] = {
        scalar_store<&profile::kernel_offsets::kernel_major>("kernel_major"),
        scalar_store<&profile::kernel_offsets::recommend_shizuku>("recommend_shizuku"),
        scalar_store<&profile::kernel_offsets::kernel_phys_load>("kernel_phys_load"),
        scalar_store<&profile::kernel_offsets::pselect_waiter_shift>("pselect_waiter_shift"),
        scalar_store<&profile::kernel_offsets::mcast_waiter_off>("mcast_waiter_off"),
        scalar_store<&profile::kernel_offsets::mcast_buffer_size>("mcast_buffer_size"),
        scalar_store<&profile::kernel_offsets::mcast_task_offset>("mcast_task_offset"),
        scalar_store<&profile::kernel_offsets::mcast_lock_offset>("mcast_lock_offset"),
        scalar_store<&profile::kernel_offsets::mcast_fake_lock_offset>("mcast_fake_lock_offset"),
        scalar_store<&profile::kernel_offsets::mcast_fake_task_offset>("mcast_fake_task_offset"),
        scalar_store<&profile::kernel_offsets::mcast_lock_slots_offset>("mcast_lock_slots_offset"),
        scalar_store<&profile::kernel_offsets::mcast_lock_slot_count>("mcast_lock_slot_count"),
        scalar_store<&profile::kernel_offsets::mcast_lock_slot_stride>("mcast_lock_slot_stride"),
        scalar_store<&profile::kernel_offsets::kernelsnitch_collisions>("kernelsnitch_collisions"),
        scalar_store<&profile::kernel_offsets::compact_waiter>("compact_waiter"),
        scalar_store<&profile::kernel_offsets::mm_struct_sz>("mm_struct_sz"),
        scalar_store<&profile::kernel_offsets::cred_copy_size>("cred_copy_size"),
        scalar_store<&profile::kernel_offsets::cred_usage_offset>("cred_usage_offset"),
        scalar_store<&profile::kernel_offsets::cred_usage_value>("cred_usage_value"),
        scalar_store<&profile::kernel_offsets::cred_caps_offset>("cred_caps_offset"),
        scalar_store<&profile::kernel_offsets::cred_caps_count>("cred_caps_count"),
        scalar_store<&profile::kernel_offsets::cred_caps_value>("cred_caps_value"),
        scalar_store<&profile::kernel_offsets::cred_ref_count>("cred_ref_count"),
        scalar_store<&profile::kernel_offsets::cred_ref0_offset>("cred_ref0_offset"),
        scalar_store<&profile::kernel_offsets::cred_ref1_offset>("cred_ref1_offset"),
        scalar_store<&profile::kernel_offsets::cred_ref2_offset>("cred_ref2_offset"),
        scalar_store<&profile::kernel_offsets::cred_ref3_offset>("cred_ref3_offset"),
        scalar_store<&profile::kernel_offsets::cred_ref0_image>("cred_ref0_image"),
        scalar_store<&profile::kernel_offsets::cred_ref1_image>("cred_ref1_image"),
        scalar_store<&profile::kernel_offsets::cred_ref2_image>("cred_ref2_image"),
        scalar_store<&profile::kernel_offsets::cred_ref3_image>("cred_ref3_image"),
    };

    struct execution_field {
        const char *name;

        void (*store)(profile::execution_settings &, uint32_t);
    };

    /* Same member-pointer pattern for the execution groups. */
    template<auto Member>
    void store_execution(profile::execution_settings &settings, uint32_t value) {
        settings.*Member = value;
    }

    template<auto Member>
    constexpr execution_field exec_field(const char *name) {
        return {name, &store_execution<Member>};
    }

    static int32_t parse_execution_group(std::string_view parent,
                                     std::string_view group_name, const struct execution_field *fields,
                                     size_t field_count, profile::execution_settings *out) {
        const auto group_value = json_member_value(parent, group_name);
        if (!group_value || group_value->empty() || group_value->front() != '{')
            return -1;
        const auto group = json_value_span(*group_value);
        if (!group) return -1;
        for (size_t i = 0; i < field_count; i++) {
            const auto value = json_member_value(*group, fields[i].name);
            int64_t parsed = -1;
            if (!value || !json_parse_int(*value, &parsed) || parsed < 0 ||
                static_cast<uint64_t>(parsed) > UINT32_MAX)
                return -1;
            fields[i].store(*out, static_cast<uint32_t>(parsed));
        }
        return 0;
    }

    static int32_t fill_execution_settings(std::string_view object,
                                       profile::execution_settings *out) {
        const auto execution_value = json_member_value(object, "execution");
        if (!execution_value || execution_value->empty() ||
            execution_value->front() != '{')
            return -1;
        const auto execution = json_value_span(*execution_value);
        if (!execution) return -1;
        memset(out, 0, sizeof(*out));

        static constexpr struct execution_field cpus[] = {
            exec_field<&profile::execution_settings::recommended_main_cpu>("main"),
            exec_field<&profile::execution_settings::recommended_consumer_cpu>("consumer"),
        };
        static constexpr struct execution_field heap[] = {
            exec_field<&profile::execution_settings::heap_prepare_max_attempts>("prepare_max_attempts"),
            exec_field<&profile::execution_settings::heap_prepare_timeout_ms>("prepare_timeout_ms"),
            exec_field<&profile::execution_settings::heap_kernelsnitch_timeout_ms>("kernelsnitch_timeout_ms"),
        };
        static constexpr struct execution_field race[] = {
            exec_field<&profile::execution_settings::race_route_wait_ms>("route_wait_ms"),
            exec_field<&profile::execution_settings::race_setup_settle_us>("setup_settle_us"),
            exec_field<&profile::execution_settings::race_state_poll_interval_us>("state_poll_interval_us"),
        };
        static constexpr struct execution_field stages[] = {
            exec_field<&profile::execution_settings::w1_attempts>("w1_attempts"),
            exec_field<&profile::execution_settings::w1_settle_us>("w1_settle_us"),
            exec_field<&profile::execution_settings::w1_scratch_repair_attempts>("w1_scratch_repair_attempts"),
            exec_field<&profile::execution_settings::w2_attempts>("w2_attempts"),
            exec_field<&profile::execution_settings::w2_settle_us>("w2_settle_us"),
            exec_field<&profile::execution_settings::w3_chain_rounds>("w3_chain_rounds"),
            exec_field<&profile::execution_settings::w3_attempts>("w3_attempts"),
            exec_field<&profile::execution_settings::w3_settle_us>("w3_settle_us"),
        };
        static constexpr struct execution_field handoff[] = {
            exec_field<&profile::execution_settings::handoff_pre_dispatch_settle_ms>("pre_dispatch_settle_ms"),
            exec_field<&profile::execution_settings::handoff_module_poll_attempts>("module_poll_attempts"),
            exec_field<&profile::execution_settings::handoff_module_poll_interval_ms>("module_poll_interval_ms"),
            exec_field<&profile::execution_settings::handoff_enforce_poll_attempts>("enforce_poll_attempts"),
            exec_field<&profile::execution_settings::handoff_enforce_poll_interval_ms>("enforce_poll_interval_ms"),
        };
        if (parse_execution_group(*execution, "recommended_cpus", cpus,
                                  std::size(cpus), out) ||
            parse_execution_group(*execution, "heap", heap,
                                  std::size(heap), out) ||
            parse_execution_group(*execution, "race", race,
                                  std::size(race), out) ||
            parse_execution_group(*execution, "stages", stages,
                                  std::size(stages), out) ||
            parse_execution_group(*execution, "handoff", handoff,
                                  std::size(handoff), out))
            return -1;

        const auto routes_value = json_member_value(*execution, "routes");
        if (!routes_value || routes_value->empty() || routes_value->front() != '{')
            return -1;
        const auto routes = json_value_span(*routes_value);
        if (!routes) return -1;
        static constexpr struct execution_field tcp[] = {
            exec_field<&profile::execution_settings::tcp_attempts>("attempts"),
            exec_field<&profile::execution_settings::tcp_arm_sequence>("arm_sequence"),
            exec_field<&profile::execution_settings::tcp_post_receive_hold_iterations>("post_receive_hold_iterations"),
        };
        static constexpr struct execution_field select_stack[] = {
            exec_field<&profile::execution_settings::select_enter_delay_us>("enter_delay_us"),
            exec_field<&profile::execution_settings::select_timeout_us>("timeout_us"),
            exec_field<&profile::execution_settings::select_consumer_max_calls>("consumer_max_calls"),
            exec_field<&profile::execution_settings::select_consumer_burst_calls>("consumer_burst_calls"),
        };
        static constexpr struct execution_field multicast[] = {
            exec_field<&profile::execution_settings::multicast_ready_timeout_ms>("ready_timeout_ms"),
            exec_field<&profile::execution_settings::multicast_post_requeue_settle_us>("post_requeue_settle_us"),
            exec_field<&profile::execution_settings::multicast_post_adjust_settle_us>("post_adjust_settle_us"),
        };
        return parse_execution_group(*routes, "tcp_zerocopy", tcp,
                                     std::size(tcp), out) ||
               parse_execution_group(*routes, "select_stack", select_stack,
                                     std::size(select_stack), out) ||
               parse_execution_group(*routes, "multicast_waiter", multicast,
                                     std::size(multicast), out)
                   ? -1
                   : 0;
    }


    /* Fill `out` from one JSON object. Fields absent from the JSON keep whatever
 * the caller put into `out` (zeroed for a fresh table, or a built-in entry the
 * JSON is overriding). */
    /* Per-route branch decoder registry: adding a route branch means adding a
     * decoder here, not extending a switch. */
    struct route_branch_decoder {
        uint8_t route_kind;
        void (*apply)(std::string_view branch, profile::kernel_offsets *out, int64_t *num);
    };

    static void decode_tcp_branch(std::string_view branch,
                                  profile::kernel_offsets *out, int64_t *num) {
        const auto v = json_member_value(branch, "compact_waiter");
        if (v && json_parse_int(*v, num)) out->compact_waiter = static_cast<uint8_t>(*num);
    }

    static void decode_select_branch(std::string_view branch,
                                     profile::kernel_offsets *out, int64_t *num) {
        const auto v = json_member_value(branch, "waiter_shift");
        if (v && json_parse_int(*v, num)) out->pselect_waiter_shift = static_cast<int32_t>(*num);
    }

    static void decode_multicast_branch(std::string_view branch,
                                        profile::kernel_offsets *out, int64_t *num) {
        static constexpr struct scalar_field kMcastFields[] = {
            scalar_store<&profile::kernel_offsets::mcast_waiter_off>("waiter_off"),
            scalar_store<&profile::kernel_offsets::mcast_buffer_size>("buffer_size"),
            scalar_store<&profile::kernel_offsets::mcast_task_offset>("task_offset"),
            scalar_store<&profile::kernel_offsets::mcast_lock_offset>("lock_offset"),
            scalar_store<&profile::kernel_offsets::mcast_fake_lock_offset>("fake_lock_offset"),
            scalar_store<&profile::kernel_offsets::mcast_fake_task_offset>("fake_task_offset"),
            scalar_store<&profile::kernel_offsets::mcast_lock_slots_offset>("lock_slots_offset"),
            scalar_store<&profile::kernel_offsets::mcast_lock_slot_count>("lock_slot_count"),
            scalar_store<&profile::kernel_offsets::mcast_lock_slot_stride>("lock_slot_stride"),
        };
        for (const struct scalar_field &field: kMcastFields) {
            const auto v = json_member_value(branch, field.name);
            if (v && json_parse_int(*v, num)) field.store(*out, *num);
        }
        /* The multicast route also relies on the compact waiter layout. */
        const auto compact = json_member_value(branch, "compact_waiter");
        if (compact && json_parse_int(*compact, num)) {
            out->compact_waiter = static_cast<uint8_t>(*num);
        }
    }

    static const route_branch_decoder kRouteBranchDecoders[] = {
        {profile::kRouteTcpZerocopy, decode_tcp_branch},
        {profile::kRouteSelectStack, decode_select_branch},
        {profile::kRouteMulticastWaiter, decode_multicast_branch},
    };

    /* Writes one route branch's fields into the native struct. */
    static void apply_route_branch_values(std::string_view branch,
                                          uint8_t route_kind, profile::kernel_offsets *out, int64_t *num) {
        for (const route_branch_decoder &decoder: kRouteBranchDecoders) {
            if (decoder.route_kind == route_kind) {
                decoder.apply(branch, out, num);
                return;
            }
        }
    }

    static void fill_external_entry(profile::kernel_offsets *out,
                                    const char *release_buf, std::string_view object) {
        int64_t num;
        out->uname_r = release_buf;
        /* Offsets are namespaced as task/cred/off/mcast objects; the flat keys
     * remain as a legacy fallback for older offsets.json files. */
        const ProfileGroups groups = {
            .task_struct = json_object_span(object, "task_struct"),
            .cred = json_object_span(object, "cred"),
            .offset = json_object_span(object, "offset"),
            .mcast = json_object_span(object, "mcast"),
        };
        for (size_t i = 0; i < std::size(g_profile_map); i++) {
            if (read_namespaced_scalar(object, g_profile_map[i].name, groups, &num)) {
                g_profile_map[i].store(*out, num);
            }
        }
        /* Resolved profiles use one flat object. Keep the nested reads below only
     * for compatibility with offsets.json files produced by older extractors. */
        for (size_t i = 0; i < std::size(g_symbol_map); i++) {
            if (read_namespaced_scalar(object, g_symbol_map[i].name, groups, &num)) {
                g_symbol_map[i].store(*out, num);
            }
        }
        for (size_t i = 0; i < std::size(g_task_map); i++) {
            if (read_namespaced_scalar(object, g_task_map[i].name, groups, &num)) {
                g_task_map[i].store(*out, num);
            }
        }
        const auto symbols = json_member_value(object, "symbols");
        if (symbols && !symbols->empty() && symbols->front() == '{') {
            if (const auto symbols_span = json_value_span(*symbols)) {
                for (size_t i = 0; i < std::size(g_symbol_map); i++) {
                    const auto mv = json_member_value(*symbols_span,
                                                      g_symbol_map[i].name);
                    if (mv && json_parse_int(*mv, &num)) {
                        g_symbol_map[i].store(*out, num);
                    }
                }
            }
        }
        const auto struct_fields = json_member_value(object, "struct_fields");
        if (struct_fields && !struct_fields->empty() &&
            struct_fields->front() == '{') {
            if (const auto fields_span = json_value_span(*struct_fields)) {
                for (size_t i = 0; i < std::size(g_task_map); i++) {
                    const auto mv = json_member_value(*fields_span,
                                                      g_task_map[i].name);
                    if (mv && json_parse_int(*mv, &num)) {
                        g_task_map[i].store(*out, num);
                    }
                }
            }
        }
        /* KernelSnitch tuning moved under a nested object; the flat keys read above
     * stay as a legacy fallback for older offsets.json files. */
        const auto snitch = json_member_value(object, "kernelsnitch");
        if (snitch && !snitch->empty() && snitch->front() == '{') {
            if (const auto snitch_span = json_value_span(*snitch)) {
                const auto collisions = json_member_value(*snitch_span, "collisions");
                if (collisions && json_parse_int(*collisions, &num)) {
                    out->kernelsnitch_collisions = static_cast<uint32_t>(num);
                }
                const auto stride = json_member_value(*snitch_span, "mm_struct_sz");
                if (stride && json_parse_int(*stride, &num)) {
                    out->mm_struct_sz = static_cast<uint32_t>(num);
                }
            }
        }
        /* Routes are {"<name>": {fields}} with exactly one branch. */
        const auto route_value = json_member_value(object, "route");
        if (route_value && !route_value->empty() && route_value->front() == '{') {
            std::string_view branch_name;
            std::string_view branch;
            if (json_first_member(*route_value, &branch_name, &branch)) {
                const uint8_t route_kind = profile::route_kind_from_string(branch_name);
                if (route_kind != profile::kRouteAuto) {
                    out->route = route_kind;
                    apply_route_branch_values(branch, route_kind, out, &num);
                }
            }
        } else if (route_value && !route_value->empty() &&
                   route_value->front() == '"') {
            /* Legacy flat spelling: "route": "<name>". */
            std::string_view route_cursor = *route_value;
            std::array < char, 32 > route_name{};
            if (json_read_string(route_cursor, route_name.data(), route_name.size())) {
                out->route = profile::route_kind_from_string(route_name.data());
            }
        }
        /* Fallback declaration: {"to": "<route>", "route": {"<route>": {fields}}}. */
        const auto fallback_value = json_member_value(object, "fallback");
        if (fallback_value && !fallback_value->empty() &&
            fallback_value->front() == '{') {
            const auto to_value = json_member_value(*fallback_value, "to");
            if (to_value && !to_value->empty() && to_value->front() == '"') {
                std::string_view to_cursor = *to_value;
                std::array < char, 32 > to_name{};
                if (json_read_string(to_cursor, to_name.data(), to_name.size())) {
                    /* "none" and unknown names both decode as kRouteAuto. */
                    out->fallback_route = profile::route_kind_from_string(to_name.data());
                }
            }
            const auto fallback_route_value =
                    json_member_value(*fallback_value, "route");
            if (fallback_route_value && !fallback_route_value->empty() &&
                fallback_route_value->front() == '{') {
                std::string_view branch_name;
                std::string_view branch;
                if (json_first_member(*fallback_route_value, &branch_name, &branch)) {
                    const uint8_t route_kind = profile::route_kind_from_string(branch_name);
                    if (route_kind != profile::kRouteAuto) {
                        apply_route_branch_values(branch, route_kind, out, &num);
                    }
                }
            }
        } else if (fallback_value && !fallback_value->empty() &&
                   fallback_value->front() == '"') {
            /* Legacy flat spelling: "fallback_to": "<name>". */
            std::string_view fallback_cursor = *fallback_value;
            std::array < char, 32 > fallback_name{};
            if (json_read_string(fallback_cursor, fallback_name.data(), fallback_name.size())) {
                out->fallback_route = profile::route_kind_from_string(fallback_name.data());
            }
        }
        /* Legacy flat fallback key from transition builds. */
        const auto legacy_fallback = json_member_value(object, "fallback_to");
        if (legacy_fallback && !legacy_fallback->empty() &&
            legacy_fallback->front() == '"') {
            std::string_view legacy_cursor = *legacy_fallback;
            std::array < char, 32 > legacy_name{};
            if (json_read_string(legacy_cursor, legacy_name.data(), legacy_name.size())) {
                out->fallback_route = profile::route_kind_from_string(legacy_name.data());
            }
        }
        /* a zeroed entry selects the 6.6 waiter layout; warn rather than fail quietly */
        if (std::string_view(out->uname_r).starts_with("6.1.") && !out->compact_waiter) {
            fprintf(stderr,
                    "warning: imported 6.1 entry has no compact_waiter; it will run "
                    "the 6.6 rb_node waiter layout and miss\n");
        }
    }

    int32_t parse_resolved_profile_json(std::string_view document,
                                    profile::kernel_offsets *out, char *release_buf, size_t release_buf_cap) {
        const std::string_view object = json_skip_ws(document);
        int32_t result = -1;
        if (!object.empty() && object.front() == '{') {
            std::string_view cursor = object;
            if (json_skip_value(cursor) && json_skip_ws(cursor).empty()) {
                int64_t schema_version = 0;
                const auto schema_value =
                        json_member_value(object, "schema_version");
                const auto execution_value =
                        json_member_value(object, "execution");
                const auto release_value = json_member_value(object, "release");
                if (schema_value &&
                    json_parse_int(*schema_value, &schema_version) &&
                    schema_version == 1 && release_value) {
                    std::string_view release_cursor = *release_value;
                    std::array < char, 256 > release{};
                    if (json_read_string(release_cursor, release.data(),
                                         release.size()) &&
                        strlen(release.data()) < release_buf_cap) {
                        memset(out, 0, sizeof(*out));
                        /* strlen(release) < release_buf_cap was checked above */
                        strcpy(release_buf, release.data()); // NOLINT(clang-analyzer-security.insecureAPI.strcpy)
                        fill_external_entry(out, release_buf, object);
                        /* execution tuning is merged by Kotlin defaults; it is
                     * only decoded when a source document carries one. */
                        if (execution_value && !execution_value->empty() &&
                            execution_value->front() == '{') {
                            result = fill_execution_settings(object, &out->execution);
                        } else {
                            result = 0;
                        }
                    }
                }
            }
        }
        return result;
    }

    /* Native no longer infers the route: a document without an explicit branch is
 * rejected here. Legacy documents get their route baked in by Kotlin's
 * LegacyProfileConverter before reaching native. */
    static int32_t require_explicit_route(const profile::kernel_offsets *out) {
        if (out->route == profile::kRouteAuto) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }

    int32_t load_resolved_profile_json(const char *path, profile::kernel_offsets *out,
                                   char *release_buf, size_t release_buf_cap) {
        auto file_result = read_profile_file(path);
        if (!file_result) {
            errno = file_result.error().code.value();
            return -1;
        }
        const std::string &file = file_result.value();
        const int32_t rc = parse_resolved_profile_json(
            std::string_view(file.data(), file.size()), out, release_buf,
            release_buf_cap);
        return rc == 0 ? require_explicit_route(out) : rc;
    }

    /* Legacy/JSON lexical access for legacy_support. Thin wrappers keep the
 * bounded-cursor primitives private to this file. */
    namespace profile_json {
        void fill_entry(profile::kernel_offsets *out, const char *release_buf,
                        std::string_view object) {
            fill_external_entry(out, release_buf, object);
        }

        int32_t select_entry(std::string_view document, const char *release,
                         std::string_view *entry_out) {
            if (!release || !entry_out) return -1;
            const std::string_view start = json_skip_ws(document);
            if (start.empty()) return -1;
            if (start.front() == '{') {
                *entry_out = start;
                return 0;
            }
            if (start.front() != '[') return -1;
            std::string_view cursor = start;
            cursor.remove_prefix(1);
            for (;;) {
                cursor = json_skip_ws(cursor);
                if (cursor.empty()) return -1;
                if (cursor.front() == ']') return -1;
                const auto span = json_value_span(cursor);
                if (!span) return -1;
                cursor.remove_prefix(span->size());
                const auto rel = json_member_value(*span, "release");
                if (rel && !rel->empty() && rel->front() == '"') {
                    std::string_view rel_view = *rel;
                    std::array < char, 256 > name{};
                    if (json_read_string(rel_view, name.data(), name.size()) &&
                        std::string_view(name.data()) == release) {
                        *entry_out = *span;
                        return 0;
                    }
                }
                cursor = json_skip_ws(cursor);
                if (!cursor.empty() && cursor.front() == ',') cursor.remove_prefix(1);
            }
        }
    } // namespace profile_json
} // namespace ghostlock::legacy
