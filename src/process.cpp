#include "process.hpp"

std::string hydroExecutableDirectory() {
    char buffer[MAX_PATH];

    DWORD length = GetModuleFileNameA(
        NULL,
        buffer,
        MAX_PATH
    );

    if (length == 0 || length >= MAX_PATH) {
        return ".";
    }

    std::string path(buffer, length);
    std::size_t slash = path.find_last_of("\\/");

    if (slash == std::string::npos) {
        return ".";
    }

    return path.substr(0, slash);
}

bool hydroWriteTextFile(
    const std::string& path,
    const std::string& contents
) {
    std::ofstream output(
        path.c_str(),
        std::ios::binary
    );

    if (!output.is_open()) {
        return false;
    }

    output << contents;
    return true;
}

bool hydroRunProcessCapture(
    const std::string& commandLine,
    const std::string& workingDirectory,
    std::string& outputText,
    DWORD& exitCode
) {
    SECURITY_ATTRIBUTES security;
    ZeroMemory(&security, sizeof(security));

    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readPipe = NULL;
    HANDLE writePipe = NULL;

    if (!CreatePipe(
        &readPipe,
        &writePipe,
        &security,
        0
    )) {
        outputText =
            "Nem sikerult letrehozni a csatornat.";

        return false;
    }

    SetHandleInformation(
        readPipe,
        HANDLE_FLAG_INHERIT,
        0
    );

    STARTUPINFOA startup;
    PROCESS_INFORMATION process;

    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));

    startup.cb = sizeof(startup);
    startup.dwFlags =
        STARTF_USESTDHANDLES |
        STARTF_USESHOWWINDOW;

    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput =
        GetStdHandle(STD_INPUT_HANDLE);

    startup.wShowWindow = SW_HIDE;

    std::vector<char> mutableCommand(
        commandLine.begin(),
        commandLine.end()
    );

    mutableCommand.push_back('\0');

    BOOL created = CreateProcessA(
        NULL,
        &mutableCommand[0],
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        workingDirectory.c_str(),
        &startup,
        &process
    );

    CloseHandle(writePipe);

    if (!created) {
        DWORD errorCode = GetLastError();
        std::stringstream message;

        message
            << "A folyamat nem indult el. Windows hibakod: "
            << errorCode;

        outputText = message.str();

        CloseHandle(readPipe);
        return false;
    }

    outputText.clear();

    char buffer[4096];
    DWORD bytesRead = 0;

    while (
        ReadFile(
            readPipe,
            buffer,
            sizeof(buffer),
            &bytesRead,
            NULL
        ) &&
        bytesRead > 0
    ) {
        outputText.append(buffer, bytesRead);
    }

    WaitForSingleObject(
        process.hProcess,
        INFINITE
    );

    if (!GetExitCodeProcess(
        process.hProcess,
        &exitCode
    )) {
        exitCode = 1;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);

    return true;
}

bool hydroRunProgramVisible(
    const std::string& programPath,
    const std::string& workingDirectory,
    DWORD& windowsError
) {
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;

    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));

    startup.cb = sizeof(startup);

    std::string command =
        "cmd.exe /k \"\"" +
        programPath +
        "\" & echo. & echo A program befejezodott. & pause\"";

    std::vector<char> mutableCommand(
        command.begin(),
        command.end()
    );

    mutableCommand.push_back('\0');

    BOOL created = CreateProcessA(
        NULL,
        &mutableCommand[0],
        NULL,
        NULL,
        FALSE,
        CREATE_NEW_CONSOLE,
        NULL,
        workingDirectory.c_str(),
        &startup,
        &process
    );

    if (!created) {
        windowsError = GetLastError();
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    windowsError = 0;
    return true;
}
