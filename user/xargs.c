// xargs.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 带参数列表，执行某个程序
void run(char *program, char **args) {
	if(fork() == 0) { // child exec
		exec(program, args);
		exit(0);
	}
	return; 
}

int main(int argc,char * argv[]){
    if (argc<2){
        printf("usage: xargs  <command>  [command args]...\n");
        exit(1);
    }
    char buf[2048];
    char *argsbuf[128]; //参数列表
    char **args=argsbuf; // 指向第一个从stdin传入的参数
    for (int i=1; i<argc; i++) {
        *args=argv[i];
        args++;
    }
    char *p=buf,*pbgin=buf;
    char ** pargs =args; //用来操作参数列表的指针
    while(read(0,p,1)!=0){
        // 遇到空格表示读入一个参数完成
        if (*p==' '||*p=='\n'){
            *p='\0';  // 用\0分隔参数
            *(pargs++)=pbgin;
            pbgin=p+1; 

            //表示读入一行参数完成可以开始运行命令
            if (*p=='\n'){ 
                *pargs=0; //表示参数结束

                run(argv[1],argsbuf);
                pargs=args;  //重新指向第一个从stdin传入的参数的
            }
        }
        p++;
    }
    //如果最后一行不是换行符
    if (pargs!=args){

        *p='\0';
        *(pargs++)=pbgin;
        *pargs=0;
        run(argv[1],argsbuf);
    }
    while(wait(0)!=-1) ;
    exit(0);



}

