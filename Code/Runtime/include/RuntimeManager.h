#pragma once

#include <string>

class RuntimeManager
{
public:
    RuntimeManager(const std::string& qemuExecutablePath,
                   const std::string& diskImagePath);

    // Milestone 3 v0: just bring the VM up, talk to QMP, shut it down.
    void runFirstPass(const std::string& samplePath);

private:
    std::string m_qemuExecutablePath;
    std::string m_diskImagePath;
};
