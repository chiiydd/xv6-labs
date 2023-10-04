#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void prime(int fpipe[2]){
	int p;
	read(fpipe[0],&p,sizeof(int));
	if (p==0){
		exit(0);
	}
	printf("prime %d\n",p);
	int cpipe[2];
	pipe(cpipe);
	if (fork()==0) {
		close(fpipe[0]);
		close(cpipe[1]);
//		close(fpipe[1]);
		prime(cpipe);
	}else{
		close(cpipe[0]);
		int number;
		while (1) {

		 read(fpipe[0],&number,sizeof(int));
		 if (number==0){
		 	break;
		 }
		 if (number%p){
			write(cpipe[1],&number,sizeof(int));
		 }
		}
		
		write(cpipe[1],&number,sizeof(int));
		close(cpipe[1]);
		close(fpipe[0]);
		wait(0);
		exit(0);
	}


}
int main(){

	int fpipe[2]; 
	pipe(fpipe);
	if (fork()==0){
		close(fpipe[1]);
		prime(fpipe);
	}else{
		close(fpipe[0]);
		int i;
		for (i=2; i<=35; i++) {
			write(fpipe[1],&i,sizeof(int));
		}
		i=0;
		write(fpipe[1],&i,sizeof(int));
		close(fpipe[1]);
	
	}
	wait(0);
	exit(0);


}
