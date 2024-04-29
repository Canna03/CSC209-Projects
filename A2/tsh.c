/* 
 * tsh - A tiny shell program with job control
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>


//#include <sigset_t.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* Per-job data */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, FG, BG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

volatile sig_atomic_t ready; /* Is the newest child in its own process group? */

/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);
void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);
void sigusr1_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int freejid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(STDOUT_FILENO, STDERR_FILENO);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != -1) {
        switch (c) {
            case 'h':             /* print help message */
                usage();
                break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
                break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
                break;
            default:
                usage();
        }
    }

    /* Install the signal handlers */

    Signal(SIGUSR1, sigusr1_handler); /* Child is ready */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) {
    char *arguments[MAXARGS + 1]; // Space for MAXARGS arguments and NULL
    int argc = parseline(cmdline, arguments);
    arguments[argc] = NULL;
    pid_t child_pid;

    if (argc == 0) {
        return; // Empty command line
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP);

    int bg = (strcmp(arguments[argc - 1], "&") == 0) ? 1 : 0; // boolean indicating background process or not

    if(bg) {arguments[argc - 1] = NULL;} // arguments[argc - 1] is &, need to remove that.

    // arguments should be of form: {"executable", "-d", "argument"}
    // builtin commands are to be executed immediately.
    if(builtin_cmd(arguments)) {return;} //command is built in, return to end the function.

    //At this stage, the executable should not be a built in command.
    
    if (sigprocmask(SIG_BLOCK, &mask, NULL)) { //sigprocmask returns 0 on success
        unix_error("sigprocmask failed");
        exit(EXIT_FAILURE);
    }
    /*
    // > < | Implementation
    // Check for input/output redirection and pipe
    int input_redirection = 0;
    int output_redirection = 0;
    int pipe_pos = -1;
    for (int i = 0; arguments[i] != NULL; i++) {
        if (strcmp(arguments[i], "<") == 0) {
            input_redirection = i;  // Set input redirection flag
        } else if (strcmp(arguments[i], ">") == 0) {
            output_redirection = i;  // Set output redirection flag
        } else if (strcmp(arguments[i], "|") == 0) {
            pipe_pos = i;  // Record the position of pipe
        }
    }
    // Handle input/output redirection and pipe
    if (input_redirection || output_redirection || pipe_pos != -1) {
        // Input Redirection
        if (input_redirection) {
            arguments[input_redirection] = NULL;  // Null terminate before input redirection symbol
            int infd = open(arguments[input_redirection + 1], O_RDONLY);
            if (infd < 0) {
                unix_error("Input file open error");
            }
            dup2(infd, STDIN_FILENO);
            close(infd);
        }
        // Output Redirection
        // Handle output redirection
        if (output_redirection) {
            // Print debug information
            printf("Output redirection detected at index %d\n", output_redirection);
            printf("Output file: %s\n", arguments[output_redirection + 1]);

            // Close stdout
            close(STDOUT_FILENO);

            // Open output file for writing
            int outfd = open(arguments[output_redirection + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (outfd < 0) {
                unix_error("Output file open error");
            }

            // Duplicate file descriptor to stdout
            if (dup2(outfd, STDOUT_FILENO) < 0) {
                unix_error("Dup2 error");
            }

            // Close output file descriptor
            close(outfd);

            // Nullify the output redirection symbol and the filename
            arguments[output_redirection] = NULL;
            arguments[output_redirection + 1] = NULL;
        }
        // Piping
        if (pipe_pos != -1) {
            arguments[pipe_pos] = NULL;  // Null terminate before pipe symbol
            int pipefd[2];
            if (pipe(pipefd) < 0) {
                unix_error("Pipe error");
            }
            if ((child_pid = fork()) == 0) {  // Child process for pipe
                close(pipefd[0]);  // Close read end
                dup2(pipefd[1], STDOUT_FILENO);  // Redirect stdout to pipe
                close(pipefd[1]);
                if (execvp(arguments[0], arguments) < 0) {
                    unix_error("Exec error");
                }
            }
            if (child_pid < 0) {
                unix_error("Fork error");
            }
            // Parent process continues
            close(pipefd[1]);  // Close write end
            dup2(pipefd[0], STDIN_FILENO);  // Redirect stdin to pipe
            close(pipefd[0]);
        }
    }*/

    child_pid = fork();

    if (child_pid == 0) {
        if (setpgid(0, 0)) {
            unix_error("setpgid error");
            exit(EXIT_FAILURE);
        }
        if(execve(arguments[0], arguments, environ) == -1 ) {
            printf("%s: Command not found\n", arguments[0]);
            exit(0);
        }
        if(sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
            unix_error("sigprocmask failed");
            exit(EXIT_FAILURE);
        }
    }
    else if (child_pid > 0) {
        // parent
        if(bg) { // add to jobs accordingly checking if bg job or fg job
            if(addjob(jobs, child_pid, BG, cmdline) == 0) {
                app_error("Failed to add job to job list");
                exit(EXIT_FAILURE);
            }
            if(sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
                unix_error("sigprocmask failed");
                exit(EXIT_FAILURE);
            }
            struct job_t *job = getjobpid(jobs, child_pid);
            printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
        }
        else {
            if(addjob(jobs, child_pid, FG, cmdline) == 0) {
                //addjob failed
                app_error("Failed to add job to job list");
                exit(EXIT_FAILURE);
            }
            if(sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
                unix_error("sigprocmask failed");
                exit(EXIT_FAILURE);
            }
            waitfg(child_pid);
        }
        
        //exit(EXIT_SUCCESS);
    }
    else {
        unix_error("fork");
    }

    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return number of arguments parsed.
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to space or quote delimiters */
    int argc;                   /* number of args */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;
    
    return argc;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) {
    if (strcmp(argv[0], "quit") == 0 ) {
        exit(0);
    }
    else if(strcmp(argv[0], "jobs") == 0) {
        listjobs(jobs);
        return 1;
    }
    else if(strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "fg") == 0) {
        
        do_bgfg(argv);
        return 1;
    }
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {

    // Check if the second argument exists:
    if (argv[1] == NULL) {
        printf("%s command requires PID or %%jid argument\n", argv[0]);
        return;
    }

    // Second argument exists: Check if the first character is % or not.
    int check_jid = (argv[1][0] == '%') ? 1 : 0; //jid start with %, pid dont. Check if the first character of the number is % or not.
    struct job_t *job;

    if (check_jid) {
        // Second element's first character is %
        // JID, second digit of argv[1] should be number
        for (int i = 1; i < strlen(argv[1]); i++) {
            if (!isdigit(argv[1][i])) {
                printf("%s: argument must be a PID or %%jid\n ", argv[0]); //%% is the way to print % in printf
                return;
                // Invalid JID
            }
        }
        job = getjobjid(jobs, atoi((&argv[1][1])));
        // JID is valid, check if job with jid exists
        if (job == NULL) {
            // job doesn't exist
            printf("%%%d: No such job\n", atoi((&argv[1][1])));
            return;
        }
        // job does exist. Found by JID.
    }
    else {
        // Second element's first character is not %. Should be pid.
        for (int i = 0; i < strlen(argv[1]); i++) {
            if (!isdigit(argv[1][i])) {    //segfault in isdigit
                printf("%s: argument must be a PID or %%jid\n ", argv[0]);
                return;
            }
        }
        job = getjobpid(jobs, atoi(argv[1]));
        if (job == NULL) {
            printf("(%d): No such process\n", atoi(argv[1]));
            return;
        }
    }

    if(kill(-(job->pid), SIGCONT) < 0) {unix_error("SIGCONT failed");} // send signal to continue job (SIGSTP sent when?) to foreground.
    // -job_pid sends SIGCONT to all processes of the same process group id.

    if (strcmp(argv[0], "bg") == 0) {
        // bg
        // Send foreground process to background.
        
        //Changing job state
        job->state = BG;
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    }
    else {
        // fg
        job->state = FG; // change state of job.
        if(fgpid(jobs) != 0) {  
            waitfg(fgpid(jobs)); // wait till foreground is available.
        }
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
    while (pid == fgpid(jobs)) {
        sleep(1);
    }
}


/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) {
    // child terminated
    // if the child stopped, change job state.
    pid_t pid;
    int status;
    
    //printf("entered sigchld_handler\n");
    while ((pid=waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) { // WNOHANG | WUNTRACED
        // WUNTRACED: Child may have stopped instead of exited
        // WNOHANG: Return immediately if no child has exited.
        
        struct job_t *job = getjobpid(jobs, pid); //Possible for this function to fail?
            
        if(WIFSIGNALED(status)) { // Che cks if child was terminated by an uncaught signal
            printf("Job [%d] (%d) Process was terminated by signal %d\n", job->jid, job->pid, sig);
            deletejob(jobs, job->pid);
        }
        else if (WIFEXITED(status)) {
            // child was terminated normally            
            deletejob(jobs, job->pid);
        }
    }
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenever the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {
    pid_t pid = fgpid(jobs);
    if (pid != 0) { //pid is 0 if failed
        if(kill(-pid, sig) == -1) {unix_error("SIGINT kill failed");}
        struct job_t *job = getjobpid(jobs, pid);
        printf("Job [%d] (%d) terminated by signal %d\n", job->jid, job->pid, sig);
        deletejob(jobs, job->pid);
    }
    
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {

    pid_t pid = fgpid(jobs);
    if (pid != 0) { //pid is 0 if failed
        if(kill(-pid, sig) == -1) {unix_error("SIGTSTP kill failed");}
        struct job_t *job = getjobpid(jobs, pid);
        job->state = ST;
        printf("Job [%d] (%d) stopped by signal %d\n", job->jid, job->pid, sig);
    }
    
    
    
    return;
}

/*
 * sigusr1_handler - child is ready
 */
void sigusr1_handler(int sig) {
    ready = 1;
}


/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* freejid - Returns smallest free job ID */
int freejid(struct job_t *jobs) {
    int i;
    int taken[MAXJOBS + 1] = {0};
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid != 0) 
        taken[jobs[i].jid] = 1;
    for (i = 1; i <= MAXJOBS; i++)
        if (!taken[i])
            return i;
    return 0;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;
    
    if (pid < 1)
        return 0;
    int free = freejid(jobs);
    if (!free) {
        printf("Tried to create too many jobs\n");
        return 0;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = free;
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    return 0; /*suppress compiler warning*/
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
    }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG: 
                    printf("Running ");
                    break;
                case FG: 
                    printf("Foreground ");
                    break;
                case ST: 
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ", 
                       i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
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
 * usage - print a help message and terminate
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
void print_joblist(struct job_t *jobs) {
    int i;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("Job [%d]:\n", i+1);
            printf("\tPID: %d\n", jobs[i].pid);
            printf("\tState: %s\n", (jobs[i].state == BG) ? "BG" : "FG");
            printf("\tCommand: %s\n", jobs[i].cmdline);
        }
    }
}