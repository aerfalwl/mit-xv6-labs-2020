#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{

  int pipefd[2];
  int cpid;
  char buf = 'c';
  if (pipe(pipefd) == -1) {
      printf("pipe error\n");
      exit(1);
  }

  cpid = fork();
  if (cpid == -1) {
      printf("fork error\n");
      exit(1);
  }

  if (cpid == 0) { // child process
      read(pipefd[0], &buf, 1);
      cpid = getpid();
      printf("%d: received ping\n", cpid);
      write(pipefd[1], &buf, 1);

  } else {
      write(pipefd[1], &buf, 1);
      // if we do not sleep here, the parent would read from the pipe, and the child would not be able to read from the pipe
      // if you do not want to sleep here, you should use two pipes instead of one
      sleep(1);
      read(pipefd[0], &buf, 1);
      cpid = getpid();
      printf("%d: received pong\n", cpid);
  }
  exit(0);
}
