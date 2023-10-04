#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(){
	

   // pipe[0] for read, pipe[1] for write;
   int fpipe[2];
   int cpipe[2];
   pipe(fpipe);
   pipe(cpipe);
   char buffer[10];
   int pid;
   pid=fork();
   if (pid<0) {
    printf("FATAL: fork errors\n");
    exit(1);
   }else if (pid==0){
     // child process 

     close(fpipe[1]); 
     close(cpipe[0]);

     read(fpipe[0],buffer,4);
     
     printf("%d: received %s\n",getpid(),buffer);
     write(cpipe[1],"pong",4);


     close(fpipe[0]);
     close(cpipe[1]);

   }else {

     // parent process 
     //  close  child's  pipe of write and parent's pipe of read 
     close(cpipe[1]);
     close(fpipe[0]);

     write(fpipe[1],"ping",4);
     read(cpipe[0],buffer,4);
     close(fpipe[1]);
     close(cpipe[0]);

     printf("%d: received %s\n", getpid(),buffer);
   }
   exit(0);
}
