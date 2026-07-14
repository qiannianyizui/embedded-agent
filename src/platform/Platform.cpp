#include "Platform.h"
#include "common/io/FileSystem.h"
#include "common/base/StringUtil.h"
#include <unistd.h>
#include <sys/utsname.h>

namespace ea::platform {

static PlatformKind cached_kind = PlatformKind::Unknown;

PlatformKind detect() {
    if (cached_kind != PlatformKind::Unknown) return cached_kind;
    if (is_android()) { cached_kind = PlatformKind::Android; return cached_kind; }
    if (is_wsl()) { cached_kind = PlatformKind::Wsl; return cached_kind; }
    cached_kind = PlatformKind::Linux;
    return cached_kind;
}

bool is_android() {
    const char* termux = getenv("TERMUX_VERSION");
    if (termux && termux[0] != '\0') return true;
    return access("/system/bin/sh", X_OK) == 0;
}

bool is_linux() {
    struct utsname buf;
    if (uname(&buf) != 0) return false;
    return util::starts_with(buf.sysname, "Linux");
}

bool is_wsl() {
    auto content = fs::read_file("/proc/version");
    if (!content.ok()) return false;
    return content.value().find("microsoft") != std::string::npos ||
           content.value().find("WSL") != std::string::npos;
}

std::string home_dir() {
    auto r = fs::home_dir();
    return r.ok() ? r.value() : "/tmp";
}

std::string shell_path() {
    if (is_android()) return "/system/bin/sh";
    const char* shell = getenv("SHELL");
    if (shell && shell[0] != '\0') return shell;
    return "/bin/bash";
}

std::string tmp_dir() {
    const char* tmp = getenv("TMPDIR");
    if (tmp && tmp[0] != '\0') return tmp;
    if (is_android()) return home_dir() + "/tmp";
    return "/tmp";
}

bool has_shell_access() { return true; }
bool has_filesystem_access() { return true; }
bool supports_long_running() { return !is_android(); }

std::string platform_description() {
    struct utsname buf;
    uname(&buf);
    std::string desc = "Platform: ";
    switch (detect()) {
        case PlatformKind::Android: desc += "Android/Termux"; break;
        case PlatformKind::Wsl: desc += "WSL2"; break;
        case PlatformKind::Linux: desc += "Linux"; break;
        default: desc += "Unknown"; break;
    }
    desc += " | OS: " + std::string(buf.sysname);
    desc += " | Arch: " + std::string(buf.machine);
    desc += " | Shell: " + shell_path();
    return desc;
}

}  // namespace ea::platform
