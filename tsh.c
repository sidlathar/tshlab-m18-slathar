/* Siddhanth Lathar slathar */
/*
 * tsh - A tiny shell program with job control
 * <The line above is not a sufficient documentation.
 *  You will need to write your program documentation.
 *  Follow the 15-213/18-213/15-513 style guide at
 *  http://www.cs.cmu.edu/~213/codeStyle.html.>
 */

#include "tsh_helper.h"
#if 0
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include "csapp.h"
#endif


/*
 * If DEBUG is defined, enable contracts and printing on dbg_printf.
 */
#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/* Function prototypes */
void eval(const char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);

/*
 * Takes command line arguments and does the following:
 * redirects stderr to stdout, 
 * handles verbose outputs and promts, 
 * installs signal handlers, 
 * initalizes job list,
 * and then runs shells read/eval loop.
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE_TSH];  // Cmdline for fgets
    bool emit_prompt = true;    // Emit prompt (default)

    // Redirect stderr to stdout (so that driver will get all output
    // on the pipe connected to stdout)
    Dup2(STDOUT_FILENO, STDERR_FILENO);

    // Parse the command line
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':                   // Prints help message
            usage();
            break;
        case 'v':                   // Emits additional diagnostic info
            verbose = true;
            break;
        case 'p':                   // Disables prompt printing
            emit_prompt = false;
            break;
        default:
            usage();
        }
    }

    // Create environment variable
    if (putenv("MY_ENV=42") < 0) {
        perror("putenv");
        exit(1);
    }


    // Install the signal handlers
    Signal(SIGINT,  sigint_handler);   // Handles ctrl-c
    Signal(SIGTSTP, sigtstp_handler);  // Handles ctrl-z
    Signal(SIGCHLD, sigchld_handler);  // Handles terminated or stopped child

    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    Signal(SIGQUIT, sigquit_handler);

    // Initialize the job list
    init_job_list();

    // Execute the shell's read/eval loop
    while (true) {
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }

        if ((fgets(cmdline, MAXLINE_TSH, stdin) == NULL) && ferror(stdin)) {
            app_error("fgets error");
        }

        if (feof(stdin)) {
            // End of file (ctrl-d)
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            return 0;
        }

        // Remove the trailing newline
        cmdline[strlen(cmdline)-1] = '\0';

        // Evaluate the command line
        eval(cmdline);

        fflush(stdout);
    }

    return -1; // control never reaches here
}


/* Handy guide for eval:
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg),
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.
 * Note: each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */

/*
 * Handles whats to be done when. It takes cmdline from main() and divides
 * the input into BUILTIN commands or FG, BG and handles them seperatley 
 * displaying process info differently in each case & handles I/O redirection.
 */
void eval(const char *cmdline) {
    parseline_return parse_result;
    struct cmdline_tokens token;

    /* Parse command line */
    parse_result = parseline(cmdline, &token); 

    /* Make Blocking List from empty set*/
    sigset_t proc_mask, suspend_mask, temp;
    pid_t pid;
    int jid;

    /* Handling I/O redirection */ 
    int in_file = 0;
    int out_file = 0;

    if(token.infile != NULL)
    {
        in_file = open(token.infile, O_RDONLY, 0);
        dup2(in_file, STDIN_FILENO);
    }
     if(token.outfile != NULL)
    {
        out_file = open(token.outfile, O_RDWR | O_CREAT, 0);
        dup2(out_file, STDOUT_FILENO);
    }

    /* Check for valid parse */
    if (parse_result == PARSELINE_ERROR || parse_result == PARSELINE_EMPTY) 
    {
        return;
    }
    /* Not a builtin command */
    else if(token.builtin == BUILTIN_NONE)
    {
        /* Add signals to block to the mask set */
        sigemptyset(&proc_mask);
        sigaddset(&proc_mask, SIGCHLD);
        sigaddset(&proc_mask, SIGINT);
        sigaddset(&proc_mask, SIGTSTP);

        /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking set 
         * before forking
         */
        sigprocmask(SIG_BLOCK, &proc_mask, &temp);

        /* forking */
        pid = fork();

        /* parent process received child's pid */
        if(pid > 0)
        {
            if(parse_result == PARSELINE_BG)
            {
                add_job(pid, BG, cmdline);
                jid = find_jid_by_pid(pid);

                /* output */
                sio_printf("[%d] (%d) %s\n", jid, pid, cmdline);
            }
            else /* FG process, wait to finish */
            {
                add_job(pid, FG, cmdline);

                /* empty mask for sigsuspend */
                sigemptyset(&suspend_mask);

                while(fg_pid() != 0)
                {
                    sigsuspend(&suspend_mask);
                }
            }

            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);
        }
        /* successful fork */
        else if(pid == 0)
        {
            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);

            /* put child process in new process group */
            Setpgid(0,0);

            /* run */
            if(execve(token.argv[0], &token.argv[0], environ) < 0)
            {
                sio_printf("%s: Command not found. \n", token.argv[0]);
                return;
            }
            return;
        }
    }
    /* Built in command */
    else
    {
        /* variables for buultin job */
        struct job_t *built_in_job;
        pid_t b_pid;
        int b_jid;

        /* Add signals to block to the mask set */
        sigemptyset(&proc_mask);
        sigaddset(&proc_mask, SIGCHLD);
        sigaddset(&proc_mask, SIGINT);
        sigaddset(&proc_mask, SIGTSTP);

        /* BULTIN QUIT*/
        if(token.builtin == BUILTIN_QUIT)
        {
            exit(0);
        }
        /* BULTIN JOBS*/
        else if(token.builtin == BUILTIN_JOBS)
        {
            /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking set*/
            sigprocmask(SIG_BLOCK, &proc_mask, &temp);

            list_jobs(STDOUT_FILENO);

            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);
        }
        /* BULTIN BG */
        else if(token.builtin == BUILTIN_BG)
        {
            /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking */
            sigprocmask(SIG_BLOCK, &proc_mask, &temp);

            /* JID is supplied. It starts with '%' */
            if(token.argv[1][0] == '%')
            {
                /* convert argument to int */
                b_jid = atoi(&token.argv[1][1]);

                built_in_job = find_job_with_jid(b_jid);
                b_pid = get_pid_of_job(built_in_job);
            }
            /* PID is supplied */
            else 
            {
                /* convert argument to int */
                b_pid = atoi(token.argv[1]);

                b_jid = find_jid_by_pid(b_pid);
                built_in_job = find_job_with_pid(b_pid);
            }
           
            kill(-b_pid, SIGCONT);
            set_state_of_job(built_in_job, BG);

            /* output */
            sio_printf("[%d] (%d) %s\n", b_jid, b_pid, 
                                            get_cmdline_of_job(built_in_job));

            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);
        }
        else if(token.builtin == BUILTIN_FG)
        {
            /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking set */
            sigprocmask(SIG_BLOCK, &proc_mask, &temp);

            /* JID is supplied. It starts with '%' */
            if(token.argv[1][0] == '%')
            {
                /* convert argument to int */
                b_jid = atoi(&token.argv[1][1]);

                built_in_job = find_job_with_jid(b_jid);
                b_pid = get_pid_of_job(built_in_job);
            }
            else
            {
                /* convert argument to int */
                b_pid = atoi(token.argv[1]);

                built_in_job = find_job_with_pid(b_pid);
                b_jid = find_jid_by_pid(b_pid);
            }
            
            kill(-b_pid, SIGCONT);
            set_state_of_job(built_in_job, FG);

            /* empty mask for sugsuspend, wait for it to finish */
            sigemptyset(&suspend_mask);

            while(fg_pid() != 0)
            {
                Sigsuspend(&suspend_mask);
            }
            
            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);
        }
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/*
 * Handles SIGCHLD signal. Blocks SIGINT, SIGTSTP and outputs information
 * about termination or stoppage of a process based on status from waitpid
 */
void sigchld_handler(int sig) 
{
    pid_t pid;
    int status, jid;
    struct job_t *job;

    /* add SIGINT, SIGSTP in mask set to block  */
    sigset_t proc_mask, temp;
    sigaddset(&proc_mask, SIGINT);
    sigaddset(&proc_mask, SIGTSTP);

    /* BLOCK {SIGINT, SIGTSTP} */ 
    sigprocmask(SIG_BLOCK, &proc_mask, &temp);

    while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        /* Child prcess terminated */
        if(WIFSIGNALED(status))
        {
            jid = find_jid_by_pid(pid);
            /* output */
            sio_printf("Job [%d] (%d) terminated by signal %d\n", jid, pid, 
                WTERMSIG(status));

            delete_job(pid);
        }
        /* Child is stopped */
        else if(WIFSTOPPED(status))
        {
            jid = find_jid_by_pid(pid);
            /* output */
            sio_printf("Job [%d] (%d) stopped by signal %d\n", jid, pid, 
                WSTOPSIG(status));

            job = find_job_with_pid(pid);
            set_state_of_job(job, ST);
        }
        else
        {
            delete_job(pid);
        }
    }
    /* UNBLOCK {SIGINT, SIGTSTP} */
    sigprocmask(SIG_SETMASK, &temp, NULL);
    return;
}

/*
 * Handles SIGINT signal by blocking {SIGCHLD, SIGINT, SIGTSTP} first and 
 * sending SIGINT signal to the process
 */
void sigint_handler(int sig) 
{
    pid_t pid;

    /* add SIGINT, SIGSTP, SIGCHLD in mask set to block */
    sigset_t proc_mask, temp;
    sigaddset(&proc_mask, SIGCHLD);
    sigaddset(&proc_mask, SIGINT);
    sigaddset(&proc_mask, SIGTSTP);

    /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking set */
    sigprocmask(SIG_BLOCK, &proc_mask, &temp);

    pid = fg_pid();
    if(pid != 0)
    {
        kill(-pid, sig);
    }

    /* Unblock {SIGCHLD, SIGINT, SIGTSTP} */
    sigprocmask(SIG_SETMASK, &temp, NULL);

    return;
}

/*
 * Handles SIGINT signal by blocking {SIGCHLD, SIGINT, SIGTSTP} first and 
 * sending SIGTSTP signal to the process
 */
void sigtstp_handler(int sig) {
    pid_t pid;

    /* add SIGINT, SIGSTP, SIGCHLD in mask set to block */
    sigset_t proc_mask, temp;
    sigaddset(&proc_mask, SIGCHLD);
    sigaddset(&proc_mask, SIGINT);
    sigaddset(&proc_mask, SIGTSTP);

    /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking set */
    sigprocmask(SIG_BLOCK, &proc_mask, &temp);

    pid = fg_pid();
    if(pid != 0)
    {
        kill(-pid, sig);
    }

    /* Unblock {SIGCHLD, SIGINT, SIGTSTP} */
    sigprocmask(SIG_SETMASK, &temp, NULL);
    
    return;
}

