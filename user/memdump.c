#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data);

int
main(int argc, char *argv[])
{
  if(argc == 1){
    printf("Example 1:\n");
    int a[2] = { 61810, 2025 };
    memdump("ii", (char*) a);
    
    printf("Example 2:\n");
    memdump("S", "a string");
    
    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *) &s);

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;
    
    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");
    
    printf("Example 4:\n");
    memdump("pihcS", (char*) &example);
    
    printf("Example 5:\n");
    memdump("sccccc", (char*) &example);
  } else if(argc == 2){
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while(n < sizeof(data)){
      int nn = read(0, data + n, sizeof(data) - n);
      if(nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data)
{
  // print the contents of the memory pointed to by data 
  // in the format described by the fmt argument. 
  // The format is a C string. 
  // Each character of the string indicates how to print successive parts of the data. 
  // Your code here.
  int i = 0;
  char token;
  while ((token = *(fmt + i)) != '\0') {
    switch (token) {
      case 'i': 
        printf("%d", *(int*)data);
        data += 4;
        break;
      case 'p': 
        printf("%lx", *(long*)data);
        data += 8;
        break;
      case 'h': 
        printf("%d", *(short*)data);
        data += 2;
        break;
      case 'c': 
        printf("%c", *data);
        data += 1;
        break;
      case 's': 
        printf("%s", (char *)*(char **)data);
        data += 8;
        break;
      case 'S': 
        printf("%s", data);
        break;
      default: 
        printf("Error processing input. %c is not a valid token.\n", token);
        return;
    }
    i += 1;
    printf("\n");
  }
  printf("\n");
}

