#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

static void 
run_cmd(char** cmd,int cmd_argc,char* path)
{
  char* args[MAXARG];

  if(cmd_argc + 2 > MAXARG){
    fprintf(2, "find: too many arguments for command\n");
    return;
  }

  for(int i = 0; i < cmd_argc; i++){
    args[i] = cmd[i];
  }

  args[cmd_argc] = path;

  args[cmd_argc + 1] = 0;

  int pid;
  if((pid = fork()) < 0){
    fprintf(2, "find: fork failed\n");
    return;
  }
  if(pid == 0){
    exec(args[0], args);
    fprintf(2, "find: exec %s failed\n", args[0]);
    exit(1);
  }
  else{
    wait(0);
  }
}

static char *
basename(char *path)
{
  char *p;

  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;

  return p + 1;
}

void 
find(char* path,char* target,char** cmd,int cmd_argc)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    if(strcmp(basename(path),target)==0){
      if(!cmd)
        printf("%s\n",path);
      else{
        run_cmd(cmd, cmd_argc, path);
      }
    }
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
        continue;
      find(buf,target,cmd,cmd_argc);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc == 3){
     find(argv[1], argv[2],0,0);
  }
  else if(argc >= 5 && strcmp(argv[3],"-exec")==0){
    find(argv[1], argv[2], &argv[4], argc - 4);
  }
  else{
    fprintf(2, "Usage: find <path> <target>\n");
    exit(1);
  }
  exit(0);
}
