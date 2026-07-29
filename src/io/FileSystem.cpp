#include "FileSystem.h"
#include "log/Logger.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ea::fs {

// Runtime detection: same binary works on Linux/WSL/Termux/Android
// without recompilation. Build-time #ifdef can't distinguish Termux
// from native Android — both are Linux.
static bool is_android_runtime() {
    return access("/system/bin/sh", X_OK) == 0;
}

static std::string android_home_fallback() {
    if (const char* termux_home = getenv("TERMUX_HOME")) {
        if (termux_home[0] != '\0') return std::string(termux_home);
    }
    // Native Android: /data/local/tmp is world-writable and survives
    // across app restarts for adb-pushed binaries.
    return "/data/local/tmp";
}

std::string expand_tilde(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    auto home = home_dir();
    if (!home.ok()) return path;
    if (path.size() == 1) return home.value();
    if (path[1] == '/') return home.value() + path.substr(1);
    return path;
}

Result<std::string> home_dir() {
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') return std::string(home);

    // Native Android doesn't set HOME — probe runtime and use a
    // writable data path instead of failing outright.
    if (is_android_runtime()) {
        return android_home_fallback();
    }

    return Error::io("HOME not set and no Android home directory detected");
}

Result<std::string> config_dir() {
    const char* env_dir = getenv("EA_CONFIG_DIR");
    if (env_dir && env_dir[0] != '\0') {
        return expand_tilde(std::string(env_dir));
    }
    auto home = home_dir();
    if (!home.ok()) return home.error();
    return home.value() + "/.embedded-agent";
}

Result<std::string> data_dir() {
    // EA_CONFIG_DIR takes precedence — when set, it pins both config
    // and data locations. EA_DATA_DIR is ignored with a warning.
    const char* config_env = getenv("EA_CONFIG_DIR");
    const char* data_env = getenv("EA_DATA_DIR");
    if (config_env && config_env[0] != '\0') {
        if (data_env && data_env[0] != '\0') {
            EA_WARN("EA_CONFIG_DIR is set; EA_DATA_DIR is ignored "
                    "(CONFIG_DIR pins both config and data directories)");
        }
        auto cfg = config_dir();
        if (!cfg.ok()) return cfg.error();
        return cfg.value() + "/data";
    }
    if (data_env && data_env[0] != '\0') {
        return expand_tilde(std::string(data_env));
    }
    auto cfg = config_dir();
    if (!cfg.ok()) return cfg.error();
    return cfg.value() + "/data";
}

Result<std::string> trace_dir() {
    const char* env_dir = getenv("EA_TRACE_DIR");
    if (env_dir && env_dir[0] != '\0') {
        return expand_tilde(std::string(env_dir));
    }
    auto cfg = config_dir();
    if (!cfg.ok()) return cfg.error();
    return cfg.value() + "/trace";
}

Result<std::string> resolve_data_path(const std::string& filename) {
    auto dir = data_dir();
    if (!dir.ok()) return dir.error();
    return dir.value() + "/" + filename;
}

Result<bool> exists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return true;
    if (errno == ENOENT) return false;
    return Error::io(std::string("stat failed: ") + strerror(errno));
}

Result<bool> is_dir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return Error::io(std::string("stat failed: ") + strerror(errno));
    }
    return S_ISDIR(st.st_mode);
}

Result<void> mkdir_p(const std::string& path) {
    if (path.empty()) return Error::invalid_arg("empty path");

    auto ex = exists(path);
    if (ex.ok() && ex.value()) {
        auto dir = is_dir(path);
        if (dir.ok() && dir.value()) return {};
        return Error::io(path + " exists but is not a directory");
    }

    auto parent = path.substr(0, path.rfind('/'));
    if (!parent.empty() && parent != path) {
        auto parent_exists = exists(parent);
        if (!parent_exists.ok() || !parent_exists.value()) {
            auto r = mkdir_p(parent);
            if (!r.ok()) return r;
        }
    }

    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        return Error::io(
                     std::string("mkdir failed for ") + path + ": " + strerror(errno));
    }
    return {};
}

Result<void> remove(const std::string& path) {
    if (::remove(path.c_str()) != 0) {
        return Error::io(
                     std::string("remove failed for ") + path + ": " + strerror(errno));
    }
    return {};
}

Result<std::string> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return Error::io("cannot open: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Result<void> write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::out | std::ios::binary);
    if (!f.is_open()) {
        return Error::io("cannot open for write: " + path);
    }
    f << content;
    if (!f.good()) {
        return Error::io("write failed: " + path);
    }
    return {};
}

Result<std::vector<std::string>> list_dir(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return Error::io("opendir failed: " + path);
    }
    std::vector<std::string> entries;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name != "." && name != "..") {
            entries.push_back(std::move(name));
        }
    }
    closedir(dir);
    return entries;
}

std::string parent_path(const std::string& path) {
    if (path.empty()) return "";

    // Remove trailing slashes (except root "/")
    std::string p = path;
    while (p.size() > 1 && p.back() == '/') {
        p.pop_back();
    }

    auto pos = p.rfind('/');
    if (pos == std::string::npos) return "";
    if (pos == 0) return "/";  // root
    return p.substr(0, pos);
}

Result<std::string> find_git_root(const std::string& start_dir) {
    std::string current = start_dir;

    // Normalize: remove trailing slashes
    while (current.size() > 1 && current.back() == '/') {
        current.pop_back();
    }

    for (int i = 0; i < 256; ++i) {  // safety limit
        std::string git_path = current + "/.git";
        auto ex = exists(git_path);
        if (ex.ok() && ex.value()) {
            return current;
        }

        std::string parent = parent_path(current);
        if (parent.empty() || parent == current) {
            break;  // reached filesystem root
        }
        current = std::move(parent);
    }

    return Error::not_found("No .git directory found from: " + start_dir);
}

Result<std::string> walk_up_find(const std::string& start_dir,
                                 const std::vector<std::string>& filenames,
                                 const std::string& stop_at) {
    // Safety: if no stop_at, only check start_dir itself
    if (stop_at.empty()) {
        for (const auto& name : filenames) {
            std::string candidate = start_dir + "/" + name;
            auto ex = exists(candidate);
            if (ex.ok() && ex.value()) {
                return candidate;
            }
        }
        return std::string{};
    }

    std::string current = start_dir;

    // Normalize: remove trailing slashes
    while (current.size() > 1 && current.back() == '/') {
        current.pop_back();
    }
    std::string normalized_stop = stop_at;
    while (normalized_stop.size() > 1 && normalized_stop.back() == '/') {
        normalized_stop.pop_back();
    }

    for (int i = 0; i < 256; ++i) {  // safety limit
        for (const auto& name : filenames) {
            std::string candidate = current + "/" + name;
            auto ex = exists(candidate);
            if (ex.ok() && ex.value()) {
                return candidate;
            }
        }

        if (current == normalized_stop) {
            break;  // stop at git root (inclusive)
        }

        std::string parent = parent_path(current);
        if (parent.empty() || parent == current) {
            break;  // reached filesystem root
        }
        current = std::move(parent);
    }

    return std::string{};
}

}  // namespace ea::fs
