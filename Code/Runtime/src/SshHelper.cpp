#include "SshHelper.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>  // std::system

SshHelper::SshHelper(const RuntimeConfig& cfg)
    : m_host(cfg.sshHost),
    m_user(cfg.sshUser),
    m_port(cfg.sshPort),
    m_privateKeyPath(cfg.sshKeyPath)
{
}

std::string SshHelper::buildCommonSshOptions() const
{
    // Shared between ssh and scp; port flag is added separately.
    std::string opts;
    opts += "-i \"" + m_privateKeyPath + "\" ";
    opts += "-o StrictHostKeyChecking=no ";
	opts += "-o UserKnownHostsFile=A:\\ssh_known_hosts ";
    opts += "-o BatchMode=yes ";
    // Logger::debug("[SshHelper] Key File: " + m_privateKeyPath);     - [!] Bug squashed, retained for historical reference
    return opts;
}

bool SshHelper::waitForReady(int timeoutSeconds)
{
    Logger::info(std::string("[SshHelper] Waiting for SSH on ") + m_host + ":" + std::to_string(m_port) + " (timeout " + std::to_string(timeoutSeconds) + "s)...");

    const int sleepMillis = 2000;
    int waited = 0;

    while (waited < timeoutSeconds * 1000)
    {
        // ssh uses lowercase -p for port
        std::string cmd = "ssh -p " + std::to_string(m_port) + " "
            + buildCommonSshOptions()
            + m_user + "@" + m_host + " \"echo ready\"";

        int rc = std::system(cmd.c_str());
        if (rc == 0)
        {
            Logger::info("[SshHelper] SSH is reachable and key auth works.");
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMillis));
        waited += sleepMillis;
    }

    Logger::error("[SshHelper] Timeout waiting for SSH.");
    return false;
}

bool SshHelper::runRemote(const std::string& remoteCommand)
{
    // ssh: -p for port
    std::string cmd = "ssh -p " + std::to_string(m_port) + " "
        + buildCommonSshOptions()
        + m_user + "@" + m_host + " \"" + remoteCommand + "\"";

    Logger::info("[SshHelper] Running remote command: " + cmd);
    int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        Logger::error(std::string("[SshHelper] ssh command failed with code ") + std::to_string(rc));
        return false;
    }
    return true;
}

bool SshHelper::copyTo(const std::string& localPath,
    const std::string& remotePath)
{
    // scp: -P (capital) for port
    std::string cmd = "scp -P " + std::to_string(m_port) + " "
        + buildCommonSshOptions()
        + localPath + " "
        + m_user + "@" + m_host + ":" + remotePath;

    Logger::info("[SshHelper] Copying to guest: " + cmd);
    int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        Logger::error(std::string("[SshHelper] SCP (to guest) failed with code ") + std::to_string(rc));
        return false;
    }
    return true;
}

bool SshHelper::copyFrom(const std::string& remotePath,
    const std::string& localPath)
{
    // scp: -P (capital) for port
    std::string cmd = "scp -P " + std::to_string(m_port) + " "
        + buildCommonSshOptions()
        + m_user + "@" + m_host + ":" + remotePath + " "
        + localPath;

    Logger::info("[SshHelper] Copying from guest: " + cmd);
    int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        Logger::error(std::string("[SshHelper] scp (from guest) failed with code ") + std::to_string(rc));
        return false;
    }
    return true;
}