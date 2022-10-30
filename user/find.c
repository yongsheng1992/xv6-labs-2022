#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *
fmtname(char *path, char *name) {
    char *buf = malloc(sizeof(strlen(path) + strlen(name) + 1));
    int i,j;

    for (i = 0, j = 0; i < strlen(path); i++, j++) {
        buf[j] = path[i];
    }

    buf[j++] = '/';
    for (i = 0; i < strlen(name); i++, j++) {
        buf[j] = name[i];
    }

    buf[j] = 0;

    return buf;
}

int
match(char *path, char *name) {
    int n = strlen(path);
    int m = strlen(name);

    if (m > n) {
        return 0;
    }

    int i, j;

    for (i = m-1, j = n-1; i >=0 && j>=0; i--, j--) {
        if (name[i] != path[j]) {
            return 0;
        }
    }

    if (path[j] != '/') {
        return 0;
    }

    return 1;
}

void
find(char *path, char *name) {
    // printf("find %s\n", path);
    char buf[512];
    int fd;
    struct stat st;
    struct dirent de;
    char *p;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_FILE:
        if (match(path, name)) {
            printf("%s\n", path);
        }
        break;
    case T_DIR:
        strcpy(buf, path);
        p = buf + strlen(path);
        *p++ = '/';

        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if(de.inum == 0) {
                continue;
            }
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;

            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
                continue;
            }
            find(buf, name);
        }
        break;
    default:
        break;
    }
    close(fd);
}

int
main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(2, "Usage: find ticks\n");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}