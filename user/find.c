#include "../kernel/types.h"
#include "../kernel/fs.h"
#include "../kernel/fcntl.h"
#include "../kernel/stat.h"
#include "../kernel/fcntl.h"
#include "../kernel/param.h"
#include "find.h"
#include "user.h"

void print_usage_message() {
  printf("Usage: find <dir> <filename>\nfind <dir> <filename> -exec <cmd> // where dir the subtree to begin searchign in\n");
}

int cmd_file(char * filename, void * context) {
  //char * args[MAXARG]; // allocate array of char pointers
  struct ctx * argv_ptr = (struct ctx *)context;
  argv_ptr->argv[argv_ptr->argc] = filename;
  int pid = fork();
  if(pid == 0) {
    exec(argv_ptr->argv[0], argv_ptr->argv);
  }
  wait(0);
  return 0;
}

int display_file(char * filename, void * context) {
  printf("%s\n", filename);
  return 0;
}

int append_subdirectory(char * dirname, char * subdirname) {
  int len = strlen(dirname);
  char * endptr = dirname + len;

  if(dirname != endptr) {
    endptr[0] = '/';
    endptr++;
  }
  strcpy(endptr, subdirname);
  return 0;
}

int readstat(char * dirname, struct stat * st) {
  int fd;
  int result;
  if((fd = open(dirname, O_RDONLY)) < 0) {
    printf("Error opening file %s.\n", dirname);
    return -1;
  }
  if((result = fstat(fd, st)) < 0) {
    printf("Error reading fstat on file %s.\n", dirname);
    return -1;
  }
  return fd;
}

int find(char * dirname, char * filename, int (*callback)(char *, void *), struct ctx * context) {
  struct stat st;
  struct dirent de;
  char dirent_buf[512];

  int dirname_fd;
  if((dirname_fd = readstat(dirname, &st)) < 0) {
    printf("Error calling readstat on file %s.\n", dirname);
    return 1;
  }
    
  if(st.type != T_DIR) {
    printf("Error: %s is not a directory.\n", dirname);
    close(dirname_fd);
    return 1;
  }

  // read the directory file into de, one de at a time
  while(read(dirname_fd, &de, sizeof(de))==sizeof(de)) {
    if(de.inum == 0) {
      continue;
    }

    strcpy(dirent_buf, dirname);

    int dirent_fd;
    append_subdirectory(dirent_buf, de.name);

    if((dirent_fd = readstat(dirent_buf, &st)) < 0) {
      printf("Error.\n");
    }
    close(dirent_fd);

    if(st.type == T_FILE || st.type == T_DIR) {
      if(strcmp(de.name, filename) == 0) {
        callback(dirent_buf, context);
      }
    }

    if(st.type == T_DIR) {
      if(strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
        find(dirent_buf, filename, callback, context);
      }
    }
  }
  close(dirname_fd);

  return 0;
}

int main(int argc, char * argv[]) {
  int cmd = 0;
  int cmd_argc = argc - 4;
  struct ctx context;
  if(argc != 3 && argc < 5) {
    printf("Error: 2 or 4+ arguments required, %d provided.\n", argc - 1);
    print_usage_message();
    exit(1);
  }
  if(argc >= 5 && strcmp(argv[3], "-exec") == 0) {
    int i;
    for (i = 0; i < cmd_argc; i++) {
      context.argv[i] = argv[i + 4];
    }
    context.argc = cmd_argc;
    cmd = 1;
  } else {
    if(argc >= 5) {
      printf("Error: unsupported flag: %s.\n", argv[3]);
      exit(1);
    }
  }

  int result = find(argv[1], argv[2], cmd ? (cmd_file) : (display_file), &context);
  exit(result);
}

