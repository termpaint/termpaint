#include "termpaintx.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <errno.h>
#include <stdbool.h>

#include <termpaint_compiler.h>

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

termpaint_integration *termpaint_full_integration() {
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

    return termpaint_full_integration_from_fd(fd, auto_close);
}


termpaint_integration *termpaint_full_integration_from_controlling_terminal() {
    int fd = -1;
    fd = open("/dev/tty", O_RDWR | O_NOCTTY | FD_CLOEXEC);
    if (fd == -1) {
        return nullptr;
    }
    return termpaint_full_integration_from_fd(fd, true);
}

typedef struct termpaint_integration_fd_ {
    termpaint_integration base;
    int fd;
    bool auto_close;
    bool awaiting_response;
} termpaint_integration_fd;

static void fd_free(termpaint_integration* integration) {
    if (FDPTR(integration)->auto_close && FDPTR(integration)->fd != -1) {
        close(FDPTR(integration)->fd);
    }
    free(integration);
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
    int written = 0;
    int ret;
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

static void fd_expect_response(struct termpaint_integration_ *integration) {
    FDPTR(integration)->awaiting_response = true;
}

termpaint_integration *termpaint_full_integration_from_fd(int fd, _Bool auto_close) {
    termpaint_integration_fd *ret = calloc(1, sizeof(termpaint_integration_fd));
    ret->base.free = fd_free;
    ret->base.write = fd_write;
    ret->base.flush = fd_flush;
    ret->base.is_bad = fd_is_bad;
    ret->base.expect_response = fd_expect_response;
    ret->fd = fd;
    ret->auto_close = auto_close;
    ret->awaiting_response = false;

    return (termpaint_integration*)ret;
}

_Bool termpaint_full_integration_poll_ready(termpaint_integration *integration) {
    UNUSED(integration); // TODO
    return false;
}
