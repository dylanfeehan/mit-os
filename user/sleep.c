#include "kernel/types.h"
#include "user/user.h"

void print_usage_message() {
  printf("usage: sleep <n> // where n is the number of seconds.\n");
}

int main(int argc, char * argv[]) {
  if(argc != 2) {
    printf("Error: %d arguments provided, 1 required.\n", argc);
    print_usage_message();
    exit(1);
  }
  char * str_input_ptr = *(argv + 1);

  if(strcmp(str_input_ptr, "0") == 0) {
    printf("Error: cannot sleep for 0 seconds.\n");
    print_usage_message();
    exit(1);
  }

  int seconds_input =  atoi((str_input_ptr));
  if(seconds_input == 0) {
    printf("Error converting %s to integer.\n", (str_input_ptr));
    exit(1);
  }

  printf("Sleeping for %d seconds.\n", seconds_input);
  int result = pause(seconds_input * 10);
  exit(result);
}

