/* Resolved-profile transport decoder.
 *
 * Native receives one fully resolved profile file from Kotlin and performs
 * strict decoding plus defensive validation. The parser is a bounded
 * string_view cursor: every helper consumes from or returns a view into the
 * caller-owned std::string, so no pointer pair can drift out of bounds.
 * Configuration-source selection and merging stay on the Kotlin side.
 */
#include "offsets_json.h"
#include "profile_binary.h"
#include "support/native_resource.hpp"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <string_view>

using namespace ghostlock;

#define PROFILE_JSON_MAX_SIZE (1U << 20)

static Result<std::string> read_profile_file(const char *path) {
    UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
    if (!fd.valid()) {
        return Result<std::string>::failure(
                SysError::from_errno());
    }

    std::string buffer(PROFILE_JSON_MAX_SIZE, '\0');
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
        return Result<std::string>::failure(
                SysError::from_errno());
    }
    if (used == 0 || used == PROFILE_JSON_MAX_SIZE) {
        errno = used == PROFILE_JSON_MAX_SIZE ? EFBIG : EINVAL;
        return Result<std::string>::failure(
                SysError::from_errno());
    }
    buffer.resize(used);
    return Result<std::string>::success(std::move(buffer));
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
        int depth = 1;
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
        return object.empty() ? std::nullopt
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
    if (strncmp(name, "task_", 5) == 0) {
        group = &groups.task_struct;
        field = std::string_view(name + 5);
    } else if (strncmp(name, "cred_", 5) == 0) {
        group = &groups.cred;
        field = std::string_view(name + 5);
    } else if (strncmp(name, "off_", 4) == 0) {
        group = &groups.offset;
        field = std::string_view(name + 4);
    } else if (strncmp(name, "mcast_", 6) == 0) {
        group = &groups.mcast;
        field = std::string_view(name + 6);
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
        int digits = 0;
        while (i < in.size() && isxdigit((unsigned char) in[i])) {
            const char c = in[i];
            const int d = (c <= '9') ? (c - '0')
                                     : (tolower((unsigned char) c) - 'a' + 10);
            v = v * 16 + (uint64_t) d;
            digits++;
            i++;
        }
        if (!digits) return false;
    } else {
        int digits = 0;
        while (i < in.size() && in[i] >= '0' && in[i] <= '9') {
            v = v * 10 + (uint64_t) (in[i] - '0');
            digits++;
            i++;
        }
        if (!digits) return false;
    }
    *out = neg ? -(int64_t) v : (int64_t) v;
    return true;
}

static const struct {
    const char *name;
    size_t off;
} g_symbol_map[] = {
        {"off_init_task", offsetof(struct kernel_offsets, off_init_task)},
        {"off_init_cred", offsetof(struct kernel_offsets, off_init_cred)},
        {"off_empty_zero_page", offsetof(struct kernel_offsets, off_empty_zero_page)},
        {"off_mcast_fake_bss", offsetof(struct kernel_offsets, off_mcast_fake_bss)},
        {"off_root_task_group", offsetof(struct kernel_offsets, off_root_task_group)},
        {"off_selinux_enforcing", offsetof(struct kernel_offsets, off_selinux_enforcing)},
        {"off_selinux_blob_sizes", offsetof(struct kernel_offsets, off_selinux_blob_sizes)},
        {"off_security_hook_heads", offsetof(struct kernel_offsets, off_security_hook_heads)},
        {"off_slide_nfulnl_logger", offsetof(struct kernel_offsets, off_slide_nfulnl_logger)},
        {"off_slide_loggers_0_1", offsetof(struct kernel_offsets, off_slide_loggers_0_1)},
        {"off_slide_boot_id", offsetof(struct kernel_offsets, off_slide_boot_id)},
};

static const struct {
    const char *name;
    size_t off;
} g_task_map[] = {
        {"task_prio", offsetof(struct kernel_offsets, task_prio)},
        {"task_normal_prio", offsetof(struct kernel_offsets, task_normal_prio)},
        {"task_sched_task_group", offsetof(struct kernel_offsets, task_sched_task_group)},
        {"task_pi_lock", offsetof(struct kernel_offsets, task_pi_lock)},
        {"task_pi_waiters", offsetof(struct kernel_offsets, task_pi_waiters)},
        {"task_pi_top_task", offsetof(struct kernel_offsets, task_pi_top_task)},
        {"task_pi_blocked_on", offsetof(struct kernel_offsets, task_pi_blocked_on)},
        {"task_pid", offsetof(struct kernel_offsets, task_pid)},
        {"task_tgid", offsetof(struct kernel_offsets, task_tgid)},
        {"task_atomic_flags", offsetof(struct kernel_offsets, task_atomic_flags)},
        {"task_real_cred", offsetof(struct kernel_offsets, task_real_cred)},
        {"task_cred", offsetof(struct kernel_offsets, task_cred)},
        {"task_comm", offsetof(struct kernel_offsets, task_comm)},
        {"task_tasks", offsetof(struct kernel_offsets, task_tasks)},
        {"task_seccomp", offsetof(struct kernel_offsets, task_seccomp)},
};

enum class ScalarWidth : int {
    U8,
    U32,
    U64,
    I32,
};
static const struct {
    const char *name;
    size_t off;
    ScalarWidth width;
} g_profile_map[] = {
        {"kernel_major", offsetof(struct kernel_offsets, kernel_major), ScalarWidth::U8},
        {"recommend_shizuku", offsetof(struct kernel_offsets, recommend_shizuku), ScalarWidth::U8},
        {"kernel_phys_load", offsetof(struct kernel_offsets, kernel_phys_load), ScalarWidth::U64},
        {"pselect_waiter_shift", offsetof(struct kernel_offsets, pselect_waiter_shift), ScalarWidth::I32},
        {"mcast_waiter_off", offsetof(struct kernel_offsets, mcast_waiter_off), ScalarWidth::I32},
        {"mcast_buffer_size", offsetof(struct kernel_offsets, mcast_buffer_size), ScalarWidth::U32},
        {"mcast_task_offset", offsetof(struct kernel_offsets, mcast_task_offset), ScalarWidth::U32},
        {"mcast_lock_offset", offsetof(struct kernel_offsets, mcast_lock_offset), ScalarWidth::U32},
        {"mcast_fake_lock_offset", offsetof(struct kernel_offsets, mcast_fake_lock_offset), ScalarWidth::U32},
        {"mcast_fake_task_offset", offsetof(struct kernel_offsets, mcast_fake_task_offset), ScalarWidth::U32},
        {"mcast_lock_slots_offset", offsetof(struct kernel_offsets, mcast_lock_slots_offset), ScalarWidth::U32},
        {"mcast_lock_slot_count", offsetof(struct kernel_offsets, mcast_lock_slot_count), ScalarWidth::U32},
        {"mcast_lock_slot_stride", offsetof(struct kernel_offsets, mcast_lock_slot_stride), ScalarWidth::U32},
        {"kernelsnitch_collisions", offsetof(struct kernel_offsets, kernelsnitch_collisions), ScalarWidth::U32},
        {"compact_waiter", offsetof(struct kernel_offsets, compact_waiter), ScalarWidth::U8},
        {"mm_struct_sz", offsetof(struct kernel_offsets, mm_struct_sz), ScalarWidth::U32},
        {"cred_copy_size", offsetof(struct kernel_offsets, cred_copy_size), ScalarWidth::U32},
        {"cred_usage_offset", offsetof(struct kernel_offsets, cred_usage_offset), ScalarWidth::U32},
        {"cred_usage_value", offsetof(struct kernel_offsets, cred_usage_value), ScalarWidth::U32},
        {"cred_caps_offset", offsetof(struct kernel_offsets, cred_caps_offset), ScalarWidth::U32},
        {"cred_caps_count", offsetof(struct kernel_offsets, cred_caps_count), ScalarWidth::U32},
        {"cred_caps_value", offsetof(struct kernel_offsets, cred_caps_value), ScalarWidth::U64},
        {"cred_ref_count", offsetof(struct kernel_offsets, cred_ref_count), ScalarWidth::U32},
        {"cred_ref0_offset", offsetof(struct kernel_offsets, cred_ref0_offset), ScalarWidth::U32},
        {"cred_ref1_offset", offsetof(struct kernel_offsets, cred_ref1_offset), ScalarWidth::U32},
        {"cred_ref2_offset", offsetof(struct kernel_offsets, cred_ref2_offset), ScalarWidth::U32},
        {"cred_ref3_offset", offsetof(struct kernel_offsets, cred_ref3_offset), ScalarWidth::U32},
        {"cred_ref0_image", offsetof(struct kernel_offsets, cred_ref0_image), ScalarWidth::U64},
        {"cred_ref1_image", offsetof(struct kernel_offsets, cred_ref1_image), ScalarWidth::U64},
        {"cred_ref2_image", offsetof(struct kernel_offsets, cred_ref2_image), ScalarWidth::U64},
        {"cred_ref3_image", offsetof(struct kernel_offsets, cred_ref3_image), ScalarWidth::U64},
};

/* Decoupling plan: store one typed scalar in a profile under construction.
 * Inputs: destination, field descriptor and value; output: updated candidate.
 * Future: target_profile_store_scalar(ProfileBuilder *, ...). */
static void store_profile_scalar(struct kernel_offsets *out, size_t off,
        ScalarWidth width, int64_t value) {
    char *field = reinterpret_cast<char *>(out) + off;
    switch (width) {
        case ScalarWidth::U8:
            *reinterpret_cast<uint8_t *>(field) = static_cast<uint8_t>(value);
            break;
        case ScalarWidth::U32:
            *reinterpret_cast<uint32_t *>(field) = static_cast<uint32_t>(value);
            break;
        case ScalarWidth::U64:
            *reinterpret_cast<uint64_t *>(field) = static_cast<uint64_t>(value);
            break;
        case ScalarWidth::I32:
            *reinterpret_cast<int *>(field) = static_cast<int>(value);
            break;
    }
}

struct execution_field {
    const char *name;
    size_t offset;
};

#define EXEC_FIELD(json_name, member) \
  {json_name, offsetof(struct execution_settings, member)}

static int parse_execution_group(std::string_view parent,
        std::string_view group_name, const struct execution_field *fields,
        size_t field_count, struct execution_settings *out) {
    const auto group_value = json_member_value(parent, group_name);
    if (!group_value || group_value->empty() || group_value->front() != '{')
        return -1;
    const auto group = json_value_span(*group_value);
    if (!group) return -1;
    for (size_t i = 0; i < field_count; i++) {
        const auto value = json_member_value(*group, fields[i].name);
        int64_t parsed = -1;
        if (!value || !json_parse_int(*value, &parsed) || parsed < 0 ||
                (uint64_t) parsed > UINT32_MAX)
            return -1;
        *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(out) + fields[i].offset) = static_cast<uint32_t>(parsed);
    }
    return 0;
}

static int fill_execution_settings(std::string_view object,
        struct execution_settings *out) {
    const auto execution_value = json_member_value(object, "execution");
    if (!execution_value || execution_value->empty() ||
            execution_value->front() != '{')
        return -1;
    const auto execution = json_value_span(*execution_value);
    if (!execution) return -1;
    memset(out, 0, sizeof(*out));

    static const struct execution_field cpus[] = {
            EXEC_FIELD("main", recommended_main_cpu),
            EXEC_FIELD("consumer", recommended_consumer_cpu),
    };
    static const struct execution_field heap[] = {
            EXEC_FIELD("prepare_max_attempts", heap_prepare_max_attempts),
            EXEC_FIELD("prepare_timeout_ms", heap_prepare_timeout_ms),
            EXEC_FIELD("kernelsnitch_timeout_ms", heap_kernelsnitch_timeout_ms),
    };
    static const struct execution_field race[] = {
            EXEC_FIELD("route_wait_ms", race_route_wait_ms),
            EXEC_FIELD("setup_settle_us", race_setup_settle_us),
            EXEC_FIELD("state_poll_interval_us", race_state_poll_interval_us),
    };
    static const struct execution_field stages[] = {
            EXEC_FIELD("w1_attempts", w1_attempts),
            EXEC_FIELD("w1_settle_us", w1_settle_us),
            EXEC_FIELD("w1_scratch_repair_attempts", w1_scratch_repair_attempts),
            EXEC_FIELD("w2_attempts", w2_attempts),
            EXEC_FIELD("w2_settle_us", w2_settle_us),
            EXEC_FIELD("w3_chain_rounds", w3_chain_rounds),
            EXEC_FIELD("w3_attempts", w3_attempts),
            EXEC_FIELD("w3_settle_us", w3_settle_us),
    };
    static const struct execution_field handoff[] = {
            EXEC_FIELD("pre_dispatch_settle_ms", handoff_pre_dispatch_settle_ms),
            EXEC_FIELD("module_poll_attempts", handoff_module_poll_attempts),
            EXEC_FIELD("module_poll_interval_ms", handoff_module_poll_interval_ms),
            EXEC_FIELD("enforce_poll_attempts", handoff_enforce_poll_attempts),
            EXEC_FIELD("enforce_poll_interval_ms", handoff_enforce_poll_interval_ms),
    };
    if (parse_execution_group(*execution, "recommended_cpus", cpus,
            sizeof(cpus) / sizeof(cpus[0]), out) ||
            parse_execution_group(*execution, "heap", heap,
                    sizeof(heap) / sizeof(heap[0]), out) ||
            parse_execution_group(*execution, "race", race,
                    sizeof(race) / sizeof(race[0]), out) ||
            parse_execution_group(*execution, "stages", stages,
                    sizeof(stages) / sizeof(stages[0]), out) ||
            parse_execution_group(*execution, "handoff", handoff,
                    sizeof(handoff) / sizeof(handoff[0]), out))
        return -1;

    const auto routes_value = json_member_value(*execution, "routes");
    if (!routes_value || routes_value->empty() || routes_value->front() != '{')
        return -1;
    const auto routes = json_value_span(*routes_value);
    if (!routes) return -1;
    static const struct execution_field tcp[] = {
            EXEC_FIELD("attempts", tcp_attempts),
            EXEC_FIELD("arm_sequence", tcp_arm_sequence),
            EXEC_FIELD("post_receive_hold_iterations", tcp_post_receive_hold_iterations),
    };
    static const struct execution_field select_stack[] = {
            EXEC_FIELD("enter_delay_us", select_enter_delay_us),
            EXEC_FIELD("timeout_us", select_timeout_us),
            EXEC_FIELD("consumer_max_calls", select_consumer_max_calls),
            EXEC_FIELD("consumer_burst_calls", select_consumer_burst_calls),
    };
    static const struct execution_field multicast[] = {
            EXEC_FIELD("ready_timeout_ms", multicast_ready_timeout_ms),
            EXEC_FIELD("post_requeue_settle_us", multicast_post_requeue_settle_us),
            EXEC_FIELD("post_adjust_settle_us", multicast_post_adjust_settle_us),
    };
    return parse_execution_group(*routes, "tcp_zerocopy", tcp,
            sizeof(tcp) / sizeof(tcp[0]), out) ||
            parse_execution_group(*routes, "select_stack", select_stack,
                    sizeof(select_stack) / sizeof(select_stack[0]), out) ||
            parse_execution_group(*routes, "multicast_waiter", multicast,
                    sizeof(multicast) / sizeof(multicast[0]), out)
            ? -1
            : 0;
}

#undef EXEC_FIELD

/* Fill `out` from one JSON object. Fields absent from the JSON keep whatever
 * the caller put into `out` (zeroed for a fresh table, or a built-in entry the
 * JSON is overriding). */
/* Writes one route branch's fields into the native struct. */
static void apply_route_branch_values(std::string_view branch,
        uint8_t route_kind, struct kernel_offsets *out, int64_t *num) {
    switch (route_kind) {
        case kRouteTcpZerocopy: {
            const auto v = json_member_value(branch, "compact_waiter");
            if (v && json_parse_int(*v, num)) out->compact_waiter = (uint8_t) *num;
            break;
        }
        case kRouteSelectStack: {
            const auto v = json_member_value(branch, "waiter_shift");
            if (v && json_parse_int(*v, num)) out->pselect_waiter_shift = (int) *num;
            break;
        }
        case kRouteMulticastWaiter: {
            struct Scalar {
                const char *name;
                size_t offset;
            };
            static const Scalar kMcastFields[] = {
                {"waiter_off", offsetof(struct kernel_offsets, mcast_waiter_off)},
                {"buffer_size", offsetof(struct kernel_offsets, mcast_buffer_size)},
                {"task_offset", offsetof(struct kernel_offsets, mcast_task_offset)},
                {"lock_offset", offsetof(struct kernel_offsets, mcast_lock_offset)},
                {"fake_lock_offset", offsetof(struct kernel_offsets, mcast_fake_lock_offset)},
                {"fake_task_offset", offsetof(struct kernel_offsets, mcast_fake_task_offset)},
                {"lock_slots_offset", offsetof(struct kernel_offsets, mcast_lock_slots_offset)},
                {"lock_slot_count", offsetof(struct kernel_offsets, mcast_lock_slot_count)},
                {"lock_slot_stride", offsetof(struct kernel_offsets, mcast_lock_slot_stride)},
            };
            for (const Scalar &field : kMcastFields) {
                const auto v = json_member_value(branch, field.name);
                if (v && json_parse_int(*v, num)) {
                    *reinterpret_cast<uint32_t *>(
                            reinterpret_cast<char *>(out) + field.offset) =
                            (uint32_t) *num;
                }
            }
            /* The multicast route also relies on the compact waiter layout. */
            const auto compact = json_member_value(branch, "compact_waiter");
            if (compact && json_parse_int(*compact, num)) {
                out->compact_waiter = (uint8_t) *num;
            }
            break;
        }
        default:
            break;
    }
}

/* Decoupling plan: merge one JSON entry into a profile candidate. Inputs:
 * object span and release buffer; output: populated candidate/error. Future:
 * target_profile_parse_entry(ProfileBuilder *, JsonObjectView). */
static void fill_external_entry(struct kernel_offsets *out,
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
    for (size_t i = 0; i < sizeof(g_profile_map) / sizeof(g_profile_map[0]); i++) {
        if (read_namespaced_scalar(object, g_profile_map[i].name, groups, &num)) {
            store_profile_scalar(out, g_profile_map[i].off,
                    g_profile_map[i].width, num);
        }
    }
    /* Resolved profiles use one flat object. Keep the nested reads below only
     * for compatibility with offsets.json files produced by older extractors. */
    for (size_t i = 0; i < sizeof(g_symbol_map) / sizeof(g_symbol_map[0]); i++) {
        if (read_namespaced_scalar(object, g_symbol_map[i].name, groups, &num)) {
            *reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(out) + g_symbol_map[i].off) = static_cast<uint64_t>(num);
        }
    }
    for (size_t i = 0; i < sizeof(g_task_map) / sizeof(g_task_map[0]); i++) {
        if (read_namespaced_scalar(object, g_task_map[i].name, groups, &num)) {
            *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(out) + g_task_map[i].off) = static_cast<uint32_t>(num);
        }
    }
    const auto symbols = json_member_value(object, "symbols");
    if (symbols && !symbols->empty() && symbols->front() == '{') {
        if (const auto symbols_span = json_value_span(*symbols)) {
            for (size_t i = 0; i < sizeof(g_symbol_map) / sizeof(g_symbol_map[0]);
                 i++) {
                const auto mv = json_member_value(*symbols_span,
                        g_symbol_map[i].name);
                if (mv && json_parse_int(*mv, &num)) {
                    *reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(out) + g_symbol_map[i].off) =
                            (uint64_t) num;
                }
            }
        }
    }
    const auto struct_fields = json_member_value(object, "struct_fields");
    if (struct_fields && !struct_fields->empty() &&
            struct_fields->front() == '{') {
        if (const auto fields_span = json_value_span(*struct_fields)) {
            for (size_t i = 0; i < sizeof(g_task_map) / sizeof(g_task_map[0]);
                 i++) {
                const auto mv = json_member_value(*fields_span,
                        g_task_map[i].name);
                if (mv && json_parse_int(*mv, &num)) {
                    *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(out) + g_task_map[i].off) =
                            (uint32_t) num;
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
                out->kernelsnitch_collisions = (uint32_t) num;
            }
            const auto stride = json_member_value(*snitch_span, "mm_struct_sz");
            if (stride && json_parse_int(*stride, &num)) {
                out->mm_struct_sz = (uint32_t) num;
            }
        }
    }
    /* Routes are {"<name>": {fields}} with exactly one branch. */
    const auto route_value = json_member_value(object, "route");
    if (route_value && !route_value->empty() && route_value->front() == '{') {
        std::string_view branch_name;
        std::string_view branch;
        if (json_first_member(*route_value, &branch_name, &branch)) {
            const uint8_t route_kind = route_kind_from_string(branch_name);
            if (route_kind != kRouteAuto) {
                out->route = route_kind;
                apply_route_branch_values(branch, route_kind, out, &num);
            }
        }
    } else if (route_value && !route_value->empty() &&
            route_value->front() == '"') {
        /* Legacy flat spelling: "route": "<name>". */
        std::string_view route_cursor = *route_value;
        char route_name[32];
        if (json_read_string(route_cursor, route_name, sizeof(route_name))) {
            out->route = route_kind_from_string(route_name);
        }
    }
    /* Fallback declaration: {"to": "<route>", "route": {"<route>": {fields}}}. */
    const auto fallback_value = json_member_value(object, "fallback");
    if (fallback_value && !fallback_value->empty() &&
            fallback_value->front() == '{') {
        const auto to_value = json_member_value(*fallback_value, "to");
        if (to_value && !to_value->empty() && to_value->front() == '"') {
            std::string_view to_cursor = *to_value;
            char to_name[32];
            if (json_read_string(to_cursor, to_name, sizeof(to_name))) {
                /* "none" and unknown names both decode as kRouteAuto. */
                out->fallback_route = route_kind_from_string(to_name);
            }
        }
        const auto fallback_route_value =
                json_member_value(*fallback_value, "route");
        if (fallback_route_value && !fallback_route_value->empty() &&
                fallback_route_value->front() == '{') {
            std::string_view branch_name;
            std::string_view branch;
            if (json_first_member(*fallback_route_value, &branch_name, &branch)) {
                const uint8_t route_kind = route_kind_from_string(branch_name);
                if (route_kind != kRouteAuto) {
                    apply_route_branch_values(branch, route_kind, out, &num);
                }
            }
        }
    } else if (fallback_value && !fallback_value->empty() &&
            fallback_value->front() == '"') {
        /* Legacy flat spelling: "fallback_to": "<name>". */
        std::string_view fallback_cursor = *fallback_value;
        char fallback_name[32];
        if (json_read_string(fallback_cursor, fallback_name, sizeof(fallback_name))) {
            out->fallback_route = route_kind_from_string(fallback_name);
        }
    }
    /* Legacy flat fallback key from transition builds. */
    const auto legacy_fallback = json_member_value(object, "fallback_to");
    if (legacy_fallback && !legacy_fallback->empty() &&
            legacy_fallback->front() == '"') {
        std::string_view legacy_cursor = *legacy_fallback;
        char legacy_name[32];
        if (json_read_string(legacy_cursor, legacy_name, sizeof(legacy_name))) {
            out->fallback_route = route_kind_from_string(legacy_name);
        }
    }
    /* a zeroed entry selects the 6.6 waiter layout; warn rather than fail quietly */
    if (strncmp(out->uname_r, "6.1.", 4) == 0 && !out->compact_waiter) {
        fprintf(stderr,
                "warning: imported 6.1 entry has no compact_waiter; it will run "
                "the 6.6 rb_node waiter layout and miss\n");
    }
}

int parse_resolved_profile_json(std::string_view document,
        struct kernel_offsets *out, char *release_buf, size_t release_buf_cap) {
    const std::string_view object = json_skip_ws(document);
    int result = -1;
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
                char release[256];
                if (json_read_string(release_cursor, release,
                            sizeof(release)) &&
                        strlen(release) < release_buf_cap) {
                    memset(out, 0, sizeof(*out));
                    /* strlen(release) < release_buf_cap was checked above */
                    strcpy(release_buf, release);  // NOLINT(clang-analyzer-security.insecureAPI.strcpy)
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

int load_resolved_profile_json(const char *path, struct kernel_offsets *out,
        char *release_buf, size_t release_buf_cap) {
    auto file_result = read_profile_file(path);
    if (!file_result) {
        errno = file_result.error().code.value();
        return -1;
    }
    const std::string &file = file_result.value();
    return parse_resolved_profile_json(
            std::string_view(file.data(), file.size()), out, release_buf,
            release_buf_cap);
}

int load_resolved_profile(const char *path, struct kernel_offsets *out,
        char *release_buf, size_t release_buf_cap) {
    auto file_result = read_profile_file(path);
    if (!file_result) {
        errno = file_result.error().code.value();
        return -1;
    }
    const std::string &file = file_result.value();
    const std::string_view document(file.data(), file.size());
    /* Kotlin hands over the typed binary layout; JSON stays for assets,
     * imports and host tests. */
    if (document.size() >= 4 &&
            static_cast<uint8_t>(document[0]) == (uint8_t) (ghostlock::binary_profile::kMagic & 0xff) &&
            static_cast<uint8_t>(document[1]) == (uint8_t) ((ghostlock::binary_profile::kMagic >> 8) & 0xff) &&
            static_cast<uint8_t>(document[2]) == (uint8_t) ((ghostlock::binary_profile::kMagic >> 16) & 0xff) &&
            static_cast<uint8_t>(document[3]) == (uint8_t) ((ghostlock::binary_profile::kMagic >> 24) & 0xff)) {
        return ghostlock::binary_profile::parse(document, out, release_buf,
                release_buf_cap);
    }
    return parse_resolved_profile_json(document, out, release_buf,
            release_buf_cap);
}
