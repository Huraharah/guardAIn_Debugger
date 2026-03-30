#pragma once

#include <string>
#include "Logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

// Connects to QEMU human monitor (e.g. tcp:127.0.0.1:10023) for clean shutdown and control.
class QemuMonitorClient
{
public:
    QemuMonitorClient(const std::string& host, int port);
    ~QemuMonitorClient();

    bool connectToServer();
    // Send a monitor command (e.g. "quit"). Newline is appended. Returns true if sent.
    bool sendCommand(const std::string& command);
    // Read one line of response. Returns false on error/EOF.
    bool readLine(std::string& out);
    void disconnect();

private:
    std::string m_host;
    int m_port;

#ifdef _WIN32
    SOCKET m_socket;
    bool   m_wsaInitialized;
#endif

    bool initSocket();
    void cleanup();
};
