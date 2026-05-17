#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define SHM_KEY 1234
#define MSG_KEY 2345

typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} MyClock;

int main(int argc, char *argv[]) { 
    int shmId;
    MyClock *myClock;

    shmId = shmget(SHM_KEY, sizeof(MyClock), 0666);
    if (shmId == -1) {
        perror("worker shmget");
        return EXIT_FAILURE;
    }

    myClock = (MyClock *)shmat(shmId, NULL, 0);
    if (myClock == (void *)-1) {
        perror("worker shmat");
        return EXIT_FAILURE;
    }
}