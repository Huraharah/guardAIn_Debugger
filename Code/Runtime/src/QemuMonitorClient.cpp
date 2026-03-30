#include "QemuMonitorClient.h"

#ifdef _WIN32
#include <iostream>
#endif

QemuMonitorClient::QemuMonitorClient(const std::string& host, int port)
    : m_host(host),
      m_port(port)
#ifdef _WIN32
    , m_socket(INVALID_SOCKET),
      m_wsaInitialized(false)
#endif
{
}

QemuMonitorClient::~QemuMonitorClient()
{
    cleanup();
}

bool QemuMonitorClient::initSocket()
{
#ifdef _WIN32
    if (!m_wsaInitialized)
    {
        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != 0)
        {
            Logger::error("[QemuMonitorClient] WSAStartup failed.");
            return false;
        }
        m_wsaInitialized = true;
    }
    return true;
#else
    return false;
#endif
}

void QemuMonitorClient::cleanup()
{
#ifdef _WIN32
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
#endif
}

void QemuMonitorClient::disconnect()
{
    cleanup();
}

bool QemuMonitorClient::connectToServer()
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
        Logger::error("[QemuMonitorClient] getaddrinfo failed for " + m_host + ":" + portStr);
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
        break;
    }

    freeaddrinfo(result);

    if (sock == INVALID_SOCKET)
    {
        Logger::error("[QemuMonitorClient] Unable to connect to QEMU monitor at " + m_host + ":" + portStr);
        return false;
    }

    m_socket = sock;
    Logger::info("[QemuMonitorClient] Connected to QEMU monitor at " + m_host + ":" + portStr);
    return true;
#else
    Logger::error("[QemuMonitorClient] connectToServer not implemented on this platform.");
    return false;
#endif
}

bool QemuMonitorClient::sendCommand(const std::string& command)
{
#ifdef _WIN32
    if (m_socket == INVALID_SOCKET)
    {
        Logger::warn("[QemuMonitorClient] sendCommand called but not connected.");
        return false;
    }
    std::string line = command;
    if (line.empty() || line.back() != '\n')
        line += "\n";
    const char* data = line.c_str();
    int total = static_cast<int>(line.size());
    int sent = 0;
    while (sent < total)
    {
        int res = send(m_socket, data + sent, total - sent, 0);
        if (res == SOCKET_ERROR)
        {
            Logger::error("[QemuMonitorClient] send failed.");
            return false;
        }
        sent += res;
    }
    Logger::debug("[QemuMonitorClient] Sent command: " + command);
    return true;
#else
    return false;
#endif
}

bool QemuMonitorClient::readLine(std::string& out)
{
#ifdef _WIN32
    if (m_socket == INVALID_SOCKET)
        return false;
    out.clear();
    char c;
    while (recv(m_socket, &c, 1, 0) == 1)
    {
        if (c == '\n' || c == '\r')
            return true;
        out += c;
    }
    return false;
#else
    return false;
#endif
}
