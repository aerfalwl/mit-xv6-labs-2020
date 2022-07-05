#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


void get_files(const char* file_path, const char* file_name) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(file_path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", file_path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", file_path);
        close(fd);
        return;
    }

    switch(st.type) {
        case T_FILE:
            printf("Find a file instead of a path: %s\n", file_path);
            break;

        case T_DIR:
            if(strlen(file_path) + 1 + DIRSIZ + 1 > sizeof(buf)){
                printf("find: path too long\n");
                break;
            }
            strcpy(buf, file_path);
            p = buf+strlen(buf);
            *p++ = '/';
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0)
                    continue;
                if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
                    continue;
                }
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                if(stat(buf, &st) < 0){
                    printf("ls: cannot stat %s\n", buf);
                    continue;
                }
                // printf("find a file: %s\n", de.name);
                if (st.type == T_DIR) {
                    get_files(buf, file_name);
                } else if (st.type == T_FILE) {
                    if (strcmp(file_name, de.name) == 0) {
                        printf("%s\n", buf);
                    }
                }
            }
            break;
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    if(argc < 3) {
        printf("Usage: <path> <filename>\n");
        exit(0);
    }
    char* file_path = argv[1];
    char* file_name = argv[2];
    get_files(file_path, file_name);
    exit(0);
}
