// SPDX-License-Identifier: BSL-1.0
#define _GNU_SOURCE
#include "termpaintx.h"

#ifdef USE_TK_DEBUGLOG
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

typedef struct termpaint_integration_fd_ {
    termpaint_integration base;
    char *options;
    // In rare situations, read and write MUST use different fds to be able to communicate with the terminal
    int fd_read;
    int fd_write;
    bool auto_close; // if true fd_read == fd_write is assumed
    struct termios original_terminal_attributes;
    bool callback_requested;
    bool awaiting_response;
    bool poll_sigwinch;
    bool inline_active;
    int inline_height;
    termpaint_terminal *terminal;
    termpaintx_ttyrescue *rescue;
} termpaint_integration_fd;

static bool sigwinch_set;
static int sigwinch_pipe[2];

static void termpaintx_sig_winch_pipe_handler(int sig, siginfo_t *info, void *ucontext) {
    (void)sig; (void)info; (void)ucontext;
    char dummy = ' ';
    (void)!write(sigwinch_pipe[1], &dummy, 1); // nothing much we can do, signal just gets lost then...
}

static void termpaintp_setup_winch(void) {
    if (!sigwinch_set) {
        bool ok = true;
#ifdef __linux__
        ok &= (pipe2(sigwinch_pipe, O_CLOEXEC | O_NONBLOCK) == 0);
#else
        ok &= (pipe(sigwinch_pipe) == 0);
        ok &= (fcntl(sigwinch_pipe[0], F_SETFD, FD_CLOEXEC) == 0);
        ok &= (fcntl(sigwinch_pipe[1], F_SETFD, FD_CLOEXEC) == 0);
        ok &= (fcntl(sigwinch_pipe[0], F_SETFL, O_NONBLOCK) == 0);
        ok &= (fcntl(sigwinch_pipe[1], F_SETFL, O_NONBLOCK) == 0);
#endif
        if (!ok) {
            return;
        }
        struct sigaction act;
        if (sigfillset(&act.sa_mask) != 0) {
            return;
        }
        act.sa_flags = SA_RESTART | SA_SIGINFO;
        act.sa_sigaction = &termpaintx_sig_winch_pipe_handler;

        if (sigaction(SIGWINCH, &act, NULL) != 0) {
            return;
        }

        sigwinch_set = true;
    }
}

static bool termpaintp_is_file_rw(int fd) {
    int ret = fcntl(fd, F_GETFL);
    return ret != -1 && (ret & O_ACCMODE) == O_RDWR;
}

static bool termpaintp_is_file_readable(int fd) {
    int ret = fcntl(fd, F_GETFL);
    return ret != -1 && ((ret & O_ACCMODE) == O_RDWR || (ret & O_ACCMODE) == O_RDONLY);
}

static bool termpaintp_is_file_writable(int fd) {
    int ret = fcntl(fd, F_GETFL);
    return ret != -1 && ((ret & O_ACCMODE) == O_RDWR || (ret & O_ACCMODE) == O_WRONLY);
}

static bool termpaintp_is_terminal_fd_pair(int fd_read, int fd_write) {
    if (isatty(fd_read) && isatty(fd_write)
            && termpaintp_is_file_readable(fd_read) && termpaintp_is_file_writable(fd_write)) {
        struct stat statbuf_r;
        struct stat statbuf_w;

        if (fstat(fd_read, &statbuf_r) == 0 && fstat(fd_write, &statbuf_w) == 0) {
            if (statbuf_r.st_rdev == statbuf_w.st_rdev
                    && statbuf_r.st_dev == statbuf_w.st_dev
                    && statbuf_r.st_ino == statbuf_w.st_ino) {
                return true;
            }
        }
    }
    return false;
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

    // as last resort check if (0, 1) or (0, 2) are matching read/write pairs pointing to the same terminal
    if (termpaintp_is_terminal_fd_pair(0, 1)) {
        return true;
    }

    if (termpaintp_is_terminal_fd_pair(0, 2)) {
        return true;
    }

    return false;
}

termpaint_integration *termpaintx_full_integration(const char *options) {
    int fd = -1;
    int fd_write = -1; // only if differs
    _Bool auto_close = false;
    bool might_be_controlling_terminal = true;

    if (isatty(0) && termpaintp_is_file_rw(0)) {
        fd = 0;
    } else if (isatty(1) && termpaintp_is_file_rw(1)) {
        fd = 1;
    } else if (isatty(2) && termpaintp_is_file_rw(2)) {
        fd = 2;
    } else {
        fd = open("/dev/tty", O_RDWR | O_NOCTTY | FD_CLOEXEC);
        if (fd != -1) {
            auto_close = true;
        } else {
            might_be_controlling_terminal = false;
            // as last resort check if (0, 1) or (0, 2) are matching read/write pairs pointing to the same terminal

            if (termpaintp_is_terminal_fd_pair(0, 1)) {
                fd = 0;
                fd_write = 1;
            } else if (termpaintp_is_terminal_fd_pair(0, 2)) {
                fd = 0;
                fd_write = 2;
            } else {
                return nullptr;
            }
        }
    }

    termpaint_integration *ret = (fd_write == -1) ? termpaintx_full_integration_from_fd(fd, auto_close, options)
                                                  : termpaintx_full_integration_from_fds(fd, fd_write, options);
    if (might_be_controlling_terminal) {
        termpaintp_setup_winch();
        FDPTR(ret)->poll_sigwinch = true;
    }
    return ret;
}


termpaint_integration *termpaintx_full_integration_from_controlling_terminal(const char *options) {
    int fd = -1;
    fd = open("/dev/tty", O_RDWR | O_NOCTTY | FD_CLOEXEC);
    if (fd == -1) {
        return nullptr;
    }
    termpaint_integration *ret = termpaintx_full_integration_from_fd(fd, true, options);
    termpaintp_setup_winch();
    FDPTR(ret)->poll_sigwinch = true;
    return ret;
}


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
            info.fd = fd_data->fd_read;
            info.events = POLLIN;
            ret = poll(&info, 1, 100 - time_waited_ms);
            if (ret == 1) {
                char buff[1000];
                int amount = (int)read(fd_data->fd_read, buff, 999);
                if (amount < 0) {
                    break;
                }
            }
        }
    }

    if (fd_data->rescue) {
        termpaintx_ttyrescue_stop(fd_data->rescue);
        fd_data->rescue = nullptr;
    }

    tcsetattr (fd_data->fd_read, TCSAFLUSH, &fd_data->original_terminal_attributes);
    if (fd_data->auto_close && fd_data->fd_read != -1) {
        // assumnes that auto_close will only be true if fd_read == fd_write
        close(fd_data->fd_read);
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
    FDPTR(integration)->fd_read = -1;
    FDPTR(integration)->fd_write = -1;
}

static _Bool fd_is_bad(termpaint_integration* integration) {
    return FDPTR(integration)->fd_read == -1;
}

static void fd_write_data(termpaint_integration* integration, const char *data, int length) {
    ssize_t written = 0;
    ssize_t ret;
    errno = 0;
    while (written != length) {
        ret = write(FDPTR(integration)->fd_write, data+written, length-written);
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

static bool termpaintp_fd_set_termios(int fd, const char *options) {
    struct termios tattr;
    if (tcgetattr(fd, &tattr) < 0) {
        return false;
    }
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

    if (tcsetattr (fd, TCSAFLUSH, &tattr) < 0) {
        return false;
    }
    return true;
}

bool termpaintx_fd_set_termios(int fd, const char *options) {
    return termpaintp_fd_set_termios(fd, options);
}

static termpaint_integration *termpaintp_full_integration_from_fds(int fd_read, int fd_write, _Bool auto_close, const char *options) {
    // NOTE: If fd_read != fd_write then auto_close must be false.
    termpaint_integration_fd *ret = calloc(1, sizeof(termpaint_integration_fd));
    termpaint_integration_init(&ret->base, fd_free, fd_write_data, fd_flush);
    termpaint_integration_set_is_bad(&ret->base, fd_is_bad);
    termpaint_integration_set_request_callback(&ret->base, fd_request_callback);
    termpaint_integration_set_awaiting_response(&ret->base, fd_awaiting_response);
    ret->options = strdup(options);
    ret->fd_read = fd_read;
    ret->fd_write = fd_write;
    ret->auto_close = auto_close;
    ret->callback_requested = false;
    ret->awaiting_response = false;

    tcgetattr(ret->fd_read, &ret->original_terminal_attributes);
    termpaintp_fd_set_termios(ret->fd_read, options);
    return (termpaint_integration*)ret;
}

termpaint_integration *termpaintx_full_integration_from_fd(int fd, _Bool auto_close, const char *options) {
    return termpaintp_full_integration_from_fds(fd, fd, auto_close, options);
}

termpaint_integration *termpaintx_full_integration_from_fds(int fd_read, int fd_write, const char *options) {
    return termpaintp_full_integration_from_fds(fd_read, fd_write, false, options);
}

void termpaintx_full_integration_wait_for_ready(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);
    while (termpaint_terminal_auto_detect_state(t->terminal) == termpaint_auto_detect_running) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            break;
        }
    }
}

void termpaintx_full_integration_wait_for_ready_with_message(termpaint_integration *integration, int milliseconds, const char* message) {
    termpaint_integration_fd *t = FDPTR(integration);
    while (termpaint_terminal_auto_detect_state(t->terminal) == termpaint_auto_detect_running) {
        if (milliseconds > 0) {
            if (!termpaintx_full_integration_do_iteration_with_timeout(integration, &milliseconds)) {
                // some kind of error
                break;
            }
            if (milliseconds <= 0) {
                fd_write_data(integration, message, strlen(message));
            }
        } else {
            if (!termpaintx_full_integration_do_iteration(integration)) {
                // some kind of error
                break;
            }
        }
    }
}

bool termpaintx_full_integration_terminal_size(termpaint_integration *integration, int *width, int *height) {
    if (fd_is_bad(integration) || !isatty(FDPTR(integration)->fd_read)) {
        return false;
    }
    return termpaintx_fd_terminal_size(FDPTR(integration)->fd_read, width, height);
}

void termpaintx_full_integration_set_terminal(termpaint_integration *integration, termpaint_terminal *terminal) {
    termpaint_integration_fd *t = FDPTR(integration);
    t->terminal = terminal;
}

static void termpaintp_handle_self_pipe(termpaint_integration_fd *t, struct pollfd *pfd) {
    if (pfd->revents == POLLIN) {
        // drain signaling pipe
        char buff[1000];
        int ret = read(sigwinch_pipe[0], buff, 999);
        if (ret < 0 && ret != EINTR && ret != EAGAIN && ret != EWOULDBLOCK) {
            // something broken, don't try again
            sigwinch_set = false;
        }
        int width, height;
        termpaintx_full_integration_terminal_size(&t->base, &width, &height);
        if (t->inline_active && t->inline_height && height > t->inline_height) {
            height = t->inline_height;
        }
        termpaint_surface* surface = termpaint_terminal_get_surface(t->terminal);
        termpaint_surface_resize(surface, width, height);
    } else {
        // something broken, don't try again
        sigwinch_set = false;
    }
}

bool termpaintx_full_integration_do_iteration(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);

    char buff[1000];
    if (t->poll_sigwinch && sigwinch_set) {
        struct pollfd info[2];
        info[0].fd = t->fd_read;
        info[0].events = POLLIN;
        info[1].fd = sigwinch_pipe[0];
        info[1].events = POLLIN;
        int ret = poll(info, 2, -1);
        if (ret < 0 && errno == EINTR) {
            return true;
        }
        if (ret > 0 && info[1].revents != 0) {
            termpaintp_handle_self_pipe(t, &info[1]);
            return true;
        }
    }
    int amount = (int)read(t->fd_read, buff, 999);
    if (amount < 0) {
        if (errno != EINTR && errno != EWOULDBLOCK) {
            return false;
        } else {
            return true;
        }
    }
    t->awaiting_response = false;
    termpaint_terminal_add_input_data(t->terminal, buff, amount);

    if (t->callback_requested) {
        t->callback_requested = false;
        struct pollfd info;
        info.fd = t->fd_read;
        info.events = POLLIN;
        int ret = poll(&info, 1, 100);
        if (ret == 1) {
            int amount = (int)read(t->fd_read, buff, 999);
            if (amount < 0) {
                if (errno != EINTR && errno != EWOULDBLOCK) {
                    return false;
                } else {
                    return true;
                }
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

    struct timespec start_time;
    clock_gettime(CLOCK_REALTIME, &start_time);

    int ret;
    {
        int count = 1;
        struct pollfd info[2];
        info[0].fd = t->fd_read;
        info[0].events = POLLIN;
        if (t->poll_sigwinch && sigwinch_set) {
            info[1].fd = sigwinch_pipe[0];
            info[1].events = POLLIN;
            ++count;
        }
        ret = poll(info, count, *milliseconds);
        if (ret < 0 && errno == EINTR) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            *milliseconds -= ((now.tv_sec - start_time.tv_sec) * 1000
                       + now.tv_nsec / 1000000 - start_time.tv_nsec / 1000000);
            return true;
        }
        if (count >= 2 && ret > 0 && info[1].revents != 0) {
            termpaintp_handle_self_pipe(t, &info[1]);
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            *milliseconds -= ((now.tv_sec - start_time.tv_sec) * 1000
                       + now.tv_nsec / 1000000 - start_time.tv_nsec / 1000000);
            return true;
        }
    }
    if (ret == 1) {
        int amount = (int)read(t->fd_read, buff, 999);
        if (amount < 0) {
            if (errno != EINTR && errno != EWOULDBLOCK) {
                return false;
            } else {
                return true;
            }
        }
        t->awaiting_response = false;
        termpaint_terminal_add_input_data(t->terminal, buff, amount);

        if (t->callback_requested) {
            t->callback_requested = false;

            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            long remaining = *milliseconds
                    - ((now.tv_sec - start_time.tv_sec) * 1000
                       + now.tv_nsec / 1000000 - start_time.tv_nsec / 1000000);
            if (remaining > 0) {
                struct pollfd info;
                info.fd = t->fd_read;
                info.events = POLLIN;
                ret = poll(&info, 1, remaining < 100 ? remaining : 100);
            }
            if (ret == 1) {
                int amount = (int)read(t->fd_read, buff, 999);
                if (amount < 0) {
                    if (errno != EINTR && errno != EWOULDBLOCK) {
                        return false;
                    } else {
                        return true;
                    }
                }
                t->awaiting_response = false;
                termpaint_terminal_add_input_data(t->terminal, buff, amount);
            }
            termpaint_terminal_callback(t->terminal);
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        *milliseconds -= (int)((now.tv_sec - start_time.tv_sec) * 1000
                               + now.tv_nsec / 1000000 - start_time.tv_nsec / 1000000);
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

void termpaintx_full_integration_apply_input_quirks(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);
    termpaint_terminal_auto_detect_apply_input_quirks(t->terminal, t->original_terminal_attributes.c_cc[VERASE] == 0x08);
}

static void fd_restore_sequence_updated(struct termpaint_integration_ *integration, const char *data, int length) {
    termpaint_integration_fd *t = FDPTR(integration);
    if (t->rescue) {
        termpaintx_ttyrescue_update(t->rescue, data, length);
    }
}

bool termpaintx_full_integration_ttyrescue_start(termpaint_integration *integration) {
    termpaint_integration_fd *t = FDPTR(integration);
    if (t->rescue || !t->terminal) return false;
    t->rescue = termpaintx_ttyrescue_start_or_nullptr(t->fd_write, termpaint_terminal_restore_sequence(t->terminal));
    if (t->rescue) {
        termpaint_integration_set_restore_sequence_updated(integration, fd_restore_sequence_updated);
        termpaintx_ttyrescue_set_restore_termios(t->rescue, &t->original_terminal_attributes);
        return true;
    }
    return false;
}

static termpaint_integration *termpaintp_full_integration_setup_terminal_common(const char *options,
                                                                             void (*event_handler)(void *, termpaint_event *),
                                                                             void *event_handler_user_data,
                                                                             termpaint_terminal **terminal_out) {
    termpaint_integration *integration = termpaintx_full_integration(options);
    if (!integration) {
        const char* error = "Error: Terminal not available!";
        (void)!write(1, error, strlen(error)); // already printing an error message
        return nullptr;
    }

    termpaint_terminal *terminal = termpaint_terminal_new(integration);
    termpaintx_full_integration_set_terminal(integration, terminal);
    termpaint_terminal_set_event_cb(terminal, event_handler, event_handler_user_data);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready_with_message(integration, 10000,
                                           "Terminal auto detection is taking unusually long, press space to abort.");
    termpaintx_full_integration_apply_input_quirks(integration);
    *terminal_out = terminal;
    return integration;
}

termpaint_integration *termpaintx_full_integration_setup_terminal_fullscreen(const char *options,
                                                                             void (*event_handler)(void *, termpaint_event *),
                                                                             void *event_handler_user_data,
                                                                             termpaint_terminal **terminal_out) {
    termpaint_terminal *terminal;
    termpaint_integration *integration = termpaintp_full_integration_setup_terminal_common(options,
                                                                                           event_handler,
                                                                                           event_handler_user_data,
                                                                                           &terminal);
    if (!integration) {
        return nullptr;
    }

    int width, height;
    termpaintx_full_integration_terminal_size(integration, &width, &height);
    termpaint_terminal_setup_fullscreen(terminal, width, height, options);
    termpaintx_full_integration_ttyrescue_start(integration);
    *terminal_out = terminal;
    return integration;
}


termpaint_integration *termpaintx_full_integration_setup_terminal_inline(const char *options,
                                                                         int lines,
                                                                         void (*event_handler)(void *, termpaint_event *),
                                                                         void *event_handler_user_data,
                                                                         termpaint_terminal **terminal_out) {
    termpaint_terminal *terminal;
    termpaint_integration *integration = termpaintp_full_integration_setup_terminal_common(options,
                                                                                           event_handler,
                                                                                           event_handler_user_data,
                                                                                           &terminal);
    if (!integration) {
        return nullptr;
    }
    termpaint_integration_fd* fd_data = FDPTR(integration);
    fd_data->inline_height = lines;
    fd_data->inline_active = true;
    int width, height;
    termpaintx_full_integration_terminal_size(integration, &width, &height);
    if (height > lines) {
        height = lines;
    }
    termpaint_terminal_setup_inline(terminal, width, height, options);
    termpaintx_full_integration_ttyrescue_start(integration);
    *terminal_out = terminal;
    return integration;
}

void termpaintx_full_integration_set_inline(termpaint_integration *integration, _Bool enabled, int height) {
    termpaint_integration_fd* fd_data = FDPTR(integration);

    if (height > 0) {
        fd_data->inline_height = height;
    }

    termpaint_terminal_set_inline(fd_data->terminal, enabled);
    fd_data->inline_active = enabled;

    int term_width, term_height;
    termpaintx_full_integration_terminal_size(integration, &term_width, &term_height);
    if (fd_data->inline_active && fd_data->inline_height && term_height > fd_data->inline_height) {
        term_height = fd_data->inline_height;
    }
    termpaint_surface* surface = termpaint_terminal_get_surface(fd_data->terminal);
    termpaint_surface_resize(surface, term_width, term_height);
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
