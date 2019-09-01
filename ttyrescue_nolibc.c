// compile with: gcc -fno-asynchronous-unwind-tables -fno-ident -nostdlib -static -Os -fvisibility=hidden -std=gnu11 ttyrescue_nolibc.c -o ttyrescue_nolibc
// strip: strip --strip-all --remove-section=.comment --remove-section=.note --remove-section=.eh_frame_hdr --remove-section=.eh_frame --remove-section=.note.gnu.gold-version  ttyrescue_nolibc

#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <termios.h>

static int sys_errno;

#define SYS_ERRNO sys_errno

#include "third-party/linux_syscall_support.h"

// hopefully just intrinsics
#include <stdatomic.h>

#ifndef nullptr
#define nullptr ((void*)0)
#endif

static inline pid_t sysx_tcgetpgrp(int fd) {
    int res;
    if (sys_ioctl(fd,  TIOCGPGRP, &res) < 0) {
        return -1;
    }
    return res;
}

#if defined(__mips__)
#define kernelx_NCCS 23
#elif defined(__sparc__)
#define kernelx_NCCS 17
#else
#define kernelx_NCCS 19
#endif

#if defined(__alpha__) || defined(__powerpc64__) || defined(__powerpc__)
struct kernelx_termios {
        unsigned int c_iflag;
        unsigned int c_oflag;
        unsigned int c_cflag;
        unsigned int c_lflag;
        unsigned char c_cc[kernelx_NCCS];
        unsigned char c_line;
        unsigned int c_ispeed;
        unsigned int c_ospeed;
};
#else
struct kernelx_termios {
        unsigned int c_iflag;
        unsigned int c_oflag;
        unsigned int c_cflag;
        unsigned int c_lflag;
        unsigned char c_line;
        unsigned char c_cc[kernelx_NCCS];
};
#endif


static inline int sysx_tcgetattr(int fd, struct kernelx_termios *attr) {
    return sys_ioctl(fd, TCGETS, attr);
}

static inline pid_t sysx_tcsetattr_flush(int fd, struct kernelx_termios *attr) {
    return sys_ioctl(fd, TCSETSF, attr);
}

int x_strlen(const char *s) {
    int len = 0;
    while (*s) {
        len++;
        s++;
    }
    return len;
}

static void output(const char *s) {
    sys_write(2, s, x_strlen(s));
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

int _start(int argc, char** argv) {
    (void) argc; (void) argv;

    struct termpaint_ipcseg *ctlseg = sys_mmap(0, 8048, PROT_READ | PROT_WRITE, MAP_SHARED, 3, 0);
    sys_close(3);
    if (ctlseg == MAP_FAILED) {
        output("ttyrescue: mmap failed. Abort.\n");
        sys_exit_group(1);
    }
    atomic_fetch_or(&ctlseg->flags, TTYRESCUE_FLAG_ATTACHED);

    struct kernel_sigset_t fullset;
    sys_sigfillset(&fullset);
    sys_sigprocmask(SIG_BLOCK, &fullset, NULL);

    struct kernel_pollfd fds[1] = { { .fd = 0, .events = POLLIN } };

    while (1) {
        char buf[10];
        ssize_t retval;
        do {
            retval = sys_poll(fds, 1, -1);
        } while (retval == -EINTR);
        if (retval < 0) {
            sys_exit_group(0);
        }
        retval = sys_read(0, buf, 10);
        if (retval == 0) {
            // parent crashed
            int offset = atomic_load(&ctlseg->active);
            if (offset) {
                output((char*)ctlseg + offset);
            }
            if (atomic_load(&ctlseg->flags) & TTYRESCUE_FLAG_TERMIOS_SET) {
                if (sysx_tcgetpgrp(2) == sys_getpgrp()) {
                    struct kernelx_termios tattr;
                    sysx_tcgetattr(2, &tattr);
                    tattr.c_iflag = (unsigned int)ctlseg->termios_iflag;
                    tattr.c_oflag = (unsigned int)ctlseg->termios_oflag;
                    tattr.c_lflag = (unsigned int)ctlseg->termios_lflag;
                    tattr.c_cc[VINTR] = (unsigned char)ctlseg->termios_vintr;
                    tattr.c_cc[VMIN] = (unsigned char)ctlseg->termios_vmin;
                    tattr.c_cc[VQUIT] = (unsigned char)ctlseg->termios_vquit;
                    tattr.c_cc[VSTART] = (unsigned char)ctlseg->termios_vstart;
                    tattr.c_cc[VSTOP] = (unsigned char)ctlseg->termios_vstop;
                    tattr.c_cc[VSUSP] = (unsigned char)ctlseg->termios_vsusp;
                    tattr.c_cc[VTIME] = (unsigned char)ctlseg->termios_vtime;
                    sysx_tcsetattr_flush(2, &tattr);
                }
            }
            sys_exit_group(0);
        }
        if (retval > 0) {
            // clean close of parent
            sys_exit_group(0);
        }
    }

    sys_exit_group(0);
}
