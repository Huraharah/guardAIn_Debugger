#include "QmpClient.h"

#include <iostream>

QmpClient::QmpClient(const std::string& host, int port)
    : m_host(host),
    m_port(port)
#ifdef _WIN32
    , m_socket(INVALID_SOCKET),
    m_wsaInitialized(false)
#endif
{
}

QmpClient::~QmpClient()
{
    cleanup();
}

bool QmpClient::initSocket()
{
#ifdef _WIN32
    if (!m_wsaInitialized)
    {
        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != 0)
        {
            Logger::error("[QmpClient] WSAStartup failed: " + res);
            return false;
        }
        m_wsaInitialized = true;
    }

    return true;
#else
    return false;
#endif
}

bool QmpClient::connectToServer()
{
#ifdef _WIN32
    if (!initSocket())
        return false;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    std::string portStr = std::to_string(m_port);

    int res = getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &result);
    if (res != 0)
    {
        std::cerr << "[QmpClient] getaddrinfo failed: " << res << "\n";
        return false;
    }

    SOCKET sock = INVALID_SOCKET;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
    {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET)
            continue;

        res = ::connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen));
        if (res == SOCKET_ERROR)
        {
            closesocket(sock);
            sock = INVALID_SOCKET;
            continue;
        }

        // Success
        break;
    }

    freeaddrinfo(result);

    if (sock == INVALID_SOCKET)
    {
        Logger::error(std::string("[QmpClient] Unable to connect to QMP at ") + m_host + ":" + std::to_string(m_port));
        return false;
    }

    m_socket = sock;
    Logger::info(std::string("[QmpClient] Connected to QMP at ") + m_host + ":" + std::to_string(m_port));
    return true;
#else
    Logger::error("[QmpClient] connectToServer not implemented on this platform.");
    return false;
#endif
}

bool QmpClient::readMessage(std::string& out)
{
#ifdef _WIN32
    if (m_socket == INVALID_SOCKET)
        return false;

    out.clear();
    char buffer[512];

    while (true)
    {
        int bytes = recv(m_socket, buffer, sizeof(buffer), 0);
        if (bytes == 0)
        {
            // Connection closed
            return false;
        }
        if (bytes < 0)
        {
            Logger::error("[QmpClient] recv failed. Error=" + WSAGetLastError());
            return false;
        }

        for (int i = 0; i < bytes; ++i)
        {
            char c = buffer[i];
            out.push_back(c);
            if (c == '\n')
            {
                // Assume one message per line.
                return true;
            }
        }
    }
#else
    return false;
#endif
}

bool QmpClient::sendRaw(const std::string& message)
{
#ifdef _WIN32
    if (m_socket == INVALID_SOCKET)
        return false;

    const char* data = message.c_str();
    int total = static_cast<int>(message.size());
    int sent = 0;

    while (sent < total)
    {
        int res = send(m_socket, data + sent, total - sent, 0);
        if (res == SOCKET_ERROR)
        {
            Logger::error("[QmpClient] send failed. Error=" + WSAGetLastError());
            return false;
        }
        sent += res;
    }

    return true;
#else
    return false;
#endif
}

bool QmpClient::negotiateCapabilities()
{
    // Read initial greeting
    std::string greeting;
    if (!readMessage(greeting))
    {
        Logger::error("[QmpClient] Failed to read QMP greeting.");
        return false;
    }

    std::cout << "[QmpClient] Greeting: " << greeting;

    // Send qmp_capabilities
    std::string cmd = R"({"execute": "qmp_capabilities"})";
    cmd.push_back('\n');

    if (!sendRaw(cmd))
    {
        Logger::error("[QmpClient] Failed to send qmp_capabilities.");
        return false;
    }

    std::string response;
    if (!readMessage(response))
    {
        Logger::error("[QmpClient] Failed to read capabilities response.");
        return false;
    }

    Logger::info("[QmpClient] Capabilities response: " + response);
    return true;
}

bool QmpClient::queryStatus()
{
    std::string cmd = R"({"execute": "query-status"})";
    cmd.push_back('\n');

    if (!sendRaw(cmd))
    {
        Logger::error("[QmpClient] Failed to send query-status.");
        return false;
    }

    std::string response;
    if (!readMessage(response))
    {
        Logger::error("[QmpClient] Failed to read query-status response.");
        return false;
    }

    Logger::info("[QmpClient] query-status response: " + response);
    return true;
}

bool QmpClient::saveSnapshot(const std::string& name)
{
    // {"execute":"savevm","arguments":{"name":"snapshot_name"}}
    std::string cmd = R"({"execute": "savevm", "arguments": {"name": ")";
    cmd += name;
    cmd += R"("}})";
    cmd.push_back('\n');

    if (!sendRaw(cmd))
    {
        Logger::error("[QmpClient] Failed to send savevm.");
        return false;
    }

    std::string response;
    if (!readMessage(response))
    {
        Logger::error("[QmpClient] Failed to read savevm response.");
        return false;
    }

    Logger::info("[QmpClient] savevm response: " + response);
    return true;
}

bool QmpClient::loadSnapshot(const std::string& name)
{
    // {"execute":"loadvm","arguments":{"name":"snapshot_name"}}
    std::string cmd = R"({"execute": "loadvm", "arguments": {"name": ")";
    cmd += name;
    cmd += R"("}})";
    cmd.push_back('\n');

    if (!sendRaw(cmd))
    {
        Logger::error("[QmpClient] Failed to send loadvm.");
        return false;
    }

    std::string response;
    if (!readMessage(response))
    {
        Logger::error("[QmpClient] Failed to read loadvm response.");
        return false;
    }

    Logger::info("[QmpClient] loadvm response: " + response);
    return true;
}


void QmpClient::cleanup()
{
#ifdef _WIN32
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    if (m_wsaInitialized)
    {
        WSACleanup();
        m_wsaInitialized = false;
    }
#endif
}
