#include "kernel/types.h"
#include "user/user.h"

void
feed() {
    int i;
    for (i = 2; i < 36; i++) {
        write(1, &i, sizeof(i));
    }
}

// redirect stdin/stdout to pipe read/write end.
// eg. redirect(0, pd) redirect stdin to pipe read end and close pipe write end
void
redirect(int fd, int pd[]) {
    close(fd);
    dup(pd[fd]);
    close(pd[0]);
    close(pd[1]);
}

void
transport(int p) {
    int n;
    while (read(0, &n, sizeof(n))) {
        if (n % p != 0) {
            write(1, &n, sizeof(n));
        }
    }
}

void consume() {
    int pd[2];
    int p;
    if (read(0, &p, sizeof(p))) {
        printf("prime %d\n", p);
        pipe(pd);
        int pid = fork();
        if (pid > 0) {
            redirect(1, pd);
            transport(p);
            close(1);
        } else if (pid == 0) {
            redirect(0, pd);
            consume(p);
            close(0);
            exit(0);
        } else {
            exit(1);
        }
    }
    wait((int *)0);
    exit(0);
}

int
main(int argc, char *argv[]) {
    int pd[2];
    pipe(pd);
    int pid = fork();
    if (pid > 0) {
        redirect(1, pd);
        feed();
        close(1);
    } else if (pid == 0){
        redirect(0, pd);
        consume();
        close(0);
        exit(0);
    } else {
        exit(1);
    }

    wait((int *)0);
    exit(0);
}