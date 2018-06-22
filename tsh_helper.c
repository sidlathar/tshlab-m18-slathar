/* tsh_helper.c
 * helper routines for tshlab
 */
#if 0
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
//#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#endif

#include "tsh_helper.h"

/* Global variables */
extern char **environ;          // Defined in libc
char prompt[] = "tsh> ";        // Command line prompt (do not change)
bool verbose = false;           // If true, prints additional output
bool check_block = true;        // If true, check that signals are blocked
int nextjid = 1;                // Next job ID to allocate

struct job_t                    // The job struct
{
    pid_t pid;                  // Job PID
    int jid;                    // Job ID [1, 2, ...] defined in tsh_helper.c
    job_state state;            // UNDEF, BG, FG, or ST
    char cmdline[MAXLINE_TSH];  // Command line
};

// Parsing states, used for parseline
typedef enum parse_state
{
    ST_NORMAL,
    ST_INFILE,
    ST_OUTFILE
} parse_state;


static struct job_t job_list[MAXJOBS]; // The job list

/*
 * parseline - Parse the command line and build the argv array.
 *
 *   cmdline:  The command line, in the form:
 *
 *                command [arguments...] [< infile] [> oufile] [&]
 *
 *   token:    Pointer to a cmdline_tokens structure. The elements of this
 *             structure will be populated with the parsed tokens. Characters
 *             enclosed in single or double quotes are treated as a single
 *             argument.
 *
 * Returns:
 *   PARSELINE_EMPTY:        if the command line is empty
 *   PARSELINE_BG:           if the user has requested a BG job
 *   PARSELINE_FG:           if the user has requested a FG job
 *   PARSELINE_ERROR:        if cmdline is incorrectly formatted
 *
 */
parseline_return parseline(const char *cmdline,
                           struct cmdline_tokens *token) {
    const char delims[] = " \t\r\n";    // argument delimiters (white-space)
    char *buf;                          // ptr that traverses command line
    char *next;                         // ptr to the end of the current arg
    char *endbuf;                       // ptr to end of cmdline string

    parse_state parsing_state;          // indicates if the next token is the
                                        // input or output file

    if (cmdline == NULL) {
        fprintf(stderr, "Error: command line is NULL\n");
        return PARSELINE_EMPTY;
    }

    strncpy(token->text, cmdline, MAXLINE_TSH);

    buf = token->text;
    endbuf = token->text + strlen(token->text);

    // initialize default values
    token->argc = 0;
    token->infile = NULL;
    token->outfile = NULL;

    /* Build the argv list */
    parsing_state = ST_NORMAL;

    while (buf < endbuf) {
        /* Skip the white-spaces */
        buf += strspn(buf, delims);
        if (buf >= endbuf) break;

        /* Check for I/O redirection specifiers */
        if (*buf == '<') {
            if (token->infile) {    // infile already exists
                fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return PARSELINE_ERROR;
            }
            parsing_state = ST_INFILE;
            buf++;
            continue;
        } else if (*buf == '>') {
            if (token->outfile) {   // outfile already exists
                fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return PARSELINE_ERROR;
            }
            parsing_state = ST_OUTFILE;
            buf++;
            continue;
        } else if (*buf == '\'' || *buf == '\"') {
            /* Detect quoted tokens */
            buf++;
            next = strchr(buf, *(buf - 1));
        } else {
            /* Find next delimiter */
            next = buf + strcspn(buf, delims);
        }

        if (next == NULL) {
            /* Returned by strchr(); this means that the closing
               quote was not found. */
            fprintf (stderr, "Error: unmatched %c.\n", *(buf - 1));
            return PARSELINE_ERROR;
        }

        /* Terminate the token */
        *next = '\0';

        /* Record the token as either the next argument or the i/o file */
        switch (parsing_state) {
        case ST_NORMAL:
            token->argv[token->argc] = buf;
            token->argc = token->argc + 1;
            break;
        case ST_INFILE:
            token->infile = buf;
            break;
        case ST_OUTFILE:
            token->outfile = buf;
            break;
        default:
            fprintf(stderr, "Error: Ambiguous I/O redirection\n");
            return PARSELINE_ERROR;
        }
        parsing_state = ST_NORMAL;

        /* Check if argv is full */
        if (token->argc >= MAXARGS - 1) break;

        buf = next + 1;
    }

    if (parsing_state != ST_NORMAL) { // buf ends with < or >
        fprintf(stderr, "Error: must provide file name for redirection\n");
        return PARSELINE_ERROR;
    }

    /* The argument list must end with a NULL pointer */
    token->argv[token->argc] = NULL;

    if (token->argc == 0) {                     /* ignore blank line */
        return PARSELINE_EMPTY;
    }

    if ((strcmp(token->argv[0], "quit")) == 0) {      /* quit command */
        token->builtin = BUILTIN_QUIT;
    } else if ((strcmp(token->argv[0], "jobs")) == 0) { /* jobs command */
        token->builtin = BUILTIN_JOBS;
    } else if ((strcmp(token->argv[0], "bg")) == 0) {   /* bg command */
        token->builtin = BUILTIN_BG;
    } else if ((strcmp(token->argv[0], "fg")) == 0) {   /* fg command */
        token->builtin = BUILTIN_FG;
    } else {
        token->builtin = BUILTIN_NONE;
    }

    // Returns 1 if job runs on background; 0 if job runs on foreground

    if (*token->argv[(token->argc)-1] == '&') {
        token->argv[--(token->argc)] = NULL;
        return PARSELINE_BG;
    } else {
        return PARSELINE_FG;
    }
}


/*****************
 * Signal handlers
 *****************/

void sigquit_handler(int sig) {
    Sio_error("Terminating after receipt of SIGQUIT signal\n");
}



/*********************
 * End signal handlers
 *********************/



/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* check_blocked - Make sure that signals are blocked */
static void check_blocked() {
    if (!check_block) {
        return;
    }
    sigset_t currmask;
    Sigprocmask(SIG_SETMASK, NULL, &currmask);
    if (!sigismember(&currmask, SIGCHLD)) {
        Sio_puts("WARNING: SIGCHLD not blocked\n");
    }
    if (!sigismember(&currmask, SIGINT)) {
        Sio_puts("WARNING: SIGINT not blocked\n");
    }
    if (!sigismember(&currmask, SIGTSTP)) {
        Sio_puts("WARNING: SIGTSTP not blocked\n");
    }
}

/* clearjob - Clear the entries in a job struct */
static void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* init_job_list - Initialize the job list */
void init_job_list() {
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        clearjob(&job_list[i]);
    }
}

/* maxjid - Returns largest allocated job ID */
static int maxjid() {
    check_blocked();
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].jid > max) {
            max = job_list[i].jid;
        }
    }
    return max;
}

/* add_job - Add a job to the job list */
bool add_job(pid_t pid, job_state state, const char *cmdline) {
    check_blocked();
    int i;
    usleep(100); // fixme move this to wrapper.c
    if (pid < 1) {
        return 0;
    }

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == 0) {
            job_list[i].pid = pid;
            job_list[i].state = state;
            job_list[i].jid = nextjid++;
            if (nextjid > MAXJOBS) {
                nextjid = 1;
            }
            strcpy(job_list[i].cmdline, cmdline);
            if (verbose) {
                printf("Added job [%d] %d %s\n",
                       job_list[i].jid,
                       job_list[i].pid,
                       job_list[i].cmdline);
            }
            return true;
        }
    }
    printf("Tried to create too many jobs\n");
    return false;
}

/* delete_job - Delete a job whose PID=pid from the job list */
bool delete_job(pid_t pid) {
    check_blocked();
    int i;

    if (pid < 1) {
        if (verbose) {
            Sio_puts("delete_job: Invalid pid\n");
        }
        return false;
    }

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) {
            clearjob(&job_list[i]);
            nextjid = maxjid() + 1;
            return true;
        }
    }
    if (verbose) {
        Sio_puts("delete_job: Invalid pid\n");
    }
    return false;
}

/* fg_pid - Return PID of current foreground job, 0 if no such job */
pid_t fg_pid() {
    check_blocked();
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].state == FG) {
            return job_list[i].pid;
        }
    }
    if (verbose) {
        Sio_puts("fg_pid: No foreground job found\n");
    }
    return 0;
}

/* find_job_with_pid  - Find a job (by PID) on the job list */
struct job_t *find_job_with_pid(pid_t pid) {
    check_blocked();
    int i;

    if (pid < 1) {
        if (verbose) {
            Sio_puts("find_job_with_pid: Invalid pid\n");
        }
        return NULL;
    }

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) {
            return &job_list[i];
        }
    }
    if (verbose) {
        Sio_puts("find_job_with_pid: Invalid pid\n");
    }

    return NULL;
}

/* find_job_with_jid  - Find a job (by JID) on the job list */
struct job_t *find_job_with_jid(int jid) {
    check_blocked();
    int i;

    if (jid < 1) {
        if (verbose) {
            Sio_puts("find_job_with_jid: Invalid jid\n");
        }
        return NULL;
    }

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].jid == jid) {
            return &job_list[i];
        }
    }
    if (verbose) {
        Sio_puts("find_job_with_jid: Invalid jid\n");
    }
    return NULL;
}

job_state get_state_of_job(struct job_t *jobp) {
    check_blocked();
    return jobp->state;
}

void set_state_of_job(struct job_t *jobp, job_state state) {
    // check here for invalid transitions.
    check_blocked();
    jobp->state = state;
}

/* find_job_with_pid - returns the pid from a job struct */
pid_t get_pid_of_job(struct job_t *jobp) {
    check_blocked();
    return jobp->pid;
}

int get_jid_of_job(struct job_t *jobp) {
    check_blocked();
    return jobp->jid;
}

char *get_cmdline_of_job(struct job_t *jobp) {
    check_blocked();
    return jobp->cmdline;
}

/* find_jid_by_pid - Map process ID to job ID */
int find_jid_by_pid(pid_t pid) {
    check_blocked();
    int i;

    if (pid < 1) {
        if (verbose) {
            Sio_puts("find_jid_by_pid: Invalid pid\n");
        }
        return 0;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) {
            return job_list[i].jid;
        }
    }
    if (verbose) {
        Sio_puts("find_jid_by_pid: Invalid pid\n");
    }
    return 0;
}

/* list_jobs - Print the job list */
void list_jobs(int output_fd) {
    check_blocked();
    int i;
    char buf[MAXLINE_TSH];

    for (i = 0; i < MAXJOBS; i++) {
        memset(buf, '\0', MAXLINE_TSH);
        if (job_list[i].pid != 0) {
            sprintf(buf, "[%d] (%d) ", job_list[i].jid, job_list[i].pid);
            if (write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(EXIT_FAILURE);
            }
            memset(buf, '\0', MAXLINE_TSH);
            switch (job_list[i].state) {
            case BG:
                sprintf(buf, "Running    ");
                break;
            case FG:
                sprintf(buf, "Foreground ");
                break;
            case ST:
                sprintf(buf, "Stopped    ");
                break;
            default:
                sprintf(buf, "list_jobs: Internal error: job[%d].state=%d ",
                        i, job_list[i].state);
            }

            if (write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(EXIT_FAILURE);
            }

            memset(buf, '\0', MAXLINE_TSH);
            sprintf(buf, "%s\n", job_list[i].cmdline);
            if (write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(EXIT_FAILURE);
}
