/* espencap: minimal UDP-encapsulation socket holder for the CVE-2026-43284
 * ESP-in-UDP write primitive. Android's IpSecManager.openUdpEncapsulationSocket()
 * creates this socket; on a rooted device we can create the same kernel object
 * ourselves so the production app-call path can be exercised without the App.
 *
 * Usage: espencap <port>   (bind 127.0.0.1:<port>, set UDP_ENCAP_ESPINUDP, sleep)
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef UDP_ENCAP
#define UDP_ENCAP 100
#endif
#ifndef UDP_ENCAP_ESPINUDP
#define UDP_ENCAP_ESPINUDP 2
#endif
#ifndef SOL_UDP
#define SOL_UDP 17
#endif

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: espencap <port>\n"); return 2; }
    const int port = atoi(argv[1]);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    int encap = UDP_ENCAP_ESPINUDP;
    if (setsockopt(fd, SOL_UDP, UDP_ENCAP, &encap, sizeof(encap)) != 0) {
        perror("setsockopt(UDP_ENCAP)"); return 1;
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { perror("bind"); return 1; }
    printf("espencap ready port=%d fd=%d\n", port, fd);
    fflush(stdout);
    for (;;) pause();
}
