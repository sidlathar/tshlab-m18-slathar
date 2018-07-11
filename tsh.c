
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
 * <Write main's function header documentation. What does main do?>
 * "Each function should be prefaced with a comment describing the purpose
 *  of the function (in a sentence or two), the function's arguments and
 *  return value, any error cases that are relevant to the caller,
 *  any pertinent side effects, and any assumptions that the function makes."
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
 * <What does eval do?>
 */
void eval(const char *cmdline) {
    parseline_return parse_result;
    struct cmdline_tokens token;

    /* Parse command line */
    parse_result = parseline(cmdline, &token); 

    /* Make Blocking List from empty list*/
    sigset_t proc_mask, suspend_mask, temp;
    pid_t pid;

    /* Check for valid parse */
    if (parse_result == PARSELINE_ERROR || parse_result == PARSELINE_EMPTY) 
    {
        return;
    }
    /* Not a builtin command */
    else if(token.builtin == BUILTIN_NONE)
    {
        sigemptyset(&proc_mask);
        sigaddset(&proc_mask, SIGCHLD);
        sigaddset(&proc_mask, SIGINT);
        sigaddset(&proc_mask, SIGTSTP);

        /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking list before forking */
        sigprocmask(SIG_BLOCK, &proc_mask, &temp);

        /* forking */
        pid = fork();

        /* parent process received child's pid */
        if(pid > 0)
        {
            if(parse_result == PARSELINE_BG)
            {
                printf("dfeqv\n");
                add_job(pid, BG, cmdline);
                int jid = find_jid_by_pid(pid);
                printf("[%d] (%d) %s\n", jid, pid, cmdline);
            }
            else
            {
                printf("vvaevea\n");
                add_job(pid, FG, cmdline);
                sigemptyset(&suspend_mask);

                while(fg_pid() != 0)
                {
                    sigsuspend(&suspend_mask);
                }
                int jid = find_jid_by_pid(pid);
                printf("[%d] (%d) %s\n", jid, pid, cmdline);
            }

            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);
        }
        /* successful fork */
        else if(pid == 0)
        {
            printf("vvaeveffea\n");
            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);

            /* put child process in new process group */
            Setpgid(0,0);

            /* run */
            if(execve(token.argv[0], &token.argv[0], environ) < 0)
            {
                printf("%s: Command not found. \n", token.argv[0]);
                return;
            }
            return;
        }
    }
    /* Built in command */
    else
    {
        struct job_t *built_in_job;
        pid_t b_pid;
        int b_jid;
        //job_state st;

        if(token.builtin == BUILTIN_QUIT)
        {
            exit(0);
        }
        else if(token.builtin == BUILTIN_JOBS)
        {
            /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking list before forking */
            sigprocmask(SIG_BLOCK, &proc_mask, &temp);

            list_jobs(STDOUT_FILENO);

            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);
        }
        /* check for valid pid or jid */
        else if(token.builtin == BUILTIN_BG)
        {
            /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking list before forking */
            sigprocmask(SIG_BLOCK, &proc_mask, &temp);

            /* JID start with '%' */
            if(token.argv[1][0] == '%')
            {
                /* convert to int */
                b_jid = atoi(&token.argv[1][1]);
                built_in_job = find_job_with_jid(b_jid);
            
                b_pid = get_pid_of_job(built_in_job);
            }
            else
            {
                b_pid = atoi(token.argv[1]);
                b_jid = find_jid_by_pid(b_pid);
                built_in_job = find_job_with_pid(b_pid);
            }

            kill(-b_pid, SIGCONT);
            printf("[%d] (%d) %s\n", b_jid, b_pid, get_cmdline_of_job(built_in_job));
            set_state_of_job(built_in_job, BG);

            /* unblock {SIGCHLD, SIGINT, SIGTSTP} signals*/
            sigprocmask(SIG_SETMASK, &temp, NULL);
        }
        else if(token.builtin == BUILTIN_FG)
        {
            /* Block {SIGCHLD, SIGINT, SIGTSTP} with empty old blocking list before forking */
            sigprocmask(SIG_BLOCK, &proc_mask, &temp);

            /* JID start with '%' */
            if(token.argv[1][0] == '%')
            {
                /* convert to int */
                b_jid = atoi(&token.argv[1][1]);
                built_in_job = find_job_with_jid(b_jid);

                b_pid = get_pid_of_job(built_in_job);
            }
            else
            {
                b_pid = atoi(token.argv[1]);
                built_in_job = find_job_with_pid(b_pid);
                b_jid = find_jid_by_pid(b_pid);
            }
            
            kill(-b_pid, SIGCONT);
            printf("[%d] (%d) %s\n", b_jid, b_pid, get_cmdline_of_job(built_in_job));
            set_state_of_job(built_in_job, FG);
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
 * <What does sigchld_handler do?>
 */
void sigchld_handler(int sig) {
    return;
}

/*
 * <What does sigint_handler do?>
 */
void sigint_handler(int sig) {
    return;
}

/*
 * <What does sigtstp_handler do?>
 */
void sigtstp_handler(int sig) {
    return;
}

