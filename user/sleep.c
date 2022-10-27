#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

char *s = "(nothing happens for a little while)\n";

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: sleep ticks\n");
        exit(1);
    }
    write(1, s, strlen(s));
    int ticks = atoi(argv[1]);
    sleep(ticks);
    exit(0);
}