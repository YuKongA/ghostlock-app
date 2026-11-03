#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
int main(int c, char **v)
{
    int n = c > 1 ? atoi(v[1]) : 8192;
    int sv[2];
    char *b = malloc(n);
    memset(b, 'A', n);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
    {
        perror("sp");
        return 1;
    }
    int bufsize = 4 << 20;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof bufsize);
    ssize_t w = write(sv[0], b, n);
    printf("size=%d write=%zd\n", n, w);
    sleep(1);
    return 0;
}
