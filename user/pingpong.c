#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char* argv[]){
    if(argc > 1) {
        fprintf(2, "Usage: pingpong\n");
    }

    int pToc[2];
    int cTop[2];
    int pid = 0;
    char buf[10] = {0};

    pipe(pToc);
    pipe(cTop);
    pid = fork();

    if(pid < 0){
        fprintf(2, "fork error\n");
        exit(1);
    }

    if(0 == pid){
        close(cTop[0]);
        close(pToc[1]);

        if(read(pToc[0], buf, 4) < 0)
            exit(1);

        fprintf(1, "%d: received %s\n", getpid(), buf);

        if(write(cTop[1], "pong", 4) != 4)
            exit(1);
        
        exit(0);
    } else{
        close(pToc[0]);
        close(cTop[1]);

        if(write(pToc[1], "ping", 4) != 4)
            exit(1);
        
        if(read(cTop[0], buf, 4) != 4)
            exit(1);

        fprintf(1, "%d: received %s\n", getpid(), buf);
        
        wait((int*)0);
    }
    exit(0);
}