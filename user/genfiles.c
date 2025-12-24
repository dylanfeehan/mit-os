#include "../kernel/types.h"
#include "user/user.h"
#include "../kernel/fcntl.h"

int create_example_file(char * filename, char * example_str) {
  printf("Creating file %s.\n", filename);
  int fd = open(filename, O_CREATE|O_WRONLY);
  if (fd < 0) {
    printf("Error opening file.\n");
    return 1;
  }
  int bytes_written = write(fd, example_str, strlen(example_str));
  close(fd);
  if(bytes_written < 0) {
    printf("Error writing file.\n");
    return 1;
  }
  return 0;
}

int main(int argc, char * argv[]) {
  char * examples[] = {
    "ex1.txt", "0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 30 36 40 42 48 49 50 54 55 56 57 58 59 60", 
    "ex2.txt", "1 5-6\t7\n10.11/12,13\r14 15-16\t18\n20.21/22,23\r24", 
    "ex3.txt", "xv6 /6, 6/ x6, /6x, a12 b/12, /12b, 12b /12,", 
    "ex4.txt", "5x x5 5 /5, 5/ ,5, /6, 6/ 6x x6 60",
    "ex5.txt", "000 0005 0006 0010 0012 0015 0030 0036 0040 0042 0048 0050 0060 0007 00011", // zeroes
    "ex6.txt", "-5 -6 -10 -11 -12 -15 -18 -19 -20 25-30-35 36-37",
    "ex7.txt", "5x x5 5 /5, 5/ ,5, /6, 6/ 6x x6 60\n",
  };

  int num_examples = 7;

  int i;
  for(i = 0; i < (num_examples*2); i+=2) {
    int result = create_example_file(examples[i], examples[i+1]);
    if (result < 0) {
      exit(result);
    }
  }

  exit(0);
}

