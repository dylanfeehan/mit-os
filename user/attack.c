#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define DATASIZE (8*4096)

char data[DATASIZE];

int
main(int argc, char *argv[])
{
  // Your code here.
  int i;
  for(i = 0; i < DATASIZE; i++) {
    char pre_target[16] = {'T', 'h', 'i', 's', ' ', 'm', 'a', 'y', ' ', 'h', 'e', 'l', 'p', '.', 0, 0};
    char exe_chars[6] = {'(', 'n', 'u', 'l', 'l', ')'};

    if(data[i] == 'T') {
      if(memcmp(data + i, pre_target, 16)==0) {
        if(memcmp(data + i + 16, exe_chars, 6) == 0) {
          continue;
        }
        printf("%s\n", data + i + 16);
      }
    }
  }

  exit(1);
}
