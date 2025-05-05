// signal_demo_q1.c
// Q1: fork 4 children, parent ignores all eight test‐signals until after fork,
// children catch 4 signals each (block 2 permanently, block the other 2 during handler),
// then parent restores default handlers and sleeps 10 s to catch signals.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM_CHILD 4
pid_t child_pids[NUM_CHILD];

/* The eight signals we care about */
static int SIGNAL_LIST[] = {
    SIGINT, SIGABRT, SIGILL, SIGCHLD,
    SIGSEGV, SIGFPE, SIGHUP, SIGTSTP
};

/* For each child (index 0..3), the four signals it “catches.” */
static int catch_signals[NUM_CHILD][4] = {
    { SIGINT,  SIGABRT, SIGILL,  SIGHUP  },  /* child 0 */
    { SIGCHLD, SIGSEGV, SIGFPE,  SIGTSTP },  /* child 1 */
    { SIGINT,  SIGSEGV, SIGABRT, SIGFPE  },  /* child 2 */
    { SIGILL,  SIGHUP,  SIGCHLD, SIGTSTP }   /* child 3 */
};

/* SA_SIGINFO handler: print receiver PID, signal, and sender PID */
static void handler(int sig, siginfo_t *info, void *ucontext) {
    (void)ucontext;
    pid_t me = getpid();
    printf("  [PID %d] caught %d (%s)", me, sig, strsignal(sig));
    if (info && info->si_pid != 0)
        printf(" from PID %d", info->si_pid);
    putchar('\n');
}

/* Child’s main: set up ignores, masks, handlers, then do the sum loop */

static void child_main(int idx) {
    pid_t me = getpid();
 
    // Ignore non-catch signals
    for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++) {
        int s = SIGNAL_LIST[i], want = 0;
        for (int j = 0; j < 4; j++)
            if (catch_signals[idx][j] == s)
                want = 1;
        if (!want) {
            struct sigaction sa = { .sa_handler = SIG_IGN };
            sigemptyset(&sa.sa_mask);
            sigaction(s, &sa, NULL);
        }
    }
 
    // Block first two assigned signals
    sigset_t perm;
    sigemptyset(&perm);
    sigaddset(&perm, catch_signals[idx][0]);
    sigaddset(&perm, catch_signals[idx][1]);
    sigprocmask(SIG_BLOCK, &perm, NULL);
 
    // Install SA_SIGINFO handler
    int dyn1 = catch_signals[idx][2];
    int dyn2 = catch_signals[idx][3];
    for (int j = 0; j < 4; j++) {
        int s = catch_signals[idx][j];
        struct sigaction sa = {0};
        sa.sa_sigaction = handler;
        sa.sa_flags     = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaddset(&sa.sa_mask, dyn1);
        sigaddset(&sa.sa_mask, dyn2);
        sigaction(s, &sa, NULL);
    }
 
    sleep(3);  // Wait to ensure all children are ready
 
    // Determine target child (next one in list)
    pid_t target_pid = -1;
    extern pid_t child_pids[NUM_CHILD];  // access from main
    int target_idx = (idx + 1) % NUM_CHILD;
 
    target_pid = child_pids[target_idx];
    int sig = catch_signals[idx][2];  // pick a dynamic signal to send
 
    printf("Child %d [PID %d]: sending signal %d (%s) to Child %d [PID %d]\n",
           idx, me, sig, strsignal(sig), target_idx, target_pid);
    kill(target_pid, sig);
    sleep(1);
    kill(target_pid, sig);  // send again
 
    // Also send same signal to parent
    pid_t parent_pid = getppid();
    printf("Child %d [PID %d]: sending signal %d (%s) to Parent [PID %d]\n",
           idx, me, sig, strsignal(sig), parent_pid);
    kill(parent_pid, sig);
    sleep(1);
    kill(parent_pid, sig);  // send again
 
    // Compute sum
    int bound = 10 * (idx + 1);
    long sum = 0;
    for (int i = 0; i <= bound; i++) {
        sum += i;
        printf("Child %d [PID %d]: iteration %2d/%2d → sum=%ld\n",
               idx, me, i, bound, sum);
        sleep(1);
    }
 
    printf("Child %d [PID %d]: done, exiting.\n", idx, me);
    exit(EXIT_SUCCESS);
 } 


int main(void) {
    pid_t child_pids[NUM_CHILD];

    /* ===== PARENT SETUP =====
       Ignore the eight test‐signals *and* SIGPIPE (to survive broken‐pipe) */
    struct sigaction sa_ign = { .sa_handler = SIG_IGN };
    sigemptyset(&sa_ign.sa_mask);
    for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++)
        sigaction(SIGNAL_LIST[i], &sa_ign, NULL);
    sigaction(SIGPIPE, &sa_ign, NULL);

    /* Fork four children */
    for (int i = 0; i < NUM_CHILD; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }
        if (pid == 0) {
            /* in child */
            child_main(i);
            /* never returns */
        }
        /* in parent */
        child_pids[i] = pid;
        printf("Parent: forked child %d → PID %d\n", i, pid);
    }

    /* Wait for all children to exit */
    for (int i = 0; i < NUM_CHILD; i++) {
        int status;
        pid_t w = waitpid(child_pids[i], &status, 0);
        if (w < 0) {
            perror("waitpid");
            continue;
        }
        if (WIFEXITED(status)) {
            printf("Parent: child PID %d exited [%d]\n",
                   w, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Parent: child PID %d died on signal %d (%s)\n",
                   w, WTERMSIG(status), strsignal(WTERMSIG(status)));
        }
    }

    /* Restore defaults for the eight test‐signals */
    sa_ign.sa_handler = SIG_DFL;
    for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++)
        sigaction(SIGNAL_LIST[i], &sa_ign, NULL);

    /* Now sleep 10 s so the parent can catch random signals */
    printf("Parent: defaults restored, sleeping 10 seconds to catch signals...\n");
    for (int t = 10; t > 0; t--) {
        printf(" Parent sleeping: %2d s remaining\n", t);
        sleep(1);
    }
    printf("Parent: done, exiting.\n");
    return 0;
}
