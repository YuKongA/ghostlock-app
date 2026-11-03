#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define TRACE "/sys/kernel/tracing"
int main(int argc, char **argv)
{
	const char *cpu = argc > 1 ? argv[1] : "0";
	uint64_t want = argc > 2 ? strtoull(argv[2], 0, 16) : 0;
	char path[128];
	snprintf(path, sizeof path, TRACE "/per_cpu/cpu%s/trace_pipe_raw", cpu);
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
	{
		printf("open %d\n", errno);
		return 1;
	}
	static uint8_t buf[1 << 21];
	size_t used = 0;
	for (int i = 0; i < 400 && used < sizeof(buf); i++)
	{
		ssize_t n = read(fd, buf + used, sizeof(buf) - used);
		if (n <= 0)
		{
			if (errno == EAGAIN)
			{
				usleep(5000);
				continue;
			}
			break;
		}
		used += n;
	}
	close(fd);
	printf("raw=%zu want=%llx\n", used, (unsigned long long)want);
	int hits = 0;
	for (size_t i = 0; i + 40 <= used; i++)
	{
		uint64_t site, ptr, req, al;
		memcpy(&site, buf + i, 8);
		memcpy(&ptr, buf + i + 8, 8);
		memcpy(&req, buf + i + 16, 8);
		memcpy(&al, buf + i + 24, 8);
		if (req != 0x100 || al != 0x100)
			continue;
		if ((ptr >> 48) != 0xffff)
			continue;
		uint32_t pid;
		memcpy(&pid, buf + i - 4, 4);
		printf("site=0x%llx low=0x%llx pid=%u ptr=0x%llx off=0x%zx\n",
			   (unsigned long long)site, (unsigned long long)(site & 0x1fffff), pid,
			   (unsigned long long)ptr, i);
		if (want && site == want)
			hits++;
	}
	printf("hits=%d\n", hits);
	return 0;
}
