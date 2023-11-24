// SPDX-License-Identifier: BSL-1.0

#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef nullptr
#define nullptr ((void*)0)
#endif

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

int main(int argc, char** argv) {
    (void) argc; (void) argv;

    struct termpaint_ipcseg *ctlseg = mmap(0, 8048, PROT_READ | PROT_WRITE, MAP_SHARED, 3, 0);
    close(3);
    if (ctlseg == MAP_FAILED) {
        output("ttyrescue: mmap failed. Abort.\n");
        return 1;
    }
    atomic_fetch_or(&ctlseg->flags, TTYRESCUE_FLAG_ATTACHED);

    sigset_t fullset;
    sigfillset(&fullset);
    sigprocmask(SIG_BLOCK, &fullset, NULL);

    struct pollfd fds[1] = { { .fd = 0, .events = POLLIN } };

    while (1) {
        char buf[10];
        ssize_t retval;
        do {
            retval = poll(fds, 1, -1);
        } while (retval == -EINTR);
        if (retval < 0) {
            return 0;
        }
        retval = read(0, buf, 10);
        if (retval == 0) {
            // parent crashed
            int offset = atomic_load(&ctlseg->active);
            if (offset) {
                output((char*)ctlseg + offset);
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
                        tcsetattr(2, TCSAFLUSH, &tattr);
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
