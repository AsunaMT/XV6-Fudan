#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char buf[512];

void cat(int fd){
    int n;
    while((n = read(fd, buf, sizeof(buf))) > 0) {
        if(write(STDOUT_FILENO, buf, n) != n) {
            printf("fail: cant't write\n");
            exit(1);
        }
    }
    if(n < 0){
        printf("fail: cant't read\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    // TODO
    if(argc <= 1){
        cat(0);
        exit(0);
    }
    for(int i = 1; i < argc; i++){
        int fd = open(argv[i], 0);
        if(fd < 0){
            printf("fail: can't open %s\n", argv[i]);
            //exit(0);
            continue;
        }
        cat(fd);
        close(fd);
    }
    exit(0);
    //return 0;
}

