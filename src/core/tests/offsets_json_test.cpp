/* Host fixed-vector test for the resolved-profile transport decoder and the
 * startup address derivation.
 *
 * Every built-in profile is decoded through the same entry point the native
 * binary uses (load_resolved_profile_json), then the deterministic
 * TargetProfile accessors and SoC address formulas are checked against the
 * decoded values. Rejection vectors cover the schema and bounding guards.
 */

#include "memory/address_space.h"
#include "offsets_json.h"
#include "profile.h"
#include "target.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

constexpr const char *kProfileDir = "app/src/main/assets/kernel_profiles";
constexpr const char *kIndexFile = "index.json";
constexpr const char *kDefaultsFile = "defaults.json";
constexpr size_t kReleaseCap = 192;

struct ProfileFile {
  std::string name;
  std::string path;
};

bool read_text_file(const std::string &path, std::string *out) {
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) return false;
  char buffer[4096];
  size_t count = 0;
  while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    out->append(buffer, count);
  }
  fclose(file);
  return true;
}

size_t count_occurrences(const std::string &text, const std::string &needle) {
  size_t count = 0;
  size_t position = 0;
  while ((position = text.find(needle, position)) != std::string::npos) {
    count++;
    position += needle.size();
  }
  return count;
}

uintptr_t expected_alias(uint64_t physical_load, uint64_t image_offset) {
  const uintptr_t physical = physical_load + image_offset;
  return (physical - P0_PHYS_OFFSET) | P0_PAGE_OFFSET;
}

int g_failures = 0;

void expect(bool condition, const char *message, const std::string &context) {
  if (condition) return;
  fprintf(stderr, "FAIL %s: %s\n", message, context.c_str());
  g_failures++;
}

bool profile_execution_complete(const struct execution_settings *e) {
  return e && e->recommended_main_cpu != e->recommended_consumer_cpu &&
         e->heap_prepare_max_attempts && e->heap_prepare_timeout_ms &&
         e->heap_kernelsnitch_timeout_ms && e->race_route_wait_ms &&
         e->race_setup_settle_us && e->race_state_poll_interval_us &&
         e->w1_attempts && e->w1_settle_us && e->w1_scratch_repair_attempts &&
         e->w2_attempts && e->w2_settle_us && e->w3_chain_rounds &&
         e->w3_attempts && e->w3_settle_us && e->tcp_attempts &&
         e->tcp_arm_sequence && e->tcp_post_receive_hold_iterations &&
         e->select_enter_delay_us && e->select_timeout_us &&
         e->select_consumer_max_calls && e->select_consumer_burst_calls &&
         e->multicast_ready_timeout_ms && e->multicast_post_requeue_settle_us &&
         e->multicast_post_adjust_settle_us && e->handoff_pre_dispatch_settle_ms &&
         e->handoff_module_poll_attempts && e->handoff_module_poll_interval_ms &&
         e->handoff_enforce_poll_attempts && e->handoff_enforce_poll_interval_ms;
}

bool profile_required_fields_complete(const struct kernel_offsets *v) {
  return v->off_init_task && v->off_init_cred && v->off_root_task_group &&
         v->off_selinux_enforcing && v->task_prio && v->task_pi_lock &&
         v->task_pi_waiters && v->task_pi_blocked_on && v->task_cred &&
         v->task_seccomp && v->cred_copy_size && v->cred_caps_count;
}

void check_profile(const ProfileFile &entry) {
  const std::string &name = entry.name;
  struct kernel_offsets decoded = {};
  char release[kReleaseCap] = {0};
  if (load_resolved_profile_json(entry.path.c_str(), &decoded, release,
                                 sizeof(release)) != 0) {
    fprintf(stderr, "FAIL decode: %s\n", name.c_str());
    g_failures++;
    return;
  }
  expect(strcmp(release, decoded.uname_r) == 0, "release round-trip", name);
  expect(strcmp(release, name.c_str()) == 0, "release matches file name", name);
  expect(decoded.kernel_major == 5 || decoded.kernel_major == 6,
         "kernel_major is 5 or 6", name);
  expect(profile_required_fields_complete(&decoded),
         "required profile fields present", name);

  TargetProfile profile = target_profile_snapshot(&decoded);
  expect(target_profile_is_loaded(&profile), "profile snapshot loaded", name);
  expect(profile_execution_complete(target_profile_execution(&profile)),
         "execution settings complete", name);

  const MulticastWaiterLayout multicast =
      target_profile_multicast_waiter_layout(&profile);
  const SelectStackLayout select =
      target_profile_select_stack_layout(&profile);
  const TcpZerocopyLayout tcp = target_profile_tcp_zerocopy_layout(&profile);
  expect(select.waiter_shift == decoded.pselect_waiter_shift,
         "select waiter shift snapshot", name);
  expect(select.compact_waiter == decoded.compact_waiter,
         "select compact flag snapshot", name);
  expect(tcp.compact_waiter == decoded.compact_waiter,
         "tcp compact flag snapshot", name);
  expect(multicast.buffer_size == decoded.mcast_buffer_size,
         "multicast buffer size snapshot", name);
  expect(multicast.waiter_offset == (size_t)decoded.mcast_waiter_off,
         "multicast waiter offset snapshot", name);
  expect(multicast.lock_slot_count == decoded.mcast_lock_slot_count,
         "multicast slot count snapshot", name);

  /* QCOM with the profile's own physical load (or the 6.12/QCOM fallback). */
  ghostlock::memory::ResolvedAddresses addresses = {};
  expect(ghostlock::memory::resolved_addresses_init_for_soc(&addresses, &profile,
                                         ghostlock::memory::TARGET_SOC_QCOM) == 0,
         "address init succeeds", name);
  expect(ghostlock::memory::resolved_addresses_init_cred_image(&addresses) ==
             (uintptr_t)(KIMAGE_TEXT_BASE + decoded.off_init_cred),
         "init_cred image formula", name);
  const uint64_t expected_phys =
      decoded.kernel_phys_load
          ? decoded.kernel_phys_load
          : (strncmp(decoded.uname_r, "6.12.", 5) == 0 ? QC_GKI_6_12_PHYS_LOAD
                                                       : P0_KERNEL_PHYS_LOAD);
  expect(ghostlock::memory::resolved_addresses_kernel_phys_load(&addresses) == expected_phys,
         "kernel physical load selection", name);
  expect(ghostlock::memory::resolved_addresses_data_alias(
             &addresses, ghostlock::memory::resolved_addresses_init_cred_image(&addresses)) ==
             expected_alias(expected_phys, decoded.off_init_cred),
         "init_cred direct-map alias", name);

  /* SoC fallbacks only apply when the profile carries no measured load. */
  struct kernel_offsets zero_load = decoded;
  zero_load.kernel_phys_load = 0;
  TargetProfile zero_profile = target_profile_snapshot(&zero_load);
  ghostlock::memory::ResolvedAddresses mtk = {};
  ghostlock::memory::ResolvedAddresses xring = {};
  expect(ghostlock::memory::resolved_addresses_init_for_soc(&mtk, &zero_profile,
                                         ghostlock::memory::TARGET_SOC_MTK) == 0 &&
             ghostlock::memory::resolved_addresses_kernel_phys_load(&mtk) ==
                 (uintptr_t)(KIMAGE_TEXT_BASE - MTK_VADDR_BASE),
         "MTK physical load fallback", name);
  expect(ghostlock::memory::resolved_addresses_init_for_soc(&xring, &zero_profile,
                                         ghostlock::memory::TARGET_SOC_XRING) == 0 &&
             ghostlock::memory::resolved_addresses_kernel_phys_load(&xring) ==
                 XRING_KERNEL_PHYS_LOAD,
         "XRing physical load fallback", name);
  ghostlock::memory::ResolvedAddresses google = {};
  expect(ghostlock::memory::resolved_addresses_init_for_soc(&google, &zero_profile,
                                         ghostlock::memory::TARGET_SOC_GOOGLE) == 0 &&
             ghostlock::memory::resolved_addresses_kernel_phys_load(&google) ==
                 (uintptr_t)(KIMAGE_TEXT_BASE - MTK_VADDR_BASE),
         "Tensor/Google physical load fallback", name);
}

void check_address_rejections(void) {
  struct kernel_offsets decoded = {};
  TargetProfile profile = target_profile_snapshot(&decoded);
  ghostlock::memory::ResolvedAddresses addresses = {};
  expect(ghostlock::memory::resolved_addresses_init_for_soc(&addresses, &profile,
                                         ghostlock::memory::TARGET_SOC_QCOM) == -1,
         "empty profile rejected", "address");

  decoded.uname_r = "6.6.0-test";
  decoded.off_init_cred = 0x1000;
  decoded.kernel_phys_load = 0x1000;
  profile = target_profile_snapshot(&decoded);
  expect(ghostlock::memory::resolved_addresses_init_for_soc(&addresses, &profile,
                                         ghostlock::memory::TARGET_SOC_QCOM) == 0,
         "small physical load accepted", "address");

  const auto below_base = ghostlock::memory::resolved_addresses_data_alias_checked(
      addresses, ghostlock::target::KernelImageAddress(KIMAGE_TEXT_BASE - 1));
  expect(!below_base.has_value(), "image below text base rejected", "address");

  const auto underflow = ghostlock::memory::resolved_addresses_data_alias_checked(
      addresses, ghostlock::target::KernelImageAddress(KIMAGE_TEXT_BASE));
  expect(!underflow.has_value(), "physical underflow rejected", "address");

  decoded.kernel_phys_load = UINTPTR_MAX;
  profile = target_profile_snapshot(&decoded);
  expect(ghostlock::memory::resolved_addresses_init_for_soc(&addresses, &profile,
                                         ghostlock::memory::TARGET_SOC_QCOM) == 0,
         "max physical load accepted", "address");
  const auto overflow = ghostlock::memory::resolved_addresses_data_alias_checked(
      addresses,
      ghostlock::target::KernelImageAddress(KIMAGE_TEXT_BASE + 0x1000));
  expect(!overflow.has_value(), "physical overflow rejected", "address");

  decoded.kernel_phys_load = 0;
  decoded.off_init_cred = UINTPTR_MAX;
  profile = target_profile_snapshot(&decoded);
  expect(ghostlock::memory::resolved_addresses_init_for_soc(&addresses, &profile,
                                         ghostlock::memory::TARGET_SOC_QCOM) == -1,
         "image offset overflow rejected", "address");
}

void check_decoder_rejections(void) {
  const char *cases[] = {
      "",
      "[]",
      "{}",
      "{\"schema_version\":2,\"release\":\"x\",\"execution\":{}}",
      "{\"schema_version\":1,\"release\":\"x\"}",
      "{\"schema_version\":1,\"release\":\"x\",\"execution\":{\"recommended_cpus\":"
      "{\"main\":0,\"consumer\":1,\"heap\":{\"prepare_max_attempts\":1},"
      "\"race\":{},\"stages\":{},\"handoff\":{},\"routes\":{}}}",
      "{\"schema_version\":1,\"release\":\"x\"",
      "{\"schema_version\":1,\"release\":\"x\"} trailing",
      "{\"schema_version\":1,\"execution\":{},\"release\":7}",
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char path[] = ".build/host/offsets_reject_XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) {
      fprintf(stderr, "FAIL mkstemp for rejection vector %zu\n", i);
      g_failures++;
      continue;
    }
    const size_t length = strlen(cases[i]);
    if (length > 0 && write(fd, cases[i], length) != (ssize_t)length) {
      fprintf(stderr, "FAIL write for rejection vector %zu\n", i);
      g_failures++;
    }
    close(fd);

    struct kernel_offsets decoded = {};
    char release[kReleaseCap] = {0};
    const int result =
        load_resolved_profile_json(path, &decoded, release, sizeof(release));
    char message[64];
    snprintf(message, sizeof(message), "rejection vector %zu", i);
    expect(result == -1, message, "decoder");
    unlink(path);
  }

  /* A release longer than the caller buffer must be rejected. */
  const char *long_release =
      "{\"schema_version\":1,\"release\":\"0123456789\",\"execution\":{}}";
  char path[] = ".build/host/offsets_short_cap_XXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0) {
    fprintf(stderr, "FAIL mkstemp for release-cap vector\n");
    g_failures++;
    return;
  }
  const size_t length = strlen(long_release);
  if (write(fd, long_release, length) != (ssize_t)length) {
    fprintf(stderr, "FAIL write for release-cap vector\n");
    g_failures++;
  }
  close(fd);
  struct kernel_offsets decoded = {};
  char release[8] = {0};
  expect(load_resolved_profile_json(path, &decoded, release, sizeof(release)) ==
             -1,
         "release longer than buffer rejected", "decoder");
  unlink(path);
}

}  // namespace

int main(void) {
  DIR *directory = opendir(kProfileDir);
  if (!directory) {
    fprintf(stderr, "cannot open %s (run from the project root)\n",
            kProfileDir);
    return 1;
  }

  std::vector<ProfileFile> profiles;
  struct dirent *item = nullptr;
  while ((item = readdir(directory)) != nullptr) {
    const std::string name = item->d_name;
    if (name.size() < 5 || name.compare(name.size() - 5, 5, ".json") != 0)
      continue;
    if (name == kIndexFile || name == kDefaultsFile) continue;
    ProfileFile entry;
    entry.name = name.substr(0, name.size() - 5);
    entry.path = std::string(kProfileDir) + "/" + name;
    profiles.push_back(entry);
  }
  closedir(directory);

  if (profiles.size() < 40) {
    fprintf(stderr, "FAIL expected at least 40 built-in profiles, found %zu\n",
            profiles.size());
    g_failures++;
  }

  std::string index;
  if (!read_text_file(std::string(kProfileDir) + "/" + kIndexFile, &index)) {
    fprintf(stderr, "FAIL cannot read index.json\n");
    g_failures++;
  } else {
    const size_t indexed = count_occurrences(index, "\"file\"");
    expect(indexed == profiles.size(), "index/profile count match", "index");
  }

  for (const ProfileFile &entry : profiles) check_profile(entry);

  check_decoder_rejections();
  check_address_rejections();

  if (g_failures != 0) {
    fprintf(stderr, "offsets_json_test: %d failure(s)\n", g_failures);
    return 1;
  }
  printf("offsets_json_test: %zu profiles decoded and checked\n",
         profiles.size());
  return 0;
}
