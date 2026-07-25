#include "FileSystem.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ea::fs {

Result<std::string> home_dir() {
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') return std::string(home);
    return Error::io("HOME not set");
}

Result<std::string> config_dir() {
    auto home = home_dir();
    if (!home.ok()) return home.error();
    return home.value() + "/.embedded-agent";
}

Result<std::string> data_dir() {
    return config_dir();
}

Result<std::string> resolve_data_path(const std::string& filename) {
    auto cfg_dir = config_dir();
    if (cfg_dir.ok()) {
        return cfg_dir.value() + "/" + filename;
    }
    auto home = home_dir();
    if (!home.ok()) return home.error();
    return home.value() + "/.embedded-agent/" + filename;
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

}  // namespace ea::fs
