#define _GNU_SOURCE 1
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
static void *worker(void *a)
{
	(void)a;
	for (;;)
		pause();
	return 0;
}
int main(void)
{
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(0, sizeof(set), &set);
	pthread_t t;
	pthread_create(&t, NULL, worker, NULL);
	clockid_t cid;
	pthread_getcpuclockid(t, &cid);
	printf("pid=%d tid=%d\n", (int)getpid(), (int)syscall(SYS_gettid));
	fflush(stdout);
	struct sigevent ev;
	memset(&ev, 0, sizeof ev);
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGRTMIN;
	timer_t id;
	int r = timer_create(cid, &ev, &id);
	struct itimerspec its;
	memset(&its, 0, sizeof its);
	its.it_value.tv_sec = 3600;
	timer_settime(id, 0, &its, NULL);
	printf("create=%d clockid=0x%x\n", r, (unsigned)cid);
	fflush(stdout);
	sleep(1);
	return 0;
}
