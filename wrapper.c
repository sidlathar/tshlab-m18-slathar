/*
 * wrapper.c - Wrapper for fork() that introduces non-determinism
 *          in the order that the parent and child are executed
 *          and other wrappers to trigger race conditions.
 */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include "csapp.h"
#include "tsh_helper.h"

/* Choose how want to delay */
#define UDELAY uspin

/* Sleep for a random period between 0 and MAX_SLEEP microseconds */
#define MAX_SLEEP 100000

#define CONVERT(val) (((double)val)/(double)RAND_MAX)

static bool shellsync_kill = false;
static bool shellsync_waitpid = false;
static bool shellsync_get_pid_of_job = false;
static int shellsyncfd;

/*
 * Implement microsecond-scale delay that spins rather than sleeps.
 * Unlike usleep, this function will not be terminated when a signal
 * is received.
 */
static void uspin(useconds_t usec) {
    struct timeval time;
    if (usec == 0) {
        return;
    }
    unsigned long ustart;
    unsigned long ucurr;
    gettimeofday(&time, NULL);
    ustart = 1000000 * time.tv_sec + time.tv_usec;
    ucurr = ustart;
    while (ucurr - ustart < usec) {
        gettimeofday(&time, NULL);
        ucurr = 1000000 * time.tv_sec + time.tv_usec;
    }
}


pid_t __real_fork(void);

int __real_sigsuspend(const sigset_t *mask);
int __real_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void __real_init_job_list();
static char *shellsync = "";

/* Shell synchronisation helpers */
/* signal to runtrace */
static void shellsync_signal() {
    int rc;
    if ((rc = send(shellsyncfd, shellsync, strlen(shellsync), 0)) < 0) {
        perror("send");
        exit(1);
    }
}

/* wait for runtrace to signal us */
static void shellsync_wait() {
    int rc;
    char buf[MAXBUF];
    if ((rc = recv(shellsyncfd, buf, MAXBUF, 0)) < 0) {
        perror("recv");
        exit(1);
    }
    UDELAY(20000);
}

static int init_wrappers() {
    struct timeval time;
    char *str;
    struct stat stat;
    // initialize RNG
    gettimeofday(&time, NULL);
    srand(time.tv_usec);
    // initialize shell sync.
    if ((str = getenv("SHELLSYNCFD")) != NULL) {
        shellsyncfd = atoi(str);
        if (fstat(shellsyncfd, &stat) != -1) {
            // the file is open, check the other environment variable
            // to select which synchronisation feature is enabled.
            if ((str = getenv("SHELLSYNC")) != NULL) {
                if (!strcmp("kill", str)) {
                    shellsync_kill = true;
                } else if (!strcmp("waitpid", str)) {
                    shellsync_waitpid = true;
                } else if (!strcmp("get_pid_of_job", str)) {
                    shellsync_get_pid_of_job = true;
                }
                // insert more synchronisation points here
            }
        }
    }
    return 1;
}


/* __wrap_init_jobs - wrapper around the init_job_list function
 * it is used to properly initialise the wrappers and detect which
 * shell synchronisation facilities is enabled
 */
void __wrap_init_job_list() {
    init_wrappers();
    __real_init_job_list();
}

/*
 * __wrap_get_pid_of_job - wrapper around the pid acessor
 * It is used to test for race condition in builtins fg and bg.
 */
pid_t __real_get_pid_of_job(struct job_t *jobp);

pid_t __wrap_get_pid_of_job(struct job_t *jobp) {
    if (shellsync_get_pid_of_job) {
        shellsync_signal();
        shellsync_wait();
    }
    return __real_get_pid_of_job(jobp);
}


/*
 * __wrap_fork - Link-time wrapper for fork() that introduces
 * non-determinism in the order that parent and child are executed.
 * After calling fork, randomly decide whether to sleep for a random
 * period in either the parent or child process, which results in
 * yielding control to the other process.  Based on a link-time
 * positioning technique: Given the -Wl,--wrap,fork argument, the linker
 * replaces all references to fork to __wrap_fork(), and all
 * references to __real_fork to fork().
 */
pid_t __wrap_fork(void) {

    bool b = (unsigned)(CONVERT(rand()) + 0.5);
    unsigned secs = (unsigned)(CONVERT(rand()) * MAX_SLEEP);

    /* Call the real fork function */
    pid_t pid = __real_fork();


    /* Randomly decide to sleep in the parent or the child */
    if (pid == 0) {
        if (b) {
            UDELAY(secs);
        }
    } else {
        if (!b) {
            UDELAY(secs);
        }
    }

#if 0
    sio_printf("Parent: pid=%d, delay=%dus.  Child: pid=%d, delay=%dus\n",
           getpid(), (int) parent_delay, pid, (int) child_delay);
#endif
    /* Return the PID like a normal fork call */
    return pid;
}

/*
 * __wrap_waitpid - Link time wrapper around waitpid
 * permits precise trgiggerring of race conditions right after
 * waitpid reaped a process
 */
pid_t __real_waitpid(pid_t pid, int *status, int options);

pid_t __wrap_waitpid(pid_t pid, int *status, int options) {
    pid_t ret = __real_waitpid(pid, status, options);
    if (shellsync_waitpid && ret > 0) {
        shellsync_signal();
        shellsync_wait();
    }
    return ret;
}


/*
 * __wrap_sigsuspend - Link time wrapper for sigsuspend
 * that sleeps before executing the call, increasing the risk that
 * a signal is handled before sigsuspend runs if signals are unblocked
 */
int __wrap_sigsuspend(const sigset_t *mask) {
    UDELAY((CONVERT(rand()) * MAX_SLEEP)/10);
    return __real_sigsuspend(mask);
}

/*
 * __wrap_sigprocmask - Link time wrapper for sigprocmask
 * that sleep before and after changing the signal mask, increasing likeliness
 * that a signal is handled immediatly after unblocking
 * detects premature unblocking, or blocking too late.
 */
int __wrap_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {

    int ret = __real_sigprocmask(how, set, oldset);
    return ret;
}
/* __wrap_kill - Link time wrapper for kill, used for optional
 * shell synchronisation */
int __real_kill(pid_t pid, int sig);

int __wrap_kill(pid_t pid, int sig) {
    if (shellsync_kill) {
        shellsync_signal();
        shellsync_wait();
    }
    return __real_kill(pid, sig);
}

/* __wrap_execve - Link time wrapper around execve that checks
 * that signals are not blocked */
int __real_execve(const char *path, char *const argv[], char *const envp[]);

int __wrap_execve(const char *path, char *const argv[], char *const envp[]) {
    sigset_t currmask;
    Sigprocmask(SIG_SETMASK, NULL, &currmask);
    if (sigismember(&currmask, SIGCHLD)) {
        Sio_puts("WARNING: SIGCHLD is blocked in execve\n");
    }
    if (sigismember(&currmask, SIGINT)) {
        Sio_puts("WARNING: SIGINT is blocked in execve\n");
    }
    if (sigismember(&currmask, SIGTSTP)) {
        Sio_puts("WARNING: SIGTSTP is blocked in execve\n");
    }
    return __real_execve(path, argv, envp);
}

/* __wrap_*printf - Link time wrapper for the printf family of functions */

int __wrap_printf(const char *format, ...) {
    va_list args;
    UDELAY((CONVERT(rand()) * MAX_SLEEP)/20);
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);
    return ret;
}

int __wrap_fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    UDELAY((CONVERT(rand()) * MAX_SLEEP)/20);
    va_start(args, format);
    int ret = vfprintf(stream, format, args);
    va_end(args);
    return ret;
}

int __wrap_sprintf(char *str, const char *format, ...) {
    va_list args;
    UDELAY((CONVERT(rand()) * MAX_SLEEP)/20);
    va_start(args, format);
    int ret = vsprintf(str, format, args);
    va_end(args);
    return ret;
}

int __wrap_snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    UDELAY((CONVERT(rand()) * MAX_SLEEP)/20);
    va_start(args, format);
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}

