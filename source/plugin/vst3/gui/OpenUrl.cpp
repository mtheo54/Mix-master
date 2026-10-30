// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/gui/OpenUrl.cpp
// =============================================================================
#include "OpenUrl.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
#include <unistd.h>
extern char** environ;
#endif
#endif

namespace omnis::vst3::gui {

bool openUrl(const std::string& url) {
    const bool safe = url.rfind("https://", 0) == 0 && url.size() <= 2000 && std::all_of(url.begin(), url.end(), [](unsigned char c) {
        return std::isalnum(c) || std::string_view("-._~:/?=&%+#").find(static_cast<char>(c)) != std::string_view::npos;
    });
    if (!safe) return false;
#if defined(_WIN32)
    return reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
#if defined(__APPLE__)
    const char* tool = "/usr/bin/open";
    char** env = *_NSGetEnviron(); // une bibliothèque (le plug-in) n'a pas « environ » sur macOS
#else
    const char* tool = "xdg-open";
    char** env = environ;
#endif
    std::string program = tool, arg = url;
    char* argv[] = {program.data(), arg.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, tool, nullptr, nullptr, argv, env) != 0) return false;
    // Le navigateur garde la main ; on récupère le lanceur pour ne pas laisser de zombie.
    std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();
    return true;
#endif
}

} // namespace omnis::vst3::gui
