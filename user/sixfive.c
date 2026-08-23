#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static void is_five_or_six(int value)
{
    if(value % 5 == 0 || value % 6 == 0){
        printf("%d\n", value);
    }
}



static int process_file(int fd)
{
    char c;
    char *seps = " -\r\t\n./,";

    int value = 0;
    int valid = 1;
    int have_digit = 0;

    int n;

    while((n = read(fd, &c, 1)) == 1){

        if(strchr(seps, c)){

            if(valid && have_digit){
               is_five_or_six(value);
            }

            value = 0;
            valid = 1;
            have_digit = 0;

        } else if(c >= '0' && c <= '9'){
          value = value * 10 + (c - '0');
          have_digit = 1;

        } else {
          valid = 0;
        }
    }
    if(n < 0){
        return -1;
    }
    if(valid && have_digit){
        is_five_or_six(value);
    }
    return 0;
}


int main(int argc, char *argv[])
{
    int fd;

    if(argc < 2){
        fprintf(2, "usage: sixfive file ...\n");
        exit(1);
    }

    for(int i = 1; i < argc; i++){
        fd = open(argv[i], O_RDONLY);

        if(fd < 0){
            fprintf(2, "sixfive: cannot open %s\n", argv[i]);
            continue;
        }

        if(process_file(fd) < 0){
            fprintf(2, "sixfive: cannot read %s\n", argv[i]);
        }

        close(fd);
    }

    exit(0);
}

