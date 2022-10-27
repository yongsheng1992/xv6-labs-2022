#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

char *ping = "ping";
char *pong = "pong";

int
main(int argc, char *argv[]) {
    int p2c[2];
    int c2p[2];

    pipe(p2c);
    pipe(c2p);
    int pid = fork();

    if (pid > 0) {
        close(p2c[0]);
        close(c2p[1]);
        write(p2c[1], ping, strlen(ping));
        close(p2c[1]);
        char *recv_pong = malloc(sizeof(strlen(pong)));
        read(c2p[0], recv_pong, strlen(pong));
        fprintf(1, "%d: received %s\n", getpid(), recv_pong);
        close(c2p[0]);
        wait(0);
    } else if (pid == 0) {
        close(p2c[1]);
        close(c2p[0]);
        char *recv_ping = malloc(sizeof(strlen(ping)));
        read(p2c[0], recv_ping, strlen(ping));
        fprintf(1, "%d: received %s\n", getpid(), recv_ping);
        close(p2c[0]);
        write(c2p[1], pong, strlen(pong));
        close(c2p[1]);
    } else {
        exit(1);
    }
    exit(0);
}