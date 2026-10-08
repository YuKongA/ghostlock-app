#include "terminal/root_script.hpp"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "support/log.hpp"
#include "session/runtime_config.h"

#include <cstdio>

#include "support/native_resource.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <string>

namespace ghostlock::terminal {
    void write_root_script(bool safe_mode) {
        std::string script(12288, '\0');
        support::UniqueFd sfd(
            open((config::runtime_config_snapshot().root_script_path.c_str()), O_WRONLY | O_CREAT | O_TRUNC, 0755));
        if (!sfd.valid()) {
            pr_warning("open root script failed path=%s errno=%d\n",
                       (config::runtime_config_snapshot().root_script_path.c_str()), errno);
            return;
        }

        int32_t n = snprintf(
            script.data(), script.size(),
            "#!/system/bin/sh\n"
            "HOME_DIR='%s'\n"
            "LOG='%s'\n"
            "SAFE_MODE=%d\n"
            "KSUD=\"$HOME_DIR/ksud\"\n"
            "echo \"[*] root script start uid=$(id -u) euid=$(id -u)\" >\"$LOG\"\n"
            "chmod 644 \"$LOG\" 2>/dev/null\n"
            "echo \"[*] seccomp=$(grep Seccomp /proc/self/status 2>/dev/null | tr '\\n' ' ')\" >>\"$LOG\"\n"
            "if [ ! -x \"$KSUD\" ]; then\n"
            "  KSUD=$(find /data/app -path '*/me.weishu.kernelsu.pr*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
            "fi\n"
            "if [ ! -x \"$KSUD\" ]; then\n"
            "  KSUD=$(find /data/app -path '*/me.weishu.kernelsu-*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
            "fi\n"
            "if [ ! -x \"$KSUD\" ]; then\n"
            "  KSUD=$(find /data/app -path '*/com.resukisu.resukisu*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
            "fi\n"
            "if [ ! -x \"$KSUD\" ]; then\n"
            "  KSUD=$(find /data/app -path '*/com.kowx712.supermanager*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
            "fi\n"
            "if [ -z \"$KSUD\" ]; then KSUD=/data/local/tmp/ksud; fi\n"
            "if [ ! -x \"$KSUD\" ]; then KSUD=/data/adb/ksu/bin/ksud; fi\n"
            "echo \"[*] ksud=$KSUD\" >>\"$LOG\"\n"
            "echo \"[*] ksud_file=$(ls -l \"$KSUD\" 2>/dev/null)\" >>\"$LOG\"\n"
            "echo \"[*] uname=$(uname -r)\" >>\"$LOG\"\n"
            "if [ \"$(id -u)\" -ne 0 ]; then\n"
            "  echo '[!] temp su unavailable; aborting' >>\"$LOG\"\n"
            "  exit 1\n"
            "fi\n"
            /* the rename is atomic, so a failed read leaves the old dump in place */
            "echo \"# $(uname -r)\" >\"$HOME_DIR/.ghostlock_iomem.new\"\n"
            "if cat /proc/iomem >>\"$HOME_DIR/.ghostlock_iomem.new\" 2>/dev/null && grep -q 'System RAM' \"$HOME_DIR/.ghostlock_iomem.new\"; then\n"
            "  mv \"$HOME_DIR/.ghostlock_iomem.new\" \"$HOME_DIR/.ghostlock_iomem\"\n"
            "  chmod 644 \"$HOME_DIR/.ghostlock_iomem\" 2>/dev/null\n"
            "  echo \"[*] iomem cache: cached $(wc -c <\"$HOME_DIR/.ghostlock_iomem\") bytes\" >>\"$LOG\"\n"
            "else\n"
            "  rm -f \"$HOME_DIR/.ghostlock_iomem.new\"\n"
            "  echo '[!] iomem cache: /proc/iomem read failed' >>\"$LOG\"\n"
            "fi\n"
            /* debug archive: copy kernel/pstore/iomem evidence into the
             * --dump-kernel-log directory (empty disables the dump). */
            "DEBUG_DIR=\"%s\"\n"
            "dump_debug() {\n"
            "  [ -n \"$DEBUG_DIR\" ] || return 0\n"
            "  mkdir -p \"$DEBUG_DIR/pstore\" 2>/dev/null || { echo \"[!] debug dump: cannot write $DEBUG_DIR\" >>\"$LOG\"; return 0; }\n"
            "  {\n"
            "    echo '== uname -a =='\n"
            "    uname -a\n"
            "    echo '== /proc/version =='\n"
            "    cat /proc/version\n"
            "    echo '== /proc/cmdline =='\n"
            "    cat /proc/cmdline\n"
            "    echo '== /proc/modules =='\n"
            "    cat /proc/modules\n"
            "    echo '== selinux =='\n"
            "    getenforce 2>/dev/null\n"
            "  } >\"$DEBUG_DIR/kernel-info.txt\" 2>&1\n"
            "  if ! dmesg >\"$DEBUG_DIR/kernel-dmesg.log\" 2>&1; then\n"
            "    echo '[!] dmesg unavailable' >>\"$DEBUG_DIR/kernel-dmesg.log\"\n"
            "  fi\n"
            "  for f in /sys/fs/pstore/*; do\n"
            "    [ -f \"$f\" ] || continue\n"
            "    cp \"$f\" \"$DEBUG_DIR/pstore/\" 2>/dev/null\n"
            "  done\n"
            "  cp /proc/iomem \"$DEBUG_DIR/iomem.txt\" 2>/dev/null\n"
            "  cp \"$LOG\" \"$DEBUG_DIR/ksu.log\" 2>/dev/null\n"
            "  echo \"[*] debug dump written to $DEBUG_DIR\" >>\"$LOG\"\n"
            "}\n"
            "# W1's 64-bit child pointer makes adjacent booleans non-zero.\n"
            "echo 0 > /sys/fs/selinux/checkreqprot 2>/dev/null\n"
            "if grep -q '^kernelsu[[:space:]]' /proc/modules 2>/dev/null; then\n"
            "  echo '[+] KernelSU already loaded' >>\"$LOG\"\n"
            "fi\n"
            "KVER=$(uname -r | cut -d. -f1-2)\n"
            "AVER=$(uname -r | grep -o 'android[0-9]*' | head -1)\n"
            "if [ -z \"$AVER\" ] || [ -z \"$KVER\" ]; then\n"
            "  echo '[!] cannot parse KMI from uname -r' >>\"$LOG\"\n"
            "  exit 1\n"
            "fi\n"
            "KMI=\"${AVER}-${KVER}\"\n"
            "# safe mode: disable all modules before exec ksud\n"
            "if [ \"$SAFE_MODE\" = \"1\" ]; then\n"
            "  echo \"[*] safe mode: disabling all modules under /data/adb/modules\" >>\"$LOG\"\n"
            "  n=0\n"
            "  for m in /data/adb/modules/*/; do\n"
            "    [ -d \"$m\" ] || continue\n"
            "    if touch \"${m}disable\" 2>/dev/null; then\n"
            "      n=$((n+1))\n"
            "      echo \"  disabled ${m}\" >>\"$LOG\"\n"
            "    fi\n"
            "  done\n"
            "  echo \"[*] safe mode: $n module(s) disabled\" >>\"$LOG\"\n"
            "fi\n"
            "# step 1: restore policy\n"
            "POLICY=$(mktemp \"$HOME_DIR/.ghostlock_policy.XXXXXX\") || {\n"
            "  echo '[!] cannot create policy dump' >>\"$LOG\"\n"
            "  exit 1\n"
            "}\n"
            "trap 'rm -f \"$POLICY\"; dump_debug' EXIT\n"
            "prepare_policy() {\n"
            "  cat /sys/fs/selinux/policy >\"$POLICY\" || return 1\n"
            "  HEADER=$(od -An -tx1 -N24 \"$POLICY\" | tr -d ' \\n')\n"
            "  case \"$HEADER\" in\n"
            "    8cff7cf9080000005345204c696e7578"
            "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?) ;;\n"
            "    *) echo '[!] invalid policy header'; return 1 ;;\n"
            "  esac\n"
            "  # Restore missing Android netlink flags: bits 30/31, byte 23.\n"
            "  CONFIG=$(od -An -tu1 -j23 -N1 \"$POLICY\") || return 1\n"
            "  [ -n \"$CONFIG\" ] || return 1\n"
            "  CONFIG=$(printf '\\\\0%%03o' \"$((CONFIG | 192))\" || return 1)\n"
            "  printf '%%b' \"$CONFIG\" | dd of=\"$POLICY\" bs=1 seek=23 count=1 conv=notrunc\n"
            "}\n"
            "FIXUP_RC=1\n"
            "for i in $(seq 1 10); do\n"
            "  echo \"[*] fixup: attempt $i\" >>\"$LOG\"\n"
            "  if ! prepare_policy >>\"$LOG\" 2>&1; then\n"
            "    sleep 2\n"
            "    continue\n"
            "  fi\n"
            "  BEFORE_POLICYLOAD=$(od -An -tu4 -j12 -N4 /sys/fs/selinux/status 2>/dev/null | tr -d ' ')\n"
            "  load_policy \"$POLICY\" >>\"$LOG\" 2>&1 &\n"
            "  LPID=$!\n"
            "  (sleep 8; kill -9 $LPID 2>/dev/null) &\n"
            "  SPID=$!\n"
            "  wait $LPID 2>/dev/null\n"
            "  FIXUP_RC=$?\n"
            "  kill $SPID 2>/dev/null\n"
            "  if [ \"$FIXUP_RC\" -eq 0 ]; then\n"
            "    AFTER_POLICYLOAD=$(od -An -tu4 -j12 -N4 /sys/fs/selinux/status 2>/dev/null | tr -d ' ')\n"
            "    echo \"[*] policyload before=$BEFORE_POLICYLOAD after=$AFTER_POLICYLOAD\" >>\"$LOG\"\n"
            "    if [ -n \"$AFTER_POLICYLOAD\" ] && [ \"$AFTER_POLICYLOAD\" != \"$BEFORE_POLICYLOAD\" ]; then\n"
            "      break\n"
            "    fi\n"
            "    echo '[!] load_policy returned success without updating SELinux status' >>\"$LOG\"\n"
            "    FIXUP_RC=1\n"
            "  fi\n"
            "  sleep 2\n"
            "done\n"
            "echo \"[*] policy fixup rc=$FIXUP_RC\" >>\"$LOG\"\n"
            "if [ \"$FIXUP_RC\" -eq 0 ]; then\n"
            "# load_policy ok: late-load (module init re-enforces); already-loaded restores below\n"
            "if grep -q kernelsu /proc/modules 2>/dev/null; then\n"
            "  KSU_ALREADY=1\n"
            "  echo \"[*] kernelsu already loaded; skipping late-load\" >>\"$LOG\"\n"
            "else\n"
            "  KSU_ALREADY=0\n"
            "  if [ ! -x \"$KSUD\" ]; then\n"
            "    echo '[!] ksud missing; cannot late-load' >>\"$LOG\"\n"
            "    exit 1\n"
            "  fi\n"
            "  echo \"[*] late-load kmi=$KMI\" >>\"$LOG\"\n"
            "  chmod 755 \"$KSUD\" 2>/dev/null\n"
            "  \"$KSUD\" late-load --kmi \"$KMI\" --allow-shell >>\"$LOG\" 2>&1\n"
            "  echo \"[*] late-load exit=$?\" >>\"$LOG\"\n"
            "fi\n"
            "echo \"[*] temp su uid=$(id -u); watching kernelsu.ko\" >>\"$LOG\"\n"
            "KSU_READY=0\n"
            "for i in $(seq 1 50); do\n"
            "  if grep -q kernelsu /proc/modules 2>/dev/null; then KSU_READY=1; break; fi\n"
            "  sleep 0.1\n"
            "done\n"
            "if [ \"$KSU_READY\" -ne 1 ]; then\n"
            "  echo '[!] KernelSU module not loaded' >>\"$LOG\"\n"
            "  exit 1\n"
            "fi\n"
            "echo '[+] KernelSU module loaded' >>\"$LOG\"\n"
            "if [ \"$KSU_ALREADY\" -eq 1 ]; then\n"
            "  echo \"[*] kernelsu already loaded; restoring enforcing\" >>\"$LOG\"\n"
            "  echo 1 > /sys/fs/selinux/enforce 2>/dev/null\n"
            "fi\n"
            "else\n"
            "  echo '[!] fixup failed; SELinux left permissive' >>\"$LOG\"\n"
            "fi\n",
            (config::runtime_config_snapshot().home_dir.c_str()),
            (config::runtime_config_snapshot().ksu_log_path.c_str()),
            safe_mode ? 1 : 0,
            (config::runtime_config_snapshot().debug_dir.c_str()));
        if (n < 0 || n >= static_cast<int32_t>(script.size())) {
            pr_warning("root script too long\n");
            return;
        }
        if (write(sfd.get(), script.data(), static_cast<size_t>(n)) != n) {
            pr_warning("write root script failed errno=%d\n", errno);
        }
        sfd.reset();
        chmod((config::runtime_config_snapshot().root_script_path.c_str()), 0755);
        pr_info("root script written path=%s bytes=%d\n", (config::runtime_config_snapshot().root_script_path.c_str()),
                n);
    }
} // namespace ghostlock::terminal
