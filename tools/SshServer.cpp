#include "SshServer.h"

/* Based on sample by Audrius Butkevicius */

#include <stdlib.h>
#include <sys/wait.h>
#include <stdio.h>
#include <malloc.h>

#include <libssh/callbacks.h>
#include <libssh/server.h>

#ifdef Q_CC_GNU
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#define container_of(ptr, type, member) ((type *)((char *)ptr - offsetof(type, member)))

bool pty_requested = false;
bool start_requested = false;

/* A userdata struct for session. */
struct session_data_struct {
    /* Pointer to the channel the session will allocate. */
    ssh_channel channel;
    int auth_attempts;
    int authenticated;
};

static int data_function(ssh_session session, ssh_channel channel, void *data,
                         uint32_t len, int is_stderr, void *userdata) {
    SshServer *cdata = (SshServer*) userdata;

    (void) session;
    (void) channel;
    (void) is_stderr;

    termpaint_terminal_add_input_data(cdata->terminal, (const char*)data, len);
    cdata->newInput = true;

    return len;
}

static int pty_request(ssh_session session, ssh_channel channel,
                       const char *term, int cols, int rows, int py, int px,
                       void *userdata) {
    pty_requested = true;
    return SSH_OK;
}

static int pty_resize(ssh_session session, ssh_channel channel, int cols,
                      int rows, int py, int px, void *userdata) {
    return SSH_OK;
}

static int exec_request(ssh_session session, ssh_channel channel,
                        const char *command, void *userdata) {

    (void) session;
    start_requested = true;
    return SSH_OK;
}

static int shell_request(ssh_session session, ssh_channel channel,
                         void *userdata) {
    (void) session;
    start_requested = true;
    return SSH_OK;
}

static int auth_none(ssh_session session, const char *user, void *userdata) {
    struct session_data_struct *sdata = (struct session_data_struct *) userdata;

    (void) session;

    sdata->authenticated = 1;
    return SSH_AUTH_SUCCESS;
}

static int auth_password(ssh_session session, const char *user,
                         const char *pass, void *userdata) {
    struct session_data_struct *sdata = (struct session_data_struct *) userdata;

    (void) session;

    sdata->authenticated = 1;
    return SSH_AUTH_SUCCESS;
}

static ssh_channel channel_open(ssh_session session, void *userdata) {
    struct session_data_struct *sdata = (struct session_data_struct *) userdata;

    sdata->channel = ssh_channel_new(session);
    return sdata->channel;
}

/* SIGCHLD handler for cleaning up dead children. */
static void sigchld_handler(int signo) {
    (void) signo;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

SshServer::SshServer(int port, std::string serverKeyFile)
    : port(port), serverKeyFile(serverKeyFile)
{
}

void SshServer::run() {
    ssh_bind sshbind;
    ssh_session session;
    ssh_event event;
    struct sigaction sa;

    /* Set up SIGCHLD handler. */
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
        fprintf(stderr, "Failed to register SIGCHLD handler\n");
        return;
    }

    ssh_init();
    sshbind = ssh_bind_new();

    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY,
                         serverKeyFile.c_str());

    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, std::to_string(port).c_str());

    if(ssh_bind_listen(sshbind) < 0) {
        fprintf(stderr, "%s\n", ssh_get_error(sshbind));
        return;
    }

    while (1) {
        session = ssh_new();
        if (session == NULL) {
            fprintf(stderr, "Failed to allocate session\n");
            continue;
        }

        /* Blocks until there is a new incoming connection. */
        if(ssh_bind_accept(sshbind, session) != SSH_ERROR) {
            switch(fork()) {
                case 0:
                    /* Remove the SIGCHLD handler inherited from parent. */
                    sa.sa_handler = SIG_DFL;
                    sigaction(SIGCHLD, &sa, NULL);
                    /* Remove socket binding, which allows us to restart the
                     * parent process, without terminating existing sessions. */
                    ssh_bind_free(sshbind);

                    event = ssh_event_new();
                    if (event != NULL) {
                        /* Blocks until the SSH session ends by either
                         * child process exiting, or client disconnecting. */
                        handleSession(event, session);
                        ssh_event_free(event);
                    } else {
                        fprintf(stderr, "Could not create polling context\n");
                    }
                    ssh_disconnect(session);
                    ssh_free(session);

                    exit(0);
                case -1:
                    fprintf(stderr, "Failed to fork\n");
            }
        } else {
            fprintf(stderr, "%s\n", ssh_get_error(sshbind));
        }
        /* Since the session has been passed to a child fork, do some cleaning
         * up at the parent process. */
        ssh_disconnect(session);
        ssh_free(session);
    }

    ssh_bind_free(sshbind);
    ssh_finalize();
}

void SshServer::outStr(const char *s) {
    ssh_channel_write(channel, s, strlen(s));
}

void SshServer::handleSession(ssh_event event, ssh_session session) {
    int n;

    struct session_data_struct sdata;
        sdata.channel = NULL;
        sdata.auth_attempts = 0;
        sdata.authenticated = 0;

    struct ssh_channel_callbacks_struct channel_cb { 0 };
        channel_cb.userdata = this;
        channel_cb.channel_pty_request_function = pty_request;
        channel_cb.channel_pty_window_change_function = pty_resize;
        channel_cb.channel_shell_request_function = shell_request;
        channel_cb.channel_exec_request_function = exec_request;
        channel_cb.channel_data_function = data_function;

    struct ssh_server_callbacks_struct server_cb { 0 };
        server_cb.userdata = &sdata;
        server_cb.auth_password_function = auth_password;
        server_cb.auth_none_function = auth_none;
        server_cb.channel_open_request_session_function = channel_open;

    ssh_callbacks_init(&server_cb);
    ssh_callbacks_init(&channel_cb);

    ssh_set_server_callbacks(session, &server_cb);

    if (ssh_handle_key_exchange(session) != SSH_OK) {
        fprintf(stderr, "%s\n", ssh_get_error(session));
        return;
    }

    ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_NONE | SSH_AUTH_METHOD_HOSTBASED);
    ssh_event_add_session(event, session);

    n = 0;
    while (sdata.authenticated == 0 || sdata.channel == NULL) {
        if (sdata.auth_attempts >= 3 || n >= 100) {
            return;
        }

        if (ssh_event_dopoll(event, 100) == SSH_ERROR) {
            fprintf(stderr, "%s\n", ssh_get_error(session));
            return;
        }
        n++;
    }

    channel = sdata.channel;

    memset(&integration, 0, sizeof(integration));

    auto free = [] (termpaint_integration* ptr) {
    };
    auto write = [] (termpaint_integration* ptr, const char *data, int length) {
        auto t = container_of(ptr, SshServer, integration);
        t->outputBuffer += std::string(data, length);
        if (t->outputBuffer.size() > 1000) {
            int written = ssh_channel_write(t->channel, t->outputBuffer.data(), t->outputBuffer.size());
            if (written >= 0) {
                t->outputBuffer.erase(t->outputBuffer.begin(), t->outputBuffer.begin() + written);
            }
        }
    };
    auto flush = [] (termpaint_integration* ptr) {
        auto t = container_of(ptr, SshServer, integration);
        while (t->outputBuffer.size() > 0) {
            int written = ssh_channel_write(t->channel, t->outputBuffer.data(), t->outputBuffer.size());
            if (written >= 0) {
                t->outputBuffer.erase(t->outputBuffer.begin(), t->outputBuffer.begin() + written);
            } else {
                break; // error
            }
        }
    };

    termpaint_integration_init(&integration, free, write, flush);

    termpaint_integration_set_is_bad(&integration, [] (termpaint_integration* ptr) {
        return false;
    });

    termpaint_integration_set_request_callback(&integration, [] (termpaint_integration* ptr) {
        auto t = container_of(ptr, SshServer, integration);
        t->callback_requested = true;
    });

    terminal = termpaint_terminal_new(&integration);
    //termpaint_auto_detect(surface);

    ssh_set_channel_callbacks(sdata.channel, &channel_cb);

    while (!start_requested && ssh_channel_is_open(sdata.channel)) {
        int res = ssh_event_dopoll(event, -1);
        if (res == SSH_ERROR) {
            ssh_channel_close(sdata.channel);
        }
    }

    main([&] () -> bool {
        newInput = false;
        flush(&integration);
        do {
            int timeout = -1;
            if (callback_requested) {
                 timeout = 100;
            }
            int res = ssh_event_dopoll(event, timeout);
            if (res == SSH_ERROR) {
                ssh_channel_close(sdata.channel);
            } else if (res == SSH_AGAIN) {
                callback_requested = false;
                termpaint_terminal_callback(terminal);
            }
        } while(ssh_channel_is_open(sdata.channel) && !newInput);
        return ssh_channel_is_open(sdata.channel);
    });

    ssh_channel_send_eof(sdata.channel);
    ssh_channel_close(sdata.channel);

    /* Wait up to 5 seconds for the client to terminate the session. */
    for (n = 0; n < 50 && (ssh_get_status(session) & (SSH_CLOSED | SSH_CLOSED_ERROR)) == 0; n++) {
        ssh_event_dopoll(event, 100);
    }
    termpaint_integration_deinit(&integration);
}
