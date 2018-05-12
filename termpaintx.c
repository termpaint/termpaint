#include "termpaintx.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <poll.h>
#include <malloc.h>
#include <errno.h>
#include <stdbool.h>

#include <termpaint_compiler.h>

#if __GNUC__
// Trying to avoid this warning with e.g. bit manipulations using defines from the standard headers is just too ugly
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#define nullptr ((void*)0)

#define FDPTR(var) ((termpaint_integration_fd*)var)

_Bool termpaint_full_integration_available() {
    _Bool from_std_fd = false;
    from_std_fd = isatty(0) || isatty(1) || isatty(2);
    if (from_std_fd) {
        return true;
    }
    // also try controlling terminal
    int fd = open("/dev/tty", O_RDONLY | O_NOCTTY | FD_CLOEXEC);
    if (fd != -1) {
        close(fd);
        return true;
    }
    return false;
}

termpaint_integration *termpaint_full_integration(const char *options) {
    int fd = -1;
    _Bool auto_close = false;

    if (isatty(0)) {
        fd = 0;
    } else if (isatty(1)) {
        fd = 1;
    } else if (isatty(2)) {
        fd = 2;
    } else {
        fd = open("/dev/tty", O_RDWR | O_NOCTTY | FD_CLOEXEC);
        auto_close = true;
        if (fd == -1) {
            return nullptr;
        }
    }

    return termpaint_full_integration_from_fd(fd, auto_close, options);
}


termpaint_integration *termpaint_full_integration_from_controlling_terminal(const char *options) {
    int fd = -1;
    fd = open("/dev/tty", O_RDWR | O_NOCTTY | FD_CLOEXEC);
    if (fd == -1) {
        return nullptr;
    }
    return termpaint_full_integration_from_fd(fd, true, options);
}

typedef struct termpaint_integration_fd_ {
    termpaint_integration base;
    char *options;
    int fd;
    bool auto_close;
    struct termios original_terminal_attributes;
    bool awaiting_response;
    termpaint_terminal *terminal;
} termpaint_integration_fd;

static void fd_free(termpaint_integration* integration) {
    termpaint_integration_fd* fd_data = FDPTR(integration);
    tcsetattr (fd_data->fd, TCSAFLUSH, &fd_data->original_terminal_attributes);
    if (fd_data->auto_close && fd_data->fd != -1) {
        close(fd_data->fd);
    }
    free(fd_data->options);
    free(fd_data);
}

static void fd_flush(termpaint_integration* integration) {
    (void)integration;
    // no buffering yet
}

static void fd_mark_bad(termpaint_integration* integration) {
    FDPTR(integration)->fd = -1;
}

static _Bool fd_is_bad(termpaint_integration* integration) {
    return FDPTR(integration)->fd == -1;
}

static void fd_write(termpaint_integration* integration, char *data, int length) {
    ssize_t written = 0;
    ssize_t ret;
    errno = 0;
    while (written != length) {
        ret = write(FDPTR(integration)->fd, data+written, length-written);
        if (ret > 0) {
            written += ret;
        } else {
            // error handling?
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // fatal, non blocking is not supported by this integration
                fd_mark_bad(integration);
                return;
            }
            if (errno == EIO || errno == ENOSPC) {
                // fatal?
                fd_mark_bad(integration);
                return;
            }
            if (errno == EBADF || errno == EINVAL || errno == EPIPE) {
                // fatal, or fd is gone bad
                fd_mark_bad(integration);
                return;
            }
            if (errno == EINTR) {
                continue;
            }
        }
    }
}

static void fd_request_callback(struct termpaint_integration_ *integration) {
    FDPTR(integration)->awaiting_response = true;
}

static char *termpaintp_pad_options(const char *options) {
    size_t optlen = strlen(options);
    char *ret = calloc(1, strlen(options) + 3);
    ret[0] = ' ';
    memcpy(ret + 1, options, optlen);
    ret[optlen+1] = ' ';
    return ret;
}

bool termpaintp_fd_set_termios(int fd, const char *options) {
    struct termios tattr;
    tcgetattr(fd, &tattr);
    tattr.c_iflag |= IGNBRK|IGNPAR;
    tattr.c_iflag &= ~(BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF);
    tattr.c_oflag &= ~(OPOST|ONLCR|OCRNL|ONOCR|ONLRET);
    tattr.c_lflag &= ~(ICANON|IEXTEN|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;

    bool allow_interrupt = strstr(options, " +kbdsigint ");
    bool allow_quit = strstr(options, " +kbdsigquit ");
    bool allow_suspend = strstr(options, " +kbdsigtstp ");

    if (!(allow_interrupt || allow_quit || allow_suspend)) {
        tattr.c_lflag &= ~ISIG;
    } else {
        if (!allow_interrupt) {
            tattr.c_cc[VINTR] = 0;
        }
        if (!allow_quit) {
            tattr.c_cc[VQUIT] = 0;
        }
        if (!allow_suspend) {
            tattr.c_cc[VSUSP] = 0;
        }
    }

    tcsetattr (fd, TCSAFLUSH, &tattr);
    return true;
}

termpaint_integration *termpaint_full_integration_from_fd(int fd, _Bool auto_close, const char *options) {
    termpaint_integration_fd *ret = calloc(1, sizeof(termpaint_integration_fd));
    ret->base.free = fd_free;
    ret->base.write = fd_write;
    ret->base.flush = fd_flush;
    ret->base.is_bad = fd_is_bad;
    ret->base.request_callback = fd_request_callback;
    ret->options = termpaintp_pad_options(options);
    ret->fd = fd;
    ret->auto_close = auto_close;
    ret->awaiting_response = false;

    tcgetattr(ret->fd, &ret->original_terminal_attributes);
    termpaintp_fd_set_termios(ret->fd, options);
    return (termpaint_integration*)ret;
}

bool termpaint_full_integration_wait_for_ready(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);
    while (termpaint_terminal_auto_detect_state(t->terminal) == termpaint_auto_detect_running) {
        if (!termpaint_full_integration_do_iteration(integration)) {
            // some kind of error
            break;
        }
    }
    return false;
}

bool termpaint_full_integration_terminal_size(termpaint_integration *integration, int *width, int *height) {
    if (fd_is_bad(integration) || !isatty(FDPTR(integration)->fd)) {
        return false;
    }
    struct winsize s;
    if (ioctl(FDPTR(integration)->fd, TIOCGWINSZ, &s) < 0) {
        return false;
    }
    *width = s.ws_col;
    *height = s.ws_row;
    return true;
}

void termpaint_full_integration_set_terminal(termpaint_integration *integration, termpaint_terminal *terminal) {
    termpaint_integration_fd *t = FDPTR(integration);
    t->terminal = terminal;
}

bool termpaint_full_integration_do_iteration(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);

    char buff[1000];
    int amount = (int)read(t->fd, buff, 999);
    if (amount < 0) {
        return false;
    }
    termpaint_terminal_add_input_data(t->terminal, buff, amount);

    if (t->awaiting_response) {
        t->awaiting_response = false;
        struct pollfd info;
        info.fd = t->fd;
        info.events = POLLIN;
        int ret = poll(&info, 1, 100);
        if (ret == 1) {
            int amount = (int)read(t->fd, buff, 999);
            if (amount < 0) {
                return false;
            }
            termpaint_terminal_add_input_data(t->terminal, buff, amount);
        }
        termpaint_terminal_callback(t->terminal);
    }

    return true;
}

