#include "kernel/types.h"
#include "user/user.h"

void xargs(char* str, int argc, char* argv[]){

    char* p = str;
    int i = 0, len = strlen(str);

    for(i = 0; i < len; i++){   //依次将参数保存到指针数组
        if(str[i] == ' '){
            str[i] = 0;
            argv[argc++] = p;
            p = str + i + 1;
        }
    }

    argv[argc++] = p;
    int pid = 0;
    pid = fork();
    if(pid < 0){
        fprintf(2, "xargs: fork error\n");
    }

    if(0 == pid){
        exec(argv[0], argv);
        exit(0);
    }else {
        wait(0);
    }
    return;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("xargs <command>\n");
        exit(1);
    }

    // 添加命令运行参数的二维数组
    char* commandArgv[32];
    int commandSize;
    // 将原本argv中的参数拷贝到新的参数数组中
    int nflag = 1;
    if(0 == strcmp(argv[1], "-n") && 0 == strcmp(argv[2], "1"))
        nflag = 3;

    commandSize = argc - nflag;
    for (int i = 0; i < commandSize; ++i) {
        commandArgv[i] = argv[i + nflag];
    }


    char buf[512]; // 输入缓冲
    int n, i ;
    char* p = buf;

    while((n = read(0, p, buf + 512 - p)) > 0){
        n += p - buf;   //更新缓冲区实际字符长度
        p = buf;        //从头遍历缓冲区字符串
        for(i = 0; i < n ; i++){
            if(buf[i] == '\n' || buf[i] == '"'){    //换行符或者引号视为输入一行结束
                buf[i] = 0;
                if(p != buf + i)                    //若引号就在新一行的开头直接忽略
                    xargs(p, commandSize, commandArgv);
                p = buf + i + 1;    //下一行输入的起始位置
            }else if(buf[i] == '\\' && buf[i+1] == 'n'){    //字符串中的\n转义为换行符
                buf[i] = buf[i+1] = 0;
                xargs(p, commandSize, commandArgv);
                p = buf + i + 2;    //下一行输入的起始位置
            }
        }
        
        if(p < buf + n){
            memmove(buf, p, buf + n - p);
            p = buf + (buf + n - p);
        }
    }

  exit(0);
}