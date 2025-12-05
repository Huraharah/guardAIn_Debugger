#include "SshHelper.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>  // std::system

SshHelper::SshHelper(const std::string& host,
    const std::string& user,
    int port,
    const std::string& privateKeyPath)
    : m_host(host),
    m_user(user),
    m_port(port),
    m_privateKeyPath(privateKeyPath)
{
}

std::string SshHelper::buildCommonSshOptions() const
{
    // Shared between ssh and scp; port flag is added separately.
    std::string opts;
    opts += "-i \"" + m_privateKeyPath + "\" ";
    opts += "-o StrictHostKeyChecking=no ";
    opts += "-o UserKnownHostsFile=NUL ";
    opts += "-o BatchMode=yes ";
    return opts;
}

bool SshHelper::waitForSsh(int timeoutSeconds)
{
    std::cout << "[SshHelper] Waiting for SSH on " << m_host
        << ":" << m_port << " (timeout " << timeoutSeconds << "s)...\n";

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
            std::cout << "[SshHelper] SSH is reachable and key auth works.\n";
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMillis));
        waited += sleepMillis;
    }

    std::cerr << "[SshHelper] Timeout waiting for SSH.\n";
    return false;
}

bool SshHelper::runRemoteCommand(const std::string& remoteCommand)
{
    // ssh: -p for port
    std::string cmd = "ssh -p " + std::to_string(m_port) + " "
        + buildCommonSshOptions()
        + m_user + "@" + m_host + " \"" + remoteCommand + "\"";

    std::cout << "[SshHelper] Running remote command: " << cmd << "\n";
    int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        std::cerr << "[SshHelper] ssh command failed with code " << rc << "\n";
        return false;
    }
    return true;
}

bool SshHelper::copyToGuest(const std::string& localPath,
    const std::string& remotePath)
{
    // scp: -P (capital) for port
    std::string cmd = "scp -P " + std::to_string(m_port) + " "
        + buildCommonSshOptions()
        + localPath + " "
        + m_user + "@" + m_host + ":" + remotePath;

    std::cout << "[SshHelper] Copying to guest: " << cmd << "\n";
    int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        std::cerr << "[SshHelper] scp (to guest) failed with code " << rc << "\n";
        return false;
    }
    return true;
}

bool SshHelper::copyFromGuest(const std::string& remotePath,
    const std::string& localPath)
{
    // scp: -P (capital) for port
    std::string cmd = "scp -P " + std::to_string(m_port) + " "
        + buildCommonSshOptions()
        + m_user + "@" + m_host + ":" + remotePath + " "
        + localPath;

    std::cout << "[SshHelper] Copying from guest: " << cmd << "\n";
    int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        std::cerr << "[SshHelper] scp (from guest) failed with code " << rc << "\n";
        return false;
    }
    return true;
}