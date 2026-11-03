#define _GNU_SOURCE 1
#include <sched.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
int main(void)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(1, &s);
    sched_setaffinity(0, sizeof s, &s);
    pid_t p = fork();
    if (!p)
    {
        sleep(1);
        _exit(0);
    }
    waitpid(p, 0, 0);
    return 0;
}
