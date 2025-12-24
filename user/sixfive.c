#include "kernel/types.h"
#include "user/user.h"
#include "../kernel/fcntl.h"

char * number_tokens = "0123456789";
char * separator_tokens = " -\r\t\n./,";

void print_usage_message() {
  printf("Usage: sixfive file.txt file2.txt ...\n");
}

void process_number(int number) {
  if(number % 5 == 0 || number % 6 == 0) {
   printf("%d\n", number);
  }
}

int str_contains(char * s, char token) {
  if(strchr(s, token)) {
    return 1;
  } else {
    return 0;
  }
}

int is_number(char token) {
  return str_contains(number_tokens, token);
}

int is_separator(char token) {
  return str_contains(separator_tokens, token);
}

int process_file(int fd) {
  int prev_is_number = 0;
  int prev_is_separator = 1;

  char token;
  int bytes_read = read(fd, &token, 1);

  int number = 0;
  int token_number = 0;
  while(bytes_read > 0) {
    if (prev_is_number) {
      if(is_number(token)) {
        token_number = atoi(&token);
        number = number * 10 + token_number;
      } else if(is_separator(token)) {
        process_number(number);
        number = 0;
        prev_is_separator = 1;
        prev_is_number = 0;
      } else {
        number = 0;
        prev_is_separator = 0;
        prev_is_number = 0;
      }
    } else if(prev_is_separator) {
      if(is_number(token)) {
        number = atoi(&token);
        prev_is_separator = 0;
        prev_is_number = 1;
      } else if(is_separator(token)) {
      } else {
        prev_is_separator = 0;
        prev_is_number = 0;
      }
    } else {
      if(is_number(token)) {
      } else if(is_separator(token)) {
        prev_is_separator = 1;
        prev_is_number = 0;
      } else {
      }
    }
    bytes_read = read(fd, &token, 1);
  }

  if(bytes_read < 0) {
    printf("Error reading file.\n");
    return -1;
  }
  
  // if were processing a number, 
  // since EOF is a valid separator, 
  // need to process the number
  if(prev_is_number) {
    process_number(number);
  }

  return 0;
}

int main(int argc, char * argv[]) {
  if(argc < 2) {
    printf("Error: at least 1 argument required.\n");
    print_usage_message();
    exit(1);
  }

  int i;
  // process each text file, passing in file descriptors
  for(i = 1; i < argc; i++) {
    int fd = open(argv[i], O_RDONLY);
    if (fd < 0) {
      printf("Error opening file.\n");
      exit(1);
    }
    int result = process_file(fd);
    if (result < 0) {
      printf("Error processing file %s.\n", argv[i]);
      exit(1);
    }
  }
  exit(0);
}

// state "processing post number"
//  - to process a number, gather the number
//  - to process a separator, display if divisible by 5/6 and clear the number
//  - to process an ignorable, clear the number
// state "processing post-separator"
//  - to process a number, start gathering the number
//  - to process a separator, do nothing
//  - to process an ignorable, disable prev_is_separator and enable prev_is_ignorable
// state "processing post-ignorable"
//  - to process number, continue and do nothing
//  - to process a separator, disable prev_is_ignorable and enable prev_is_separator
//  - to process ignorable, continue and do nothing
