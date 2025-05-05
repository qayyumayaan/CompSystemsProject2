#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define NUM_CHILD 4

/* The eight signals we care about */
static int SIGNAL_LIST[] = {
    SIGINT, SIGABRT, SIGILL, SIGCHLD,
    SIGSEGV, SIGFPE, SIGHUP, SIGTSTP
};

void print_pending(const char *who) {
    sigset_t pend;
    sigpending(&pend);
    printf("%s pending signals:", who);
    for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++) {
        if (sigismember(&pend, SIGNAL_LIST[i])) {
            printf(" %s", strsignal(SIGNAL_LIST[i]));
        }
    }
    printf("\n");
}

void handler(int sig, siginfo_t *info, void *ucontext) {
    printf("[PID %d] caught signal %d (%s) from PID %d\n",
        getpid(), sig, strsignal(sig), info ? info->si_pid : -1);
}

void setup_handlers(sigset_t *blocked) {
    for (size_t i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++) {
        int s = SIGNAL_LIST[i];
        if (!sigismember(blocked, s)) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_sigaction = handler;
            sa.sa_flags = SA_SIGINFO;
            sigemptyset(&sa.sa_mask);
            sigaction(s, &sa, NULL);
        }
    }
}

void send_signals(pid_t pid, const char *role) {
    for (int i = 0; i < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); i++) {
        for (int j = 0; j < 3; j++) {
            printf("[Parent] sending %s to %s PID %d\n", strsignal(SIGNAL_LIST[i]), role, pid);
            kill(pid, SIGNAL_LIST[i]);
            usleep(100000);
        }
    }
}

int main() {
    pid_t children[NUM_CHILD];
    sigset_t parent_mask;
    sigemptyset(&parent_mask);
    sigaddset(&parent_mask, SIGINT);
    sigaddset(&parent_mask, SIGQUIT);
    sigaddset(&parent_mask, SIGTSTP);

    sigprocmask(SIG_BLOCK, &parent_mask, NULL);
    setup_handlers(&parent_mask);

    printf("[Parent] PID = %d blocking SIGINT, SIGQUIT, SIGTSTP\n", getpid());
    printf("[Parent] Send me signals now!\n");
    sleep(3);
    print_pending("Parent (before fork)");

    for (int i = 0; i < NUM_CHILD; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            sigset_t child_mask;
            sigemptyset(&child_mask);
            if (i < NUM_CHILD / 2) {
                sigaddset(&child_mask, SIGINT);
                sigaddset(&child_mask, SIGQUIT);
                sigaddset(&child_mask, SIGTSTP);
            } else {
                for (size_t j = 0; j < sizeof(SIGNAL_LIST)/sizeof(*SIGNAL_LIST); j++) {
                    int sig = SIGNAL_LIST[j];
                    if (sig != SIGINT && sig != SIGQUIT && sig != SIGTSTP) {
                        sigaddset(&child_mask, sig);
                    }
                }
            }
            sigprocmask(SIG_BLOCK, &child_mask, NULL);
            setup_handlers(&child_mask);
            printf("[Child %d] PID = %d set mask\n", i, getpid());
            sleep(3);
            print_pending("Child");
            exit(0);
        } else if (pid > 0) {
            children[i] = pid;
        } else {
            perror("fork");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NUM_CHILD; i++) {
        send_signals(children[i], "child");
    }

    send_signals(getpid(), "parent");

    for (int i = 0; i < NUM_CHILD; i++) {
        waitpid(children[i], NULL, 0);
    }

    print_pending("Parent (after wait)");
    return 0;
}

// gcc -o signal_demo_q3 signal_demo_q3.c && ./signal_demo_q3