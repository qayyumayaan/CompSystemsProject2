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
    /* 1) Ignore any of the eight signals NOT in this child’s catch-list */
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

    /* 2) Permanently block the first two of its assigned signals */
    sigset_t perm;
    sigemptyset(&perm);
    sigaddset(&perm, catch_signals[idx][0]);
    sigaddset(&perm, catch_signals[idx][1]);
    if (sigprocmask(SIG_BLOCK, &perm, NULL) < 0) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    /* 3) Install a SA_SIGINFO handler for its four signals,
          blocking the other two during handler execution */
    int dyn1 = catch_signals[idx][2];
    int dyn2 = catch_signals[idx][3];
    for (int j = 0; j < 4; j++) {
        int s = catch_signals[idx][j];
        struct sigaction sa = {0};
        sa.sa_sigaction = handler;
        sa.sa_flags     = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        /* block the other two assigned signals while in handler */
        sigaddset(&sa.sa_mask, dyn1);
        sigaddset(&sa.sa_mask, dyn2);
        if (sigaction(s, &sa, NULL) < 0) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
    }

    /* 4) Compute sum from 0..10*(idx+1), sleeping 1 s each iteration */
    int bound = 10 * (idx + 1);
    long sum = 0;
    for (int i = 0; i <= bound; i++) {
        sum += i;
        printf("Child %d [PID %d]: iteration %2d/%2d → sum=%ld\n",
               idx, getpid(), i, bound, sum);
        sleep(1);
    }

    printf("Child %d [PID %d]: done, exiting.\n", idx, getpid());
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
