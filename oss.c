#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define SIZE 20
#define SHM_KEY 1234
#define MSG_KEY 2345

typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} MyClock;

typedef struct {
int occupied; // either true or false
pid_t pid; // process id of this child
int startSeconds; // time when it was forked/created
int startNano; // time when it was forked /created
int endingTimeSeconds; // estimated time it should end
int endingTimeNano; // estimated time it should end
int messagesSent // total times oss sent a message to it
} PCB;

static PCB processTable[SIZE];
static const char *logFileName = "oss.log";
static int shmId;
MyClock *myClock;

static void print_help(void) {
    printf("Usage: oss [-h] [-n proc] [-s simul] [-t timelimit] [-i interval]\n");
    printf("  -h        Show help\n");
    printf("  -n Total number of workers to launch\n");
    printf("  -s Maximum number of workers in the system at once\n");
    printf("  -t bound of time that a child process will be launched for\n");
    printf("  -i Simulated time between launches in seconds\n");
    printf("  -f file   Log file name\n");
}

int main(int argc, char *argv[]) { 
    int totalChildren = 5;
    int maxSimul = 0;
    double timeLimit = 0.0;
    double interval = 0.0;
    char opt;
    const char optstring[] = "hn:s:t:i:f:";

    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
            case 'h':
                print_help();
                return 0;
            case 'n':
                totalChildren = atoi(optarg);
                break;
            case 's':
                maxSimul = atoi(optarg);
                break;
            case 't':
                timeLimit = atof(optarg);
                break;
            case 'i':
                interval = atof(optarg);
                break;
            case 'f':
                logFileName = optarg;
                break;
            default:
                fprintf(stderr, "Invalid option\n");
                print_help();
                return 1;
        }
    }

    shmId = shmget(SHM_KEY, sizeof(MyClock), IPC_CREAT | 0666);
    if (shmId < 0) {
        perror("oss shmget");
        exit(1);
    }

    myClock = (MyClock *)shmat(shmId, NULL, 0);
    if (myClock == (void *)-1) {
        perror("oss shmat");
        exit(1);
    }

}