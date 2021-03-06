/**
 * Krishna Thiyagarajan
 * Scheduler
 * scheduler.c
 * 12/25/2016
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <limits.h>
#include <stdarg.h>
#include "scheduler.h"
#include "adjstack.c"

#define STACKLEN 65536

int retWithError(const char *format, ...) {
    va_list arg;
    va_start (arg, format);
    vfprintf(stderr, format, arg);
    va_end(arg);
    return -1;
}

void sched_init(void (*init_fn)()) {
    resched = 0;
    numPid = 0;
    numTicks = 0;


    signal(SIGVTALRM, sched_tick);
    signal(SIGABRT, sched_ps);

    struct timeval time;
    time.tv_sec = 0;
    time.tv_usec = 100000;

    struct itimerval timer;
    timer.it_interval = time;
    timer.it_value = time;

    setitimer(ITIMER_VIRTUAL, &timer, NULL);


    if (!(running = malloc(sizeof(struct sched_waitq))))
        exit(retWithError("Error malloc-ing for running queue with code %d: %s\n", errno, strerror(errno)));

    if (!(currProc = malloc(sizeof(struct sched_proc))))
        exit(retWithError("Error malloc-ing for current processes with code %d: %s\n", errno, strerror(errno)));

    void *tempsp;
    if ((tempsp = mmap(0, STACKLEN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0)) == MAP_FAILED)
        exit(retWithError("Error mmap-ing for initial stack with error code %d: %s\n", errno, strerror(errno)));


    int initPid = getFreePid();
    if (initPid < 0)
        exit(retWithError("Error allocationg PID for child process with code %d: %s\n", errno, strerror(errno)));


    struct sched_proc *initProc;
    if (!(initProc = malloc(sizeof(struct sched_proc))))
        exit(retWithError("Error malloc-ing space for initial process with code %d: %s\n", errno, strerror(errno)));


    initProc->pid = initPid;
    initProc->ppid = initPid;
    initProc->sp = tempsp;
    initProc->state = SCHED_RUNNING;
    initProc->priority = DEFAULT_PR;
    initProc->procTime = 0;
    initProc->cpuTime = 0;
    initProc->exitcode = 0;
    initProc->parent = initProc;
    initProc->niceval = 0;

    running->procQueue[0] = initProc;
    running->countProcs = 1;
    currProc = initProc;

    struct savectx currentctx;

    currentctx.regs[JB_BP] = tempsp + STACKLEN;
    currentctx.regs[JB_SP] = tempsp + STACKLEN;
    currentctx.regs[JB_PC] = init_fn;
    restorectx(&currentctx, 0);
    sched_ps();
}

pid_t getFreePid() {
    int ii;
    for (ii = 0; ii < SCHED_NPROC; ii++)
        if (!(running->procQueue[ii])) {
            numPid++;
            return ii + 1;
        }
}

int sched_fork() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
    struct sched_proc *childProc;
    if (!(childProc = malloc(sizeof(struct sched_proc))))
        return retWithError("Error with malloc-ing space for child process!\n");

    int childPid = getFreePid();
    if (childPid < 0)
        return retWithError("Unable to allocate a pid for the child!\n");

    void *tempsp;
    if ((tempsp = mmap(0, STACKLEN, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0)) == MAP_FAILED)
        return retWithError("Error mmapping child stack with code %d: %s\n", errno, strerror(errno));

    memcpy(tempsp, currProc->sp, STACKLEN);

    childProc->pid = childPid;
    childProc->ppid = currProc->pid;
    childProc->sp = tempsp;
    childProc->state = SCHED_READY;
    childProc->priority = DEFAULT_PR;
    childProc->procTime = currProc->procTime;
    childProc->cpuTime = currProc->cpuTime;
    childProc->exitcode = 0;
    childProc->parent = currProc;
    childProc->niceval = 0;
    running->procQueue[childPid - 1] = childProc;
    running->countProcs++;
    resched = 1;
    int retVal = childPid;
    if (!savectx(&childProc->ctx)) {
        unsigned long long adj = childProc->sp - currProc->sp;
        adjstack(tempsp, tempsp + STACKLEN, adj);
        childProc->ctx.regs[JB_BP] += adj;
        childProc->ctx.regs[JB_SP] += adj;
    }
    else {
        sigprocmask(SIG_UNBLOCK, &sigset, NULL);
        retVal = 0;
    }

    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
    return retVal;
}

void sched_exit(int ecode) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
    currProc->state = SCHED_ZOMBIE;
    resched = 1;
    currProc->exitcode = ecode;
    running->countProcs--;

    if (currProc->parent->state == SCHED_SLEEPING)
        currProc->parent->state = SCHED_READY;
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
    sched_switch();
};

int sched_wait(int *ecode) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    int ii;
    int childExists = 0;
    for (ii = 0; ii < SCHED_NPROC; ii++)
        if ((running->procQueue[ii]) && (running->procQueue[ii]->ppid == currProc->pid)) if (currProc->pid !=
                                                                                             running->procQueue[ii]->pid)
            childExists = 1;

    if (!childExists) {
        sigprocmask(SIG_UNBLOCK, &sigset, NULL);
        return -1;
    }

    childExists = 0;
    while (1) {
        for (ii = 0; ii < SCHED_NPROC; ii++) {
            if ((running->procQueue[ii]) && (running->procQueue[ii]->state == SCHED_ZOMBIE)) if (
                    running->procQueue[ii]->ppid == currProc->pid) {
                childExists = 1;
                break;
            }
        }

        if (childExists)
            break;


        currProc->state = SCHED_SLEEPING;
        resched = 1;
        sigprocmask(SIG_UNBLOCK, &sigset, NULL);
        sched_switch();
    }
    *ecode = running->procQueue[ii]->exitcode;
    free(running->procQueue[ii]);
    running->procQueue[ii] = NULL;
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
    return 0;
}

void sched_nice(int nice) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    if (nice > 19)
        nice = 19;
    else if (nice < -20)
        nice = -20;
    currProc->niceval = nice;
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

int sched_getpid() {
    return currProc->pid;
}

int sched_getppid() {
    return currProc->ppid;
}

int sched_gettick() {
    return numTicks;
}

void sched_ps() {
    retWithError("|%10s|%10s|%10s|%10s|%10s|%10s|%10s|\n", "PID", "PPID", "STATE", "STACK(HEX)", "NICE", "DYNAMIC",
                 "TIME");
    int ii;
    for (ii = 0; ii < SCHED_NPROC; ii++)
        if (running->procQueue[ii]) {
            retWithError("|%10d", running->procQueue[ii]->pid);
            retWithError("|%10d", running->procQueue[ii]->ppid);

            switch (running->procQueue[ii]->state) {
                case SCHED_READY:
                    retWithError("|%10s", "READY");
                    break;
                case SCHED_RUNNING:
                    retWithError("|%10s", "RUNNING");
                    break;
                case SCHED_SLEEPING:
                    retWithError("|%10s", "SLEEPING");
                    break;
                case SCHED_ZOMBIE:
                    retWithError("|%10s", "ZOMBIE");
                    break;
            }
            retWithError("|%10x", running->procQueue[ii]->sp);
            retWithError("|%10d", running->procQueue[ii]->niceval);
            retWithError("|%10d", running->procQueue[ii]->priority);
            retWithError("|%10d|\n", running->procQueue[ii]->procTime);
        }
}

void prioritize() {
    int ii;
    for (ii = 0; ii < SCHED_NPROC; ii++)
        if (running->procQueue[ii]) {
        	int temp = 20 - running->procQueue[ii]->niceval - (running->procQueue[ii]->cpuTime/(20 - running->procQueue[ii]->nicevall)); 
        	if(temp <=0)
        		temp = 1; 
        	running->procQueue[ii]->priority = temp;
        }
}

void sched_switch() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
    prioritize();
    if (currProc->state == SCHED_RUNNING)
        currProc->state = SCHED_READY;

    int ii = 0;
    int bestProcChoice = 0;
    int bestProcPriority = INT_MIN;

    for (ii = 0; ii < SCHED_NPROC; ii++)
        if ((running->procQueue[ii] != NULL))
            if ((running->procQueue[ii]->state == SCHED_READY))
                if ((running->procQueue[ii]->priority) > bestProcPriority) {
                    bestProcChoice = ii;
                    bestProcPriority = running->procQueue[ii]->priority;
                }

    if ((running->procQueue[bestProcChoice]->pid == currProc->pid))
        if ((currProc->state == SCHED_READY)) {
            currProc->state = SCHED_RUNNING;
            sigprocmask(SIG_UNBLOCK, &sigset, NULL);
            return;
        }

    if (savectx(&(currProc->ctx)) == 0) {
        currProc->cpuTime = 0;
        currProc = running->procQueue[bestProcChoice];
        retWithError("\n\n$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ SWITCHED TO PID %d $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\n",
                     currProc->pid);
        sched_ps();
        sigprocmask(SIG_UNBLOCK, &sigset, NULL);
        restorectx(&(currProc->ctx), 1);
    }
}

void sched_tick() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
    numTicks++;
    currProc->procTime++;
    currProc->cpuTime++;

    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
    sched_switch();
}