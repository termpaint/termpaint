#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    write(2, s, strlen(s));
}

#ifndef TERMPAINT_RESCUE_EMBEDDED
int main(int argc, char** argv) {
    (void) argc; (void) argv;
    void *ctlseg = nullptr;
#else
int termpaintp_rescue_embedded(void *ctlseg) {
#endif
    restore = getenv("TTYRESCUE_RESTORE");

    if (!restore || *restore==0) {
        output("This is an internal helper to ensure that the terminal is properly restored.\n");
        output("There should be no need to call this manually.\n");
        return 0;
    }

    int res;
    res = isatty(0);
    if (res || (errno != EINVAL && errno != ENOTTY)) {
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
        atomic_fetch_or((atomic_int*)ctlseg + 1, 1);
#endif
        write(0, "x", 1);
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
                offset = atomic_load((atomic_int*)ctlseg);
            }
            if (offset) {
                output((char*)ctlseg + offset);
            } else {
                output(restore);
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
