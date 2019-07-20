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

int termpaintp_rescue_embedded(void);

int termpaint_ttyrescue_start(const char *restore_seq) {
    int pipe[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pipe) < 0) {
        return -1;
    }

    char *envvar = (char*)malloc(strlen(restore_seq) + 26);
    if (!envvar) {
        return -1;
    }
    strcpy(envvar, "TERMPAINT_RESCUE_RESTORE=");
    strcat(envvar, restore_seq);
    char *envp[] = {envvar, NULL};

    pid_t pid = fork();
    if (pid < 0) {
        free(envvar);
        return -1;
    } else if (pid) {
        free(envvar);
        close(pipe[0]);
        return pipe[1];
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
                memcpy((void*)argc_base, "ttyrescue (embedded)", 22);
            }
        }
#endif
        termpaintp_rescue_embedded();
        _exit(1);
    }
}

void termpaint_ttyrescue_stop(int fd) {
    if (fd >= 0) {
        send(fd, "~", 1, MSG_NOSIGNAL);
    }
}
