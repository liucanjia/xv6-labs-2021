#include "kernel/types.h"
#include "user/user.h"

void sieve(int* fd){
    int prime = -1;
    int n = -1;
    int pid = -1;
    int num = -1;
    
    close(fd[1]);
    if((n = read(fd[0], &prime, sizeof(prime))) == 0){
        close(fd[0]);
        exit(0);
    }else{
        fprintf(1, "prime %d\n", prime);
    }

    int child[2];
    pipe(child);

    pid = fork();
    if(pid < 0) {
        fprintf(2, "primes: fork error\n");
        exit(1);
    }

    if(0 == pid){
        sieve(child);
    }else{
        close(child[0]);
        while((n = read(fd[0], &num, sizeof(num))) > 0){
            if(num % prime != 0){
                write(child[1], &num, sizeof(num));
            }
        }
        close(fd[0]);
        close(child[1]);
        wait(0);
        exit(0);    //本身是子进程, 需要exit, 否则父进程一直在等待
    }
    return;
}

int main(int argc, char* argv[]){
    int fd[2];
    int pid = -1;
    int i = 2;

    pipe(fd);
    pid = fork();
    if(pid < 0){
        fprintf(2, "primes: fork error\n");
        exit(1);
    }
    
    if(0 == pid){
        sieve(fd);
    }else{
        close(fd[0]);
        for(i = 2; i < 36; i++)
            write(fd[1], &i, sizeof(i));
        close(fd[1]);
        wait(0);
    }
    exit(0);
}
