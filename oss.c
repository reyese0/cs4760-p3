#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SIZE 20
#define SHM_KEY 1234
#define MSG_KEY 2345
#define BILLION 1000000000U
#define HALF_SECOND 500000000U

typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} MyClock;

typedef struct {
    int occupied; // either true or false
    int workerNumber; // launch order for this child, starting at 1
    pid_t pid; // process id of this child
    unsigned int startSeconds; // time when it was forked/created
    unsigned int startNano; // time when it was forked /created
    unsigned int endingTimeSeconds; // estimated time it should end
    unsigned int endingTimeNano; // estimated time it should end
    unsigned int messagesSent; // total times oss sent a message to it
} PCB;

typedef struct {
    long mtype;
    int value;
} Message;

static PCB processTable[SIZE];
static const char *logFileName = "oss.log";
static FILE *logFile = NULL;
static int shmId = -1;
static int msgId = -1;
static MyClock *myClock = NULL;

static void print_help(void) {
    printf("Usage: oss [-h] [-n proc] [-s simul] [-t timelimit] [-i interval] [-f file]\n");
    printf("  -h        Show help\n");
    printf("  -n proc   Total number of workers to launch\n");
    printf("  -s simul  Maximum number of workers in the system at once\n");
    printf("  -t limit  bound of time that a child process will be launched for\n");
    printf("  -i time   Simulated time between launches\n");
    printf("  -f file   Log file name for oss output\n");
}

// Write every oss event to both the terminal and the requested log file
static void log_both(const char *fmt, ...) {
    va_list args;
    va_list copy;

    va_start(args, fmt);
    va_copy(copy, args);
    vprintf(fmt, args);
    if (logFile != NULL) {
        vfprintf(logFile, fmt, copy);
        fflush(logFile);
    }
    va_end(copy);
    va_end(args);
}

// Keep the simulated clock normalized after adding nanoseconds
static void add_to_clock(unsigned int seconds, unsigned int nanoseconds) {
    myClock->seconds += seconds;
    myClock->nanoseconds += nanoseconds;

    while (myClock->nanoseconds >= BILLION) {
        myClock->seconds++;
        myClock->nanoseconds -= BILLION;
    }
}

static int time_at_or_after(unsigned int secA, unsigned int nsA, unsigned int secB, unsigned int nsB) {
    return secA > secB || (secA == secB && nsA >= nsB);
}

static unsigned long long seconds_to_ns(double seconds) {
    if (seconds <= 0.0) {
        return 0ULL;
    }
    return (unsigned long long)(seconds * 1000000000.0 + 0.5);
}

static void ns_to_parts(unsigned long long totalNs, unsigned int *seconds, unsigned int *nanoseconds) {
    *seconds = (unsigned int)(totalNs / BILLION);
    *nanoseconds = (unsigned int)(totalNs % BILLION);
}

static void cleanup(void) {
    if (myClock != NULL && myClock != (void *)-1) {
        shmdt(myClock);
        myClock = NULL;
    }
    if (shmId != -1) {
        shmctl(shmId, IPC_RMID, NULL);
        shmId = -1;
    }
    if (msgId != -1) {
        msgctl(msgId, IPC_RMID, NULL);
        msgId = -1;
    }
    if (logFile != NULL) {
        fclose(logFile);
        logFile = NULL;
    }
}

static int count_running_children(void) {
    int count = 0;

    for (int i = 0; i < SIZE; i++) {
        if (processTable[i].occupied) {
            count++;
        }
    }
    return count;
}

static int find_free_slot(void) {
    for (int i = 0; i < SIZE; i++) {
        if (!processTable[i].occupied) {
            return i;
        }
    }
    return -1;
}

static int find_next_occupied_slot(int start) {
    for (int offset = 0; offset < SIZE; offset++) {
        int index = (start + offset) % SIZE;
        if (processTable[index].occupied) {
            return index;
        }
    }
    return -1;
}

static void print_process_table(void) {
    log_both("\nOSS PID:%ld SysClockS: %u SysclockNano: %u\n",
             (long)getpid(), myClock->seconds, myClock->nanoseconds);
    log_both("Process Table:\n");
    log_both("Entry Occupied Worker PID StartS StartN EndingTS EndingTN MessagesSent\n");

    for (int i = 0; i < SIZE; i++) {
        log_both("%d %d %d %ld %u %u %u %u %u\n",
                 i,
                 processTable[i].occupied,
                 processTable[i].workerNumber,
                 (long)processTable[i].pid,
                 processTable[i].startSeconds,
                 processTable[i].startNano,
                 processTable[i].endingTimeSeconds,
                 processTable[i].endingTimeNano,
                 processTable[i].messagesSent);
    }
    log_both("\n");
}

static unsigned long long random_duration_ns(unsigned long long maxRuntimeNs) {
    unsigned long long minRuntimeNs = BILLION;
    unsigned long long range;
    unsigned long long value;

    if (maxRuntimeNs < minRuntimeNs) {
        maxRuntimeNs = minRuntimeNs;
    }

    range = maxRuntimeNs - minRuntimeNs + 1ULL;
    value = ((unsigned long long)rand() << 32) ^ (unsigned long long)rand();
    return minRuntimeNs + (value % range);
}

static int launch_worker(int slot, int workerNumber, unsigned long long maxRuntimeNs) {
    unsigned int runtimeSeconds;
    unsigned int runtimeNano;
    unsigned int endingSeconds;
    unsigned int endingNano;
    char secondsArg[32];
    char nanoArg[32];
    pid_t pid;

    ns_to_parts(random_duration_ns(maxRuntimeNs), &runtimeSeconds, &runtimeNano);

    endingSeconds = myClock->seconds + runtimeSeconds;
    endingNano = myClock->nanoseconds + runtimeNano;
    while (endingNano >= BILLION) {
        endingSeconds++;
        endingNano -= BILLION;
    }

    pid = fork();
    if (pid < 0) {
        perror("oss fork");
        return -1;
    }

    if (pid == 0) {
        snprintf(secondsArg, sizeof(secondsArg), "%u", runtimeSeconds);
        snprintf(nanoArg, sizeof(nanoArg), "%u", runtimeNano);
        execl("./worker", "worker", secondsArg, nanoArg, (char *)NULL);
        perror("worker exec");
        exit(EXIT_FAILURE);
    }

    processTable[slot].occupied = 1;
    processTable[slot].workerNumber = workerNumber;
    processTable[slot].pid = pid;
    processTable[slot].startSeconds = myClock->seconds;
    processTable[slot].startNano = myClock->nanoseconds;
    processTable[slot].endingTimeSeconds = endingSeconds;
    processTable[slot].endingTimeNano = endingNano;
    processTable[slot].messagesSent = 0;

    log_both("OSS: Launched worker %d in table entry %d PID %ld for %u:%u at time %u:%u\n",
             workerNumber, slot, (long)pid, runtimeSeconds, runtimeNano,
             myClock->seconds, myClock->nanoseconds);
    print_process_table();
    return 0;
}

static void clear_process_slot(int slot) {
    memset(&processTable[slot], 0, sizeof(processTable[slot]));
}

int main(int argc, char *argv[]) {
    int totalChildren = 5;
    int maxSimul = 3;
    double timeLimit = 1.0;
    double launchInterval = 0.0;
    unsigned long long maxRuntimeNs;
    unsigned long long intervalNs;
    unsigned int nextLaunchSeconds = 0;
    unsigned int nextLaunchNano = 0;
    unsigned int nextTableSeconds = 0;
    unsigned int nextTableNano = HALF_SECOND;
    int launchedChildren = 0;
    int totalMessagesSent = 0;
    int nextSlotToMessage = 0;
    int opt;

    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
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
                launchInterval = atof(optarg);
                break;
            case 'f':
                logFileName = optarg;
                break;
            default:
                print_help();
                return 1;
        }
    }

    if (totalChildren < 0) {
        totalChildren = 0;
    }
    if (maxSimul < 1) {
        maxSimul = 1;
    }
    if (maxSimul > SIZE) {
        maxSimul = SIZE;
    }

    srand((unsigned int)(time(NULL) ^ getpid()));
    maxRuntimeNs = seconds_to_ns(timeLimit);
    intervalNs = seconds_to_ns(launchInterval);

    logFile = fopen(logFileName, "w");
    if (logFile == NULL) {
        perror("oss fopen");
        return EXIT_FAILURE;
    }

    // Remove abandoned IPC objects from an earlier interrupted run
    shmId = shmget(SHM_KEY, sizeof(MyClock), 0666);
    if (shmId != -1) {
        shmctl(shmId, IPC_RMID, NULL);
        shmId = -1;
    }
    msgId = msgget(MSG_KEY, 0666);
    if (msgId != -1) {
        msgctl(msgId, IPC_RMID, NULL);
        msgId = -1;
    }

    shmId = shmget(SHM_KEY, sizeof(MyClock), IPC_CREAT | 0666);
    if (shmId < 0) {
        perror("oss shmget");
        cleanup();
        return EXIT_FAILURE;
    }

    myClock = (MyClock *)shmat(shmId, NULL, 0);
    if (myClock == (void *)-1) {
        perror("oss shmat");
        cleanup();
        return EXIT_FAILURE;
    }
    myClock->seconds = 0;
    myClock->nanoseconds = 0;

    msgId = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msgId == -1) {
        perror("oss msgget");
        cleanup();
        return EXIT_FAILURE;
    }

    // Start with as many workers as the simul limit allows
    while (launchedChildren < totalChildren &&
           count_running_children() < maxSimul) {
        int slot = find_free_slot();
        if (slot == -1 || launch_worker(slot, launchedChildren + 1, maxRuntimeNs) == -1) {
            break;
        }
        launchedChildren++;
        ns_to_parts(intervalNs, &nextLaunchSeconds, &nextLaunchNano);
    }

    while (launchedChildren < totalChildren || count_running_children() > 0) {
        int runningChildren = count_running_children();
        int slot;
        Message outgoing;
        Message incoming;

        if (runningChildren > 0) {
            add_to_clock(0, 250000000U / (unsigned int)runningChildren);
        } else {
            add_to_clock(0, 250000000U);
        }

        while (time_at_or_after(myClock->seconds, myClock->nanoseconds,
                                nextTableSeconds, nextTableNano)) {
            print_process_table();
            nextTableNano += HALF_SECOND;
            while (nextTableNano >= BILLION) {
                nextTableSeconds++;
                nextTableNano -= BILLION;
            }
        }

        // Reuse a free PCB slot whenever capacity and launch timing permit it
        while (launchedChildren < totalChildren &&
               count_running_children() < maxSimul &&
               time_at_or_after(myClock->seconds, myClock->nanoseconds,
                                nextLaunchSeconds, nextLaunchNano)) {
            int freeSlot = find_free_slot();
            if (freeSlot == -1 || launch_worker(freeSlot, launchedChildren + 1, maxRuntimeNs) == -1) {
                break;
            }
            launchedChildren++;
            nextLaunchNano += (unsigned int)(intervalNs % BILLION);
            nextLaunchSeconds += (unsigned int)(intervalNs / BILLION);
            while (nextLaunchNano >= BILLION) {
                nextLaunchSeconds++;
                nextLaunchNano -= BILLION;
            }
        }

        runningChildren = count_running_children();
        if (runningChildren == 0) {
            continue;
        }

        slot = find_next_occupied_slot(nextSlotToMessage);
        if (slot == -1) {
            continue;
        }
        nextSlotToMessage = (slot + 1) % SIZE;

        outgoing.mtype = processTable[slot].pid;
        outgoing.value = 1;

        log_both("OSS: Sending message to worker %d in table entry %d PID %ld at time %u:%u\n",
                 processTable[slot].workerNumber, slot, (long)processTable[slot].pid,
                 myClock->seconds, myClock->nanoseconds);

        if (msgsnd(msgId, &outgoing, sizeof(outgoing.value), 0) == -1) {
            perror("oss msgsnd");
            break;
        }
        processTable[slot].messagesSent++;
        totalMessagesSent++;

        if (msgrcv(msgId, &incoming, sizeof(incoming.value), 1, 0) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("oss msgrcv");
            break;
        }

        log_both("OSS: Receiving message from worker %d in table entry %d PID %ld at time %u:%u\n",
                 processTable[slot].workerNumber, slot, (long)processTable[slot].pid,
                 myClock->seconds, myClock->nanoseconds);

        if (incoming.value == 0) {
            log_both("OSS: Worker %d in table entry %d PID %ld is planning to terminate.\n",
                     processTable[slot].workerNumber, slot, (long)processTable[slot].pid);
            waitpid(processTable[slot].pid, NULL, 0);
            clear_process_slot(slot);
        }
    }

    log_both("OSS: Ending report\n");
    log_both("OSS: Total processes launched: %d\n", launchedChildren);
    log_both("OSS: Total messages sent from oss: %d\n", totalMessagesSent);

    cleanup();
    return EXIT_SUCCESS;
}
