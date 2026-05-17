#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define SHM_KEY 1234
#define MSG_KEY 2345
#define BILLION 1000000000U

typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} MyClock;

typedef struct {
    long mtype;
    int value;
} Message;

// Return true once the simulated clock has reached or passed the target time
static int time_to_terminate(const MyClock *clock, unsigned int termSeconds, unsigned int termNano) {
    return clock->seconds > termSeconds ||
           (clock->seconds == termSeconds && clock->nanoseconds >= termNano);
}

static void print_status(const MyClock *clock,
                         unsigned int termSeconds,
                         unsigned int termNano,
                         const char *message) {
    printf("WORKER PID:%ld PPID:%ld SysClockS: %u SysclockNano: %u\n",
           (long)getpid(), (long)getppid(),
           clock->seconds, clock->nanoseconds);
    printf("TermTimeS: %u TermTimeNano: %u\n", termSeconds, termNano);
    printf("--%s\n\n", message);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    int shmId;
    int msgId;
    MyClock *myClock;
    unsigned int lifetimeSeconds;
    unsigned int lifetimeNano;
    unsigned int termSeconds;
    unsigned int termNano;
    int done = 0;
    int messagesReceived = 0;

    if (argc != 3) {
        fprintf(stderr, "worker: Invalid number of arguments\n");
        return EXIT_FAILURE;
    }

    lifetimeSeconds = (unsigned int)strtoul(argv[1], NULL, 10);
    lifetimeNano = (unsigned int)strtoul(argv[2], NULL, 10);

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

    msgId = msgget(MSG_KEY, 0666);
    if (msgId == -1) {
        perror("worker msgget");
        shmdt(myClock);
        return EXIT_FAILURE;
    }

    // The target time is based on the simulated clock value at startup
    termSeconds = myClock->seconds + lifetimeSeconds;
    termNano = myClock->nanoseconds + lifetimeNano;
    while (termNano >= BILLION) {
        termSeconds++;
        termNano -= BILLION;
    }

    print_status(myClock, termSeconds, termNano, "Just Starting");

    do {
        Message incoming;
        Message outgoing;
        char statusLine[128];

        // Tight coordination: workers do nothing until oss grants a turn
        if (msgrcv(msgId, &incoming, sizeof(incoming.value), (long)getpid(), 0) == -1) {
            perror("worker msgrcv");
            shmdt(myClock);
            return EXIT_FAILURE;
        }

        messagesReceived++;
        done = time_to_terminate(myClock, termSeconds, termNano);

        if (done) {
            snprintf(statusLine, sizeof(statusLine),
                     "Terminating after sending message back to oss after %d received messages.",
                     messagesReceived);
        } else {
            snprintf(statusLine, sizeof(statusLine), "%d message received from oss", messagesReceived);
        }
        print_status(myClock, termSeconds, termNano, statusLine);

        outgoing.mtype = 1;
        outgoing.value = done ? 0 : 1;

        if (msgsnd(msgId, &outgoing, sizeof(outgoing.value), 0) == -1) {
            perror("worker msgsnd");
            shmdt(myClock);
            return EXIT_FAILURE;
        }
    } while (!done);

    shmdt(myClock);
    return EXIT_SUCCESS;
}