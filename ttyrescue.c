// SPDX-License-Identifier: BSL-1.0
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef nullptr
#define nullptr ((void*)0)
#endif

static char *restore;

static void output(const char *s) {
    (void)!write(2, s, strlen(s)); // ignore error, exiting soon anyway.
}

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
};

#ifndef TERMPAINT_RESCUE_EMBEDDED
int main(int argc, char** argv) {
    (void) argc; (void) argv;
    struct termpaint_ipcseg *ctlseg = nullptr;
#else
int termpaintp_rescue_embedded(struct termpaint_ipcseg *ctlseg) {
#endif
    restore = getenv("TTYRESCUE_RESTORE");

    if (!restore || *restore==0) {
        output("This is an internal helper to ensure that the terminal is properly restored.\n");
        output("There should be no need to call this manually.\n");
        return 0;
    }

    int res;
    res = isatty(0);
    if (res || (errno != EINVAL && errno != ENOTTY && errno != EOPNOTSUPP)) {
        output("Invalid invocation\n");
        return 1;
    }

    res = fcntl(0, F_GETFL);
    if (res == -1 || !(res & O_NONBLOCK)) {
        output("Invalid invocation\n");
        return 1;
    }

    res = isatty(1);
    if (res || errno != EBADF) {
        output("Invalid invocation\n");
        return 1;
    }

#ifndef TERMPAINT_RESCUE_EMBEDDED
    if (getenv("TTYRESCUE_SHMFD")) {
        ctlseg = mmap(0, 8048, PROT_READ | PROT_WRITE, MAP_SHARED, 3, 0);
        close(3);
        if (ctlseg == MAP_FAILED) {
            output("ttyrescue: mmap failed. Abort.\n");
            return 1;
        }
        atomic_fetch_or(&ctlseg->flags, TTYRESCUE_FLAG_ATTACHED);
    }
#endif

    if (getenv("TTYRESCUE_SYSVSHMID")) {
#ifndef TERMPAINT_RESCUE_EMBEDDED
        char *var = getenv("TTYRESCUE_SYSVSHMID");
        char *end;
        errno = 0;
        long int val = strtol(var, &end, 10);
        if (*end != '\0' || errno != 0) {
            output("ttyrescue: Can't parse TTYRESCUE_SYSVSHMID. Abort.\n");
            return 1;
        }
        ctlseg = shmat(val, nullptr, 0);
        if (ctlseg == (void*)-1) {
            output("ttyrescue: shmat failed. Abort.\n");
            return 1;
        }
        atomic_fetch_or(&ctlseg->flags, TTYRESCUE_FLAG_ATTACHED);
#endif
        (void)!write(0, "x", 1); // nothing much we can do about an error here, likely read below will error too and exit anyway.
    }

    sigset_t fullset;
    sigfillset(&fullset);
    sigprocmask(SIG_BLOCK, &fullset, NULL);

    fd_set rfds;
    fd_set efds;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    FD_ZERO(&efds);
    FD_SET(0, &efds);
    while (1) {
        char buf[10];
        int retval = select(1, &rfds, NULL, &efds, NULL);
        if (retval < 0) {
            return 0;
        }
        retval = read(0, buf, 10);
        if (retval == 0) {
            // parent crashed
            int offset = 0;
            if (ctlseg) {
                offset = atomic_load(&ctlseg->active);
            }
            if (offset) {
                output((char*)ctlseg + offset);
            } else {
                output(restore);
            }
            if (atomic_load(&ctlseg->flags) & TTYRESCUE_FLAG_TERMIOS_SET) {
                if (tcgetpgrp(2) == getpgrp()) {
                    struct termios tattr;
                    if (tcgetattr(2, &tattr) >= 0) {
                        tattr.c_iflag = (tcflag_t)ctlseg->termios_iflag;
                        tattr.c_oflag = (tcflag_t)ctlseg->termios_oflag;
                        tattr.c_lflag = (tcflag_t)ctlseg->termios_lflag;
                        tattr.c_cc[VINTR] = (cc_t)ctlseg->termios_vintr;
                        tattr.c_cc[VMIN] = (cc_t)ctlseg->termios_vmin;
                        tattr.c_cc[VQUIT] = (cc_t)ctlseg->termios_vquit;
                        tattr.c_cc[VSTART] = (cc_t)ctlseg->termios_vstart;
                        tattr.c_cc[VSTOP] = (cc_t)ctlseg->termios_vstop;
                        tattr.c_cc[VSUSP] = (cc_t)ctlseg->termios_vsusp;
                        tattr.c_cc[VTIME] = (cc_t)ctlseg->termios_vtime;
                        tcsetattr (2, TCSAFLUSH, &tattr);
                    }
                }
            }
            return 0;
        }
        if (retval < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (retval > 0) {
            // clean close of parent
            return 0;
        }
    }

    return 0;
}
