#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

int
getcmd(char *buf, int nbuf)
{
    memset(buf, 0, nbuf);
    gets(buf, nbuf);
    if(buf[0] == 0) // EOF
        return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    if(argc < 2) {
        printf("Usage: xargs <cmd>\n");
        exit(0);
    }

    if (argc - 1 > MAXARG) {
        printf("Too many args. \n");
        exit(0);
    }

    char* cmd_args[MAXARG];
    int j = 0;
    for (int i = 1; i < argc; i++) {
        cmd_args[j] = malloc(512 * sizeof(char));
        memset(cmd_args[j], 0, 512 * sizeof(char));
        strcpy(cmd_args[j], argv[i]);
        j++;
    }

    char* cmd = argv[1];
    static char buf[100];
    while(getcmd(buf, sizeof(buf)) >= 0) {
        int len = strlen(buf) - 1;
        buf[len] = '\0';
        char pre = -1;
        int k = 0;
        for (int i = 0; i < len; i++) {
            if (buf[i] == ' ') {
                if (pre == ' ') {
                    continue;
                } else {
                    cmd_args[j][k] = 0;
                    j++;
                    k = 0;

                }
            } else {
                if (k == 0) {
                    cmd_args[j] = malloc(512 * sizeof(char));
                    memset(cmd_args[j], 0, 512 * sizeof(char));
                }
                cmd_args[j][k] = buf[i];
                k++;
            }
            pre = buf[i];
        }
        if (fork() == 0) { // child process
            exec(cmd, cmd_args);
            fprintf(2, "exec %s failed\n", cmd);
            exit(0);
        } else {
            wait(0);
        }
    }
    wait(0);
    exit(0);
}
