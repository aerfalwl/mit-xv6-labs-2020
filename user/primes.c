#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void work(int parent_pipe[]) {
    close(parent_pipe[1]);
    int child_pipe[2];

    int base;
    if (read(parent_pipe[0], &base, 4) == 0) {
        exit(0);
    }
    printf("prime %d\n", base);
    if (pipe(child_pipe) == -1) {
        printf("pipe error\n");
        exit(0);
    }

    int cpid = fork();
    if (cpid == -1) {
        printf("fork error\n");
        exit(1);
    }

    if (cpid != 0) { // parent
        close(child_pipe[0]);
        while (1) {
            int recv = 0;
            int ret = read(parent_pipe[0], &recv, sizeof(int));
            if (ret == 0) {
                break;
            }
            if (recv % base != 0) {
                int send = recv;
                write(child_pipe[1], &send, sizeof(int));
            }
        }
        close(parent_pipe[0]);
        close(child_pipe[1]);
        wait(0);
    } else {
        work(child_pipe);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        printf("pipe error\n");
        exit(1);
    }

    int cpid = fork();
    if (cpid == -1) {
        printf("fork error\n");
        exit(1);
    }

    if (cpid == 0) { // child process
        work(pipefd);
    } else {
        close(pipefd[0]);
        // the first process pipe write all numbers to the next pipe
        for (int i = 2; i <= 35; i++) {
            write(pipefd[1], &i, sizeof(int));
        }
        close(pipefd[1]);
        wait(0);
    }
    exit(0);
}
