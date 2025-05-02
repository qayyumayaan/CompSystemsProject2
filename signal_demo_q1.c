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
    SIGINT,  SIGABRT, SIGILL,  SIGCHLD,
    SIGSEGV, SIGFPE,  SIGHUP,  SIGTSTP
};

/* For each child (index 0..3), the four signals it “catches.” */
static int catch_signals[NUM_CHILD][4] = {
    { SIGINT,  SIGABRT, SIGILL,  SIGHUP  },  /* child 0 */
    { SIGCHLD, SIGSEGV, SIGFPE,  SIGTSTP },  /* child 1 */
    { SIGINT,  SIGSEGV, SIGABRT, SIGFPE  },  /* child 2 */
    { SIGILL,  SIGHUP,  SIGCHLD, SIGTSTP }   /* child 3 */
};

/* A SA_SIGINFO‐style handler that prints the signal and both
   its own PID and the sender’s PID (if any). */
static void handler(int sig, siginfo_t *info, void *ucontext) {
    pid_t me = getpid();
    printf("  [PID %d] caught signal %d (%s)",
           me, sig, strsignal(sig));
    if (info && info->si_pid != 0)
        printf(" from PID %d", info->si_pid);
    putchar('\n');
}

/* Child’s main routine: idx in [0..NUM_CHILD-1] */
static void child_main(int idx) {
    int i, j;
    /* 1) Ignore all signals *not* in catch_signals[idx][] */
    for (i = 0; i < (int)(sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST)); i++) {
        int s = SIGNAL_LIST[i];
        int want = 0;
        for (j = 0; j < 4; j++)
            if (catch_signals[idx][j] == s)
                want = 1;
        if (!want)
            signal(s, SIG_IGN);
    }

    /* 2) Permanently block the first two of its assigned signals */
    sigset_t perm_mask;
    sigemptyset(&perm_mask);
    sigaddset(&perm_mask, catch_signals[idx][0]);
    sigaddset(&perm_mask, catch_signals[idx][1]);
    if (sigprocmask(SIG_BLOCK, &perm_mask, NULL) < 0) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    /* 3) Install a handler for all four assigned signals,
          blocking the other two *during* handler execution */
    int dyn1 = catch_signals[idx][2];
    int dyn2 = catch_signals[idx][3];
    for (j = 0; j < 4; j++) {
        int s = catch_signals[idx][j];
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = handler;
        sa.sa_flags     = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        /* block the other two assigned signals while inside handler */
        sigaddset(&sa.sa_mask, dyn1);
        sigaddset(&sa.sa_mask, dyn2);
        if (sigaction(s, &sa, NULL) < 0) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
    }

    /* 4) Do the “sum 0..10*(idx+1)” loop, sleeping 1 s each turn */
    int bound = 10 * (idx + 1);
    long sum = 0;
    for (i = 0; i <= bound; i++) {
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
    int i;

    /* Parent ignores all eight signals while spawning children */
    for (i = 0; i < (int)(sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST)); i++)
        signal(SIGNAL_LIST[i], SIG_IGN);

    /* Fork four children */
    for (i = 0; i < NUM_CHILD; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
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

    /* Wait for all children */
    for (i = 0; i < NUM_CHILD; i++) {
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

    /* Restore default signal handlers */
    for (i = 0; i < (int)(sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST)); i++)
        signal(SIGNAL_LIST[i], SIG_DFL);

    printf("Parent: defaults restored, sleeping 10 seconds to catch signals...\n");
    for (i = 10; i > 0; i--) {
        printf("Parent sleeping: %2d s remaining\n", i);
        sleep(1);
    }
    printf("Parent: done, exiting.\n");
    return 0;
}
