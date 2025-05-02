
// signal_demo_q3.c
// Q3: half of the children block {SIGINT, SIGQUIT, SIGTSTP},
// the other half block the *rest* of the eight signals.
// Parent blocks {SIGINT, SIGQUIT, SIGTSTP} pre‐fork, installs handlers,
// prints a READY marker, sleeps 5 s to let you send it signals,
// then forks, prints child PIDs, waits, and finally dumps its pending set.

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

/* Generic SA_SIGINFO handler */
static void handler(int sig, siginfo_t *info, void *ucontext) {
    pid_t me = getpid();
    printf("  [PID %d] caught signal %d (%s)%s\n",
           me, sig, strsignal(sig),
           (info && info->si_pid != 0)
             ? ({ static char buf[32];
                  snprintf(buf, sizeof(buf), " from PID %d", info->si_pid);
                  buf; })
             : "");
}

/* Print the pending set (only for the eight signals) */
static void print_pending(const char *who) {
    sigset_t pend;
    if (sigpending(&pend) < 0) {
        perror("sigpending");
        exit(EXIT_FAILURE);
    }
    printf("%s pending:", who);
    for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++) {
        int s = SIGNAL_LIST[i];
        if (sigismember(&pend, s))
            printf(" %s", strsignal(s));
    }
    printf("\n");
}

/* Each child sets up its permanent mask and installs handlers
   for everything *not* blocked. */
static void child_process(int idx) {
    /* 1) Build the permanent block mask */
    sigset_t blk;
    sigemptyset(&blk);

    if (idx < NUM_CHILD/2) {
        /* Group 1 blocks SIGINT, SIGQUIT, SIGTSTP */
        sigaddset(&blk, SIGINT);
        sigaddset(&blk, SIGQUIT);
        sigaddset(&blk, SIGTSTP);
    } else {
        /* Group 2 blocks the *rest* of the eight signals */
        for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++) {
            int s = SIGNAL_LIST[i];
            if (s != SIGINT && s != SIGTSTP)
                sigaddset(&blk, s);
        }
    }

    if (sigprocmask(SIG_BLOCK, &blk, NULL) < 0) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    /* 2) Install handlers for every signal *not* in blk */
    for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++) {
        int s = SIGNAL_LIST[i];
        if (!sigismember(&blk, s)) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_sigaction = handler;
            sa.sa_flags     = SA_SIGINFO;
            sigemptyset(&sa.sa_mask);
            if (sigaction(s, &sa, NULL) < 0) {
                perror("sigaction");
                exit(EXIT_FAILURE);
            }
        }
    }

    /* 3) Do the sum loop */
    int bound = 10 * (idx + 1);
    long sum = 0;
    for (int i = 0; i <= bound; i++) {
        sum += i;
        printf("Child %d [PID %d]: iteration %2d/%2d → sum=%ld\n",
               idx, getpid(), i, bound, sum);
        sleep(1);
    }

    /* 4) Dump this child’s pending queue */
    char who[64];
    snprintf(who, sizeof(who), "Child %d [PID %d]", idx, getpid());
    print_pending(who);

    exit(EXIT_SUCCESS);
}

int main(void) {
    sigset_t parent_blk;
    /* Parent blocks the same three before fork */
    sigemptyset(&parent_blk);
    sigaddset(&parent_blk, SIGINT);
    sigaddset(&parent_blk, SIGQUIT);
    sigaddset(&parent_blk, SIGTSTP);
    if (sigprocmask(SIG_BLOCK, &parent_blk, NULL) < 0) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    /* Parent installs handlers for the eight signals */
    for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++) {
        int s = SIGNAL_LIST[i];
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = handler;
        sa.sa_flags     = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        if (sigaction(s, &sa, NULL) < 0) {
            perror("sigaction");
            return EXIT_FAILURE;
        }
    }

    /* Let the harness send us three of each before forking */
    printf("READY_FOR_SIGNALS: parent PID=%d\n", getpid());
    fflush(stdout);
    sleep(5);

    /* Fork the four children */
    pid_t child_pids[NUM_CHILD];
    for (int i = 0; i < NUM_CHILD; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }
        if (pid == 0) {
            /* In child */
            child_process(i);
            /* never returns */
        }
        child_pids[i] = pid;
    }

    /* In parent, announce child PIDs */
    printf("CHILD_PIDS:");
    for (int i = 0; i < NUM_CHILD; i++)
        printf(" %d", child_pids[i]);
    printf("\n");
    fflush(stdout);

    /* Wait for all children */
    for (int i = 0; i < NUM_CHILD; i++) {
        int status;
        waitpid(child_pids[i], &status, 0);
    }

    /* Finally, parent dumps its own pending set */
    print_pending("Parent");

    return 0;
}
