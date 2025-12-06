#include "DebugController.h"
#include <winsock2.h>

// DebugController is reserved/stubbed for a future milestone where guardAInDBG
// will speak to gdbserver (or a custom debug stub) directly over a protocol
// suitable for LLM-driven interactive debugging. For Milestone 4, we drive
// gdb in batch mode inside the guest via SSH (see RuntimeManager::runDebugPass).

bool DebugController::connect()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(m_port);
    server.sin_addr.s_addr = inet_addr(m_host.c_str());

    int result = ::connect(m_socket, (sockaddr*)&server, sizeof(server));
    return (result == 0);
}

bool DebugController::runScript(
    const std::vector<std::string>& commands,
    std::string& output)
{
    char buffer[4096];

    for (const auto& cmd : commands)
    {
        std::string line = cmd + "\n";

        send(m_socket, line.c_str(), (int)line.size(), 0);

        // read response chunk
        int bytes = recv(m_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0)
        {
            buffer[bytes] = '\0';
            output += buffer;
        }
    }

    return true;
}
