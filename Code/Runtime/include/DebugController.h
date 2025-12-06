#pragma once
#include <string>
#include <vector>

// DebugController is reserved/stubbed for a future milestone where guardAInDBG
// will speak to gdbserver (or a custom debug stub) directly over a protocol
// suitable for LLM-driven interactive debugging. For Milestone 4, we drive
// gdb in batch mode inside the guest via SSH (see RuntimeManager::runDebugPass).

class DebugController
{
public:
    DebugController(const std::string& host, int port);

    bool connect();
    bool runScript(const std::vector<std::string>& commands, std::string& output);
    void disconnect();

private:
    std::string m_host;
    int m_port;
    int m_socket;   // raw TCP socket for gdbserver protocol
};
