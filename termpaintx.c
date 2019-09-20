#define _GNU_SOURCE
#include "termpaintx.h"

#ifdef USE_TK_DEBUGLOG
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#endif

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

#include <termpaint_compiler.h>
#include <termpaintx_ttyrescue.h>

#if __GNUC__
// Trying to avoid this warning with e.g. bit manipulations using defines from the standard headers is just too ugly
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#ifndef nullptr
#define nullptr ((void*)0)
#endif

#define FDPTR(var) ((termpaint_integration_fd*)var)

bool termpaintp_is_file_rw(int fd) {
    int ret = fcntl(fd, F_GETFL);
    return ret != -1 && (ret & O_ACCMODE) == O_RDWR;
}

_Bool termpaintx_full_integration_available(void) {
    _Bool from_std_fd = false;
    from_std_fd = (isatty(0) && termpaintp_is_file_rw(0))
            || (isatty(1) && termpaintp_is_file_rw(1))
            || (isatty(2) && termpaintp_is_file_rw(2));
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

termpaint_integration *termpaintx_full_integration(const char *options) {
    int fd = -1;
    _Bool auto_close = false;

    if (isatty(0) && termpaintp_is_file_rw(0)) {
        fd = 0;
    } else if (isatty(1) && termpaintp_is_file_rw(1)) {
        fd = 1;
    } else if (isatty(2) && termpaintp_is_file_rw(2)) {
        fd = 2;
    } else {
        fd = open("/dev/tty", O_RDWR | O_NOCTTY | FD_CLOEXEC);
        auto_close = true;
        if (fd == -1) {
            return nullptr;
        }
    }

    return termpaintx_full_integration_from_fd(fd, auto_close, options);
}


termpaint_integration *termpaintx_full_integration_from_controlling_terminal(const char *options) {
    int fd = -1;
    fd = open("/dev/tty", O_RDWR | O_NOCTTY | FD_CLOEXEC);
    if (fd == -1) {
        return nullptr;
    }
    return termpaintx_full_integration_from_fd(fd, true, options);
}

typedef struct termpaint_integration_fd_ {
    termpaint_integration base;
    char *options;
    int fd;
    bool auto_close;
    struct termios original_terminal_attributes;
    bool callback_requested;
    bool awaiting_response;
    termpaint_terminal *terminal;
    termpaintx_ttyrescue *rescue;
} termpaint_integration_fd;

static void fd_free(termpaint_integration* integration) {
    termpaint_integration_fd* fd_data = FDPTR(integration);
    // If terminal auto detection or another operation with response is cut short
    // by a close the reponse will leak out into the next application.
    // We can't reliably prevent that here, but this kludge can reduce the likelyhood
    // by just discarding input for a short amount of time.
    if (fd_data->awaiting_response) {
        struct timespec start_time;
        clock_gettime(CLOCK_REALTIME, &start_time);
        while (true) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            long time_waited_ms = (now.tv_sec - start_time.tv_sec) * 1000
                    + now.tv_nsec / 1000000 - start_time.tv_nsec / 1000000;
            if (time_waited_ms >= 100 || time_waited_ms < 0) {
                break;
            }
            int ret;
            struct pollfd info;
            info.fd = fd_data->fd;
            info.events = POLLIN;
            ret = poll(&info, 1, 100 - time_waited_ms);
            if (ret == 1) {
                char buff[1000];
                int amount = (int)read(fd_data->fd, buff, 999);
                if (amount < 0) {
                    break;
                }
            }
        }
    }

    if (fd_data->rescue) {
        termpaint_ttyrescue_stop(fd_data->rescue);
        fd_data->rescue = nullptr;
    }

    tcsetattr (fd_data->fd, TCSAFLUSH, &fd_data->original_terminal_attributes);
    if (fd_data->auto_close && fd_data->fd != -1) {
        close(fd_data->fd);
    }
    free(fd_data->options);
    termpaint_integration_deinit(&fd_data->base);
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
    FDPTR(integration)->callback_requested = true;
}

static void fd_awaiting_response(struct termpaint_integration_ *integration) {
    FDPTR(integration)->awaiting_response = true;
}

static bool termpaintp_has_option(const char *options, const char *name) {
    const char *p = options;
    int name_len = strlen(name);
    while (1) {
        const char *found = strstr(p, name);
        if (!found) {
            break;
        }
        if (found == options || found[-1] == ' ') {
            if (found[name_len] == 0 || found[name_len] == ' ') {
                return true;
            }
        }
        p = found + name_len;
    }
    return false;
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

    bool allow_interrupt = termpaintp_has_option(options, "+kbdsigint");
    bool allow_quit = termpaintp_has_option(options, "+kbdsigquit");
    bool allow_suspend = termpaintp_has_option(options, "+kbdsigtstp");

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

bool termpaintx_fd_set_termios(int fd, const char *options) {
    termpaintp_fd_set_termios(fd, options);
    return true;
}

termpaint_integration *termpaintx_full_integration_from_fd(int fd, _Bool auto_close, const char *options) {
    termpaint_integration_fd *ret = calloc(1, sizeof(termpaint_integration_fd));
    termpaint_integration_init(&ret->base, fd_free, fd_write, fd_flush);
    termpaint_integration_set_is_bad(&ret->base, fd_is_bad);
    termpaint_integration_set_request_callback(&ret->base, fd_request_callback);
    termpaint_integration_set_awaiting_response(&ret->base, fd_awaiting_response);
    ret->options = strdup(options);
    ret->fd = fd;
    ret->auto_close = auto_close;
    ret->callback_requested = false;
    ret->awaiting_response = false;

    tcgetattr(ret->fd, &ret->original_terminal_attributes);
    termpaintp_fd_set_termios(ret->fd, options);
    return (termpaint_integration*)ret;
}

bool termpaintx_full_integration_wait_for_ready(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);
    while (termpaint_terminal_auto_detect_state(t->terminal) == termpaint_auto_detect_running) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            break;
        }
    }
    return false;
}

bool termpaintx_full_integration_wait_for_ready_with_message(termpaint_integration *integration, int milliseconds, char* message) {
    termpaint_integration_fd *t = FDPTR(integration);
    while (termpaint_terminal_auto_detect_state(t->terminal) == termpaint_auto_detect_running) {
        if (milliseconds > 0) {
            if (!termpaintx_full_integration_do_iteration_with_timeout(integration, &milliseconds)) {
                // some kind of error
                break;
            }
            if (milliseconds <= 0) {
                fd_write(integration, message, strlen(message));
            }
        } else {
            if (!termpaintx_full_integration_do_iteration(integration)) {
                // some kind of error
                break;
            }
        }
    }
    return false;
}

bool termpaintx_full_integration_terminal_size(termpaint_integration *integration, int *width, int *height) {
    if (fd_is_bad(integration) || !isatty(FDPTR(integration)->fd)) {
        return false;
    }
    return termpaintx_fd_terminal_size(FDPTR(integration)->fd, width, height);
}

void termpaintx_full_integration_set_terminal(termpaint_integration *integration, termpaint_terminal *terminal) {
    termpaint_integration_fd *t = FDPTR(integration);
    t->terminal = terminal;
}

bool termpaintx_full_integration_do_iteration(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);

    char buff[1000];
    int amount = (int)read(t->fd, buff, 999);
    if (amount < 0) {
        return false;
    }
    t->awaiting_response = false;
    termpaint_terminal_add_input_data(t->terminal, buff, amount);

    if (t->callback_requested) {
        t->callback_requested = false;
        struct pollfd info;
        info.fd = t->fd;
        info.events = POLLIN;
        int ret = poll(&info, 1, 100);
        if (ret == 1) {
            int amount = (int)read(t->fd, buff, 999);
            if (amount < 0) {
                return false;
            }
            t->awaiting_response = false;
            termpaint_terminal_add_input_data(t->terminal, buff, amount);
        }
        termpaint_terminal_callback(t->terminal);
    }

    return true;
}


bool termpaintx_full_integration_do_iteration_with_timeout(termpaint_integration *integration, int *milliseconds) {
    termpaint_integration_fd *t = FDPTR(integration);

    char buff[1000];

    time_t start_time = time(nullptr);

    int ret;
    {
        struct pollfd info;
        info.fd = t->fd;
        info.events = POLLIN;
        ret = poll(&info, 1, *milliseconds);
    }
    if (ret == 1) {
        int amount = (int)read(t->fd, buff, 999);
        if (amount < 0) {
            return false;
        }
        t->awaiting_response = false;
        termpaint_terminal_add_input_data(t->terminal, buff, amount);

        if (t->callback_requested) {
            t->callback_requested = false;
            int remaining = *milliseconds - (int)(1000 * difftime(time(nullptr), start_time));
            if (remaining > 0) {
                struct pollfd info;
                info.fd = t->fd;
                info.events = POLLIN;
                ret = poll(&info, 1, remaining < 100 ? remaining : 100);
            }
            if (ret == 1) {
                int amount = (int)read(t->fd, buff, 999);
                if (amount < 0) {
                    return false;
                }
                t->awaiting_response = false;
                termpaint_terminal_add_input_data(t->terminal, buff, amount);
            }
            termpaint_terminal_callback(t->terminal);
        }
        *milliseconds -= (int)(1000 * difftime(time(nullptr), start_time));
    } else {
        *milliseconds = 0;
    }

    return true;
}

bool termpaintx_fd_terminal_size(int fd, int *width, int *height) {
    struct winsize s;
    if (ioctl(fd, TIOCGWINSZ, &s) < 0) {
        return false;
    }
    *width = s.ws_col;
    *height = s.ws_row;
    return true;
}

const struct termios *termpaintx_full_integration_original_terminal_attributes(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);
    return &t->original_terminal_attributes;
}

static void fd_restore_sequence_updated(struct termpaint_integration_ *integration, const char *data, int length) {
    termpaint_integration_fd *t = FDPTR(integration);
    if (t->rescue) {
        termpaint_ttyrescue_update(t->rescue, data, length);
    }
}

bool termpaint_full_integration_ttyrescue_start(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);
    if (t->rescue || !t->terminal) return false;
    t->rescue = termpaint_ttyrescue_start(t->fd, termpaint_terminal_restore_sequence(t->terminal));
    if (t->rescue) {
        termpaint_integration_set_restore_sequence_updated(integration, fd_restore_sequence_updated);
        termpaint_ttyrescue_set_restore_termios(t->rescue, &t->original_terminal_attributes);
        return true;
    }
    return false;
}


static void termpaintx_dummy_log(struct termpaint_integration_ *integration, char *data, int length) {
    (void)integration;
    (void)data;
    (void)length;
}

#ifdef USE_TK_DEBUGLOG

#include "debugwin.py.inc"

static int logfd = -1;

static void termpaintx_fd_log(struct termpaint_integration_ *integration, char *data, int length) {
    (void)integration;
    write(logfd, data, length);
}


extern char **environ;

termpaint_logging_func termpaintx_enable_tk_logging(void) {

    if (logfd >= 0) {
        return termpaintx_fd_log;
    }

    int pipeends[2];

    if (pipe2(pipeends, O_CLOEXEC) < 0) {
        return termpaintx_dummy_log;
    }
    logfd = pipeends[1];

    int readend = pipeends[0];

    posix_spawnattr_t attr;
    posix_spawn_file_actions_t file_actions;

    posix_spawn_file_actions_init(&file_actions);
    if (readend != 1) {
        posix_spawn_file_actions_addclose(&file_actions, 1);
    }
    if (readend != 2) {
        posix_spawn_file_actions_addclose(&file_actions, 2);
    }
    if (readend != 0) {
        posix_spawn_file_actions_addclose(&file_actions, 0);
        posix_spawn_file_actions_adddup2(&file_actions, readend, 0);
    }

    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);
    sigset_t mask;
    sigfillset(&mask);
    posix_spawnattr_getsigdefault(&attr, &mask);

    char * argv[] = {
        "python3",
        "-E",
        "-c", (char*)debugwin,
        nullptr
    };

    posix_spawnp(nullptr, "python3", &file_actions, &attr,
                     argv, environ);

    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&attr);

    return termpaintx_fd_log;
}
#else
termpaint_logging_func termpaintx_enable_tk_logging(void) {
    return termpaintx_dummy_log;
}
#endif
