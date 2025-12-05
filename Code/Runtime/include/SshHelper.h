#pragma once

#include <string>

class SshHelper
{
public:
    SshHelper(const std::string& host,
        const std::string& user,
        int port,
        const std::string& privateKeyPath);

    bool waitForSsh(int timeoutSeconds);
    bool runRemoteCommand(const std::string& remoteCommand);
    bool copyToGuest(const std::string& localPath,
        const std::string& remotePath);
    bool copyFromGuest(const std::string& remotePath,
        const std::string& localPath);

private:
    std::string m_host;
    std::string m_user;
    int         m_port;
    std::string m_privateKeyPath;

    std::string buildCommonSshOptions() const;
};
