#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 检查参数数量
  if(argc != 2){
  printf("usage: sleep ticks\n");
  exit(1);
}
  // 把 argv[1] 转成整数
  int ticks = atoi(argv[1]);
  // 调用 pause
  pause(ticks);
  // 退出
   exit(0);
}
