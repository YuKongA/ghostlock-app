/* Minimal JSON parser for the runtime offsets import (offsets.json).
 * Self-contained on purpose: the binary must not depend on cJSON. */
#include "offsets_json.h"

#include <ctype.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char g_file_buf[1 << 20];

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

/* Fill `out` from one JSON object [obj, end).  Fields absent from the JSON
 * keep whatever the caller put into `out` (zeroed for a fresh table, or a
 * built-in entry the JSON is overriding). */
static void fill_external_entry(struct kernel_offsets *out,
                                const char *release_buf, const char *obj,
                                const char *end) {
  const char *v;
  int64_t num;
  out->uname_r = release_buf;
  v = json_member_value(obj, end, "kernel_phys_load");
  if (v && json_parse_int(v, end, &num)) {
    out->kernel_phys_load = (uint64_t)num;
  }
  v = json_member_value(obj, end, "pselect_waiter_shift");
  if (v && json_parse_int(v, end, &num)) {
    out->pselect_waiter_shift = (int)num;
  }
  v = json_member_value(obj, end, "compact_waiter");
  if (v && json_parse_int(v, end, &num)) {
    out->compact_waiter = (uint8_t)num;
  }
  v = json_member_value(obj, end, "mm_struct_sz");
  if (v && json_parse_int(v, end, &num)) {
    out->mm_struct_sz = (uint32_t)num;
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

int load_offsets_json(const char *path, const char *release,
                      struct kernel_offsets *out, char *release_buf,
                      size_t release_buf_cap) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  ssize_t n = read(fd, g_file_buf, sizeof(g_file_buf) - 1);
  close(fd);
  if (n <= 0) return -1;
  g_file_buf[n] = '\0';
  const char *p = g_file_buf;
  const char *end = g_file_buf + n;
  p = json_skip_ws(p, end);
  if (p < end && *p == '[') {
    p++;
    for (;;) {
      p = json_skip_ws(p, end);
      if (p < end && *p == ',') p++;
      p = json_skip_ws(p, end);
      if (p == end || *p == ']') return -1;
      if (*p != '{') return -1;
      const char *obj = p;
      if (!json_skip_value(&p, end)) return -1;
      const char *rel = json_member_value(obj, p, "release");
      char buf[256];
      const char *q = rel;
      if (rel && *rel == '"' && json_read_string(&q, p, buf, sizeof(buf)) &&
          strcmp(buf, release) == 0) {
        if (strlen(buf) >= release_buf_cap) return -1;
        strcpy(release_buf, buf);
        fill_external_entry(out, release_buf, obj, p);
        return 0;
      }
    }
  }
  if (p < end && *p == '{') {
    const char *obj = p;
    if (json_skip_value(&p, end)) {
      const char *rel = json_member_value(obj, p, "release");
      char buf[256];
      const char *q = rel;
      if (rel && *rel == '"' && json_read_string(&q, p, buf, sizeof(buf)) &&
          strcmp(buf, release) == 0) {
        if (strlen(buf) >= release_buf_cap) return -1;
        strcpy(release_buf, buf);
        fill_external_entry(out, release_buf, obj, p);
        return 0;
      }
    }
  }
  return -1;
}
