#pragma once

#include "common.hpp"

std::string hydroExecutableDirectory();

bool hydroWriteTextFile(
    const std::string& path,
    const std::string& contents
);

bool hydroRunProcessCapture(
    const std::string& commandLine,
    const std::string& workingDirectory,
    std::string& outputText,
    DWORD& exitCode
);

bool hydroRunProgramVisible(
    const std::string& programPath,
    const std::string& workingDirectory,
    DWORD& windowsError
);
