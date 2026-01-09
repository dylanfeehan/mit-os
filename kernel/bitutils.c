#include "../kernel/types.h"
#include "user.h"

void int_to_bitstring(int num) {
  unsigned int unum = (unsigned int)num;
  char bitstring[33];
  bitstring[32] = '\0';
  int i;
  for(i = 0; i < 32; i++) {
    bitstring[i] = ((unum << i) >> 31) ? '1' : '0';
  }
  
  printf("bitstring: %s\n", bitstring);
}

