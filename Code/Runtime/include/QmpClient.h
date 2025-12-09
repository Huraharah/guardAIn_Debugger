#pragma once

#include <string>
#include "Logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

class QmpClient
{
public:
    QmpClient(const std::string& host, int port);
    ~QmpClient();

    bool connectToServer();

    // Read a single line (one QMP JSON message). Returns false on error/EOF.
    bool readMessage(std::string& out);

    // Send a raw QMP JSON command string (must end with '\n').
    bool sendRaw(const std::string& message);

    // Convenience helpers:
    bool negotiateCapabilities();
    bool queryStatus();

	// Snapshot management
    bool saveSnapshot(const std::string& name);
    bool loadSnapshot(const std::string& name);

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
