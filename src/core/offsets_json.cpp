/* Minimal JSON parser for the runtime offsets import (offsets.json).
 * Self-contained on purpose: the binary must not depend on cJSON. */
#include "offsets_json.h"
#include "native_resource.hpp"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

#define PROFILE_JSON_MAX_SIZE (1U << 20)

static ghostlock::Result<std::string> read_profile_file(const char *path) {
  ghostlock::UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
  if (!fd.valid()) {
    return ghostlock::Result<std::string>::failure(
        ghostlock::SysError::from_errno());
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
    return ghostlock::Result<std::string>::failure(
        ghostlock::SysError::from_errno());
  }
  if (used == 0 || used == PROFILE_JSON_MAX_SIZE) {
    errno = used == PROFILE_JSON_MAX_SIZE ? EFBIG : EINVAL;
    return ghostlock::Result<std::string>::failure(
        ghostlock::SysError::from_errno());
  }
  buffer.resize(used);
  return ghostlock::Result<std::string>::success(std::move(buffer));
}

static const char *json_skip_ws(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  return p;
}

/* Match the string literal at *pp against key; on success advance *pp past
 * the closing quote. */
static int json_match_key(const char **pp, const char *end, const char *key) {
  const char *p = json_skip_ws(*pp, end);
  if (p == end || *p != '"') return 0;
  const char *q = p + 1;
  while (q < end && *q != '"') {
    if (*q == '\\') q++;
    q++;
  }
  if (q >= end) return 0;
  size_t len = (size_t)(q - (p + 1));
  if (len == strlen(key) && memcmp(p + 1, key, len) == 0) {
    *pp = q + 1;
    return 1;
  }
  return 0;
}

/* Advance *pp past one JSON value (string, number, object, array, literal). */
static int json_skip_value(const char **pp, const char *end) {
  const char *p = json_skip_ws(*pp, end);
  if (p == end) return 0;
  if (*p == '"') {
    p++;
    while (p < end && *p != '"') {
      if (*p == '\\') p++;
      p++;
    }
    if (p >= end) return 0;
    *pp = p + 1;
    return 1;
  }
  if (*p == '{' || *p == '[') {
    char open = *p;
    char close = (open == '{') ? '}' : ']';
    p++;
    int depth = 1;
    while (p < end && depth > 0) {
      if (*p == '"') {
        p++;
        while (p < end && *p != '"') {
          if (*p == '\\') p++;
          p++;
        }
        if (p >= end) return 0;
        p++;
      } else if (*p == open) {
        depth++;
        p++;
      } else if (*p == close) {
        depth--;
        p++;
      } else {
        p++;
      }
    }
    if (depth != 0) return 0;
    *pp = p;
    return 1;
  }
  while (p < end && *p != ',' && *p != '}' && *p != ']' &&
         *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
    p++;
  }
  *pp = p;
  return 1;
}

/* Return a pointer to the value of member `key` at depth 1 inside the object
 * starting at `obj`, or NULL when absent. */
static const char *json_member_value(const char *obj, const char *end,
                                     const char *key) {
  const char *p = json_skip_ws(obj, end);
  if (p == end || *p != '{') return NULL;
  p++;
  for (;;) {
    p = json_skip_ws(p, end);
    if (p == end || *p != '"') return NULL;
    if (!json_match_key(&p, end, key)) {
      /* json_match_key left p at the member name; skip it and the value. */
      if (!json_skip_value(&p, end)) return NULL;
      p = json_skip_ws(p, end);
      if (p == end || *p != ':') return NULL;
      p = json_skip_ws(p + 1, end);
      if (!json_skip_value(&p, end)) return NULL;
      p = json_skip_ws(p, end);
      if (p < end && *p == ',') {
        p++;
        continue;
      }
      return NULL;
    }
    p = json_skip_ws(p, end);
    if (p == end || *p != ':') return NULL;
    p = json_skip_ws(p + 1, end);
    return (p < end) ? p : NULL;
  }
}

/* Copy the JSON string at *pp (escapes stripped) into dst. */
static int json_read_string(const char **pp, const char *end, char *dst,
                            size_t cap) {
  const char *p = json_skip_ws(*pp, end);
  if (p == end || *p != '"') return 0;
  p++;
  size_t len = 0;
  while (p < end && *p != '"') {
    char c = *p;
    if (c == '\\') {
      p++;
      if (p >= end) return 0;
      c = *p;
    }
    if (len + 1 >= cap) return 0;
    dst[len++] = c;
    p++;
  }
  if (p >= end) return 0;
  dst[len] = '\0';
  *pp = p + 1;
  return 1;
}

static int json_parse_int(const char *p, const char *end, int64_t *out) {
  p = json_skip_ws(p, end);
  if (p == end) return 0;
  int neg = 0;
  if (*p == '-') {
    neg = 1;
    p++;
  }
  uint64_t v = 0;
  if (p + 2 <= end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    int digits = 0;
    while (p < end && isxdigit((unsigned char)*p)) {
      char c = *p;
      int d = (c <= '9') ? (c - '0') : (tolower((unsigned char)c) - 'a' + 10);
      v = v * 16 + (uint64_t)d;
      digits++;
      p++;
    }
    if (!digits) return 0;
  } else {
    int digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
      v = v * 10 + (uint64_t)(*p - '0');
      digits++;
      p++;
    }
    if (!digits) return 0;
  }
  *out = neg ? -(int64_t)v : (int64_t)v;
  return 1;
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

enum scalar_width { SCALAR_U8, SCALAR_U32, SCALAR_U64, SCALAR_I32 };
static const struct {
  const char *name;
  size_t off;
  enum scalar_width width;
} g_profile_map[] = {
  {"kernel_major", offsetof(struct kernel_offsets, kernel_major), SCALAR_U8},
  {"requires_shizuku", offsetof(struct kernel_offsets, requires_shizuku), SCALAR_U8},
  {"kernel_phys_load", offsetof(struct kernel_offsets, kernel_phys_load), SCALAR_U64},
  {"pselect_waiter_shift", offsetof(struct kernel_offsets, pselect_waiter_shift), SCALAR_I32},
  {"mcast_waiter_off", offsetof(struct kernel_offsets, mcast_waiter_off), SCALAR_I32},
  {"mcast_buffer_size", offsetof(struct kernel_offsets, mcast_buffer_size), SCALAR_U32},
  {"mcast_task_offset", offsetof(struct kernel_offsets, mcast_task_offset), SCALAR_U32},
  {"mcast_lock_offset", offsetof(struct kernel_offsets, mcast_lock_offset), SCALAR_U32},
  {"mcast_fake_lock_offset", offsetof(struct kernel_offsets, mcast_fake_lock_offset), SCALAR_U32},
  {"mcast_fake_task_offset", offsetof(struct kernel_offsets, mcast_fake_task_offset), SCALAR_U32},
  {"mcast_lock_slots_offset", offsetof(struct kernel_offsets, mcast_lock_slots_offset), SCALAR_U32},
  {"mcast_lock_slot_count", offsetof(struct kernel_offsets, mcast_lock_slot_count), SCALAR_U32},
  {"mcast_lock_slot_stride", offsetof(struct kernel_offsets, mcast_lock_slot_stride), SCALAR_U32},
  {"kernelsnitch_collisions", offsetof(struct kernel_offsets, kernelsnitch_collisions), SCALAR_U32},
  {"compact_waiter", offsetof(struct kernel_offsets, compact_waiter), SCALAR_U8},
  {"mm_struct_sz", offsetof(struct kernel_offsets, mm_struct_sz), SCALAR_U32},
  {"cred_copy_size", offsetof(struct kernel_offsets, cred_copy_size), SCALAR_U32},
  {"cred_usage_offset", offsetof(struct kernel_offsets, cred_usage_offset), SCALAR_U32},
  {"cred_usage_value", offsetof(struct kernel_offsets, cred_usage_value), SCALAR_U32},
  {"cred_caps_offset", offsetof(struct kernel_offsets, cred_caps_offset), SCALAR_U32},
  {"cred_caps_count", offsetof(struct kernel_offsets, cred_caps_count), SCALAR_U32},
  {"cred_caps_value", offsetof(struct kernel_offsets, cred_caps_value), SCALAR_U64},
  {"cred_ref_count", offsetof(struct kernel_offsets, cred_ref_count), SCALAR_U32},
  {"cred_ref0_offset", offsetof(struct kernel_offsets, cred_ref0_offset), SCALAR_U32},
  {"cred_ref1_offset", offsetof(struct kernel_offsets, cred_ref1_offset), SCALAR_U32},
  {"cred_ref2_offset", offsetof(struct kernel_offsets, cred_ref2_offset), SCALAR_U32},
  {"cred_ref3_offset", offsetof(struct kernel_offsets, cred_ref3_offset), SCALAR_U32},
  {"cred_ref0_image", offsetof(struct kernel_offsets, cred_ref0_image), SCALAR_U64},
  {"cred_ref1_image", offsetof(struct kernel_offsets, cred_ref1_image), SCALAR_U64},
  {"cred_ref2_image", offsetof(struct kernel_offsets, cred_ref2_image), SCALAR_U64},
  {"cred_ref3_image", offsetof(struct kernel_offsets, cred_ref3_image), SCALAR_U64},
};

/* Decoupling plan: store one typed scalar in a profile under construction.
 * Inputs: destination, field descriptor and value; output: updated candidate.
 * Future: target_profile_store_scalar(ProfileBuilder *, ...). */
static void store_profile_scalar(struct kernel_offsets *out, size_t off,
                                 enum scalar_width width, int64_t value) {
  char *field = (char *)out + off;
  switch (width) {
    case SCALAR_U8:  *(uint8_t *)field = (uint8_t)value; break;
    case SCALAR_U32: *(uint32_t *)field = (uint32_t)value; break;
    case SCALAR_U64: *(uint64_t *)field = (uint64_t)value; break;
    case SCALAR_I32: *(int *)field = (int)value; break;
  }
}

struct execution_field {
  const char *name;
  size_t offset;
};

#define EXEC_FIELD(json_name, member) \
  {json_name, offsetof(struct execution_settings, member)}

static int parse_execution_group(const char *parent, const char *parent_end,
                                 const char *group_name,
                                 const struct execution_field *fields,
                                 size_t field_count,
                                 struct execution_settings *out) {
  const char *group = json_member_value(parent, parent_end, group_name);
  const char *group_end = group;
  if (!group || *group != '{' || !json_skip_value(&group_end, parent_end))
    return -1;
  for (size_t i = 0; i < field_count; i++) {
    const char *value = json_member_value(group, group_end, fields[i].name);
    int64_t parsed = -1;
    if (!value || !json_parse_int(value, group_end, &parsed) || parsed < 0 ||
        (uint64_t)parsed > UINT32_MAX)
      return -1;
    *(uint32_t *)((char *)out + fields[i].offset) = (uint32_t)parsed;
  }
  return 0;
}

static int fill_execution_settings(const char *object, const char *object_end,
                                   struct execution_settings *out) {
  const char *execution = json_member_value(object, object_end, "execution");
  const char *execution_end = execution;
  if (!execution || *execution != '{' ||
      !json_skip_value(&execution_end, object_end))
    return -1;
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
  if (parse_execution_group(execution, execution_end, "recommended_cpus", cpus,
                            sizeof(cpus) / sizeof(cpus[0]), out) ||
      parse_execution_group(execution, execution_end, "heap", heap,
                            sizeof(heap) / sizeof(heap[0]), out) ||
      parse_execution_group(execution, execution_end, "race", race,
                            sizeof(race) / sizeof(race[0]), out) ||
      parse_execution_group(execution, execution_end, "stages", stages,
                            sizeof(stages) / sizeof(stages[0]), out) ||
      parse_execution_group(execution, execution_end, "handoff", handoff,
                            sizeof(handoff) / sizeof(handoff[0]), out))
    return -1;

  const char *routes = json_member_value(execution, execution_end, "routes");
  const char *routes_end = routes;
  if (!routes || *routes != '{' || !json_skip_value(&routes_end, execution_end))
    return -1;
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
  return parse_execution_group(routes, routes_end, "tcp_zerocopy", tcp,
                               sizeof(tcp) / sizeof(tcp[0]), out) ||
         parse_execution_group(routes, routes_end, "select_stack", select_stack,
                               sizeof(select_stack) / sizeof(select_stack[0]), out) ||
         parse_execution_group(routes, routes_end, "multicast_waiter", multicast,
                               sizeof(multicast) / sizeof(multicast[0]), out)
             ? -1
             : 0;
}

#undef EXEC_FIELD

/* Fill `out` from one JSON object [obj, end).  Fields absent from the JSON
 * keep whatever the caller put into `out` (zeroed for a fresh table, or a
 * built-in entry the JSON is overriding). */
/* Decoupling plan: merge one JSON entry into a profile candidate. Inputs:
 * object span and base profile; output: populated candidate/error. Future:
 * target_profile_parse_entry(ProfileBuilder *, JsonObjectView). */
static void fill_external_entry(struct kernel_offsets *out,
                                const char *release_buf, const char *obj,
                                const char *end) {
  const char *v;
  int64_t num;
  out->uname_r = release_buf;
  for (size_t i = 0; i < sizeof(g_profile_map) / sizeof(g_profile_map[0]); i++) {
    v = json_member_value(obj, end, g_profile_map[i].name);
    if (v && json_parse_int(v, end, &num)) {
      store_profile_scalar(out, g_profile_map[i].off,
                           g_profile_map[i].width, num);
    }
  }
  /* Resolved profiles use one flat object. Keep the nested reads below only
   * for compatibility with offsets.json files produced by older extractors. */
  for (size_t i = 0; i < sizeof(g_symbol_map) / sizeof(g_symbol_map[0]); i++) {
    v = json_member_value(obj, end, g_symbol_map[i].name);
    if (v && json_parse_int(v, end, &num)) {
      *(uint64_t *)((char *)out + g_symbol_map[i].off) = (uint64_t)num;
    }
  }
  for (size_t i = 0; i < sizeof(g_task_map) / sizeof(g_task_map[0]); i++) {
    v = json_member_value(obj, end, g_task_map[i].name);
    if (v && json_parse_int(v, end, &num)) {
      *(uint32_t *)((char *)out + g_task_map[i].off) = (uint32_t)num;
    }
  }
  v = json_member_value(obj, end, "symbols");
  if (v && *v == '{') {
    const char *v_end = v;
    if (json_skip_value(&v_end, end)) {
      for (size_t i = 0; i < sizeof(g_symbol_map) / sizeof(g_symbol_map[0]);
           i++) {
        const char *mv = json_member_value(v, v_end, g_symbol_map[i].name);
        if (mv && json_parse_int(mv, v_end, &num)) {
          *(uint64_t *)((char *)out + g_symbol_map[i].off) = (uint64_t)num;
        }
      }
    }
  }
  v = json_member_value(obj, end, "struct_fields");
  if (v && *v == '{') {
    const char *v_end = v;
    if (json_skip_value(&v_end, end)) {
      for (size_t i = 0; i < sizeof(g_task_map) / sizeof(g_task_map[0]); i++) {
        const char *mv = json_member_value(v, v_end, g_task_map[i].name);
        if (mv && json_parse_int(mv, v_end, &num)) {
          *(uint32_t *)((char *)out + g_task_map[i].off) = (uint32_t)num;
        }
      }
    }
  }
  /* a zeroed entry selects the 6.6 waiter layout; warn rather than fail quietly */
  if (strncmp(out->uname_r, "6.1.", 4) == 0 && !out->compact_waiter) {
    fprintf(stderr,
            "warning: imported 6.1 entry has no compact_waiter; it will run "
            "the 6.6 rb_node waiter layout and miss\n");
  }
}

int load_resolved_profile_json(const char *path, struct kernel_offsets *out,
                               char *release_buf, size_t release_buf_cap) {
  auto file_result = read_profile_file(path);
  if (!file_result) {
    errno = file_result.error().code.value();
    return -1;
  }
  const std::string &file = file_result.value();
  const char *file_buffer = file.data();
  const size_t size = file.size();
  const char *object = json_skip_ws(file_buffer, file_buffer + size);
  const char *end = object;
  int result = -1;
  if (object < file_buffer + size && *object == '{' &&
      json_skip_value(&end, file_buffer + size) &&
      json_skip_ws(end, file_buffer + size) == file_buffer + size) {
    int64_t schema_version = 0;
    const char *schema_value = json_member_value(object, end, "schema_version");
    const char *execution_value = json_member_value(object, end, "execution");
    const char *release_value = json_member_value(object, end, "release");
    const char *cursor = release_value;
    char release[256];
    if (schema_value && json_parse_int(schema_value, end, &schema_version) &&
        schema_version == 1 && execution_value && *execution_value == '{' &&
        release_value && json_read_string(&cursor, end, release,
                                          sizeof(release)) &&
        strlen(release) < release_buf_cap) {
      memset(out, 0, sizeof(*out));
      strcpy(release_buf, release);
      fill_external_entry(out, release_buf, object, end);
      result = fill_execution_settings(object, end, &out->execution);
    }
  }
  return result;
}
