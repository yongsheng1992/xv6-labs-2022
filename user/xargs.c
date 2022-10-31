#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"


int
main(int argc, char *argv[]) {
    char *exec_argv[32];
    char buf[32][32];
    char buf2[512];
    int i;

    for (i = 0; i < 32; i++) {
        exec_argv[i] = buf[i];
    }

    for (i = 1; i < argc; i++) {
        strcpy(buf[i-1], argv[i]);
    }

    int n;
    int pos = argc - 1;
    char *param = buf[pos];

    while((n = read(0, buf2, sizeof(buf2))) > 0) {
        for (i = 0; i < n; i++) {
            char ch = *(buf2+i);
            if (ch == ' ' || ch == '\n') {
                *param = '\0';
                pos++;
                param = buf[pos];
            } else {
                *param++ = ch;
            }
        }
    }

    *param = '\0';
    pos++;
    exec_argv[pos] = 0;

    int pid = fork();
    if (pid > 0) {
    } else if (pid == 0) {
        exec(exec_argv[0], exec_argv);
    }
    wait(0);
    exit(0);
}