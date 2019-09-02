#include "termpaintx_ttyrescue.h"

#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#ifndef nullptr
#define nullptr ((void*)0)
#endif

#ifdef TERMPAINTP_VALGRIND
#include <third-party/valgrind/memcheck.h>
#endif

int termpaintp_rescue_embedded(void);

#ifdef TERMPAINTP_VALGRIND
static void exit_wrapper(long tid, void (*fn)(int)) {
    (void)tid;
    // lazy resolving _exit by ld.so here crashes, so take it as preresolved pointer
    fn(1);
}
#endif

struct termpaint_ttyrescue_ {
    int fd;
};

termpaintx_ttyrescue *termpaint_ttyrescue_start(const char *restore_seq) {
    termpaintx_ttyrescue *ret = calloc(1, sizeof(termpaintx_ttyrescue));
    int pipe[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pipe) < 0) {
        free(ret);
        return nullptr;
    }

    char *envvar = (char*)malloc(strlen(restore_seq) + 26);
    if (!envvar) {
        close(pipe[0]);
        close(pipe[1]);
        free(ret);
        return nullptr;
    }
    strcpy(envvar, "TERMPAINT_RESCUE_RESTORE=");
    strcat(envvar, restore_seq);
    char *envp[] = {envvar, NULL};

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe[0]);
        close(pipe[1]);
        free(envvar);
        free(ret);
        return nullptr;
    } else if (pid) {
        free(envvar);
        close(pipe[0]);
        ret->fd = pipe[1];
        return ret;
    } else {
        close(pipe[1]);
        fcntl(pipe[0], F_SETFD, 0);
        dup2(pipe[0], 0);
        close(1);
        int max_fd = sysconf(_SC_OPEN_MAX);
        for (int i = 3; i < max_fd; i++) {
            close(i);
        }
        char *argv[] = {"ttyrescue", NULL};
        execvpe("ttyrescue", argv, envp);

        // if that does not work use internal fallback.

        extern char **environ;
        environ = envp;
#ifdef __linux__
        prctl(PR_SET_NAME, "ttyrescue embed", 0, 0, 0);
        int fd = open("/proc/self/stat", O_RDONLY, 0);
        if (fd) {
            char buffer[1000];
            int idx = 0;
            int max = 0;
            int argn = 0;
            intptr_t argc_base = -1;
            enum { S_A1, S_PAREN1, S_COMM, S_SPACEX, S_ARGX } state = S_A1;
            while (1) {
                if (idx+1 >= max) {
                    max = read(fd, buffer, 1000);
                    if (max <= 0) {
                        break;
                    }
                    idx = 0;
                } else {
                    ++idx;
                }
                if (state == S_A1) {
                    if (buffer[idx] == ' ') {
                        state = S_PAREN1;
                    }
                } else if (state == S_PAREN1) {
                    if (buffer[idx] == '(') {
                        state = S_COMM;
                    }
                } else if (state == S_COMM) {
                    if (buffer[idx] == ')') {
                        state = S_SPACEX;
                        argn = 2;
                    }
                } else if (state == S_SPACEX) {
                    if (buffer[idx] != ' ') {
                        ++argn;
                        state = S_ARGX;
                        if (argn == 48) { // arg_start
                            argc_base = buffer[idx] - '0';
                        }
                    }
                } else if (state == S_ARGX) {
                    if (buffer[idx] == ' ') {
                        state = S_SPACEX;
                        if (argn == 48) {
                            break;
                        }
                    } else {
                        if (argn == 48) { // arg_start
                            argc_base = argc_base * 10 + buffer[idx] - '0';
                        }
                    }
                }
            }
            close(fd);
            if (argc_base != -1) {
#ifdef TERMPAINTP_VALGRIND
                // Valgrind does not grok that this is supposed to work, use a cluebat
                VALGRIND_MAKE_MEM_DEFINED(argc_base, 22);
#endif
                memcpy((void*)argc_base, "ttyrescue (embedded)", 22);
            }
        }
#endif
#ifdef TERMPAINTP_VALGRIND
        VALGRIND_PRINTF("termpaint embedded ttyrescue running with valgrind. Maybe use --child-silent-after-fork=yes\n");
#endif
        termpaintp_rescue_embedded();
#ifdef TERMPAINTP_VALGRIND
        if (RUNNING_ON_VALGRIND) {
            // There is no way to avoid leaking the main programs allocations
            // Valgrind's leak check would report these, so instead exit this process without valgrind noticeing.
            VALGRIND_PRINTF("termpaint embedded ttyrescue exited (supressing valgrind reports in rescue process)\n");
            VALGRIND_NON_SIMD_CALL1(exit_wrapper, _exit);
        }
#endif
        _exit(1);
    }
}

void termpaint_ttyrescue_stop(termpaintx_ttyrescue *tpr) {
    if (!tpr) {
        return;
    }
    if (tpr->fd >= 0) {
        send(tpr->fd, "~", 1, MSG_NOSIGNAL);
    }
    free(tpr);
}
