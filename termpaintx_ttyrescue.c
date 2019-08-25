#include "termpaintx_ttyrescue.h"

#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#ifndef nullptr
#define nullptr ((void*)0)
#endif

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "lock free atomic_int needed");

#ifdef TERMPAINTP_VALGRIND
#include <third-party/valgrind/memcheck.h>
#endif

int termpaintp_rescue_embedded(void* ctlseg);

#ifdef TERMPAINTP_VALGRIND
static void exit_wrapper(long tid, void (*fn)(int)) {
    (void)tid;
    // lazy resolving _exit by ld.so here crashes, so take it as preresolved pointer
    fn(1);
}
#endif

#define SEGLEN 8192

struct termpaint_ipcseg {
    atomic_int active;
    atomic_int flags;
    char seq1[4000];
    char seq2[4000];
};

_Static_assert(sizeof(struct termpaint_ipcseg) < SEGLEN, "termpaint_ipcseg does not fit IPC segment size");

struct termpaint_ttyrescue_ {
    int fd;
    struct termpaint_ipcseg* seg;
};

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
static char* termpaintp_asprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);

    if (len < 0) {
        return nullptr;
    }

    char *ret = malloc(len + 1);
    if (!ret) {
        return nullptr;
    }

    va_start(ap, fmt);
    len = vsnprintf(ret, len + 1, fmt, ap);
    va_end(ap);

    if (len < 0) {
        free(ret);
        return nullptr;
    }

    return ret;
}

termpaintx_ttyrescue *termpaint_ttyrescue_start(const char *restore_seq) {
    termpaintx_ttyrescue *ret = calloc(1, sizeof(termpaintx_ttyrescue));
    int pipe[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pipe) < 0) {
        free(ret);
        return nullptr;
    }

    int shmid = -1;
    shmid = shmget(IPC_PRIVATE, SEGLEN, 0600 | IPC_CREAT | IPC_EXCL);
    ret->seg = shmat(shmid, 0, 0);
    if (ret->seg == (void*)-1) {
        ret->seg = nullptr;
        shmctl(shmid, IPC_RMID, nullptr);
        shmid = -1;
    }

    char *envvar = termpaintp_asprintf("TTYRESCUE_RESTORE=%s", restore_seq);
    char *envvar2 = nullptr;
    bool alloc_error = envvar == nullptr;
    char *envp[3] = {envvar, nullptr, nullptr};

    if (shmid != -1) {
        envvar2 = termpaintp_asprintf("TTYRESCUE_SYSVSHMID=%i", shmid);
        alloc_error |= envvar2 == nullptr;
        envp[1] = envvar2;
    }
    if (alloc_error) {
        close(pipe[0]);
        close(pipe[1]);
        free(envvar);
        free(envvar2);

        if (shmid != -1) {
            shmctl(shmid, IPC_RMID, nullptr);
        }
        free(ret);
        return nullptr;
    }

    termpaint_ttyrescue_update(ret, restore_seq, strlen(restore_seq));

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe[0]);
        close(pipe[1]);
        free(envvar);
        free(envvar2);
        if (shmid != -1) {
            shmdt(ret->seg);
            shmctl(shmid, IPC_RMID, nullptr);
        }
        free(ret);
        return nullptr;
    } else if (pid) {
        free(envvar);
        free(envvar2);
        close(pipe[0]);
        ret->fd = pipe[1];
        if (shmid != -1) {
            struct pollfd info;
            info.fd = ret->fd;
            info.events = POLLIN;
            int err;
            do {
                err = poll(&info, 1, -1);
            } while (err == EINTR);

            char buff[1];
            read(ret->fd, buff, 1);
            shmctl(shmid, IPC_RMID, nullptr);
        }
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
        termpaintp_rescue_embedded(ret->seg);
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
    if (tpr->seg) {
        shmdt(tpr->seg);
    }
    free(tpr);
}

_Bool termpaint_ttyrescue_update(termpaintx_ttyrescue *tpr, const char *data, int len) {
    if (tpr->seg && len < sizeof(tpr->seg->seq1)) {
        // active is only written from this process. It's atomic to get memory_order_seq_cst and
        // to avoid tearing
        int offset = atomic_load(&tpr->seg->active);
        if (offset == 0 || offset == offsetof(struct termpaint_ipcseg, seq2)) {
            memcpy(tpr->seg->seq1, data, len);
            tpr->seg->seq1[len] = 0;
            atomic_store(&tpr->seg->active, offsetof(struct termpaint_ipcseg, seq1));
        } else {
            memcpy(tpr->seg->seq2, data, len);
            tpr->seg->seq2[len] = 0;
            atomic_store(&tpr->seg->active, offsetof(struct termpaint_ipcseg, seq2));
        }
        return 1;
    }
    return 0;
}
