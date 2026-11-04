// =============================================================================
//  Abrasion Labs // OMNIS
//  import/HttpGet.cpp
// =============================================================================
#include "HttpGet.h"

#include <cctype>
#include <cstdio>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace omnis::import {

const std::string& userAgent() {
    static const std::string ua = "OMNIS/0.1 ( https://onirique.store )";
    return ua;
}

std::string urlEncode(const std::string& text) {
    std::string out;
    char b[4];
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else std::snprintf(b, sizeof b, "%%%02X", c), out += b;
    }
    return out;
}

namespace {

/// Une adresse sûre à passer en argument : https:// et caractères d'URL encodée.
bool safeUrl(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;
    for (unsigned char c : url)
        if (!(std::isalnum(c) || std::string_view("-._~:/?#[]@!$&'()*+,;=%").find(static_cast<char>(c)) != std::string_view::npos)) return false;
    return url.find('"') == std::string::npos && url.find(' ') == std::string::npos;
}

} // namespace

bool httpGet(const std::string& url, std::string& body, std::string& error, int timeoutSeconds) {
    body.clear();
    if (!safeUrl(url)) {
        error = "adresse refusée";
        return false;
    }
    const std::string timeout = std::to_string(timeoutSeconds);
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        error = "pas de tube";
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = nul;
    si.hStdInput = nullptr;
    // Adresse et identité sans guillemets ni espaces (vérifié) : rien à échapper.
    const std::string line = "curl.exe -sS -L --max-time " + timeout + " -A \"" + userAgent() + "\" \"" + url + "\"";
    std::wstring wide(line.begin(), line.end());
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) {
        CloseHandle(readPipe);
        error = "curl introuvable";
        return false;
    }
    char buf[8192];
    DWORD got = 0;
    while (ReadFile(readPipe, buf, sizeof buf, &got, nullptr) && got > 0) body.append(buf, got);
    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, static_cast<DWORD>((timeoutSeconds + 5) * 1000));
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        error = "curl a échoué (code " + std::to_string(code) + ")";
        return false;
    }
    return true;
#else
    int fds[2];
    if (pipe(fds) != 0) {
        error = "pas de tube";
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    std::vector<std::string> args{"curl", "-sS", "-L", "--max-time", timeout, "-A", userAgent(), url};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, "curl", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        error = "curl introuvable";
        return false;
    }
    char buf[8192];
    for (ssize_t n; (n = read(fds[0], buf, sizeof buf)) > 0;) body.append(buf, static_cast<std::size_t>(n));
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "curl a échoué";
        return false;
    }
    return true;
#endif
}

} // namespace omnis::import
