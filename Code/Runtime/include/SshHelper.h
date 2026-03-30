#pragma once

#include <string>
#include "Logger.h"
#include "RuntimeConfig.h"

class SshHelper
{
public:
    SshHelper(const RuntimeConfig& cfg_);

    bool waitForReady(int timeoutSeconds);
    bool runRemote(const std::string& remoteCommand);
    // Run remote command and capture stdout into output (for watchdog polling). Returns true if ssh exited 0.
    bool runRemoteGetOutput(const std::string& remoteCommand, std::string& output);
    bool copyTo(const std::string& localPath,
        const std::string& remotePath);
    bool copyFrom(const std::string& remotePath,
        const std::string& localPath);

private:
	RuntimeConfig cfg_;

    std::string m_host;
    std::string m_user;
    int         m_port;
    std::string m_privateKeyPath;

    std::string buildCommonSshOptions() const;
};
