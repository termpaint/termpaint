#ifndef TERMPAINT_SAMPLE_SSHSERVER_INCLUDED
#define TERMPAINT_SAMPLE_SSHSERVER_INCLUDED

#include <functional>

#include <libssh/server.h>

#include <termpaint.h>
#include <termpaint_input.h>

extern bool pty_requested;

class SshServer {
public:
    SshServer(int port, std::string serverKeyFile);

    void run();

    virtual int main(std::function<bool()> poll) = 0;

    void outStr(const char *s);

    termpaint_terminal *terminal = nullptr;

private:
    void handleSession(ssh_event event, ssh_session session);

    termpaint_integration integration;
    ssh_channel channel;
    std::string outputBuffer;
    int port;
    std::string serverKeyFile;
    bool callback_requested = false;

public:
    bool newInput = false;
};

#endif
