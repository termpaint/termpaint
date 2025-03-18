// SPDX-License-Identifier: BSL-1.0
#define _GNU_SOURCE
#include "termpaintx_ttyrescue.h"

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
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#ifdef __linux__
#include <sys/prctl.h>

// memfd
#include <sys/syscall.h>
#ifdef __NR_memfd_create
#include <linux/memfd.h>
#endif
#endif


#ifndef nullptr
#define nullptr ((void*)0)
#endif

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "lock free atomic_int needed");

#ifdef TERMPAINTP_VALGRIND
#include <third-party/valgrind/memcheck.h>
#endif

#ifdef TERMPAINT_RESCUE_FEXEC
#include "ttyrescue_blob.inc"
#endif

#ifdef TERMPAINTP_VALGRIND
static void exit_wrapper(long tid, void (*fn)(int)) {
    (void)tid;
    // lazy resolving _exit by ld.so here crashes, so take it as preresolved pointer
    fn(1);
}
#endif

#if defined(__linux__) && defined(__NR_memfd_create)
int termpaintp_memfd_create(const char *name, unsigned int flags, bool allow_exec) {
#ifdef MFD_EXEC
    if (allow_exec) {
        flags |= MFD_EXEC;
    } else {
        flags |= MFD_NOEXEC_SEAL;
    }
#endif
    int fd = (int)syscall(__NR_memfd_create, name, flags);
#ifdef MFD_EXEC
    if (fd < 0 && errno == EINVAL) {
        // Need to retry if kernel does not yet support MFD_EXEC / MFD_NOEXEC_SEAL.
        flags &= ~(MFD_EXEC | MFD_NOEXEC_SEAL);
        fd = (int)syscall(__NR_memfd_create, name, flags);
    }
#endif
    return fd;
}
#define HAVE_MEMFD 1
#endif

#define SEGLEN 8192

#define TTYRESCUE_FLAG_ATTACHED    (1 << 0)
#define TTYRESCUE_FLAG_TERMIOS_SET (1 << 1)

struct termpaint_ipcseg {
    atomic_int active;
    atomic_int flags;
    long termios_iflag;
    long termios_oflag;
    long termios_lflag;
    long termios_vintr;
    long termios_vmin;
    long termios_vquit;
    long termios_vstart;
    long termios_vstop;
    long termios_vsusp;
    long termios_vtime;
    char seq1[4000];
    char seq2[4000];
};

_Static_assert(sizeof(struct termpaint_ipcseg) < SEGLEN, "termpaint_ipcseg does not fit IPC segment size");

int termpaintp_rescue_embedded(struct termpaint_ipcseg* ctlseg);

struct termpaintx_ttyrescue_ {
    int fd;
    struct termpaint_ipcseg* seg;
    bool using_mmap;
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

termpaintx_ttyrescue *termpaintx_ttyrescue_start_or_nullptr(int tty_fd, const char *restore_seq) {
    termpaintx_ttyrescue *ret = calloc(1, sizeof(termpaintx_ttyrescue));
    if (!ret) {
        return nullptr;
    }
    ret->using_mmap = 0;
    int pipe[2];
#ifdef SOCK_CLOEXEC
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pipe) < 0) {
        free(ret);
        return nullptr;
    }
#else
    // This is racy, but systems that don't offer non racy apis seem to prefer it that way.
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipe) < 0) {
        free(ret);
        return nullptr;
    }
    fcntl(pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe[1], F_SETFL, O_NONBLOCK);
#endif

    int shmfd = -1;
#if HAVE_MEMFD
    shmfd = termpaintp_memfd_create("ttyrescue ctl", MFD_CLOEXEC | MFD_ALLOW_SEALING, false);
    if (shmfd < 0) {
        shmfd = -1;
    }
#endif
#if defined(__FreeBSD__)
    if (shmfd == -1) {
        shmfd = shm_open(SHM_ANON, O_RDWR | IPC_CREAT | IPC_EXCL, 0600); // FD_CLOEXEC is implied
        if (shmfd < 0) {
            shmfd = -1;
        }
    }
#endif
    if (shmfd != -1) {
        if (ftruncate(shmfd, SEGLEN) < 0) {
            close(shmfd);
            shmfd = -1;
        } else {
            ret->seg = mmap(0, SEGLEN, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
            if (ret->seg == MAP_FAILED) {
                close(shmfd);
                shmfd = -1;
                ret->seg = nullptr;
            } else {
                ret->using_mmap = true;
            }
        }
    }
    int shmid = -1;
    if (shmfd == -1) {
        shmid = shmget(IPC_PRIVATE, SEGLEN, 0600 | IPC_CREAT | IPC_EXCL);
        ret->seg = shmat(shmid, 0, 0);
        if (ret->seg == (void*)-1) {
            ret->seg = nullptr;
            shmctl(shmid, IPC_RMID, nullptr);
            shmid = -1;
        }
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
    if (shmfd != -1) {
        envvar2 = strdup("TTYRESCUE_SHMFD=yes");
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
        if (shmfd != -1) {
            close(shmfd);
        }
        free(ret);
        return nullptr;
    }

    termpaintx_ttyrescue_update(ret, restore_seq, strlen(restore_seq));

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
        if (shmfd != -1) {
            close(shmfd);
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
            (void)!read(ret->fd, buff, 1); // failure doesn't matter, course of action keeps same
            shmctl(shmid, IPC_RMID, nullptr);
        }
        if (shmfd != -1) {
            close(shmfd);
        }
        return ret;
    } else {
        close(pipe[1]);
        // wanted file descriptors 0: control pipe(pipe[0]), 1: closed, 2: terminal(tty_fd), 3: control segment (shmfd)
        // first move file descriptors we intend to keep out of the way
        if (tty_fd == 0) {
            tty_fd = dup(tty_fd);
        }
        if (shmfd == 0) {
            shmfd = dup(shmfd);
        }
        // setup fd 0
        if (pipe[0] == 0) {
            fcntl(0, F_SETFD, 0); // Unset O_CLOEXEC
        } else {
            dup2(pipe[0], 0);
        }
        if (shmfd == 2) {
            shmfd = dup(shmfd);
        }
        if (tty_fd == 2) {
            fcntl(pipe[0], F_SETFD, 0); // Unset O_CLOEXEC
        } else {
            dup2(tty_fd, 2);
        }
        if (shmfd != -1) {
            if (shmfd == 3) {
                fcntl(3, F_SETFD, 0); // Unset O_CLOEXEC
            } else {
                dup2(shmfd, 3);
            }
        }
        close(1);
        int from = ((shmfd != -1) ? 4 : 3);
#if defined(__FreeBSD__)
        closefrom(from);
#else
        int max_fd = sysconf(_SC_OPEN_MAX);
        for (int i = from; i < max_fd; i++) {
            close(i);
        }
#endif
        char *argv[] = {"ttyrescue", NULL};
        const char* path = TERMPAINT_RESCUE_PATH;
        char tmp[sizeof(TERMPAINT_RESCUE_PATH) + sizeof("ttyrescue") + 1];
        for (const char *item = path; *item;) {
            char *end = strchr(item, ':');
            ptrdiff_t len;
            if (end) {
                len = end - item;
            } else {
                len = strlen(item);
            }
            if (len > 0) {
                memcpy(tmp, item, len);
                tmp[len] = '/';
                tmp[len+1] = '\0';
                strcat(tmp, "ttyrescue");
                execve(tmp, argv, envp);
            }
            if (end) {
                item = end + 1;
            } else {
                break;
            }
        }

#ifdef TERMPAINT_RESCUE_FEXEC
#ifdef __linux__
        if (shmfd != -1) {
            int exefd = termpaintp_memfd_create("ttyrescue (embedded)", MFD_CLOEXEC | MFD_ALLOW_SEALING, true);
            if (write(exefd, ttyrescue_blob, sizeof(ttyrescue_blob)) == sizeof(ttyrescue_blob)) {
                argv[0] = "ttyrescue (embedded)";
                fexecve(exefd, argv, envp);
                close(exefd);
            }
        }
#else
#error ttyrescue-fexec-blob option not available on this platform: not ported yet
#endif
#endif

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
            intptr_t argc_end = -1;
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
                        if (argn == 49) { // arg_end
                            argc_end = buffer[idx] - '0';
                        }
                    }
                } else if (state == S_ARGX) {
                    if (buffer[idx] == ' ') {
                        state = S_SPACEX;
                        if (argn == 49) {
                            break;
                        }
                    } else {
                        if (argn == 48) { // arg_start
                            argc_base = argc_base * 10 + buffer[idx] - '0';
                        }
                        if (argn == 49) { // arg_end
                            argc_end = argc_end * 10 + buffer[idx] - '0';
                        }
                    }
                }
            }
            close(fd);
            if (argc_base != -1 && argc_end != -1 && argc_end > argc_base) {
#ifdef TERMPAINTP_VALGRIND
                // Valgrind does not grok that this is supposed to work, use a cluebat
                VALGRIND_MAKE_MEM_DEFINED(argc_base, argc_end - argc_base);
#endif
                int datalen = 21;
                // if datalen does not fit into the arg space, intentionally overwrite into
                // the environment space to get the full name shown.
                memcpy((void*)argc_base, "ttyrescue (embedded)", datalen);
                // zero out the rest of the space, so ps doesn't show left over parts from old name
                if (datalen <= argc_end - argc_base) {
                    memset((char*)argc_base + datalen, 0, argc_end - argc_base - datalen);
                }
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
            VALGRIND_PRINTF("termpaint embedded ttyrescue exited (suppressing valgrind reports in rescue process)\n");
            (void)VALGRIND_NON_SIMD_CALL1(exit_wrapper, _exit);
        }
#endif
        _exit(1);
    }
}

void termpaintx_ttyrescue_stop(termpaintx_ttyrescue *tpr) {
    if (!tpr) {
        return;
    }
    if (tpr->fd >= 0) {
#ifdef MSG_NOSIGNAL
        send(tpr->fd, "~", 1, MSG_NOSIGNAL);
#else
        send(tpr->fd, "~", 1, 0);
#endif
    }
    if (tpr->seg) {
        if (tpr->using_mmap) {
            munmap(tpr->seg, SEGLEN);
        } else {
            shmdt(tpr->seg);
        }
    }
    free(tpr);
}

_Bool termpaintx_ttyrescue_update(termpaintx_ttyrescue *tpr, const char *data, int len) {
    if (tpr->seg && len < (int)sizeof(tpr->seg->seq1)) {
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

bool termpaintx_ttyrescue_set_restore_termios(termpaintx_ttyrescue *tpr, const struct termios *original_terminal_attributes) {
    if (tpr->seg) {
        tpr->seg->termios_iflag = original_terminal_attributes->c_iflag;
        tpr->seg->termios_oflag = original_terminal_attributes->c_oflag;
        tpr->seg->termios_lflag = original_terminal_attributes->c_lflag;
        tpr->seg->termios_vintr = original_terminal_attributes->c_cc[VINTR];
        tpr->seg->termios_vmin = original_terminal_attributes->c_cc[VMIN];
        tpr->seg->termios_vquit = original_terminal_attributes->c_cc[VQUIT];
        tpr->seg->termios_vstart = original_terminal_attributes->c_cc[VSTART];
        tpr->seg->termios_vstop = original_terminal_attributes->c_cc[VSTOP];
        tpr->seg->termios_vsusp = original_terminal_attributes->c_cc[VSUSP];
        tpr->seg->termios_vtime = original_terminal_attributes->c_cc[VTIME];
        atomic_fetch_or(&tpr->seg->flags, TTYRESCUE_FLAG_TERMIOS_SET);
    }
    return 0;
}
