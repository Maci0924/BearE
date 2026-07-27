#pragma once

#include "common.hpp"

struct HydroCompileError {
    bool hasError;
    int line;
    int column;
    std::string message;

    HydroCompileError();
};

struct HydroCompileResult {
    bool success;
    std::string generatedCpp;
    std::vector<std::string> logLines;
    HydroCompileError error;

    HydroCompileResult();
};

HydroCompileResult hydroCompileSource(const std::string& source);
