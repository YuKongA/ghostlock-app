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
	int want = argc > 2 ? atoi(argv[2]) : 3;
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
	int shown = 0;
	for (size_t i = 0; i + 56 <= used && shown < want; i++)
	{
		uint64_t req;
		memcpy(&req, buf + i + 24, 8);
		if (req != 0x100)
			continue;
		uint64_t a, b, c, d;
		memcpy(&a, buf + i, 8);
		memcpy(&b, buf + i + 8, 8);
		memcpy(&c, buf + i + 16, 8);
		memcpy(&d, buf + i + 32, 8);
		printf("off=0x%zx: +0=0x%016llx +8=0x%016llx +16=0x%016llx req=0x%llx +32=0x%llx\n",
			   i, (unsigned long long)a, (unsigned long long)b, (unsigned long long)c,
			   (unsigned long long)req, (unsigned long long)d);
		shown++;
	}
	return 0;
}
