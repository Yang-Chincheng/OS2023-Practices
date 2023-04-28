#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(){
    
    pid_t pid;
    // OPEN FILES
    int fd;
    fd = open("test.txt" , O_RDWR | O_CREAT | O_TRUNC);
    if (fd == -1)
    {
        perror("fail on open test.txt\n");
		return -1;
    }
    //write 'hello fcntl!' to file
    if (write(fd, "hello fcntl!", 12) < 0) {
        perror("fail to write to test.txt\n");
        return -1;
    }

    // DUPLICATE FD
    int dup_fd = fcntl(fd, F_DUPFD, 0);

    pid = fork();

    if(pid < 0){
        // FAILS
        perror("error in fork\n");
        return 1;
    }
    
    struct flock fl;

    if(pid > 0){
        // PARENT PROCESS
        //set the lock
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        if (fcntl(fd, F_SETLKW, &fl) < 0) {
            perror("fail to lock file in parent\n");
            exit(-1);
        }
        //append 'b'
        if (write(fd, "b", 1) < 0) {
            perror("fail to write in parent\n");
            exit(-1);
        }
        //unlock
        fl.l_type = F_UNLCK;
        if (fcntl(fd, F_SETLKW, &fl) < 0) {
            perror("fail to release lock in parent\n");
            exit(-1);
        }

        sleep(3);

        char str[1024];
        memset(str, 0, sizeof(str));
        if (lseek(fd, 0, SEEK_SET) < 0) {
            perror("fail to move r/w pointer\n");
            exit(-1);
        }
        if (read(fd, str, 1024) < 0) {
            perror("fail to read in parent\n");
            exit(-1);
        }
        // the feedback should be 'hello fcntl!ba'
        printf("%s\n", str);

        exit(0);

    } else {
        // CHILD PROCESS
        sleep(2);
        //get the lock
        if (fcntl(dup_fd, F_GETLK, &fl) < 0) {
            perror("fail to get flock in child\n");
            exit(-1);
        }
        //append 'a'
        if (fl.l_type == F_UNLCK) {
            fl.l_type = F_WRLCK;
            if (fcntl(dup_fd, F_SETLKW, &fl) < 0) {
                perror("fail to lock file in child\n");
                exit(-1);
            }
            if (write(dup_fd, "a", 1) < 0) {
                perror("fail to write in child\n");
                exit(-1);
            }
            fl.l_type = F_UNLCK;
            if (fcntl(dup_fd, F_SETLKW, &fl) < 0) {
                perror("fail to release lock in child\n");
                exit(-1);
            }
        }
        else 
            printf("the file is locked in child\n");
        exit(0);
    }
    close(fd);
    return 0;
}